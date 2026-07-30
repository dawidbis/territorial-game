namespace Territorial.Meta.Application.Matches.Contracts;

/// <summary>
/// Start się nie powiódł — mecz nie powstanie, a lobby zostaje otwarte na nowo.
/// </summary>
/// <remarks>
/// <c>Reason</c> jest tekstem dla gracza i nie niesie żadnego szczegółu awarii: adresy
/// wewnętrzne, kody odpowiedzi orkiestratora i treści wyjątków zostają w logach serwera.
/// Idzie do grupy członków lobby, bo dotyczy wszystkich, którzy czekali na ten start.
/// </remarks>
public sealed record MatchStartFailedDto(string Reason);
