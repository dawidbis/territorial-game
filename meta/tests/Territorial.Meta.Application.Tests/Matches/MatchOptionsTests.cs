using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Application.Tests.Matches;

/// <summary>
/// Testy puli portów procesów meczów.
/// </summary>
/// <remarks>
/// Arytmetyka na trzy linie, ale rozjazd między portem a numerem wpisu w puli objawia się
/// dopiero jako mecz, do którego nie da się wejść: proxy dev-servera routuje po segmencie
/// <c>gsN</c>, a proces stoi na porcie. Ten sam zakres wpisany jest po drugiej stronie,
/// w <c>client/proxy.conf.mjs</c>, i bierze się z tego samego pliku ustawień.
/// </remarks>
public class MatchOptionsTests
{
    [Fact]
    public void GameServerPorts_CountsUpFromTheFirstPort()
    {
        var options = new MatchOptions { GameServerPort = 5101, GameServerPortCount = 4 };

        Assert.Equal([5101, 5102, 5103, 5104], options.GameServerPorts);
    }

    [Fact]
    public void GameServerPorts_FallsBackToASinglePortWhenCountIsNotPositive()
    {
        var options = new MatchOptions { GameServerPort = 5101, GameServerPortCount = 0 };

        // Zero portów znaczyłoby alokator, który nigdy nie ma czego przydzielić — a to
        // wygląda jak awaria orkiestratora, choć jest literówką w konfiguracji.
        Assert.Equal([5101], options.GameServerPorts);
    }

    [Fact]
    public void SlotOfPort_NumbersEntriesFromZero()
    {
        var options = new MatchOptions { GameServerPort = 5101, GameServerPortCount = 8 };

        Assert.Equal(0, options.SlotOfPort(5101));
        Assert.Equal(7, options.SlotOfPort(5108));
    }
}
