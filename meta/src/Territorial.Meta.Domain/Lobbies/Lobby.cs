using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Lobby zbierające graczy do jednego meczu.
/// </summary>
/// <remarks>
/// <para>
/// Czysta maszyna stanów: zero I/O, zero zależności, czas wyłącznie z parametru. Dzięki
/// temu pełny sześćdziesięciosekundowy cykl testuje się w mikrosekundach, bez bazy,
/// bez zegara i bez <c>Thread.Sleep</c>.
/// </para>
/// <para>
/// Lobby <b>nie jest encją bazodanową</b> i celowo nie ma odpowiednika w EF Core.
/// Jest jedno, żyje kilkadziesiąt sekund i zmienia się przy każdym dołączeniu —
/// trwały zapis oznaczałby INSERT na join i odczyt z powrotem przez zegar, nie
/// rozwiązując przy tym jedynego problemu, który by go usprawiedliwiał (wiele instancji;
/// od tego jest backplane). Do bazy trafia dopiero nieodwracalny wynik, czyli mecz.
/// </para>
/// </remarks>
public sealed class Lobby
{
    private readonly Dictionary<Guid, LobbyPlayer> players = [];

    private Lobby(
        Guid id,
        MapDefinition map,
        GameMode mode,
        TimeSpan gatheringWindow,
        DateTimeOffset? startsAt,
        DateTimeOffset createdAt
    ){
        Id = id;
        Map = map;
        Mode = mode;
        GatheringWindow = gatheringWindow;
        StartsAt = startsAt;
        CreatedAt = createdAt;
    }

    public Guid Id { get; }

    public MapDefinition Map { get; }

    public GameMode Mode { get; }

    public LobbyState State { get; private set; }

    /// <summary>Pełna długość okna zbierania — to, co licznik pokazuje po otwarciu.</summary>
    public TimeSpan GatheringWindow { get; }

    /// <summary>
    /// Chwila startu meczu, albo <c>null</c>, gdy odliczanie jest zatrzymane.
    /// </summary>
    /// <remarks>
    /// Świadomie <b>moment</b>, a nie „pozostało N sekund". Licznik sekundowy trzeba by
    /// rozsyłać sześćdziesiąt razy na lobby, a i tak byłby przestarzały o RTT. Absolutna
    /// chwila leci raz, klient odejmuje ją lokalnie, a reconnect nie wymaga niczego
    /// dodatkowego — snapshot niesie komplet informacji.
    /// </remarks>
    public DateTimeOffset? StartsAt { get; private set; }

    public DateTimeOffset CreatedAt { get; }

    /// <summary>Ludzie w lobby. Boty dochodzą dopiero na mapie.</summary>
    public int HumanCount => players.Count;

    /// <summary>
    /// Ilu botów dopełni mapę. Wartość <b>wyliczana</b>, nigdy przechowywana —
    /// nie ma czego synchronizować z rosterem.
    /// </summary>
    public int BotCount => Math.Max(0, Map.MaxActors - HumanCount);

    public bool IsFull => HumanCount >= Map.MaxActors;

    /// <summary>Otwiera lobby z biegnącym odliczaniem.</summary>
    public static Lobby Open(
        MapDefinition map,
        GameMode mode,
        TimeSpan gatheringWindow,
        DateTimeOffset now
    ) => new(Guid.CreateVersion7(now), map, mode, gatheringWindow, now + gatheringWindow, now);

    /// <summary>
    /// Otwiera lobby z odliczaniem zatrzymanym: licznik stoi na pełnym oknie i nigdy nie
    /// dobija do zera, więc <see cref="LobbyState.Starting"/> jest nieosiągalne.
    /// </summary>
    /// <remarks>
    /// Tryb na czas, gdy nie ma jeszcze game-serwera, któremu można oddać mecz. Reszta
    /// maszyny stanów jest normalnie zaimplementowana i przetestowana — przełącznik
    /// w konfiguracji uruchamia cykl bez dotykania kodu.
    /// </remarks>
    public static Lobby OpenFrozen(
        MapDefinition map,
        GameMode mode,
        TimeSpan gatheringWindow,
        DateTimeOffset now
    ) => new(Guid.CreateVersion7(now), map, mode, gatheringWindow, startsAt: null, now);

    /// <summary>
    /// Roster w stabilnej kolejności: od najdawniej obecnego, z identyfikatorem jako
    /// rozstrzygnięciem remisu. Sortowanie jest tu, a nie w warstwie wyżej, bo to
    /// właściwość lobby, a nie sposobu jego wyświetlenia.
    /// </summary>
    public IReadOnlyList<LobbyPlayer> Roster() =>
        [.. players.Values.OrderBy(p => p.JoinedAt).ThenBy(p => p.PlayerId)];

    public bool Contains(Guid playerId) => players.ContainsKey(playerId);

    /// <summary>
    /// Dołącza gracza. Operacja jest idempotentna — powtórne wywołanie (odświeżenie
    /// strony, druga karta, reconnect) odświeża nick i kolor, ale <b>zachowuje</b>
    /// <see cref="LobbyPlayer.JoinedAt"/>, żeby gracz nie przeskakiwał na koniec listy.
    /// </summary>
    public JoinResult Join(Guid playerId, Nickname nickname, HsvColor color, DateTimeOffset now)
    {
        if (State is not LobbyState.Gathering)
        {
            return JoinResult.NotGathering;
        }

        if (players.ContainsKey(playerId))
        {
            Refresh(playerId, nickname, color);
            return JoinResult.AlreadyJoined;
        }

        if (IsFull)
        {
            return JoinResult.Full;
        }

        players.Add(playerId, new LobbyPlayer(playerId, nickname, color, now));
        return JoinResult.Joined;
    }

    /// <summary>
    /// Odświeża nick i kolor gracza już obecnego w lobby.
    /// </summary>
    /// <remarks>
    /// Roster trzyma <b>kopię</b> profilu z chwili dołączenia, więc gracz, który wszedł
    /// na swój profil i zmienił nick, byłby dla pozostałych nadal podpisany po staremu.
    /// <see cref="LobbyPlayer.JoinedAt"/> zostaje nietknięte — zmiana nicku nie może
    /// przesuwać nikogo na koniec listy.
    /// </remarks>
    /// <returns><c>true</c>, jeśli coś się faktycznie zmieniło.</returns>
    public bool Refresh(Guid playerId, Nickname nickname, HsvColor color)
    {
        if (!players.TryGetValue(playerId, out var existing))
        {
            return false;
        }

        if (existing.Nickname == nickname && existing.Color == color)
        {
            return false;
        }

        players[playerId] = existing with { Nickname = nickname, Color = color };
        return true;
    }

    /// <returns><c>true</c>, jeśli gracz faktycznie był w lobby i został usunięty.</returns>
    public bool Leave(Guid playerId) => players.Remove(playerId);

    /// <summary>
    /// Popycha lobby do przodu względem podanej chwili. Wołane cyklicznie przez zegar;
    /// bezpieczne przy dowolnej częstotliwości, bo decyzja zależy wyłącznie od
    /// <see cref="StartsAt"/>, a nie od tego, ile czasu minęło od poprzedniego wywołania.
    /// </summary>
    public LobbyTick Advance(DateTimeOffset now)
    {
        if (State is not LobbyState.Gathering || StartsAt is not { } startsAt || now < startsAt)
        {
            return LobbyTick.Idle;
        }

        // Puste lobby nie ma z czego zrobić meczu. Zamiast blokować się w oczekiwaniu na
        // pierwszego gracza, okno startuje od nowa — licznik biegnie cyklicznie zawsze.
        if (players.Count == 0)
        {
            StartsAt = now + GatheringWindow;
            return LobbyTick.WindowReset;
        }

        State = LobbyState.Starting;
        return LobbyTick.Started;
    }

    /// <summary>
    /// Domyka lobby po oddaniu meczu. Od tej chwili nie da się już nic z nim zrobić —
    /// jedyną kontynuacją jest otwarcie nowego.
    /// </summary>
    public void Close() => State = LobbyState.Closed;
}
