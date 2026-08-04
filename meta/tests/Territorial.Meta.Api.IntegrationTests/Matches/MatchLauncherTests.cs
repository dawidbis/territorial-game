using System.Text.Json;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Time.Testing;
using NSubstitute;
using Territorial.Meta.Api.Hubs;
using Territorial.Meta.Api.Lobbies;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Application.Matches.Contracts;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Api.IntegrationTests.Matches;

/// <summary>
/// Testy ścieżki „zamrożony roster → mecz → bilety → nowe lobby".
/// </summary>
/// <remarks>
/// <para>
/// Prawdziwe są tu wszystkie elementy, które podejmują decyzje: launcher, lobby,
/// broadcaster i wystawianie biletów. Podstawione są wyłącznie porty na zewnątrz —
/// orkiestrator, baza i transport SignalR — bo to one wymagałyby procesu, pliku i gniazda.
/// </para>
/// <para>
/// Launcher wołany jest wprost, a nie przez kanał i wątek w tle: kanał ma jedną
/// odpowiedzialność i jest trywialny, a test hostowanego serwisu musiałby na coś czekać,
/// czyli migotać z powodów niezwiązanych z testowaną logiką.
/// </para>
/// </remarks>
public class MatchLauncherTests
{
    private static readonly DateTimeOffset Start = new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    private const int WindowSeconds = 60;

    private sealed class StubMapCatalog : IMapCatalog
    {
        public MapDefinition ForNextLobby() => Moon;
    }

    /// <summary>
    /// Alokator, który psuje się dokładnie tyle razy, ile każe test, i przy okazji
    /// zagląda, czy mecz był już zapisany, gdy go zawołano.
    /// </summary>
    private sealed class StubAllocator(int failures, Func<bool> matchAlreadySaved) : IMatchAllocator
    {
        public int Calls { get; private set; }

        /// <summary>Czy przy pierwszym wywołaniu mecz siedział już w bazie.</summary>
        public bool SawASavedMatch { get; private set; }

        /// <summary>Ostatnie zlecenie — stąd sprawdza się, co orkiestrator dostał do ręki.</summary>
        public MatchAllocationRequest? LastRequest { get; private set; }

        public Task<MatchAllocation> AllocateAsync(
            MatchAllocationRequest request,
            CancellationToken cancellationToken
        ){
            Calls++;
            LastRequest = request;

            if (Calls == 1)
            {
                SawASavedMatch = matchAlreadySaved();
            }

            if (Calls <= failures)
            {
                throw new InvalidOperationException("Brak wolnej maszyny.");
            }

            return Task.FromResult(
                new MatchAllocation(
                    "10.0.0.7",
                    5101,
                    $"wss://gs.example.com/match/{request.MatchId}"
                )
            );
        }
    }

    private sealed record Fixture(
        MatchLauncher Launcher,
        CurrentLobby Lobby,
        StubAllocator Allocator,
        ILobbyClient Client,
        IHubClients<ILobbyClient> Clients,
        IMatchRepository Matches,
        FakeTimeProvider Time
    );

    private static Fixture CreateFixture(int allocationFailures = 0)
    {
        var time = new FakeTimeProvider(Start);

        var lobby = new CurrentLobby(
            new StubMapCatalog(),
            time,
            new LobbyOptions { GatheringSeconds = WindowSeconds, CountdownEnabled = true },
            new MatchOptions()
        );

        var client = Substitute.For<ILobbyClient>();
        var clients = Substitute.For<IHubClients<ILobbyClient>>();
        clients.All.Returns(client);
        clients.Group(Arg.Any<string>()).Returns(client);
        clients.User(Arg.Any<string>()).Returns(client);

        var hub = Substitute.For<IHubContext<LobbyHub, ILobbyClient>>();
        hub.Clients.Returns(clients);

        var matches = Substitute.For<IMatchRepository>();

        var scope = Substitute.For<IServiceScope>();
        var provider = Substitute.For<IServiceProvider>();
        provider.GetService(typeof(IMatchRepository)).Returns(matches);
        scope.ServiceProvider.Returns(provider);

        var scopeFactory = Substitute.For<IServiceScopeFactory>();
        scopeFactory.CreateScope().Returns(scope);

        var allocator = new StubAllocator(
            allocationFailures,
            () =>
                matches
                    .ReceivedCalls()
                    .Any(call =>
                        call.GetMethodInfo().Name == nameof(IMatchRepository.SaveChangesAsync)
                    )
        );

        // Zerowe opóźnienie ponowienia i systemowy zegar dla launchera: jego czas służy
        // wyłącznie do stemplowania meczu, a odliczaniem lobby steruje osobny FakeTimeProvider.
        var options = new MatchOptions
        {
            AllocationAttempts = 3,
            AllocationRetryDelayMilliseconds = 0,
            TicketLifetimeSeconds = 60,
        };

        var launcher = new MatchLauncher(
            new MatchStartChannel(),
            lobby,
            new LobbyBroadcaster(hub),
            allocator,
            TestTickets.For(options),
            options,
            TimeProvider.System,
            scopeFactory,
            NullLogger<MatchLauncher>.Instance
        );

        return new Fixture(launcher, lobby, allocator, client, clients, matches, time);
    }

    /// <summary>Sadza graczy w lobby i przewija je do chwili, w której roster zamarza.</summary>
    private static (MatchStartRequest Start, Guid[] Players) StartWith(
        Fixture fixture,
        params string[] nicknames
    ){
        var players = new Guid[nicknames.Length];

        for (var i = 0; i < nicknames.Length; i++)
        {
            players[i] = Guid.CreateVersion7();

            fixture.Lobby.TrackConnection($"c{i}", players[i]);
            fixture.Lobby.Join(
                players[i],
                Nickname.Create(nicknames[i]),
                HsvColor.Create(120, 72, 88)
            );
        }

        fixture.Time.Advance(TimeSpan.FromSeconds(WindowSeconds));

        var start = fixture.Lobby.Tick().Start;

        return (start!, players);
    }

    [Fact]
    public async Task Launch_SendsATicketToEveryHuman_AndToNobodyElse()
    {
        var fixture = CreateFixture();
        var (start, players) = StartWith(fixture, "Alice", "Bob");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        await fixture.Client.Received(2).MatchReady(Arg.Any<MatchReadyDto>());

        foreach (var playerId in players)
        {
            fixture.Clients.Received().User(playerId.ToString());
        }

        // Bilet jest poświadczeniem na slot — nie ma prawa iść do wszystkich.
        await fixture.Client.DidNotReceive().MatchStartFailed(Arg.Any<MatchStartFailedDto>());
    }

    [Fact]
    public async Task Launch_SavesTheMatchBeforeTalkingToTheOrchestrator()
    {
        var fixture = CreateFixture();
        var (start, _) = StartWith(fixture, "Alice");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        // Kolejność jest tu istotna: proces postawiony przed zapisem byłby procesem,
        // o którym meta nic nie wie.
        fixture.Allocator.Calls.ShouldBe(1);
        fixture.Allocator.SawASavedMatch.ShouldBeTrue();

        fixture.Matches.Received().Add(Arg.Is<Match>(m => m != null && m.HumanCount == 1));
    }

    [Fact]
    public async Task Launch_RecordsTheEndpointOnTheMatch()
    {
        var fixture = CreateFixture();
        var (start, _) = StartWith(fixture, "Alice");

        Match? captured = null;
        fixture.Matches.Add(Arg.Do<Match>(m => captured = m));

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        var match = captured.ShouldNotBeNull();

        match.State.ShouldBe(MatchState.Live);
        match.Endpoint.ShouldBe("10.0.0.7:5101");
        // Adres publiczny zapisany razem z wewnętrznym — z niego korzysta ponowne wydanie biletu.
        match.WsUrl.ShouldBe($"wss://gs.example.com/match/{match.Id}");
        match.Participants.ShouldHaveSingleItem().Slot.ShouldBe(ActorSlot.FirstActor);
    }

    /// <summary>
    /// Roster nie ma jak dojść do procesu inaczej niż manifestem. Bez niego mecz wstaje
    /// na samych botach i nikt tego nie zauważa aż do pierwszego <c>MatchInit</c>.
    /// </summary>
    [Fact]
    public async Task Launch_HandsTheOrchestratorAManifestWithEveryHuman()
    {
        var fixture = CreateFixture();
        var (start, _) = StartWith(fixture, "Alice", "Bob");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        var manifest = fixture.Allocator.LastRequest.ShouldNotBeNull().Manifest;

        var players = JsonDocument
            .Parse(manifest)
            .RootElement.GetProperty("players")
            .EnumerateArray()
            .ToArray();

        players
            .Select(player => player.GetProperty("name").GetString())
            .ShouldBe(["Alice", "Bob"], ignoreOrder: true);

        players
            .Select(player => player.GetProperty("slot").GetByte())
            .ShouldBe([(byte)1, (byte)2], ignoreOrder: true);
    }

    [Fact]
    public async Task Launch_OpensTheNextLobby()
    {
        var fixture = CreateFixture();
        var (start, _) = StartWith(fixture, "Alice");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        var header = fixture.Lobby.Snapshot().Header;

        header.LobbyId.ShouldNotBe(start.LobbyId);
        header.State.ShouldBe(nameof(LobbyState.Gathering));
        header.PlayerCount.ShouldBe(0);
    }

    [Fact]
    public async Task Launch_RetriesAllocationBeforeGivingUp()
    {
        var fixture = CreateFixture(allocationFailures: 2);
        var (start, _) = StartWith(fixture, "Alice");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        fixture.Allocator.Calls.ShouldBe(3);
        await fixture.Client.Received(1).MatchReady(Arg.Any<MatchReadyDto>());
    }

    [Fact]
    public async Task Launch_WhenAllocationKeepsFailing_TellsTheWaitingPlayers()
    {
        var fixture = CreateFixture(allocationFailures: int.MaxValue);
        var (start, _) = StartWith(fixture, "Alice");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        await fixture.Client.Received(1).MatchStartFailed(Arg.Any<MatchStartFailedDto>());
        await fixture.Client.DidNotReceive().MatchReady(Arg.Any<MatchReadyDto>());
    }

    [Fact]
    public async Task Launch_WhenAllocationKeepsFailing_StillOpensTheNextLobby()
    {
        var fixture = CreateFixture(allocationFailures: int.MaxValue);
        var (start, _) = StartWith(fixture, "Alice");

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        // Lobby zostawione w stanie Starting nie przyjmuje nikogo i nigdy z niego nie
        // wyjdzie — awaria alokacji zamieniłaby się w martwy serwis.
        fixture.Lobby.Snapshot().Header.State.ShouldBe(nameof(LobbyState.Gathering));
    }

    [Fact]
    public async Task Launch_WhenAllocationKeepsFailing_MarksTheMatchFailed()
    {
        var fixture = CreateFixture(allocationFailures: int.MaxValue);
        var (start, _) = StartWith(fixture, "Alice");

        Match? captured = null;
        fixture.Matches.Add(Arg.Do<Match>(m => captured = m));

        await fixture.Launcher.LaunchAsync(start, CancellationToken.None);

        captured.ShouldNotBeNull().State.ShouldBe(MatchState.Failed);
    }
}
