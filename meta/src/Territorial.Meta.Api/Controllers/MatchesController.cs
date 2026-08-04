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
public sealed class MatchesController(
    IMatchRepository matches,
    MatchTicketService tickets,
    TimeProvider timeProvider
) : ControllerBase{
    /// <summary>
    /// Opuszcza mecz — nieodwracalnie.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Mecz jest stanem wyłącznym, więc gracz musi mieć jawne wyjście — inaczej jedyną drogą
    /// z rozgrywki byłoby zamknięcie karty, a to nie jest decyzja, tylko ucieczka. Po wyjściu
    /// nie da się wrócić: bilet nie zostanie wydany, a pytanie „w czym gram" przestaje ten
    /// mecz widzieć.
    /// </para>
    /// <para>
    /// Slotu nie zwalniamy i nie mówimy o tym procesowi meczu. Terytorium gracza istnieje
    /// dalej, a proces prowadzi jego aktora do końca — meta odnotowuje wyłącznie to, że ten
    /// człowiek już nie wróci.
    /// </para>
    /// </remarks>
    [HttpPost("{matchId:guid}/leave")]
    [ProducesResponseType(StatusCodes.Status204NoContent)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<IActionResult> Leave(Guid matchId, CancellationToken cancellationToken)
    {
        if (User.GetPlayerId() is not { } playerId)
        {
            return Unauthorized();
        }

        var match = await matches.GetAsync(matchId, cancellationToken);

        if (match is null || !match.Leave(playerId, timeProvider.GetUtcNow()))
        {
            // Nie ma takiego meczu, nie grasz w nim albo już z niego wyszedłeś — dla
            // wołającego to jedna i ta sama odpowiedź, bo w każdym z tych przypadków
            // nie ma go tam, gdzie właśnie próbuje przestać być.
            return NotFound();
        }

        await matches.SaveChangesAsync(cancellationToken);

        return NoContent();
    }

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
    /// <summary>
    /// Mówi wołającemu, czy gra w trwającym meczu — i od razu daje bilet.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Mecz jest stanem wyłącznym: dopóki trwa, gracz nie ma po co oglądać lobby ani profilu.
    /// Klient pyta o to przy każdym wejściu do aplikacji, bo bilet żyje wyłącznie w pamięci
    /// karty i ginie razem z nią — bez tego zapytania odświeżenie strony gdziekolwiek poza
    /// adresem meczu wyglądałoby jak wypisanie z rozgrywki.
    /// </para>
    /// <para>
    /// Pytanie dotyczy <b>wołającego</b>, więc 404 nie ukrywa tu niczyich danych — znaczy
    /// dokładnie „nie grasz w niczym" i tak jest czytane po drugiej stronie.
    /// </para>
    /// </remarks>
    [HttpGet("mine")]
    [ProducesResponseType<ActiveMatchResponse>(StatusCodes.Status200OK)]
    [ProducesResponseType(StatusCodes.Status401Unauthorized)]
    [ProducesResponseType(StatusCodes.Status404NotFound)]
    public async Task<ActionResult<ActiveMatchResponse>> GetMine(CancellationToken cancellationToken)
    {
        if (User.GetPlayerId() is not { } playerId)
        {
            return Unauthorized();
        }

        var match = await matches.GetLiveForPlayerAsync(playerId, cancellationToken);

        if (match?.WsUrl is not { } wsUrl)
        {
            return NotFound();
        }

        if (match.ParticipantOf(playerId) is not { } participant)
        {
            return NotFound();
        }

        var ticket = tickets.Issue(playerId, match.Id, participant.Slot);

        return Ok(new ActiveMatchResponse(match.Id, ticket.Value, wsUrl, ticket.ExpiresAt));
    }

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
