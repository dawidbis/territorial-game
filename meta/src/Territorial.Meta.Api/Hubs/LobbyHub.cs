using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.SignalR;
using Territorial.Meta.Api.Auth;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Lobbies.Contracts;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Api.Hubs;

/// <summary>
/// Kanał lobby. Zgodnie z D11 to SignalR i JSON — kilka wiadomości na minutę, więc
/// protobuf byłby narzutem bez korzyści.
/// </summary>
/// <remarks>
/// <para>
/// Każde połączenie jest uwierzytelnione, ale <b>nie</b> oznacza od razu członkostwa:
/// strona główna łączy się tylko po to, żeby widzieć nagłówek na żywo. Dopiero
/// <see cref="Join"/> wprowadza gracza do rostera i do grupy odbierającej pełną listę.
/// </para>
/// <para>
/// Hub <b>niczego nie rozgłasza</b> — odpowiada wyłącznie wołającemu. Fan-out do
/// pozostałych robi zegar raz na sekundę, dzięki czemu seria dołączeń schodzi do jednej
/// wiadomości. Patrz <see cref="CurrentLobby"/>.
/// </para>
/// </remarks>
[Authorize]
public sealed class LobbyHub(CurrentLobby lobby, IPlayerRepository players)
    : Hub<ILobbyClient>{
    /// <summary>Grupa odbierająca roster. Trafiają do niej wyłącznie połączenia po <see cref="Join"/>.</summary>
    public const string MembersGroup = "lobby-members";

    public override async Task OnConnectedAsync()
    {
        if (Context.User?.GetPlayerId() is not { } playerId)
        {
            Context.Abort();
            return;
        }

        var snapshot = lobby.TrackConnection(Context.ConnectionId, playerId);

        await Clients.Caller.LobbyHeader(snapshot.Header);

        await base.OnConnectedAsync();
    }

    public override async Task OnDisconnectedAsync(Exception? exception)
    {
        // Gracz zostaje w rosterze przez czas karencji — odświeżenie strony nie może
        // mrugać listą u pozostałych. Skreśleniem zajmie się zegar.
        lobby.Disconnect(Context.ConnectionId);

        await base.OnDisconnectedAsync(exception);
    }

    /// <summary>
    /// Dołącza wołającego do lobby.
    /// </summary>
    /// <remarks>
    /// Idempotentne — widok lobby woła to po każdym (re)połączeniu, więc odświeżenie
    /// strony wraca do lobby samo. Nick i kolor czytane są z bazy, a nie z tokenu, żeby
    /// roster nigdy nie pokazywał nieaktualnych danych.
    /// </remarks>
    /// <returns>
    /// Nazwa <see cref="JoinResult"/> — klient musi umieć pokazać komplet, zamknięte lobby
    /// i nieaktualną sesję.
    /// </returns>
    public async Task<string> Join()
    {
        // Hub jest [Authorize], a OnConnectedAsync zrywa połączenie bez tożsamości, więc
        // tu już się nie da wejść. Gałąź zostaje, bo brak identyfikatora i brak gracza to
        // dla klienta ten sam problem: token, którym się przedstawił, jest bezużyteczny.
        if (Context.User?.GetPlayerId() is not { } playerId)
        {
            return JoinResult.UnknownPlayer.ToString();
        }

        var player = await players.GetAsync(playerId, Context.ConnectionAborted);

        if (player is null)
        {
            // Token jest ważny, ale gracza nie ma w bazie — na przykład po skasowaniu pliku
            // bazy w dev. Klient odnowi tożsamość przez GET /api/players/me i wróci z nowym
            // handshake'iem; samo ponowienie Join by nie pomogło, bo tożsamość połączenia
            // ustala się raz, przy jego otwarciu.
            return JoinResult.UnknownPlayer.ToString();
        }

        var (result, snapshot) = lobby.Join(playerId, player.Nickname, player.Color);

        if (result is JoinResult.Joined or JoinResult.AlreadyJoined)
        {
            await Groups.AddToGroupAsync(
                Context.ConnectionId,
                MembersGroup,
                Context.ConnectionAborted
            );

            // Roster dla wołającego natychmiast — nie każemy mu czekać do sekundy
            // na najbliższy tik zegara, żeby zobaczyć, do czego dołączył.
            await Clients.Caller.LobbyRoster(
                new LobbyRosterDto(snapshot.Header.LobbyId, snapshot.Players)
            );
        }

        return result.ToString();
    }

    /// <summary>Wychodzi z lobby, zostawiając połączenie żywe — gracz nadal widzi nagłówek.</summary>
    public async Task Leave()
    {
        if (Context.User?.GetPlayerId() is not { } playerId)
        {
            return;
        }

        lobby.Leave(playerId);

        await Groups.RemoveFromGroupAsync(
            Context.ConnectionId,
            MembersGroup,
            Context.ConnectionAborted
        );
    }
}
