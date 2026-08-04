using System.Text.Json;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Lobbies;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Tests.Matches;

/// <summary>
/// Testy manifestu — jedynej rzeczy, którą meta mówi procesowi meczu o graczach.
/// </summary>
/// <remarks>
/// Kształt jest tu kontraktem z kodem w C++, a nie szczegółem implementacji: parser po
/// tamtej stronie odrzuca manifest, którego nie rozumie, a odrzucony manifest to mecz,
/// który nie wstaje. Stąd asercje na nazwy pól, nie na „jakiś sensowny JSON".
/// </remarks>
public class MatchManifestTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 31, 12, 0, 0, TimeSpan.Zero);
    private static readonly MapDefinition Map = new("synthetic", "Testowa", MaxActors: 100);

    private static Match MatchWith(params (string Nickname, HsvColor Color)[] players)
    {
        var roster = players
            .Select(player => new LobbyPlayer(
                Guid.CreateVersion7(),
                Nickname.Create(player.Nickname),
                player.Color,
                Now
            ))
            .ToArray();

        return Match.Create(Map, GameMode.Ffa, 0x5EED, roster, Now);
    }

    private static JsonElement[] PlayersIn(string manifest) =>
        JsonDocument.Parse(manifest).RootElement.GetProperty("players").EnumerateArray().ToArray();

    [Fact]
    public void For_WritesSlotNameAndColorForEveryHuman()
    {
        var match = MatchWith(
            ("Alice", HsvColor.Create(0, 100, 100)),
            ("Bob", HsvColor.Create(240, 100, 100))
        );

        var players = PlayersIn(MatchManifest.For(match.Participants));

        players.Length.ShouldBe(2);

        players[0].GetProperty("slot").GetByte().ShouldBe(ActorSlot.FirstActor);
        players[0].GetProperty("name").GetString().ShouldBe("Alice");
        players[0].GetProperty("colorRgb").GetInt32().ShouldBe(0xFF0000);

        players[1].GetProperty("name").GetString().ShouldBe("Bob");
        players[1].GetProperty("colorRgb").GetInt32().ShouldBe(0x0000FF);
    }

    /// <summary>
    /// Boty nie mają wiersza w bazie i nie mają go mieć: ich nicki i kolory wynikają
    /// z ziarna meczu, bo tylko wtedy re-symulacja odtwarza ten sam mecz.
    /// </summary>
    [Fact]
    public void For_LeavesBotsOutEvenThoughTheMatchIsFullOfThem()
    {
        var match = MatchWith(("Alice", HsvColor.Create(120, 72, 88)));

        match.BotCount.ShouldBe(99);

        PlayersIn(MatchManifest.For(match.Participants)).ShouldHaveSingleItem();
    }

    /// <summary>
    /// Nick spoza ASCII wychodzi escapowany, więc manifest przechodzi przez potok
    /// niezależnie od strony kodowej konsoli, która stoi po drodze.
    /// </summary>
    [Fact]
    public void For_EscapesNonAsciiNicknames()
    {
        const string nickname = "Zdzisław";

        var match = MatchWith((nickname, HsvColor.Create(120, 72, 88)));

        var manifest = MatchManifest.For(match.Participants);

        manifest.ShouldNotContain("ł");
        manifest.ShouldContain("\\u0142");

        PlayersIn(manifest)[0].GetProperty("name").GetString().ShouldBe(nickname);
    }

    [Fact]
    public void For_WithoutHumans_IsStillAValidManifest()
    {
        PlayersIn(MatchManifest.For([])).ShouldBeEmpty();
    }
}
