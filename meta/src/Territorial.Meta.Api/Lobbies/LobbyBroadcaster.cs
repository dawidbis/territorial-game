using Microsoft.AspNetCore.SignalR;
using Territorial.Meta.Api.Hubs;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Lobbies.Contracts;
using Territorial.Meta.Application.Matches.Contracts;

namespace Territorial.Meta.Api.Lobbies;

/// <summary>
/// Jedyne miejsce, w którym zapisana jest reguła podziału odbiorców: nagłówek widzą
/// wszyscy, roster tylko członkowie lobby, bilet wyłącznie jego właściciel.
/// </summary>
/// <remarks>
/// <para>
/// Pełny roster przy stu graczach to kilka kilobajtów. Rozsyłanie go każdemu, kto tylko
/// otworzył stronę główną, byłoby setkami kilobajtów na sekundę — a strona główna i tak
/// pokazuje wyłącznie liczby z nagłówka.
/// </para>
/// <para>
/// Klasa konkretna, bez interfejsu: wołają ją wyłącznie <see cref="LobbyClock"/>
/// i <see cref="MatchLauncher"/>, które mieszkają w tej samej warstwie. Port miałby sens,
/// gdyby rozgłaszała warstwa aplikacji.
/// </para>
/// </remarks>
public sealed class LobbyBroadcaster(IHubContext<LobbyHub, ILobbyClient> hub)
{
    public async Task PublishAsync(LobbySnapshot snapshot)
    {
        await hub.Clients.All.LobbyHeader(snapshot.Header);

        await hub
            .Clients.Group(LobbyHub.MembersGroup)
            .LobbyRoster(new LobbyRosterDto(snapshot.Header.LobbyId, snapshot.Players));
    }

    /// <summary>
    /// Wysyła zaproszenie do meczu jednemu graczowi, na wszystkie jego karty.
    /// </summary>
    /// <remarks>
    /// Identyfikator w postaci tekstowej musi zgadzać się co do znaku z tym, co oddaje
    /// <see cref="Auth.PlayerUserIdProvider"/> — po obu stronach jest to
    /// <c>Guid.ToString()</c> w formacie „D".
    /// </remarks>
    public async Task MatchReadyAsync(Guid playerId, MatchReadyDto match) =>
        await hub.Clients.User(playerId.ToString()).MatchReady(match);

    /// <summary>Informuje czekających, że z tego lobby nie będzie meczu.</summary>
    public async Task MatchStartFailedAsync(MatchStartFailedDto failure) =>
        await hub.Clients.Group(LobbyHub.MembersGroup).MatchStartFailed(failure);
}
