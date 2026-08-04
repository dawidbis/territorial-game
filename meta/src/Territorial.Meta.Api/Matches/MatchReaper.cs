using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Zamyka mecze, których procesy zgasły.
/// </summary>
/// <remarks>
/// <para>
/// Bez tego wiersz meczu zostaje w stanie <c>Live</c> na zawsze: meta wydaje bilety do
/// procesu, którego nie ma, <c>matches/mine</c> wciąga gracza z powrotem na ekran meczu,
/// a jedyne, co ten ekran może mu powiedzieć, to „tego meczu już nie ma". Zamknięcie wiersza
/// zabiera problem u źródła — gracz po powrocie ląduje wprost w kolejce.
/// </para>
/// <para>
/// To <b>nie jest</b> odbiór wyniku meczu (plan alokacji, etap 4). Wynik przyjdzie z procesu
/// własną drogą i będzie niósł, kto wygrał; tutaj wiemy wyłącznie tyle, że procesu już nie
/// ma. Dlatego stan zmienia <c>MarkCompleted</c>, które o wyniku nic nie mówi — i dlatego
/// wygrywa tu ten, kto zdąży pierwszy: mecz zamknięty odbiorem wyniku zignoruje tę ścieżkę.
/// </para>
/// <para>
/// Osobna usługa, a nie kod w alokatorze: zdarzenie wyjścia procesu przychodzi z wątku puli,
/// bez scope'a i bez kontekstu żądania, a repozytorium żyje krócej niż singleton alokatora.
/// </para>
/// </remarks>
public sealed partial class MatchReaper(
    MatchEndChannel ended,
    TimeProvider timeProvider,
    IServiceScopeFactory scopeFactory,
    ILogger<MatchReaper> logger
) : BackgroundService{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        try
        {
            await foreach (var notice in ended.ReadAllAsync(stoppingToken))
            {
                await CloseAsync(notice, stoppingToken);
            }
        }
        catch (OperationCanceledException)
        {
            // Normalne zatrzymanie hosta.
        }
    }

    /// <summary>Zamyka jeden mecz.</summary>
    /// <remarks>
    /// <c>internal</c>, żeby test mógł przejść tę ścieżkę wprost, bez uruchamiania wątku
    /// w tle i bez czekania na cokolwiek — kolejka jest przetestowana osobno.
    /// </remarks>
    internal async Task CloseAsync(MatchEndedNotice notice, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(notice);

        using var scope = scopeFactory.CreateScope();
        var matches = scope.ServiceProvider.GetRequiredService<IMatchRepository>();

        try
        {
            var match = await matches.GetAsync(notice.MatchId, cancellationToken);

            if (match is null)
            {
                // Proces bez wiersza w bazie znaczy mecz usunięty spod nas albo notyfikację
                // z poprzedniego uruchomienia. Nie ma czego zamykać i nie ma o co się spierać.
                return;
            }

            match.MarkCompleted(timeProvider.GetUtcNow());

            await matches.SaveChangesAsync(cancellationToken);

            LogMatchClosed(logger, notice.MatchId);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            // Nieudane zamknięcie zostawia wiersz `Live` — czyli dokładnie stan sprzed tej
            // usługi. Wyjątek wypuszczony wyżej zabiłby pętlę i zabrał zamykanie wszystkim
            // następnym meczom, a to jest gorsze niż jeden wiersz do posprzątania ręcznie.
            LogClosingFailed(logger, notice.MatchId, exception);
        }
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "Mecz {MatchId} zamknięty: proces zgasł.")]
    private static partial void LogMatchClosed(ILogger logger, Guid matchId);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Nie udało się zamknąć meczu {MatchId} po wyjściu procesu. Wiersz zostaje Live."
    )]
    private static partial void LogClosingFailed(ILogger logger, Guid matchId, Exception exception);
}
