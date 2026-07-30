using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Tests.Matches;

/// <summary>
/// Testy zakładania meczu — czyli przypisania slotów (D12) i przejść stanu.
/// <see cref="Match.Create"/> jest czystą funkcją: ziarno i czas przychodzą parametrem,
/// więc cały test to jedno wywołanie i kilka asercji, bez bazy i bez zegara.
/// </summary>
public class MatchTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    private const long Seed = 0x5EED;

    private const string WsUrl = "wss://gs.example.com/match/whatever";

    private static LobbyPlayer Someone(string nickname, int minutesAgo) =>
        new(
            Guid.CreateVersion7(),
            Nickname.Create(nickname),
            HsvColor.Create(120, 72, 88),
            Now.AddMinutes(-minutesAgo)
        );

    private static IReadOnlyList<LobbyPlayer> Roster(params string[] nicknames) =>
        [.. nicknames.Select((nickname, index) => Someone(nickname, nicknames.Length - index))];

    [Fact]
    public void Create_GivesHumansConsecutiveSlotsInRosterOrder()
    {
        var roster = Roster("Alice", "Bob", "Carol");

        var match = Match.Create(Moon, GameMode.Ffa, Seed, roster, Now);

        match.Participants.Select(p => p.Slot).ShouldBe([1, 2, 3]);
        match.Participants.Select(p => p.PlayerId).ShouldBe(roster.Select(p => p.PlayerId));
    }

    [Fact]
    public void Create_LeavesSlotZeroAndTwoFiftyFiveAlone()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);

        // 0 to pustkowie, 255 to woda. Pierwszy aktor musi zacząć się od 1, inaczej
        // game-serwer przypisałby graczowi kafelki niczyje.
        match.Participants.ShouldHaveSingleItem().Slot.ShouldBe(ActorSlot.FirstActor);
    }

    [Fact]
    public void Create_CopiesNicknameAndColor_SoLaterProfileChangesDoNotRewriteHistory()
    {
        var roster = Roster("Alice");

        var match = Match.Create(Moon, GameMode.Ffa, Seed, roster, Now);

        var participant = match.Participants.ShouldHaveSingleItem();
        participant.Nickname.ShouldBe(roster[0].Nickname);
        participant.Color.ShouldBe(roster[0].Color);
    }

    [Fact]
    public void Create_FillsTheRestOfTheMapWithBots()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice", "Bob"), Now);

        match.HumanCount.ShouldBe(2);
        match.BotCount.ShouldBe(Moon.MaxActors - 2);
        match.MaxActors.ShouldBe(Moon.MaxActors);
    }

    [Fact]
    public void Create_StartsInAllocating_BecauseTheProcessDoesNotExistYet()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);

        match.State.ShouldBe(MatchState.Allocating);
        match.Endpoint.ShouldBeNull();
        match.StartedAt.ShouldBeNull();
        match.Seed.ShouldBe(Seed);
    }

    [Fact]
    public void Create_RejectsAnEmptyRoster()
    {
        Should.Throw<ArgumentException>(() => Match.Create(Moon, GameMode.Ffa, Seed, [], Now));
    }

    [Fact]
    public void Create_RejectsARosterLargerThanTheMap()
    {
        var tiny = new MapDefinition("tiny", "Tiny", MaxActors: 2);

        Should.Throw<ArgumentOutOfRangeException>(() =>
            Match.Create(tiny, GameMode.Ffa, Seed, Roster("Alice", "Bob", "Carol"), Now)
        );
    }

    [Fact]
    public void Create_RejectsAMapWiderThanTheSlotSpace()
    {
        var huge = new MapDefinition("huge", "Huge", ActorSlot.MaxActorsPerMatch + 1);

        Should.Throw<ArgumentOutOfRangeException>(() =>
            Match.Create(huge, GameMode.Ffa, Seed, Roster("Alice"), Now)
        );
    }

    [Fact]
    public void MarkLive_RecordsBothAddressesAndTheStartInstant()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);

        match.MarkLive("10.0.0.7:5101", WsUrl, Now);

        match.State.ShouldBe(MatchState.Live);
        match.IsLive.ShouldBeTrue();
        match.Endpoint.ShouldBe("10.0.0.7:5101");
        // Adres publiczny jest zapisany, a nie składany na nowo — ponowne wydanie biletu
        // musi oddać dokładnie ten, który gracz dostał w MatchReady.
        match.WsUrl.ShouldBe(WsUrl);
        match.StartedAt.ShouldBe(Now);
    }

    [Fact]
    public void MarkLive_RefusesToStartTheSameMatchTwice()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);
        match.MarkLive("10.0.0.7:5101", WsUrl, Now);

        Should.Throw<InvalidOperationException>(
            () => match.MarkLive("10.0.0.8:5101", WsUrl, Now)
        );
    }

    [Fact]
    public void ParticipantOf_FindsTheSlotOfSomebodyWhoPlays_AndNothingForAnybodyElse()
    {
        var roster = Roster("Alice", "Bob");
        var match = Match.Create(Moon, GameMode.Ffa, Seed, roster, Now);

        match.ParticipantOf(roster[1].PlayerId).ShouldNotBeNull().Slot.ShouldBe((byte)2);
        match.ParticipantOf(Guid.CreateVersion7()).ShouldBeNull();
    }

    [Fact]
    public void MarkFailed_IsQuietWhenTheMatchIsAlreadyLive()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);
        match.MarkLive("10.0.0.7:5101", WsUrl, Now);

        // Wołane ze ścieżki obsługi awarii — wyjątek stąd przesłoniłby przyczynę.
        match.MarkFailed(Now);

        match.State.ShouldBe(MatchState.Live);
    }

    [Fact]
    public void MarkFailed_ClosesAMatchThatNeverGotAnEndpoint()
    {
        var match = Match.Create(Moon, GameMode.Ffa, Seed, Roster("Alice"), Now);

        match.MarkFailed(Now);

        match.State.ShouldBe(MatchState.Failed);
        match.EndedAt.ShouldBe(Now);
    }
}
