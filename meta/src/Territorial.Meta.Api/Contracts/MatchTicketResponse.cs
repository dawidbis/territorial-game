namespace Territorial.Meta.Api.Contracts;

/// <summary>
/// Świeży bilet do trwającego meczu.
/// </summary>
/// <remarks>
/// Ten sam komplet co w <c>MatchReady</c> minus identyfikator meczu — ten wołający ma już
/// w adresie żądania. Adres jest odczytywany z meczu, a nie składany tutaj, żeby nie mieć
/// dwóch źródeł prawdy o tym, dokąd gracz ma się połączyć.
/// </remarks>
public sealed record MatchTicketResponse(string Ticket, string WsUrl, DateTimeOffset ExpiresAt);
