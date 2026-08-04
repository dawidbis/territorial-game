using System.Security.Cryptography;
using Territorial.Meta.Api.Lobbies;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Application.Matches.Contracts;
using Territorial.Meta.Domain.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Zamienia zamrożony roster w mecz: sloty, zapis, alokacja, bilety, nowe lobby.
/// </summary>
/// <remarks>
/// <para>
/// Osobny wątek, a nie kod w tiku zegara — to najważniejsza decyzja tej ścieżki. Alokacja
/// to I/O sieciowe z ponowieniami; wykonana w tiku zatrzymywałaby rozgłaszanie stanu dla
/// całego serwisu na czas rozmowy z orkiestratorem, a zegar łapie każdy wyjątek i tylko go
/// loguje — nieudany start zniknąłby bez śladu w stanie systemu.
/// </para>
/// <para>
/// <b>To launcher domyka lobby</b>, nie zegar. Zamrożony roster musi dożyć do chwili, w
/// której zostanie zużyty; <c>CloseAndReopen</c> w zegarze wyrzucałby go, zanim ktokolwiek
/// zdążyłby go zobaczyć.
/// </para>
/// <para>
/// Dwie właściwości są już w domenie i nie trzeba o nie tutaj walczyć: podwójny start jest
/// niemożliwy, bo <c>Lobby.Advance</c> reaguje wyłącznie w stanie <c>Gathering</c>,
/// a dołączenia w trakcie alokacji odbijają się o <c>NotGathering</c>.
/// </para>
/// </remarks>
public sealed partial class MatchLauncher(
    MatchStartChannel launches,
    CurrentLobby lobby,
    LobbyBroadcaster broadcaster,
    IMatchAllocator allocator,
    MatchTicketService tickets,
    MatchOptions options,
    TimeProvider timeProvider,
    IServiceScopeFactory scopeFactory,
    ILogger<MatchLauncher> logger
) : BackgroundService{
    /// <summary>Komunikat dla graczy. Bez szczegółów awarii — te zostają w logach.</summary>
    private const string FailureMessage =
        "Nie udało się przygotować serwera gry. Za chwilę otworzy się nowe lobby.";

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        try
        {
            await foreach (var request in launches.ReadAllAsync(stoppingToken))
            {
                await LaunchAsync(request, stoppingToken);
            }
        }
        catch (OperationCanceledException)
        {
            // Normalne zatrzymanie hosta.
        }
    }

    /// <summary>
    /// Cały cykl startu jednego meczu.
    /// </summary>
    /// <remarks>
    /// <c>internal</c>, a nie <c>private</c>, żeby testy mogły przejść tę ścieżkę wprost,
    /// bez uruchamiania wątku w tle i bez czekania na cokolwiek. Kolejka i tak jest
    /// przetestowana osobno, a asynchroniczne testy hostowanych serwisów bywają migotliwe
    /// z powodów, które nie mają nic wspólnego z testowaną logiką.
    /// </remarks>
    internal async Task LaunchAsync(MatchStartRequest request, CancellationToken cancellationToken)
    {
        // Własny scope: launcher jest singletonem, a repozytorium i DbContext żyją krócej.
        using var scope = scopeFactory.CreateScope();
        var matches = scope.ServiceProvider.GetRequiredService<IMatchRepository>();

        Match? match = null;

        try
        {
            match = Match.Create(
                request.Map,
                request.Mode,
                NewSeed(),
                request.Roster,
                timeProvider.GetUtcNow()
            );

            // Zapis PRZED alokacją: gdyby proces game-serwera wstał, a meta nie zdążyła
            // niczego zapisać, zostałby stojący gdzieś proces, o którym nikt nie wie.
            matches.Add(match);
            await matches.SaveChangesAsync(cancellationToken);

            var allocation = await AllocateAsync(match, cancellationToken);

            match.MarkLive(allocation.Endpoint, allocation.WsUrl, timeProvider.GetUtcNow());
            await matches.SaveChangesAsync(cancellationToken);

            await DeliverTicketsAsync(match, allocation);

            LogMatchStarted(logger, match.Id, match.HumanCount, match.BotCount, allocation.WsUrl);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Host się zatrzymuje. Lobby żyje w pamięci, więc i tak zniknie razem z nim —
            // nie ma czego domykać ani komu tłumaczyć.
            throw;
        }
        catch (Exception exception)
        {
            LogMatchStartFailed(logger, request.LobbyId, exception);

            await FailAsync(matches, match, cancellationToken);
        }
        finally
        {
            if (!cancellationToken.IsCancellationRequested)
            {
                await ReopenLobbyAsync();
            }
        }
    }

    /// <summary>
    /// Ziarno symulacji z CSPRNG.
    /// </summary>
    /// <remarks>
    /// Nie <c>Random.Shared</c>: ziarno wyznacza całą losowość meczu — także zachowanie
    /// botów — więc możliwość jego przewidzenia byłaby przewagą w rozgrywce, a nie
    /// akademickim niuansem.
    /// </remarks>
    private static long NewSeed() =>
        BitConverter.ToInt64(RandomNumberGenerator.GetBytes(sizeof(long)));

    private async Task<MatchAllocation> AllocateAsync(
        Match match,
        CancellationToken cancellationToken
    ){
        // Manifest budowany tutaj, a nie w alokatorze: to jedyna warstwa, która wie, co to
        // jest nick i co to jest kolor gracza. Dla orkiestratora jest nieprzezroczysty.
        var request = new MatchAllocationRequest(
            match.Id,
            match.MapId,
            match.MaxActors,
            match.Seed,
            MatchManifest.For(match.Participants)
        );

        var attempts = Math.Max(1, options.AllocationAttempts);
        var retryDelay = TimeSpan.FromMilliseconds(
            Math.Max(0, options.AllocationRetryDelayMilliseconds)
        );

        for (var attempt = 1; ; attempt++)
        {
            try
            {
                return await allocator.AllocateAsync(request, cancellationToken);
            }
            catch (Exception exception)
                when (attempt < attempts && exception is not OperationCanceledException)
            {
                LogAllocationRetrying(logger, match.Id, attempt, exception);

                await Task.Delay(retryDelay, timeProvider, cancellationToken);
            }
        }
    }

    /// <summary>
    /// Rozsyła bilety — po jednym, imiennie.
    /// </summary>
    /// <remarks>
    /// Nieudana wysyłka do jednego gracza <b>nie</b> przerywa pozostałych: proces trzyma
    /// jego slot, a gracz dobierze bilet ponownie (plan alokacji, §3.7). Przerwanie pętli
    /// zabrałoby mecz także tym, do których wysyłka by się udała.
    /// </remarks>
    private async Task DeliverTicketsAsync(Match match, MatchAllocation allocation)
    {
        foreach (var participant in match.Participants)
        {
            try
            {
                var ticket = tickets.Issue(participant.PlayerId, match.Id, participant.Slot);

                await broadcaster.MatchReadyAsync(
                    participant.PlayerId,
                    new MatchReadyDto(match.Id, allocation.WsUrl, ticket.Value, ticket.ExpiresAt)
                );
            }
            catch (Exception exception)
            {
                LogTicketDeliveryFailed(logger, match.Id, participant.PlayerId, exception);
            }
        }
    }

    /// <summary>Oznacza mecz jako nieudany i mówi o tym czekającym.</summary>
    private async Task FailAsync(
        IMatchRepository matches,
        Match? match,
        CancellationToken cancellationToken
    ){
        try
        {
            if (match is not null)
            {
                match.MarkFailed(timeProvider.GetUtcNow());

                await matches.SaveChangesAsync(cancellationToken);
            }
        }
        catch (Exception exception)
        {
            // Baza może być właśnie tym, co padło. Nie ma sensu drugi raz przez to
            // przechodzić — gracze i tak dostaną komunikat, a ślad zostaje w logu.
            LogFailureNotRecorded(logger, exception);
        }

        try
        {
            await broadcaster.MatchStartFailedAsync(new MatchStartFailedDto(FailureMessage));
        }
        catch (Exception exception)
        {
            LogFailureNotAnnounced(logger, exception);
        }
    }

    /// <summary>
    /// Domyka lobby i rozgłasza następne.
    /// </summary>
    /// <remarks>
    /// Wołane <b>zawsze</b>, także po nieudanym starcie: lobby zostawione w stanie
    /// <c>Starting</c> nie przyjmuje nikogo i nigdy z niego nie wyjdzie, więc awaria
    /// alokacji zamieniłaby się w martwy serwis.
    /// </remarks>
    private async Task ReopenLobbyAsync()
    {
        try
        {
            await broadcaster.PublishAsync(lobby.CloseAndReopen());
        }
        catch (Exception exception)
        {
            // Samo otwarcie nowego lobby już się udało — nie rozeszła się tylko wiadomość
            // o nim. Klienci zobaczą je przy najbliższej zmianie stanu.
            LogReopenNotAnnounced(logger, exception);
        }
    }

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Mecz {MatchId} wystartował: {HumanCount} ludzi, {BotCount} botów, {WsUrl}."
    )]
    private static partial void LogMatchStarted(
        ILogger logger,
        Guid matchId,
        int humanCount,
        int botCount,
        string wsUrl
    );

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Alokacja meczu {MatchId} nie powiodła się (próba {Attempt}). Ponawiam."
    )]
    private static partial void LogAllocationRetrying(
        ILogger logger,
        Guid matchId,
        int attempt,
        Exception exception
    );

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Start meczu dla lobby {LobbyId} nie powiódł się. Lobby zostaje otwarte na nowo."
    )]
    private static partial void LogMatchStartFailed(
        ILogger logger,
        Guid lobbyId,
        Exception exception
    );

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Nie udało się dostarczyć biletu meczu {MatchId} graczowi {PlayerId}. "
            + "Slot zostaje zajęty — gracz może poprosić o bilet ponownie."
    )]
    private static partial void LogTicketDeliveryFailed(
        ILogger logger,
        Guid matchId,
        Guid playerId,
        Exception exception
    );

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Nie udało się zapisać nieudanego startu meczu."
    )]
    private static partial void LogFailureNotRecorded(ILogger logger, Exception exception);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Nie udało się rozesłać informacji o nieudanym starcie meczu."
    )]
    private static partial void LogFailureNotAnnounced(ILogger logger, Exception exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "Nie udało się rozgłosić nowego lobby.")]
    private static partial void LogReopenNotAnnounced(ILogger logger, Exception exception);
}
