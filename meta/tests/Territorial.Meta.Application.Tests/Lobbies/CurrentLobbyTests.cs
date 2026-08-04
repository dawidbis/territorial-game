using Microsoft.Extensions.Time.Testing;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Tests.Lobbies;

/// <summary>
/// Testy dwóch zachowań, których nie widać w samej domenie: karencji po rozłączeniu
/// i sklejania zmian do jednego rozgłoszenia na tik.
/// </summary>
public class CurrentLobbyTests
{
    private static readonly DateTimeOffset Start = new(2026, 7, 29, 12, 0, 0, TimeSpan.Zero);

    private const int GraceSeconds = 5;

    private const int WindowSeconds = 60;

    private sealed class StubMapCatalog : IMapCatalog
    {
        public MapDefinition ForNextLobby() => new("moon", "Moon", MaxActors: 100);
    }

    private static (CurrentLobby Lobby, FakeTimeProvider Time) Create(
        bool countdown = false,
        bool fillWithBots = true
    ){
        var time = new FakeTimeProvider(Start);

        var lobby = new CurrentLobby(
            new StubMapCatalog(),
            time,
            new LobbyOptions
            {
                GatheringSeconds = WindowSeconds,
                CountdownEnabled = countdown,
                DisconnectGraceSeconds = GraceSeconds,
            },
            new MatchOptions { FillWithBots = fillWithBots }
        );

        return (lobby, time);
    }

    private static Guid JoinPlayer(CurrentLobby lobby, string connectionId, string nickname)
    {
        var playerId = Guid.CreateVersion7();

        lobby.TrackConnection(connectionId, playerId);
        lobby.Join(playerId, Nickname.Create(nickname), HsvColor.Create(120, 72, 88));

        return playerId;
    }

    [Fact]
    public void Tick_PublishesOnceAfterAChange_ThenStaysQuiet()
    {
        var (lobby, _) = Create();
        JoinPlayer(lobby, "c1", "Alice");

        lobby.Tick().Snapshot.ShouldNotBeNull().Header.PlayerCount.ShouldBe(1);

        // Nic się nie zmieniło, więc nie ma czego rozsyłać — to jest cały sens
        // przeniesienia broadcastu z huba do zegara.
        lobby.Tick().Snapshot.ShouldBeNull();
    }

    [Fact]
    public void Tick_CollapsesABurstOfJoinsIntoASingleSnapshot()
    {
        var (lobby, _) = Create();

        JoinPlayer(lobby, "c1", "Alice");
        JoinPlayer(lobby, "c2", "Bob");
        JoinPlayer(lobby, "c3", "Carol");

        var snapshot = lobby.Tick().Snapshot.ShouldNotBeNull();

        snapshot.Header.PlayerCount.ShouldBe(3);
        lobby.Tick().Snapshot.ShouldBeNull();
    }

    [Fact]
    public void Disconnect_KeepsPlayerInRosterDuringGrace()
    {
        var (lobby, time) = Create();
        JoinPlayer(lobby, "c1", "Alice");
        lobby.Tick();

        lobby.Disconnect("c1");

        // Samo rozłączenie nie zmienia rostera, więc nie generuje ani wiadomości,
        // ani mignięcia listy u pozostałych graczy.
        lobby.Tick().Snapshot.ShouldBeNull();

        time.Advance(TimeSpan.FromSeconds(GraceSeconds - 1));
        lobby.Tick().Snapshot.ShouldBeNull();
        lobby.Snapshot().Header.PlayerCount.ShouldBe(1);
    }

    [Fact]
    public void Disconnect_RemovesPlayerOnceGraceElapses()
    {
        var (lobby, time) = Create();
        JoinPlayer(lobby, "c1", "Alice");
        lobby.Tick();

        lobby.Disconnect("c1");
        time.Advance(TimeSpan.FromSeconds(GraceSeconds));

        lobby.Tick().Snapshot.ShouldNotBeNull().Header.PlayerCount.ShouldBe(0);
    }

    [Fact]
    public void Reconnect_WithinGrace_KeepsThePlayerSeated()
    {
        var (lobby, time) = Create();
        var playerId = JoinPlayer(lobby, "c1", "Alice");
        lobby.Tick();

        lobby.Disconnect("c1");
        time.Advance(TimeSpan.FromSeconds(1));

        // Tak wygląda F5: nowe połączenie tego samego gracza tuż po zerwaniu starego.
        lobby.TrackConnection("c2", playerId);

        time.Advance(TimeSpan.FromSeconds(GraceSeconds * 2));

        lobby.Tick().Snapshot.ShouldBeNull();
        lobby.Snapshot().Header.PlayerCount.ShouldBe(1);
    }

    [Fact]
    public void Disconnect_OfOneOfTwoTabs_DoesNotStartGrace()
    {
        var (lobby, time) = Create();
        var playerId = JoinPlayer(lobby, "c1", "Alice");
        lobby.TrackConnection("c2", playerId);
        lobby.Tick();

        lobby.Disconnect("c1");
        time.Advance(TimeSpan.FromSeconds(GraceSeconds * 2));

        lobby.Tick().Snapshot.ShouldBeNull();
        lobby.Snapshot().Header.PlayerCount.ShouldBe(1);
    }

    [Fact]
    public void Leave_RemovesImmediately_WithoutWaitingOutTheGrace()
    {
        var (lobby, _) = Create();
        var playerId = JoinPlayer(lobby, "c1", "Alice");
        lobby.Tick();

        lobby.Leave(playerId);

        lobby.Snapshot().Header.PlayerCount.ShouldBe(0);
        lobby.Tick().Snapshot.ShouldNotBeNull().Header.PlayerCount.ShouldBe(0);
    }

    [Fact]
    public void RefreshPlayer_MakesTheNewNicknameVisibleToEveryone()
    {
        var (lobby, _) = Create();
        var playerId = JoinPlayer(lobby, "c1", "Alice");
        lobby.Tick();

        lobby.RefreshPlayer(playerId, Nickname.Create("Alicja"), HsvColor.Create(10, 50, 50));

        var snapshot = lobby.Tick().Snapshot.ShouldNotBeNull();
        snapshot.Players.ShouldHaveSingleItem().Nickname.ShouldBe("Alicja");
    }

    [Fact]
    public void Tick_OnStart_HandsOverTheFrozenRoster()
    {
        var (lobby, time) = Create(countdown: true);

        // Sekunda przerwy między dołączeniami, żeby kolejność wynikała z JoinedAt.
        // Przy identycznym znaczniku roster rozstrzyga remis identyfikatorem, a porządek
        // Guidów nie odpowiada kolejności ich powstawania — test byłby migotliwy.
        var alice = JoinPlayer(lobby, "c1", "Alice");
        time.Advance(TimeSpan.FromSeconds(1));
        var bob = JoinPlayer(lobby, "c2", "Bob");

        time.Advance(TimeSpan.FromSeconds(WindowSeconds));

        var result = lobby.Tick();

        result.Tick.ShouldBe(LobbyTick.Started);
        result.Snapshot.ShouldNotBeNull().Header.State.ShouldBe(nameof(LobbyState.Starting));

        var start = result.Start.ShouldNotBeNull();
        start.Map.MaxActors.ShouldBe(100);
        start.Roster.Select(p => p.PlayerId).ShouldBe([alice, bob]);
    }

    [Fact]
    public void Tick_AfterStart_KeepsTheRosterUntilSomebodyUsesIt()
    {
        var (lobby, time) = Create(countdown: true);
        JoinPlayer(lobby, "c1", "Alice");

        time.Advance(TimeSpan.FromSeconds(WindowSeconds));
        lobby.Tick();

        // Zegar NIE domyka lobby — robi to launcher, gdy zużyje roster. Gdyby domykał,
        // zamrożona lista zniknęłaby, zanim ktokolwiek zrobiłby z niej mecz.
        var afterwards = lobby.Tick();

        afterwards.Tick.ShouldBe(LobbyTick.Idle);
        afterwards.Start.ShouldBeNull();
        lobby.Snapshot().Header.PlayerCount.ShouldBe(1);
        lobby.Snapshot().Header.State.ShouldBe(nameof(LobbyState.Starting));
    }

    [Fact]
    public void Tick_WithNobodyWaiting_RestartsTheWindowInsteadOfStarting()
    {
        var (lobby, time) = Create(countdown: true);

        time.Advance(TimeSpan.FromSeconds(WindowSeconds));

        var result = lobby.Tick();

        result.Tick.ShouldBe(LobbyTick.WindowReset);
        result.Start.ShouldBeNull();
    }

    [Fact]
    public void CloseAndReopen_OpensAnEmptyLobbyWithANewIdentity()
    {
        var (lobby, time) = Create(countdown: true);
        JoinPlayer(lobby, "c1", "Alice");

        time.Advance(TimeSpan.FromSeconds(WindowSeconds));
        var started = lobby.Tick().Snapshot.ShouldNotBeNull();

        var reopened = lobby.CloseAndReopen();

        reopened.Header.LobbyId.ShouldNotBe(started.Header.LobbyId);
        reopened.Header.State.ShouldBe(nameof(LobbyState.Gathering));
        reopened.Header.PlayerCount.ShouldBe(0);
    }

    [Fact]
    public void Header_PromisesBotsOnlyWhenTheMatchWillActuallyPlaceThem()
    {
        var (withBots, _) = Create();
        JoinPlayer(withBots, "c1", "Alice");

        // Mapa mieści stu aktorów, więc dziewięćdziesięciu dziewięciu dopełni boty.
        withBots.Snapshot().Header.BotCount.ShouldBe(99);

        var (withoutBots, _) = Create(fillWithBots: false);
        JoinPlayer(withoutBots, "c1", "Alice");

        // Liczba botów w nagłówku jest zapowiedzią. Zapowiedź niezgodna z tym, co postawi
        // proces meczu, jest gorsza od jej braku — gracz liczy na dziewięćdziesięciu
        // dziewięciu przeciwników i wchodzi na pustą mapę.
        withoutBots.Snapshot().Header.BotCount.ShouldBe(0);
    }

    [Fact]
    public void RefreshPlayer_IsQuietForSomebodyWhoIsNotInTheLobby()
    {
        var (lobby, _) = Create();

        lobby.RefreshPlayer(
            Guid.CreateVersion7(),
            Nickname.Create("Nikt"),
            HsvColor.Create(1, 1, 1)
        );

        lobby.Tick().Snapshot.ShouldBeNull();
    }
}
