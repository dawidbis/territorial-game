using Territorial.Meta.Application.Lobbies.Contracts;

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
}
