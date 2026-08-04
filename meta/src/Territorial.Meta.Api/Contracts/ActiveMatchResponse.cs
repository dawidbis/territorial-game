namespace Territorial.Meta.Api.Contracts;

/// <summary>
/// Trwający mecz wołającego razem ze świeżym biletem.
/// </summary>
/// <remarks>
/// Identyfikator meczu jest tu, a nie w adresie żądania, bo o to właśnie pyta klient:
/// „czy gram w czymś, a jeśli tak, to gdzie". Bilet dokładany od razu — gracz i tak
/// potrzebowałby go w następnym żądaniu, a mecz jest stanem wyłącznym, więc odpowiedź
/// twierdząca zawsze kończy się wejściem do gry.
/// </remarks>
public sealed record ActiveMatchResponse(
    Guid MatchId,
    string Ticket,
    string WsUrl,
    DateTimeOffset ExpiresAt
);
