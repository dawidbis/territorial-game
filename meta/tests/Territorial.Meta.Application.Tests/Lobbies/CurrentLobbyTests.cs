using Microsoft.Extensions.Time.Testing;
using Territorial.Meta.Application.Lobbies;
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

    private sealed class StubMapCatalog : IMapCatalog
    {
        public MapDefinition ForNextLobby() => new("moon", "Moon", MaxActors: 100);
    }

    private static (CurrentLobby Lobby, FakeTimeProvider Time) Create()
    {
        var time = new FakeTimeProvider(Start);

        var lobby = new CurrentLobby(
            new StubMapCatalog(),
            time,
            new LobbyOptions
            {
                GatheringSeconds = 60,
                CountdownEnabled = false,
                DisconnectGraceSeconds = GraceSeconds,
            }
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
