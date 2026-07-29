using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Lobbies.Contracts;

namespace Territorial.Meta.Api.Controllers;

[ApiController]
[Route("api/lobby")]
public sealed class LobbyController(CurrentLobby lobby) : ControllerBase
{
    /// <summary>
    /// Nagłówek aktualnego lobby — liczba graczy, mapa, tryb, odliczanie.
    /// </summary>
    /// <remarks>
    /// Aktualizacje na żywo idą hubem; ten endpoint istnieje na pierwszy paint i na
    /// wypadek środowiska bez WebSocketów. Anonimowy, bo to publiczna informacja
    /// widoczna na stronie głównej. Rostera tu nie ma — ten należy się wyłącznie
    /// graczom, którzy do lobby dołączyli.
    /// </remarks>
    [HttpGet]
    [AllowAnonymous]
    [ProducesResponseType<LobbyHeaderDto>(StatusCodes.Status200OK)]
    public ActionResult<LobbyHeaderDto> Get() => Ok(lobby.Snapshot().Header);
}
