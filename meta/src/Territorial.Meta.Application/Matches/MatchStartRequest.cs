using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Zamrożony roster oddany do uruchomienia meczu.
/// </summary>
/// <remarks>
/// <para>
/// Powstaje pod lockiem lobby, w tym samym momencie, w którym lobby wchodzi w
/// <see cref="LobbyState.Starting"/> — dzięki temu launcher pracuje na liście, która
/// nie może się już zmienić, i nie musi wracać po nią do lobby.
/// </para>
/// <para>
/// Kolejność <c>Roster</c> jest znacząca: to z niej wynikają sloty <c>1..N</c>.
/// </para>
/// </remarks>
public sealed record MatchStartRequest(
    Guid LobbyId,
    MapDefinition Map,
    GameMode Mode,
    IReadOnlyList<LobbyPlayer> Roster
);
