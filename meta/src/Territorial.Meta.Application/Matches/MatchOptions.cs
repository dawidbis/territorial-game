namespace Territorial.Meta.Application.Matches;

public sealed class MatchOptions
{
    public const string SectionName = "Match";

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
    /// Zgodnie z D9 klient wchodzi jednym wejściem na 443, a routing idzie po ścieżce —
    /// dlatego <c>host:port</c> procesu nigdy nie opuszcza meta.
    /// </remarks>
    public string MatchWebSocketBaseUrl { get; init; } = "wss://localhost:5001/match";

    /// <summary>
    /// Adres wewnętrzny zwracany przez atrapę alokatora.
    /// </summary>
    /// <remarks>
    /// Etap 1 nie ma czego uruchamiać — pod tym adresem nikt nie nasłuchuje i to jest
    /// w porządku. Chodzi o to, żeby cała ścieżka „lobby → mecz → bilet → nowe lobby"
    /// działała i dała się obejrzeć bez jednej linii C++.
    /// </remarks>
    public string FakeAllocatorEndpoint { get; init; } = "127.0.0.1:5101";
}
