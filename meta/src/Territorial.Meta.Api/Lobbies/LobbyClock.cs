using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Lobbies;

namespace Territorial.Meta.Api.Lobbies;

/// <summary>
/// Jeden zegar na cały system, zamiast timera per lobby — i jedyne miejsce, z którego
/// wychodzi rozgłoszenie stanu lobby.
/// </summary>
/// <remarks>
/// <para>
/// Tyka raz na sekundę: zamiata wygasłe karencje po rozłączeniach, pyta lobby, czy termin
/// startu już minął, i rozsyła stan, jeśli cokolwiek się zmieniło. Decyzja o starcie
/// zależy wyłącznie od <c>StartsAt</c>, a nie od odstępu między wywołaniami, więc
/// spóźniony albo zdublowany tik niczego nie psuje.
/// </para>
/// <para>
/// Sekundowy takt jest jednocześnie naturalnym coalescingiem — dziesięć dołączeń w tej
/// samej sekundzie to jedna wiadomość do wszystkich zamiast dziesięciu.
/// </para>
/// <para>
/// <b>Zegar nie uruchamia meczu.</b> Zamrożony roster ląduje w kolejce i tyle; alokacją,
/// biletami i domknięciem lobby zajmuje się <see cref="MatchLauncher"/>. Alokacja to I/O
/// z ponowieniami, a każde <c>await</c> w tiku zatrzymuje rozgłaszanie dla całego serwisu.
/// </para>
/// <para>
/// <see cref="PeriodicTimer"/> dostaje <see cref="TimeProvider"/>, dzięki czemu w testach
/// integracyjnych całą dobę pracy zegara da się przewinąć bez czekania.
/// </para>
/// </remarks>
public sealed partial class LobbyClock(
    CurrentLobby lobby,
    LobbyBroadcaster broadcaster,
    MatchStartChannel launches,
    TimeProvider timeProvider,
    ILogger<LobbyClock> logger
) : BackgroundService{
    private static readonly TimeSpan Interval = TimeSpan.FromSeconds(1);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(Interval, timeProvider);

        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
            {
                await TickAsync();
            }
        }
        catch (OperationCanceledException)
        {
            // Normalne zatrzymanie hosta.
        }
    }

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Lobby {LobbyId} weszło w fazę startu z {PlayerCount} graczami. "
            + "Roster jest zamrożony i czeka na launcher."
    )]
    private static partial void LogMatchQueued(ILogger logger, Guid lobbyId, int playerCount);

    [LoggerMessage(Level = LogLevel.Error, Message = "Tik zegara lobby zakończył się błędem.")]
    private static partial void LogTickFailed(ILogger logger, Exception exception);

    private async Task TickAsync()
    {
        LobbyTickResult result;

        try
        {
            result = lobby.Tick();
        }
        catch (Exception exception)
        {
            // Zegar musi przeżyć każdy pojedynczy błąd — inaczej jedna nieudana operacja
            // zatrzymuje odliczanie dla całego serwisu aż do restartu.
            LogTickFailed(logger, exception);
            return;
        }

        try
        {
            // Nagłówek ze stanem "Starting" wychodzi PRZED wpisaniem do kolejki. Przy
            // szybkiej alokacji launcher zdążyłby inaczej rozgłosić już nowe lobby, a gracze
            // zobaczyliby kolejność odwróconą: najpierw następne lobby, potem start poprzedniego.
            if (result.Snapshot is { } snapshot)
            {
                await broadcaster.PublishAsync(snapshot);
            }
        }
        catch (Exception exception)
        {
            // Nieudane rozgłoszenie NIE może zjeść startu meczu — dlatego wpis do kolejki
            // jest poza tym blokiem. Lobby jest już zamrożone, a jedynym wyjściem ze stanu
            // Starting jest launcher; pominięty wpis zostawiłby martwe lobby na zawsze.
            LogTickFailed(logger, exception);
        }

        if (result.Start is { } start)
        {
            LogMatchQueued(logger, start.LobbyId, start.Roster.Count);

            launches.Enqueue(start);
        }
    }
}
