using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Territorial.Meta.Api.Auth;
using Territorial.Meta.Api.Contracts;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Application.Players.Contracts;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Api.Controllers;

[ApiController]
[Route("api/players")]
[Authorize]
public sealed class PlayersController(
    GetOrCreatePlayer getOrCreatePlayer,
    UpdatePlayerProfile updatePlayerProfile,
    PlayerTokenService tokens,
    CurrentLobby lobby
) : ControllerBase{
    /// <summary>
    /// Zwraca profil odwiedzającego wraz ze świeżym tokenem, zakładając gracza przy
    /// pierwszej wizycie.
    /// </summary>
    /// <remarks>
    /// Jedyny anonimowy endpoint w serwisie — to nim wchodzi się do systemu. Żądanie bez
    /// tokenu zakłada gościa; żądanie z tokenem wskazującym gracza, którego już nie ma,
    /// też zakłada gościa, zamiast zwracać błąd, którego klient nie umiałby naprawić.
    /// Token wraca przy każdym wywołaniu, więc wejście na stronę odnawia sesję.
    /// </remarks>
    [HttpGet("me")]
    [AllowAnonymous]
    [ProducesResponseType<PlayerSessionResponse>(StatusCodes.Status200OK)]
    public async Task<ActionResult<PlayerSessionResponse>> GetMe(
        CancellationToken cancellationToken
    ){
        var profile = await getOrCreatePlayer.ExecuteAsync(User.GetPlayerId(), cancellationToken);

        // Token wystawiany jest na identyfikator ZWRÓCONY przez use case, a nie na ten
        // z żądania — gdy stary gracz zniknął z bazy, klient dostaje token nowego gościa.
        var token = tokens.Issue(profile.Id);

        return Ok(new PlayerSessionResponse(profile, token.Value, token.ExpiresAt));
    }

    /// <summary>
    /// Zmienia nick i kolor zalogowanego gracza.
    /// </summary>
    [HttpPut("me")]
    [ProducesResponseType<PlayerProfileDto>(StatusCodes.Status200OK)]
    [ProducesResponseType<ValidationProblemDetails>(StatusCodes.Status400BadRequest)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<ActionResult<PlayerProfileDto>> UpdateMe(
        [FromBody] UpdatePlayerProfileRequest request,
        CancellationToken cancellationToken
    ){
        // Tożsamość pochodzi z podpisanego tokenu, nie z nagłówka sterowanego przez klienta.
        // Wcześniejsze X-Player-Id pozwalało jednym curl-em przejąć dowolny profil.
        if (User.GetPlayerId() is not { } playerId)
        {
            return Unauthorized();
        }

        // TryCreate, a nie Create: atrybuty walidacyjne widzą surowy tekst, domena widzi
        // tekst przycięty. "  ab  " ma sześć znaków, więc StringLength przepuszcza,
        // a Nickname odrzuca. Bez tej gałęzi byłby to wyjątek i 500 zamiast 400.
        if (!Nickname.TryCreate(request.Nickname, out var nickname))
        {
            ModelState.AddModelError(
                nameof(request.Nickname),
                $"Nick musi mieć po przycięciu {Nickname.MinLength}-{Nickname.MaxLength} znaków "
                    + "i składać się z liter, cyfr, '-' lub '_'."
            );

            return ValidationProblem(ModelState);
        }

        // Zakresy pilnowane atrybutami [Range] na HsvColorRequest, więc Create nie rzuci.
        var color = HsvColor.Create(
            request.Color.Hue,
            request.Color.Saturation,
            request.Color.Value
        );

        var command = new UpdatePlayerProfileCommand(playerId, nickname, color);
        var profile = await updatePlayerProfile.ExecuteAsync(command, cancellationToken);

        if (profile is null)
        {
            return NotFound();
        }

        // Roster trzyma kopię profilu z chwili dołączenia, więc gracz zmieniający nick
        // w trakcie siedzenia w lobby byłby dla pozostałych nadal podpisany po staremu.
        // Operacja jest cicha, gdy gracza w lobby nie ma; rozgłoszeniem zajmie się zegar.
        lobby.RefreshPlayer(playerId, nickname, color);

        return Ok(profile);
    }
}
