namespace Territorial.Meta.Application.Lobbies.Contracts;

/// <summary>
/// Lista graczy w lobby. Trafia wyłącznie do tych, którzy dołączyli.
/// </summary>
/// <remarks>
/// <c>LobbyId</c> pozwala klientowi odrzucić roster należący do lobby, którego już nie
/// ogląda — bez tego wiadomość wyprzedzająca nagłówek pokazałaby listę z poprzedniej rundy.
/// </remarks>
public sealed record LobbyRosterDto(Guid LobbyId, IReadOnlyList<LobbyPlayerDto> Players);
