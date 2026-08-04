namespace Territorial.Meta.Application.Matches;

public sealed class MatchOptions
{
    public const string SectionName = "Match";

    /// <summary>Atrapa alokatora — wartość <see cref="Allocator"/> wybierająca <c>FakeMatchAllocator</c>.</summary>
    public const string FakeAllocator = "Fake";

    /// <summary>Prawdziwy proces na tej samej maszynie — wartość <see cref="Allocator"/> dla dev.</summary>
    public const string LocalProcessAllocator = "LocalProcess";

    /// <summary>
    /// Ile razy łącznie próbujemy dogadać się z orkiestratorem.
    /// </summary>
    /// <remarks>
    /// Kilka szybkich prób, a nie wykładnicze wycofywanie: po drugiej stronie czeka
    /// stu graczy z zamrożonym rosterem, więc lepiej szybko przyznać się do porażki
    /// i otworzyć nowe lobby, niż trzymać ich przez pół minuty na ekranie startu.
    /// </remarks>
    public int AllocationAttempts { get; init; } = 3;

    public int AllocationRetryDelayMilliseconds { get; init; } = 250;

    /// <summary>
    /// Ważność biletu meczowego.
    /// </summary>
    /// <remarks>
    /// Minuta wystarcza na przejście z lobby do gry, a jest za krótka, żeby przechwycony
    /// bilet dało się wykorzystać później. Gracz z kartą w tle, który przegapi okno,
    /// poprosi o nowy — to jest ta sama ścieżka co reconnect (D14).
    /// </remarks>
    public int TicketLifetimeSeconds { get; init; } = 60;

    /// <summary>
    /// Prefiks publicznego adresu meczu; pełny adres to <c>{prefiks}/{matchId}</c>.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Zgodnie z D9 klient wchodzi jednym wejściem na 443, a routing idzie po ścieżce —
    /// dlatego <c>host:port</c> procesu nigdy nie opuszcza meta.
    /// </para>
    /// <para>
    /// Segment <c>/ws/</c> oddziela gniazdo od trasy SPA <c>/match/{matchId}</c>. Bez niego
    /// odświeżenie strony w trakcie meczu wysyła żądanie dokumentu HTML pod adres, na którym
    /// stoi WebSocket — i gracz zamiast mapy dostaje komunikat o brakującym upgrade'zie.
    /// </para>
    /// </remarks>
    public string MatchWebSocketBaseUrl { get; init; } = "wss://localhost:5001/ws/match";

    /// <summary>
    /// Klucz prywatny ECDSA P-256 (PEM) do podpisywania biletów.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Nigdy nie trafia do repozytorium — w dev z user-secrets, w produkcji z sekretów hosta.
    /// Pusty w środowisku deweloperskim oznacza klucz generowany na czas życia procesu;
    /// poza dev pusta wartość zatrzymuje start, bo bilety podpisane kluczem ginącym przy
    /// restarcie to awaria, która ujawnia się dopiero po wdrożeniu.
    /// </para>
    /// <para>
    /// Asymetryczny, w odróżnieniu od tokenu gracza (HS256): bilet weryfikuje game-serwer,
    /// który nie ma i nie ma mieć dostępu do niczego, czym da się podpisywać.
    /// </para>
    /// </remarks>
    public string TicketPrivateKeyPem { get; init; } = string.Empty;

    /// <summary>
    /// Dokąd zapisać klucz publiczny przy starcie; pusta wartość wyłącza zapis.
    /// </summary>
    /// <remarks>
    /// Game-serwer dostaje ten plik przez <c>--ticket-key</c> i weryfikuje nim bilety offline
    /// (§4.3). Zapis przy starcie zamiast ręcznego eksportu, bo klucz publiczny nie jest
    /// sekretem, a ręczny krok rozjeżdża się z kluczem prywatnym dokładnie wtedy, gdy ten
    /// się zmieni.
    /// </remarks>
    public string TicketPublicKeyPath { get; init; } = "App_Data/ticket.pub";

    /// <summary>
    /// Adres wewnętrzny zwracany przez atrapę alokatora.
    /// </summary>
    /// <remarks>
    /// Etap 1 nie ma czego uruchamiać — pod tym adresem nikt nie nasłuchuje i to jest
    /// w porządku. Chodzi o to, żeby cała ścieżka „lobby → mecz → bilet → nowe lobby"
    /// działała i dała się obejrzeć bez jednej linii C++.
    /// </remarks>
    public string FakeAllocatorEndpoint { get; init; } = "127.0.0.1:5101";

    /// <summary>
    /// Który alokator obsługuje starty meczów: <see cref="FakeAllocator"/> albo
    /// <see cref="LocalProcessAllocator"/>.
    /// </summary>
    /// <remarks>
    /// Konfiguracja, a nie środowisko: brak zbudowanej binarki C++ nie ma zamieniać dev-a
    /// w serwis, w którym każde lobby kończy się awarią. Powrót do atrapy jest wtedy jedną
    /// wartością w pliku, nie rekompilacją.
    /// </remarks>
    public string Allocator { get; init; } = FakeAllocator;

    /// <summary>Czy starty meczów idą przez prawdziwy proces na tej maszynie.</summary>
    public bool UsesLocalProcess =>
        string.Equals(Allocator, LocalProcessAllocator, StringComparison.OrdinalIgnoreCase);

    /// <summary>Ścieżka do binarki game-serwera; wymagana przy <see cref="LocalProcessAllocator"/>.</summary>
    public string GameServerPath { get; init; } = string.Empty;

    /// <summary>Katalog z plikami <c>.tmap</c>; proces dostaje z niego <c>{mapId}.tmap</c>.</summary>
    /// <remarks>
    /// Tym samym katalogiem serwowany jest teren dla klienta pod <c>/maps/...</c> w dev —
    /// jedno źródło, więc suma kontrolna z <c>MatchInit</c> ma szansę zgodzić się z plikiem,
    /// który przeglądarka ma w cache'u.
    /// </remarks>
    public string MapsRoot { get; init; } = "maps";

    /// <summary>
    /// Pierwszy port pętli zwrotnej puli, na której stają procesy meczów.
    /// </summary>
    public int GameServerPort { get; init; } = 5101;

    /// <summary>
    /// Ile kolejnych portów, licząc od <see cref="GameServerPort"/>, obejmuje pula.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>Jeden port znaczyłby jeden mecz naraz na całej maszynie</b> — a to nie jest
    /// ograniczenie przejściowe, tylko trwające tyle, ile cały trwający mecz. Gracz, który
    /// z niego wyszedł i wrócił do kolejki, nie mógł zacząć następnego, dopóki grali w tamtym
    /// pozostali: każde lobby otwarte w tym czasie kończyło się komunikatem o awarii.
    /// </para>
    /// <para>
    /// Sufit jest niski, bo to pula deweloperska: na produkcji porty przydziela agent
    /// z własnej puli, a proces meta nie ma tam nic do wybierania.
    /// </para>
    /// </remarks>
    public int GameServerPortCount { get; init; } = 8;

    /// <summary>Porty puli w kolejności przydzielania.</summary>
    public IEnumerable<int> GameServerPorts =>
        Enumerable.Range(GameServerPort, Math.Max(1, GameServerPortCount));

    /// <summary>
    /// Numer wpisu w puli dla danego portu.
    /// </summary>
    /// <remarks>
    /// Trafia do adresu WebSocketa w dev jako segment <c>gsN</c>, bo proxy dev-servera nie
    /// umie wybierać celu per żądanie i każdy port puli ma tam własny statyczny wpis. Numer
    /// wpisu, a <b>nie</b> port: D9 mówi, że klient nie ogląda adresów sieci wewnętrznej.
    /// </remarks>
    /// <param name="port">Port z puli, na którym stanął proces meczu.</param>
    public int SlotOfPort(int port) => port - GameServerPort;

    /// <summary>
    /// Ile czekamy, aż proces zacznie nasłuchiwać.
    /// </summary>
    /// <remarks>
    /// Proces wczytuje mapę i liczy jej sumę kontrolną, zanim otworzy gniazdo, więc chwilę
    /// to trwa. Po drugiej stronie czeka stu graczy z zamrożonym rosterem — lepiej przyznać
    /// się do porażki i otworzyć nowe lobby, niż trzymać ich na ekranie startu.
    /// </remarks>
    public int ReadinessTimeoutMilliseconds { get; init; } = 15_000;

    /// <summary>
    /// Ile czekamy, aż w puli zwolni się jakikolwiek port.
    /// </summary>
    /// <remarks>
    /// Zajęty port jest stanem <b>przejściowym i normalnym</b>, nie awarią: proces dożywa
    /// okna reconnectu i zwalnia gniazdo sam. Czekanie jest ograniczone, bo po drugiej
    /// stronie stoi zamrożony roster — przy puli zajętej w całości lepiej szybko przyznać
    /// się do porażki niż trzymać graczy na ekranie startu.
    /// </remarks>
    public int PortWaitMilliseconds { get; init; } = 30_000;

    /// <summary>
    /// Czy proces meczu ma dopełnić obsadę botami do sufitu aktorów mapy.
    /// </summary>
    /// <remarks>
    /// Wyłączone daje mecz wyłącznie z ludzi — reszta mapy zostaje pustkowiem. Boty nie
    /// podejmują dziś żadnych decyzji, więc mecz z dziewięćdziesięcioma dziewięcioma z nich
    /// jest meczem z dziewięćdziesięcioma dziewięcioma nieruchomymi celami; do czasu, aż
    /// dostaną własną logikę, lepiej ich nie stawiać. Sufit aktorów zostaje nietknięty —
    /// to on ogranicza zakres slotów w bilecie.
    /// </remarks>
    public bool FillWithBots { get; init; } = true;

    /// <summary>
    /// Okno bezczynności procesu meczu w sekundach; 0 zostawia jego wartość domyślną (120 s).
    /// </summary>
    /// <remarks>
    /// <b>Wyłącznie do dev.</b> Na maszynie deweloperskiej stoi jeden mecz naraz, więc
    /// dwuminutowe czekanie na powrót gracza znaczy dwie minuty bez możliwości zaczęcia
    /// następnego meczu. Na produkcji zostaje 120 s, bo tyle trwa okno reconnectu obiecane
    /// graczowi (D14) — i tam porty są z puli, więc nic na siebie nie czeka.
    /// </remarks>
    public int MatchIdleSeconds { get; init; }
}
