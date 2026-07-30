namespace Territorial.Meta.Application.Matches.Contracts;

/// <summary>
/// Zaproszenie do meczu — wysyłane <b>wyłącznie</b> do jednego gracza.
/// </summary>
/// <remarks>
/// <para>
/// Bilet jest poświadczeniem na konkretny slot, więc ta wiadomość nie ma prawa iść ani
/// przez <c>Clients.All</c>, ani przez grupę członków lobby. Adresowanie po graczu
/// dostarcza ją przy okazji na wszystkie jego karty.
/// </para>
/// <para>
/// Ładunek jest celowo minimalny: mapa, tick rate i lista slotów przyjdą w <c>MatchInit</c>
/// od game-serwera, żeby nie mieć dwóch źródeł prawdy o tym samym meczu. Zostaje
/// <c>ExpiresAt</c>, bo tylko klient wie, kiedy naprawdę próbuje wejść, i to on musi
/// zdecydować o poproszeniu o świeży bilet.
/// </para>
/// </remarks>
public sealed record MatchReadyDto(
    Guid MatchId,
    string WsUrl,
    string Ticket,
    DateTimeOffset ExpiresAt
);
