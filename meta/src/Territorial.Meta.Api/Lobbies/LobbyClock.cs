using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Api.Lobbies;

/// <summary>
/// Jeden zegar na cały system, zamiast timera per lobby — i jedyne miejsce, z którego
/// wychodzi rozgłoszenie stanu.
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
/// <see cref="PeriodicTimer"/> dostaje <see cref="TimeProvider"/>, dzięki czemu w testach
/// integracyjnych całą dobę pracy zegara da się przewinąć bez czekania.
/// </para>
/// </remarks>
public sealed partial class LobbyClock(
    CurrentLobby lobby,
    LobbyBroadcaster broadcaster,
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
        Level = LogLevel.Warning,
        Message = "Lobby {LobbyId} dobiło do terminu startu, ale alokacja meczu nie jest "
            + "jeszcze zaimplementowana — nie ma game-serwera, któremu można go oddać. Lobby "
            + "zostaje zamknięte i otwarte na nowo. To jest miejsce, w które wchodzi wywołanie "
            + "orkiestratora i rozesłanie biletów (dokument §5③)."
    )]
    private static partial void LogMatchAllocationMissing(ILogger logger, Guid lobbyId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Tik zegara lobby zakończył się błędem.")]
    private static partial void LogTickFailed(ILogger logger, Exception exception);

    private async Task TickAsync()
    {
        try
        {
            var (tick, snapshot) = lobby.Tick();

            if (snapshot is null)
            {
                return;
            }

            if (tick is LobbyTick.Started)
            {
                LogMatchAllocationMissing(logger, snapshot.Header.LobbyId);

                snapshot = lobby.CloseAndReopen();
            }

            await broadcaster.PublishAsync(snapshot);
        }
        catch (Exception exception)
        {
            // Zegar musi przeżyć każdy pojedynczy błąd — inaczej jedna nieudana wysyłka
            // zatrzymuje odliczanie dla całego serwisu aż do restartu.
            LogTickFailed(logger, exception);
        }
    }
}
