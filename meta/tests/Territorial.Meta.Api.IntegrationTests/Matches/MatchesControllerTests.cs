using System.Security.Claims;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.IdentityModel.JsonWebTokens;
using NSubstitute;
using Territorial.Meta.Api.Contracts;
using Territorial.Meta.Api.Controllers;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Api.IntegrationTests.Matches;

/// <summary>
/// Testy ponownego wydania biletu.
/// </summary>
/// <remarks>
/// Najważniejsze jest tu to, czego endpoint <b>nie</b> zdradza: wszystkie odmowy wyglądają
/// tak samo (404), więc nie da się nim sprawdzić, kto gra w meczu o zgadniętym identyfikatorze.
/// </remarks>
public class MatchesControllerTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    private const string WsUrl = "wss://gs.example.com/match/whatever";

    private static Match LiveMatch(params Guid[] players)
    {
        var roster = players
            .Select(id => new LobbyPlayer(
                id,
                Nickname.Create("Gracz"),
                HsvColor.Create(120, 72, 88),
                Now
            ))
            .ToArray();

        var match = Match.Create(Moon, GameMode.Ffa, 0x5EED, roster, Now);
        match.MarkLive("10.0.0.7:5101", WsUrl, Now);

        return match;
    }

    private static MatchesController CreateController(IMatchRepository matches, Guid? playerId)
    {
        var options = new MatchOptions { TicketLifetimeSeconds = 60 };

        var claims = playerId is { } id
            ? new ClaimsIdentity([new Claim(JwtRegisteredClaimNames.Sub, id.ToString())], "test")
            : new ClaimsIdentity();

        return new MatchesController(matches, new MatchTicketService(options, TimeProvider.System))
        {
            ControllerContext = new ControllerContext
            {
                HttpContext = new DefaultHttpContext { User = new ClaimsPrincipal(claims) },
            },
        };
    }

    [Fact]
    public async Task IssueTicket_GivesAParticipantAFreshTicketAndTheSameAddressAsBefore()
    {
        var playerId = Guid.CreateVersion7();
        var match = LiveMatch(playerId);

        var matches = Substitute.For<IMatchRepository>();
        matches.GetAsync(match.Id, Arg.Any<CancellationToken>()).Returns(match);

        var response = await CreateController(matches, playerId)
            .IssueTicket(match.Id, CancellationToken.None);

        var body = response
            .Result.ShouldBeOfType<OkObjectResult>()
            .Value.ShouldBeOfType<MatchTicketResponse>();

        body.Ticket.ShouldNotBeNullOrWhiteSpace();
        // Adres pochodzi z meczu, a nie ze składania go na nowo z konfiguracji — inaczej
        // ponowne wydanie mogłoby wysłać gracza gdzie indziej niż pierwotne MatchReady.
        body.WsUrl.ShouldBe(WsUrl);
        body.ExpiresAt.ShouldBeGreaterThan(DateTimeOffset.UtcNow);
    }

    [Fact]
    public async Task IssueTicket_IsNotFoundForSomebodyWhoDoesNotPlayInThatMatch()
    {
        var match = LiveMatch(Guid.CreateVersion7());

        var matches = Substitute.For<IMatchRepository>();
        matches.GetAsync(match.Id, Arg.Any<CancellationToken>()).Returns(match);

        var response = await CreateController(matches, Guid.CreateVersion7())
            .IssueTicket(match.Id, CancellationToken.None);

        response.Result.ShouldBeOfType<NotFoundResult>();
    }

    [Fact]
    public async Task IssueTicket_IsNotFoundWhileTheMatchIsStillBeingAllocated()
    {
        var playerId = Guid.CreateVersion7();

        var roster = new[]
        {
            new LobbyPlayer(playerId, Nickname.Create("Gracz"), HsvColor.Create(1, 1, 1), Now),
        };

        var match = Match.Create(Moon, GameMode.Ffa, 0x5EED, roster, Now);

        var matches = Substitute.For<IMatchRepository>();
        matches.GetAsync(match.Id, Arg.Any<CancellationToken>()).Returns(match);

        var response = await CreateController(matches, playerId)
            .IssueTicket(match.Id, CancellationToken.None);

        // Mecz bez adresu nie ma dokąd wpuścić gracza.
        response.Result.ShouldBeOfType<NotFoundResult>();
    }

    [Fact]
    public async Task IssueTicket_IsNotFoundForAMatchThatDoesNotExist()
    {
        var matches = Substitute.For<IMatchRepository>();
        matches
            .GetAsync(Arg.Any<Guid>(), Arg.Any<CancellationToken>())
            .Returns((Match?)null);

        var response = await CreateController(matches, Guid.CreateVersion7())
            .IssueTicket(Guid.CreateVersion7(), CancellationToken.None);

        response.Result.ShouldBeOfType<NotFoundResult>();
    }

    [Fact]
    public async Task IssueTicket_IsUnauthorizedWithoutAPlayerIdentity()
    {
        var matches = Substitute.For<IMatchRepository>();

        var response = await CreateController(matches, playerId: null)
            .IssueTicket(Guid.CreateVersion7(), CancellationToken.None);

        response.Result.ShouldBeOfType<UnauthorizedResult>();
        await matches.DidNotReceive().GetAsync(Arg.Any<Guid>(), Arg.Any<CancellationToken>());
    }
}
