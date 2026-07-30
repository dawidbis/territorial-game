using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Zamyka mecze, które restart procesu zastał w trakcie alokacji.
/// </summary>
/// <remarks>
/// <para>
/// Alokacja żyje w pamięci jednej instancji: launcher czyta z kanału, rozmawia
/// z orkiestratorem i dopisuje wynik. Gdy proces zniknie w środku tej sekwencji, wiersz
/// zostaje w stanie <see cref="Domain.Matches.MatchState.Allocating"/> na zawsze — nikt
/// nie wróci, żeby go domknąć. Lobby tego problemu nie ma, bo żyje w pamięci i po restarcie
/// zaczyna od zera.
/// </para>
/// <para>
/// Zamiatanie jest bezpieczne wyłącznie <b>przy starcie</b> i wyłącznie <b>przy jednej
/// instancji</b> — wtedy żaden wiersz w tym stanie nie ma już swojego launchera. Dołożenie
/// drugiej instancji API (backplane, §7 dokumentu architektury) wymaga tu progu wieku albo
/// dzierżawy, inaczej jedna instancja oznaczałaby jako nieudane mecze zakładane właśnie
/// przez drugą.
/// </para>
/// <para>
/// <see cref="BackgroundService"/>, a nie robota w <c>StartAsync</c>: pytanie do bazy nie
/// ma powodu opóźniać przyjmowania żądań, a wynik nikomu nie jest potrzebny natychmiast.
/// </para>
/// </remarks>
public sealed partial class StaleMatchSweeper(
    IServiceScopeFactory scopeFactory,
    TimeProvider timeProvider,
    ILogger<StaleMatchSweeper> logger
) : BackgroundService{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        try
        {
            await SweepAsync(stoppingToken);
        }
        catch (OperationCanceledException)
        {
            // Normalne zatrzymanie hosta.
        }
        catch (Exception exception)
        {
            // Nieudane zamiatanie nie może przewrócić startu serwisu: to porządki po
            // poprzednim życiu procesu, a nie warunek działania tego.
            LogSweepFailed(logger, exception);
        }
    }

    /// <summary>
    /// Jeden przebieg zamiatania.
    /// </summary>
    /// <remarks>
    /// <c>internal</c> i bez łapania wyjątków — dzięki temu test widzi błąd zamiast ciszy,
    /// a połykanie awarii zostaje wyłącznie w <see cref="ExecuteAsync"/>, gdzie jest po coś.
    /// </remarks>
    internal async Task SweepAsync(CancellationToken cancellationToken)
    {
        using var scope = scopeFactory.CreateScope();
        var matches = scope.ServiceProvider.GetRequiredService<IMatchRepository>();

        var stale = await matches.GetAllocatingAsync(cancellationToken);

        if (stale.Count == 0)
        {
            return;
        }

        var now = timeProvider.GetUtcNow();

        foreach (var match in stale)
        {
            match.MarkFailed(now);
        }

        await matches.SaveChangesAsync(cancellationToken);

        LogSweptStaleMatches(logger, stale.Count);
    }

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Oznaczono {Count} meczów porzuconych w trakcie alokacji jako nieudane."
    )]
    private static partial void LogSweptStaleMatches(ILogger logger, int count);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Zamiatanie meczów porzuconych w trakcie alokacji nie powiodło się."
    )]
    private static partial void LogSweepFailed(ILogger logger, Exception exception);
}
