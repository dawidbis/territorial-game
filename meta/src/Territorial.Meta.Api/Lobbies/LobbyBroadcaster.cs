using Microsoft.AspNetCore.SignalR;
using Territorial.Meta.Api.Hubs;
using Territorial.Meta.Application.Lobbies.Contracts;

namespace Territorial.Meta.Api.Lobbies;

/// <summary>
/// Jedyne miejsce, w którym zapisana jest reguła podziału odbiorców: nagłówek widzą
/// wszyscy, roster tylko członkowie lobby.
/// </summary>
/// <remarks>
/// <para>
/// Pełny roster przy stu graczach to kilka kilobajtów. Rozsyłanie go każdemu, kto tylko
/// otworzył stronę główną, byłoby setkami kilobajtów na sekundę — a strona główna i tak
/// pokazuje wyłącznie liczby z nagłówka.
/// </para>
/// <para>
/// Klasa konkretna, bez interfejsu: jedynym wołającym jest <see cref="LobbyClock"/>,
/// który mieszka w tej samej warstwie. Port miałby sens, gdyby rozgłaszała warstwa
/// aplikacji — po przeniesieniu całego broadcastu do zegara nie ma już czego odwracać.
/// </para>
/// </remarks>
public sealed class LobbyBroadcaster(IHubContext<LobbyHub, ILobbyClient> hub)
{
    public async Task PublishAsync(LobbySnapshot snapshot)
    {
        await hub.Clients.All.LobbyHeader(snapshot.Header);

        await hub.Clients.Group(LobbyHub.MembersGroup)
            .LobbyRoster(new LobbyRosterDto(snapshot.Header.LobbyId, snapshot.Players));
    }
}
