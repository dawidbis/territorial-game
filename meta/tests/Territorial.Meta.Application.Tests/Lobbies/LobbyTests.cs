using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Tests.Lobbies;

/// <summary>
/// Testy maszyny stanów lobby. Nie ma tu żadnego zegara ani atrapy — <see cref="Lobby"/>
/// przyjmuje chwilę parametrem, więc pełne sześćdziesięciosekundowe okno przechodzi się
/// jednym dodaniem <see cref="TimeSpan"/>.
/// </summary>
public class LobbyTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 29, 12, 0, 0, TimeSpan.Zero);
    private static readonly TimeSpan Window = TimeSpan.FromSeconds(60);
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    private static Lobby Gathering() => Lobby.Open(Moon, GameMode.Ffa, Window, Now);

    private static (Guid Id, Nickname Nickname, HsvColor Color) Someone(string nickname) =>
        (Guid.CreateVersion7(), Nickname.Create(nickname), HsvColor.Create(120, 72, 88));

    private static JoinResult Join(Lobby lobby, string nickname, DateTimeOffset at)
    {
        var (id, nick, color) = Someone(nickname);
        return lobby.Join(id, nick, color, at);
    }

    [Fact]
    public void Open_StartsGatheringWithCountdownRunning()
    {
        var lobby = Gathering();

        lobby.State.ShouldBe(LobbyState.Gathering);
        lobby.StartsAt.ShouldBe(Now + Window);
        lobby.HumanCount.ShouldBe(0);
    }

    [Fact]
    public void OpenFrozen_LeavesCountdownWithoutAnInstant()
    {
        var lobby = Lobby.OpenFrozen(Moon, GameMode.Ffa, Window, Now);

        lobby.State.ShouldBe(LobbyState.Gathering);
        lobby.StartsAt.ShouldBeNull();
        lobby.GatheringWindow.ShouldBe(Window);
    }

    [Fact]
    public void OpenFrozen_NeverStarts_EvenLongAfterTheWindowWouldHaveElapsed()
    {
        var lobby = Lobby.OpenFrozen(Moon, GameMode.Ffa, Window, Now);
        Join(lobby, "Alice", Now);

        lobby.Advance(Now + TimeSpan.FromDays(7)).ShouldBe(LobbyTick.Idle);
        lobby.State.ShouldBe(LobbyState.Gathering);
    }

    [Fact]
    public void Join_AddsPlayerToRoster()
    {
        var lobby = Gathering();

        Join(lobby, "Alice", Now).ShouldBe(JoinResult.Joined);

        lobby.HumanCount.ShouldBe(1);
        lobby.Roster().ShouldHaveSingleItem().Nickname.Value.ShouldBe("Alice");
    }

    [Fact]
    public void Join_IsIdempotent_AndKeepsOriginalPositionWhileRefreshingProfile()
    {
        var lobby = Gathering();
        var (id, nick, color) = Someone("Alice");

        lobby.Join(id, nick, color, Now).ShouldBe(JoinResult.Joined);

        var renamed = Nickname.Create("Alice-2");
        var recolored = HsvColor.Create(200, 50, 50);
        var later = Now + TimeSpan.FromSeconds(30);

        lobby.Join(id, renamed, recolored, later).ShouldBe(JoinResult.AlreadyJoined);

        lobby.HumanCount.ShouldBe(1);

        var entry = lobby.Roster().ShouldHaveSingleItem();
        entry.Nickname.ShouldBe(renamed);
        entry.Color.ShouldBe(recolored);
        // Kolejność liczona jest od pierwszego wejścia — odświeżenie strony nie może
        // przerzucać gracza na koniec listy.
        entry.JoinedAt.ShouldBe(Now);
    }

    [Fact]
    public void Join_IsRejectedWhenMapIsFull()
    {
        var tiny = new MapDefinition("tiny", "Tiny", MaxActors: 2);
        var lobby = Lobby.Open(tiny, GameMode.Ffa, Window, Now);

        Join(lobby, "Alice", Now).ShouldBe(JoinResult.Joined);
        Join(lobby, "Bob", Now).ShouldBe(JoinResult.Joined);
        Join(lobby, "Carol", Now).ShouldBe(JoinResult.Full);

        lobby.HumanCount.ShouldBe(2);
        lobby.IsFull.ShouldBeTrue();
    }

    [Fact]
    public void Join_IsRejectedOnceTheLobbyIsStarting()
    {
        var lobby = Gathering();
        Join(lobby, "Alice", Now);

        lobby.Advance(Now + Window).ShouldBe(LobbyTick.Started);

        Join(lobby, "Bob", Now + Window).ShouldBe(JoinResult.NotGathering);
        lobby.HumanCount.ShouldBe(1);
    }

    [Fact]
    public void Refresh_UpdatesProfileWithoutMovingPlayerInTheRoster()
    {
        var lobby = Gathering();
        var (id, nick, color) = Someone("Alice");

        lobby.Join(id, nick, color, Now);
        Join(lobby, "Bob", Now + TimeSpan.FromSeconds(1));

        var renamed = Nickname.Create("Alicja");
        lobby.Refresh(id, renamed, color).ShouldBeTrue();

        // Zmiana nicku nie może przesuwać gracza na koniec kolejki.
        lobby.Roster().Select(p => p.Nickname.Value).ShouldBe(["Alicja", "Bob"]);
    }

    [Fact]
    public void Refresh_ReportsNoChangeWhenNothingActuallyDiffers()
    {
        var lobby = Gathering();
        var (id, nick, color) = Someone("Alice");
        lobby.Join(id, nick, color, Now);

        // Zapis profilu bez zmian nie może wywoływać rozgłoszenia.
        lobby.Refresh(id, nick, color).ShouldBeFalse();
    }

    [Fact]
    public void Refresh_IgnoresPlayersOutsideTheLobby()
    {
        var lobby = Gathering();

        lobby.Refresh(Guid.CreateVersion7(), Nickname.Create("Nikt"), HsvColor.Create(1, 1, 1))
            .ShouldBeFalse();
    }

    [Fact]
    public void Leave_RemovesPlayer_AndReportsWhetherAnythingChanged()
    {
        var lobby = Gathering();
        var (id, nick, color) = Someone("Alice");
        lobby.Join(id, nick, color, Now);

        lobby.Leave(id).ShouldBeTrue();
        lobby.HumanCount.ShouldBe(0);

        lobby.Leave(id).ShouldBeFalse();
    }

    [Theory]
    [InlineData(0, 100)]
    [InlineData(1, 99)]
    [InlineData(100, 0)]
    public void BotCount_FillsTheMapUpToMaxActors(int humans, int expectedBots)
    {
        var lobby = Gathering();

        for (var i = 0; i < humans; i++)
        {
            Join(lobby, $"Player-{i}", Now);
        }

        lobby.HumanCount.ShouldBe(humans);
        lobby.BotCount.ShouldBe(expectedBots);
    }

    [Fact]
    public void Advance_DoesNothingBeforeTheInstantArrives()
    {
        var lobby = Gathering();
        Join(lobby, "Alice", Now);

        lobby.Advance(Now + Window - TimeSpan.FromMilliseconds(1)).ShouldBe(LobbyTick.Idle);
        lobby.State.ShouldBe(LobbyState.Gathering);
    }

    [Fact]
    public void Advance_RestartsTheWindowWhenNobodyShowedUp()
    {
        var lobby = Gathering();
        var elapsed = Now + Window;

        lobby.Advance(elapsed).ShouldBe(LobbyTick.WindowReset);

        lobby.State.ShouldBe(LobbyState.Gathering);
        lobby.StartsAt.ShouldBe(elapsed + Window);
    }

    [Fact]
    public void Advance_MeasuresTheNewWindowFromNow_NotFromTheMissedInstant()
    {
        // Zegar tyka raz na sekundę, więc regularnie spóźnia się o ułamek okna.
        // Nowe okno ma trwać pełne 60 s od chwili zauważenia, a nie 60 s od terminu,
        // bo inaczej opóźnienia kumulowałyby się przez kolejne cykle.
        var lobby = Gathering();
        var late = Now + Window + TimeSpan.FromMilliseconds(900);

        lobby.Advance(late).ShouldBe(LobbyTick.WindowReset);

        lobby.StartsAt.ShouldBe(late + Window);
    }

    [Fact]
    public void Advance_StartsTheMatchWhenSomebodyIsWaiting()
    {
        var lobby = Gathering();
        Join(lobby, "Alice", Now);

        lobby.Advance(Now + Window).ShouldBe(LobbyTick.Started);
        lobby.State.ShouldBe(LobbyState.Starting);
    }

    [Fact]
    public void Advance_IsIdleOnceTheLobbyLeftGathering()
    {
        var lobby = Gathering();
        Join(lobby, "Alice", Now);
        lobby.Advance(Now + Window);

        lobby.Advance(Now + Window + Window).ShouldBe(LobbyTick.Idle);
        lobby.State.ShouldBe(LobbyState.Starting);
    }

    [Fact]
    public void Roster_KeepsJoinOrder_RegardlessOfIdentifiers()
    {
        var lobby = Gathering();

        var first = Someone("First");
        var second = Someone("Second");
        var third = Someone("Third");

        lobby.Join(third.Id, third.Nickname, third.Color, Now + TimeSpan.FromSeconds(3));
        lobby.Join(first.Id, first.Nickname, first.Color, Now + TimeSpan.FromSeconds(1));
        lobby.Join(second.Id, second.Nickname, second.Color, Now + TimeSpan.FromSeconds(2));

        lobby
            .Roster()
            .Select(p => p.Nickname.Value)
            .ShouldBe(["First", "Second", "Third"]);
    }

    [Fact]
    public void FullCycle_GathersThenStartsThenCloses()
    {
        var lobby = Gathering();

        Join(lobby, "Alice", Now);
        lobby.Advance(Now + Window).ShouldBe(LobbyTick.Started);

        lobby.Close();

        lobby.State.ShouldBe(LobbyState.Closed);
        lobby.Advance(Now + TimeSpan.FromHours(1)).ShouldBe(LobbyTick.Idle);
    }
}
