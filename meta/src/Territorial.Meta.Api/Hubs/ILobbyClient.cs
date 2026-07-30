using Territorial.Meta.Application.Lobbies.Contracts;
using Territorial.Meta.Application.Matches.Contracts;

namespace Territorial.Meta.Api.Hubs;

/// <summary>
/// Wiadomości serwer → klient. Interfejs zamiast magicznych stringów: literówka
/// w nazwie metody staje się błędem kompilacji, a nie ciszą po stronie klienta.
/// </summary>
public interface ILobbyClient
{
    /// <summary>Do wszystkich połączonych — także tych, którzy tylko oglądają stronę główną.</summary>
    Task LobbyHeader(LobbyHeaderDto header);

    /// <summary>Wyłącznie do graczy, którzy dołączyli do lobby.</summary>
    Task LobbyRoster(LobbyRosterDto roster);

    /// <summary>
    /// Wyłącznie do jednego gracza — bilet jest poświadczeniem na jego slot.
    /// </summary>
    /// <remarks>
    /// Ani <c>Clients.All</c>, ani grupa członków lobby. Adresowanie po graczu wymaga
    /// <see cref="Auth.PlayerUserIdProvider"/>, bez którego wysyłka trafiłaby w pustkę.
    /// </remarks>
    Task MatchReady(MatchReadyDto match);

    /// <summary>Do członków lobby, którego start się nie powiódł.</summary>
    Task MatchStartFailed(MatchStartFailedDto failure);
}
