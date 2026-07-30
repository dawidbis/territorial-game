using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Domain.Matches;

/// <summary>
/// Mecz oddany game-serwerowi — pierwsza rzecz w systemie, która musi przetrwać restart.
/// </summary>
/// <remarks>
/// <para>
/// Lobby świadomie nie jest encją bazodanową (patrz <see cref="Lobby"/>), mecz nią
/// <b>musi</b> być: meta musi umieć odpowiedzieć „czy gracz X jest uczestnikiem meczu Y na
/// slocie S", i to po restarcie. Bez tego nie da się ani wydać biletu ponownie, ani przyjąć
/// wyniku.
/// </para>
/// <para>
/// Encja jest zapisywana <b>przed</b> rozmową z orkiestratorem, w stanie
/// <see cref="MatchState.Allocating"/>. Odwrotna kolejność — zapis po udanej alokacji —
/// zostawiałaby po nieudanym starcie stojący gdzieś proces game-serwera, o którym meta
/// nic nie wie.
/// </para>
/// <para>
/// Ziarno przychodzi z zewnątrz, a nie jest losowane tutaj: domena zostaje czysta
/// i testowalna, a zapisane ziarno jest warunkiem replayów z D10 — log komend bez niego
/// nie odtworzy niczego.
/// </para>
/// </remarks>
public sealed class Match
{
    private readonly List<MatchParticipant> participants = [];

    private Match()
    {
        // Konstruktor dla EF Core. MapId ma wartość domyślną, więc nie ma tu "= null!".
    }

    private Match(
        Guid id,
        string mapId,
        GameMode mode,
        int maxActors,
        long seed,
        DateTimeOffset now
    ){
        Id = id;
        MapId = mapId;
        Mode = mode;
        MaxActors = maxActors;
        Seed = seed;
        CreatedAt = now;
    }

    public Guid Id { get; private set; }

    /// <summary>Identyfikator mapy, nie referencja do katalogu — mapy są niezmienne i adresowane po id.</summary>
    public string MapId { get; private set; } = string.Empty;

    public GameMode Mode { get; private set; }

    /// <summary>Sufit aktorów — ludzie i boty razem. Kopia z mapy, bo mecz musi być czytelny sam z siebie.</summary>
    public int MaxActors { get; private set; }

    /// <summary>Ziarno symulacji. Bez zapisanego ziarna replay z D10 nie istnieje.</summary>
    public long Seed { get; private set; }

    /// <summary>
    /// <c>host:port</c> procesu game-serwera; <c>null</c> do zakończenia alokacji.
    /// </summary>
    /// <remarks>
    /// Klient nigdy tego nie widzi — dostaje <see cref="WsUrl"/>. To adres w sieci
    /// wewnętrznej i szczegół, który ma prawo się zmienić.
    /// </remarks>
    public string? Endpoint { get; private set; }

    /// <summary>
    /// Publiczny adres meczu dla klienta (D9); <c>null</c> do zakończenia alokacji.
    /// </summary>
    /// <remarks>
    /// Zapisany, a nie składany na nowo przy każdym pytaniu. Regułę adresu zna alokator —
    /// za wspólnym proxy wychodzi <c>wss://host/match/{matchId}</c>, ale wersja odpalająca
    /// proces lokalnie oddaje adres z portem tego procesu. Gdyby ponowne wydanie biletu
    /// budowało adres samodzielnie, byłoby drugim źródłem prawdy i w dev rozjechałoby się
    /// z tym, co gracz dostał w <c>MatchReady</c>.
    /// </remarks>
    public string? WsUrl { get; private set; }

    public MatchState State { get; private set; }

    public DateTimeOffset CreatedAt { get; private set; }

    /// <summary>Chwila, w której game-serwer był gotów przyjmować graczy.</summary>
    public DateTimeOffset? StartedAt { get; private set; }

    public DateTimeOffset? EndedAt { get; private set; }

    /// <summary>Ludzie w meczu, w kolejności slotów.</summary>
    public IReadOnlyList<MatchParticipant> Participants => participants;

    /// <summary>Ilu ludzi gra. Czyta listę wczytaną do pamięci — nie jest zapytaniem do bazy.</summary>
    public int HumanCount => participants.Count;

    /// <summary>Ilu botów dopełnia mapę. Wyliczane z sufitu, nigdy przechowywane.</summary>
    public int BotCount => Math.Max(0, MaxActors - HumanCount);

    /// <summary>Czy mecz przyjmuje graczy. Tylko wtedy wolno wydać bilet.</summary>
    public bool IsLive => State is MatchState.Live;

    /// <summary>
    /// Znajduje uczestnika po graczu.
    /// </summary>
    /// <remarks>
    /// Pytanie „czy gracz X jest uczestnikiem meczu Y i na jakim slocie" pada w jednym
    /// wywołaniu, bo odpowiedź jest jedna: bez slotu bilet nie ma sensu, a bez uczestnictwa
    /// nie ma slotu.
    /// </remarks>
    /// <returns><c>null</c>, gdy gracz nie gra w tym meczu.</returns>
    public MatchParticipant? ParticipantOf(Guid playerId) =>
        participants.Find(p => p.PlayerId == playerId);

    /// <summary>
    /// Zakłada mecz dla zamrożonego rostera i przypisuje sloty.
    /// </summary>
    /// <remarks>
    /// Czysta funkcja: zero I/O, czas i ziarno z parametrów. Ludzie dostają sloty
    /// <c>1..N</c> w podanej kolejności — a ta jest stabilna, bo roster lobby sortuje się
    /// po <c>JoinedAt</c> i <c>PlayerId</c>. Boty zajmują <c>N+1..MaxActors</c> i nie mają
    /// wiersza w bazie.
    /// </remarks>
    /// <exception cref="ArgumentException">Roster jest pusty.</exception>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Sufit mapy wychodzi poza <see cref="ActorSlot.MaxActorsPerMatch"/> albo roster go przekracza.
    /// </exception>
    public static Match Create(
        MapDefinition map,
        GameMode mode,
        long seed,
        IReadOnlyList<LobbyPlayer> roster,
        DateTimeOffset now
    ){
        ArgumentOutOfRangeException.ThrowIfLessThan(map.MaxActors, ActorSlot.FirstActor);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(map.MaxActors, ActorSlot.MaxActorsPerMatch);

        if (roster.Count == 0)
        {
            throw new ArgumentException(
                "Mecz bez ludzi nie ma sensu — puste lobby otwiera okno zbierania od nowa.",
                nameof(roster)
            );
        }

        ArgumentOutOfRangeException.ThrowIfGreaterThan(roster.Count, map.MaxActors);

        var match = new Match(Guid.CreateVersion7(now), map.Id, mode, map.MaxActors, seed, now);

        for (var position = 0; position < roster.Count; position++)
        {
            var player = roster[position];

            match.participants.Add(
                new MatchParticipant(
                    match.Id,
                    player.PlayerId,
                    ActorSlot.ForRosterPosition(position),
                    player.Nickname,
                    player.Color
                )
            );
        }

        return match;
    }

    /// <summary>Odnotowuje udaną alokację: mecz ma adres i przyjmuje graczy.</summary>
    public void MarkLive(string endpoint, string wsUrl, DateTimeOffset now)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(wsUrl);

        if (State is not MatchState.Allocating)
        {
            throw new InvalidOperationException(
                $"Mecz {Id} jest w stanie {State} — nie da się go uruchomić po raz drugi."
            );
        }

        Endpoint = endpoint;
        WsUrl = wsUrl;
        State = MatchState.Live;
        StartedAt = now;
    }

    /// <summary>
    /// Odnotowuje nieudaną alokację.
    /// </summary>
    /// <remarks>
    /// Świadomie <b>nie rzuca</b> przy powtórnym wywołaniu ani przy meczu w innym stanie:
    /// wołane jest ze ścieżki obsługi awarii, a wyjątek w takim miejscu przesłoniłby
    /// przyczynę, dla której się tam trafiło.
    /// </remarks>
    public void MarkFailed(DateTimeOffset now)
    {
        if (State is not MatchState.Allocating)
        {
            return;
        }

        State = MatchState.Failed;
        EndedAt = now;
    }
}
