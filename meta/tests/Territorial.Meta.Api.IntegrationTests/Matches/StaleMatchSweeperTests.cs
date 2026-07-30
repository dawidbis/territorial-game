using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Time.Testing;
using NSubstitute;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Api.IntegrationTests.Matches;

/// <summary>
/// Testy porządków po restarcie: mecze porzucone w trakcie alokacji mają zostać zamknięte,
/// a proces ma wstać także wtedy, gdy baza akurat nie odpowiada.
/// </summary>
public class StaleMatchSweeperTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    private static Match Allocating() =>
        Match.Create(
            Moon,
            GameMode.Ffa,
            0x5EED,
            [
                new LobbyPlayer(
                    Guid.CreateVersion7(),
                    Nickname.Create("Gracz"),
                    HsvColor.Create(1, 1, 1),
                    Now
                ),
            ],
            Now
        );

    private static StaleMatchSweeper CreateSweeper(IMatchRepository matches)
    {
        var provider = Substitute.For<IServiceProvider>();
        provider.GetService(typeof(IMatchRepository)).Returns(matches);

        var scope = Substitute.For<IServiceScope>();
        scope.ServiceProvider.Returns(provider);

        var scopeFactory = Substitute.For<IServiceScopeFactory>();
        scopeFactory.CreateScope().Returns(scope);

        return new StaleMatchSweeper(
            scopeFactory,
            new FakeTimeProvider(Now),
            NullLogger<StaleMatchSweeper>.Instance
        );
    }

    [Fact]
    public async Task Sweep_ClosesEveryMatchLeftInAllocating()
    {
        Match[] stale = [Allocating(), Allocating()];

        var matches = Substitute.For<IMatchRepository>();
        matches.GetAllocatingAsync(Arg.Any<CancellationToken>()).Returns(stale);

        await CreateSweeper(matches).SweepAsync(CancellationToken.None);

        stale.ShouldAllBe(m => m.State == MatchState.Failed);
        stale.ShouldAllBe(m => m.EndedAt == Now);
        await matches.Received(1).SaveChangesAsync(Arg.Any<CancellationToken>());
    }

    [Fact]
    public async Task Sweep_TouchesNothingWhenThereIsNothingToClose()
    {
        var matches = Substitute.For<IMatchRepository>();
        matches
            .GetAllocatingAsync(Arg.Any<CancellationToken>())
            .Returns(Array.Empty<Match>());

        await CreateSweeper(matches).SweepAsync(CancellationToken.None);

        await matches.DidNotReceive().SaveChangesAsync(Arg.Any<CancellationToken>());
    }

    [Fact]
    public async Task Sweep_DoesNotBringDownTheHostWhenTheDatabaseIsUnavailable()
    {
        var matches = Substitute.For<IMatchRepository>();
        matches
            .GetAllocatingAsync(Arg.Any<CancellationToken>())
            .Returns<IReadOnlyList<Match>>(_ => throw new InvalidOperationException("baza padła"));

        var sweeper = CreateSweeper(matches);

        // Zamiatanie samo w sobie wyjątek przepuszcza...
        await Should.ThrowAsync<InvalidOperationException>(
            () => sweeper.SweepAsync(CancellationToken.None)
        );

        // ...ale hostowany serwis go połyka: to porządki po poprzednim życiu procesu,
        // a nie warunek działania tego.
        await Should.NotThrowAsync(async () =>
        {
            await sweeper.StartAsync(CancellationToken.None);
            await sweeper.StopAsync(CancellationToken.None);
        });
    }
}
