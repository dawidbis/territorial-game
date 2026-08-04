using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Players;

/// <summary>
/// Zamiana koloru gracza na spakowane RGB.
/// </summary>
/// <remarks>
/// <para>
/// Konwersja stoi w warstwie aplikacji, a nie w domenie i nie w game-serwerze, i to jest
/// świadome. Domena trzyma HSV, bo w tej przestrzeni kolor się wybiera i w niej sprawdza
/// się, czy dwa kolory da się od siebie odróżnić. Protokół meczu chce gotowego
/// <c>colorRgb</c> — a gdyby przeliczał go C++, proces meczu musiałby wiedzieć o istnieniu
/// przestrzeni, której nigdy nie zobaczy (plan serwera gry, §3.5).
/// </para>
/// <para>
/// Arytmetyka jest w całości całkowita. Nie chodzi o wydajność, tylko o to, żeby ten sam
/// kolor dawał ten sam bajt niezależnie od maszyny — kolory jadą do wszystkich graczy
/// w meczu i rozjazd na ostatnim bicie widać jako dwa różne odcienie tego samego gracza.
/// </para>
/// </remarks>
public static class HsvColorConversion
{
    /// <summary>Ile stopni obejmuje jeden z sześciu sektorów koła barw.</summary>
    private const int SectorDegrees = 60;

    private const int ChannelMax = 255;

    /// <summary>Kolor w postaci <c>0xRRGGBB</c> — dokładnie to, czego oczekuje manifest.</summary>
    public static int ToRgb(this HsvColor color)
    {
        var saturation = color.Saturation * ChannelMax / HsvColor.MaxPercent;
        var value = color.Value * ChannelMax / HsvColor.MaxPercent;

        // Hue jest z zakresu 0..359, więc sektor zawsze mieści się w 0..5 i nie ma tu
        // przypadku brzegowego do obsłużenia.
        var sector = color.Hue / SectorDegrees;
        var offset = color.Hue % SectorDegrees * ChannelMax / SectorDegrees;

        var down = value * (ChannelMax - saturation) / ChannelMax;
        var falling = value * (ChannelMax - saturation * offset / ChannelMax) / ChannelMax;
        var rising =
            value * (ChannelMax - saturation * (ChannelMax - offset) / ChannelMax) / ChannelMax;

        return sector switch
        {
            0 => Pack(value, rising, down),
            1 => Pack(falling, value, down),
            2 => Pack(down, value, rising),
            3 => Pack(down, falling, value),
            4 => Pack(rising, down, value),
            _ => Pack(value, down, falling),
        };
    }

    private static int Pack(int red, int green, int blue) => (red << 16) | (green << 8) | blue;
}
