using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Territorial.Meta.Api.Auth;
using Territorial.Meta.Api.Contracts;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Controllers;

[ApiController]
[Route("api/matches")]
[Authorize]
public sealed class MatchesController(IMatchRepository matches, MatchTicketService tickets)
    : ControllerBase{
    /// <summary>
    /// Wydaje wołającemu świeży bilet do meczu, w którym gra.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Trzy powody, dla których to nie jest dodatek: bilet żyje minutę, więc gracz z kartą
    /// w tle zdąży przegapić <c>MatchReady</c>; dostarczenie do części graczy mogło się nie
    /// udać, a proces trzyma ich sloty; i wreszcie jest to dokładnie ta sama ścieżka,
    /// której wymaga powrót do trwającego meczu po zerwaniu połączenia (D14).
    /// </para>
    /// <para>
    /// <b>404 na wszystko, co nie jest zaproszeniem.</b> Nie ma tu rozróżnienia „nie ma
    /// takiego meczu" od „nie grasz w nim" — inaczej endpoint odpowiadałby na pytanie, kto
    /// gra w meczu o podanym identyfikatorze, komukolwiek, kto ma token gościa.
    /// </para>
    /// </remarks>
    [HttpPost("{matchId:guid}/ticket")]
    [ProducesResponseType<MatchTicketResponse>(StatusCodes.Status200OK)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<ActionResult<MatchTicketResponse>> IssueTicket(
        Guid matchId,
        CancellationToken cancellationToken
    ){
        if (User.GetPlayerId() is not { } playerId)
        {
            return Unauthorized();
        }

        var match = await matches.GetAsync(matchId, cancellationToken);

        // Mecz w trakcie alokacji jeszcze nie ma adresu, a zakończony albo nieudany nie
        // przyjmie już nikogo — w obu przypadkach nie ma czego wydawać.
        if (match?.WsUrl is not { } wsUrl || !match.IsLive)
        {
            return NotFound();
        }

        if (match.ParticipantOf(playerId) is not { } participant)
        {
            return NotFound();
        }

        var ticket = tickets.Issue(playerId, match.Id, participant.Slot);

        return Ok(new MatchTicketResponse(ticket.Value, wsUrl, ticket.ExpiresAt));
    }
}
