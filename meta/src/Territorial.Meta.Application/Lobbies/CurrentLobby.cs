using Territorial.Meta.Application.Lobbies.Contracts;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Application.Players.Contracts;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Lobbies;

/// <summary>
/// Jedyne aktywne lobby w systemie — singleton trzymający bieżącą instancję
/// <see cref="Lobby"/> i otwierający kolejną, gdy poprzednia się domknie.
/// </summary>
/// <remarks>
/// <para>
/// <b>Rozgłaszanie.</b> Ta klasa niczego nie wysyła. Mutacje zaznaczają jedynie, że stan
/// się zmienił, a jedyny broadcast wychodzi z zegara raz na sekundę (<see cref="Tick"/>).
/// Dziesięć dołączeń w tej samej sekundzie to jedna wiadomość zamiast dziesięciu, a przy
/// okazji znika cała klasa nadużyć polegających na klikaniu join/leave w pętli. Kosztem
/// jest do sekundy opóźnienia, zanim pozostali zobaczą nowego gracza — w lobby bez
/// znaczenia.
/// </para>
/// <para>
/// <b>Współbieżność.</b> Zwykły <see cref="Lock"/> wokół każdej mutacji. Przy kilku
/// wiadomościach na sekundę kolejka komunikatów czy model aktorowy byłyby złożonością bez
/// zysku. Pod lockiem dzieje się wyłącznie mutacja i zbudowanie snapshotu — nigdy
/// wysyłka ani żadne <c>await</c>.
/// </para>
/// <para>
/// <b>Skala.</b> Stan w pamięci oznacza jedną instancję API. Przy skali z dokumentu §7 to
/// właściwy wybór, ale nie udawajmy, że skaluje się poziomo — przy wielu instancjach
/// dochodzi backplane i wybór lidera dla zegara.
/// </para>
/// </remarks>
public sealed class CurrentLobby
{
    private readonly Lock gate = new();

    /// <summary>Każde uwierzytelnione połączenie z hubem — także obserwatorów strony głównej.</summary>
    private readonly Dictionary<string, Guid> connectionOwners = [];

    /// <summary>
    /// Ile połączeń ma dany gracz. Członkostwo w lobby kluczowane jest graczem, a nie
    /// połączeniem — inaczej druga karta przeglądarki byłaby drugim graczem, a zamknięcie
    /// jednej z nich wyrzucałoby go z lobby mimo wciąż otwartej drugiej.
    /// </summary>
    private readonly Dictionary<Guid, int> connectionsPerPlayer = [];

    /// <summary>
    /// Gracze bez połączenia, czekający na skreślenie. Patrz
    /// <see cref="LobbyOptions.DisconnectGraceSeconds"/>.
    /// </summary>
    private readonly Dictionary<Guid, DateTimeOffset> pendingRemovals = [];

    private readonly IMapCatalog maps;
    private readonly TimeProvider timeProvider;
    private readonly LobbyOptions options;

    /// <summary>
    /// Czy mecz dopełni obsadę botami.
    /// </summary>
    /// <remarks>
    /// Ustawienie meczu, a nie lobby — i tak ma zostać. Lobby czyta je wyłącznie po to, żeby
    /// nagłówek nie obiecywał dziewięćdziesięciu dziewięciu przeciwników, których proces
    /// meczu nie postawi: liczba botów w lobby jest **zapowiedzią**, a zapowiedź niezgodna
    /// z tym, co się dzieje po starcie, jest gorsza od jej braku.
    /// </remarks>
    private readonly bool fillWithBots;

    private readonly TimeSpan disconnectGrace;

    private Lobby lobby;

    /// <summary>Czy od ostatniego rozgłoszenia zmieniło się cokolwiek, co widzą gracze.</summary>
    private bool dirty;

    public CurrentLobby(
        IMapCatalog maps,
        TimeProvider timeProvider,
        LobbyOptions options,
        MatchOptions matchOptions
    ){
        ArgumentNullException.ThrowIfNull(matchOptions);

        this.maps = maps;
        this.timeProvider = timeProvider;
        this.options = options;

        fillWithBots = matchOptions.FillWithBots;

        disconnectGrace = TimeSpan.FromSeconds(options.DisconnectGraceSeconds);
        lobby = OpenNext(timeProvider.GetUtcNow());
    }

    public LobbySnapshot Snapshot()
    {
        lock (gate)
        {
            return Capture();
        }
    }

    /// <summary>Odnotowuje nowe połączenie i oddaje stan, który należy mu od razu odesłać.</summary>
    public LobbySnapshot TrackConnection(string connectionId, Guid playerId)
    {
        lock (gate)
        {
            if (connectionOwners.TryAdd(connectionId, playerId))
            {
                connectionsPerPlayer[playerId] =
                    connectionsPerPlayer.GetValueOrDefault(playerId) + 1;
            }

            // Gracz wrócił w oknie karencji — skreślenie odwołane. Roster nigdy nie drgnął,
            // więc nie ma czego rozgłaszać.
            pendingRemovals.Remove(playerId);

            return Capture();
        }
    }

    public (JoinResult Result, LobbySnapshot Snapshot) Join(
        Guid playerId,
        Nickname nickname,
        HsvColor color
    ){
        lock (gate)
        {
            var result = lobby.Join(playerId, nickname, color, timeProvider.GetUtcNow());

            pendingRemovals.Remove(playerId);

            if (result is JoinResult.Joined)
            {
                dirty = true;
            }

            return (result, Capture());
        }
    }

    /// <summary>Wyjście na życzenie gracza — natychmiastowe, bez karencji.</summary>
    public LobbySnapshot Leave(Guid playerId)
    {
        lock (gate)
        {
            pendingRemovals.Remove(playerId);

            if (lobby.Leave(playerId))
            {
                dirty = true;
            }

            return Capture();
        }
    }

    /// <summary>
    /// Przepisuje do rostera nick i kolor po zmianie profilu. Wołane, gdy gracz zapisze
    /// zmiany, siedząc w lobby.
    /// </summary>
    public void RefreshPlayer(Guid playerId, Nickname nickname, HsvColor color)
    {
        lock (gate)
        {
            if (lobby.Refresh(playerId, nickname, color))
            {
                dirty = true;
            }
        }
    }

    /// <summary>
    /// Zamyka połączenie. Gracz traci miejsce dopiero po upływie karencji od utraty
    /// <b>ostatniego</b> połączenia — samo rozłączenie nie zmienia jeszcze rostera.
    /// </summary>
    public void Disconnect(string connectionId)
    {
        lock (gate)
        {
            if (!connectionOwners.Remove(connectionId, out var playerId))
            {
                return;
            }

            var remaining = connectionsPerPlayer.GetValueOrDefault(playerId) - 1;

            if (remaining > 0)
            {
                connectionsPerPlayer[playerId] = remaining;
                return;
            }

            connectionsPerPlayer.Remove(playerId);

            if (lobby.Contains(playerId))
            {
                pendingRemovals[playerId] = timeProvider.GetUtcNow() + disconnectGrace;
            }
        }
    }

    /// <summary>
    /// Jedyne miejsce, z którego wychodzi rozgłoszenie. Wołane przez zegar raz na sekundę:
    /// zamiata wygasłe karencje, popycha lobby i oddaje snapshot, jeśli jest co pokazać.
    /// </summary>
    /// <remarks>
    /// Przy wejściu w fazę startu razem ze snapshotem wychodzi <see cref="MatchStartRequest"/>
    /// z rosterem zamrożonym pod tym samym lockiem. Bez tego launcher musiałby wrócić po
    /// roster osobnym wywołaniem — a między jednym a drugim lobby zdążyłoby się zmienić.
    /// </remarks>
    public LobbyTickResult Tick()
    {
        lock (gate)
        {
            var now = timeProvider.GetUtcNow();

            SweepExpiredDisconnects(now);

            var tick = lobby.Advance(now);

            if (tick is not LobbyTick.Idle)
            {
                dirty = true;
            }

            var start =
                tick is LobbyTick.Started
                    ? new MatchStartRequest(lobby.Id, lobby.Map, lobby.Mode, lobby.Roster())
                    : null;

            if (!dirty)
            {
                return new LobbyTickResult(tick, null, start);
            }

            dirty = false;

            return new LobbyTickResult(tick, Capture(), start);
        }
    }

    /// <summary>
    /// Domyka bieżące lobby i otwiera następne.
    /// </summary>
    /// <remarks>
    /// Roster nowego lobby jest pusty — połączenia zostają, ale członkostwo nie jest
    /// dziedziczone. Klient zauważa zmianę <c>LobbyId</c> w nagłówku i dołącza ponownie,
    /// jeśli gracz nadal siedzi na widoku lobby.
    /// </remarks>
    public LobbySnapshot CloseAndReopen()
    {
        lock (gate)
        {
            lobby.Close();
            lobby = OpenNext(timeProvider.GetUtcNow());

            // Karencje dotyczyły rostera, którego już nie ma.
            pendingRemovals.Clear();
            dirty = false;

            return Capture();
        }
    }

    private void SweepExpiredDisconnects(DateTimeOffset now)
    {
        if (pendingRemovals.Count == 0)
        {
            return;
        }

        // Materializacja przed usuwaniem — modyfikowanie słownika w trakcie iteracji
        // po nim jest błędem.
        List<Guid>? expired = null;

        foreach (var (playerId, deadline) in pendingRemovals)
        {
            if (now >= deadline)
            {
                (expired ??= []).Add(playerId);
            }
        }

        if (expired is null)
        {
            return;
        }

        foreach (var playerId in expired)
        {
            pendingRemovals.Remove(playerId);

            if (lobby.Leave(playerId))
            {
                dirty = true;
            }
        }
    }

    private Lobby OpenNext(DateTimeOffset now)
    {
        var window = TimeSpan.FromSeconds(options.GatheringSeconds);
        var map = maps.ForNextLobby();

        return options.CountdownEnabled
            ? Lobby.Open(map, GameMode.Ffa, window, now)
            : Lobby.OpenFrozen(map, GameMode.Ffa, window, now);
    }

    /// <summary>Buduje snapshot. Wolno wołać wyłącznie spod <see cref="gate"/>.</summary>
    private LobbySnapshot Capture()
    {
        var header = new LobbyHeaderDto(
            lobby.Id,
            lobby.State.ToString(),
            lobby.Map.Id,
            lobby.Map.Name,
            lobby.Mode.ToString(),
            lobby.HumanCount,
            lobby.Map.MaxActors,
            fillWithBots ? lobby.BotCount : 0,
            lobby.StartsAt,
            lobby.StartsAt is null ? (int)Math.Round(lobby.GatheringWindow.TotalSeconds) : null,
            timeProvider.GetUtcNow()
        );

        var players = lobby
            .Roster()
            .Select(p => new LobbyPlayerDto(
                p.PlayerId,
                p.Nickname.Value,
                new HsvColorDto(p.Color.Hue, p.Color.Saturation, p.Color.Value)
            ))
            .ToArray();

        return new LobbySnapshot(header, players);
    }
}
