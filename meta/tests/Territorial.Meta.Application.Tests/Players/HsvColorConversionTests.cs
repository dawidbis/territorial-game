using Territorial.Meta.Application.Players;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Tests.Players;

/// <summary>
/// Testy konwersji HSV → RGB.
/// </summary>
/// <remarks>
/// Punkty sprawdzane po nazwach kolorów, a nie po liczbach z drugiej implementacji:
/// czysta czerwień ma wyjść czerwienią, a nie „tym, co wyszło poprzednio". Testowanie
/// konwersji przez jej powtórzenie w teście nie sprawdza niczego.
/// </remarks>
public class HsvColorConversionTests
{
    [Theory]
    [InlineData(0, 0xFF0000)] // czerwony
    [InlineData(60, 0xFFFF00)] // żółty
    [InlineData(120, 0x00FF00)] // zielony
    [InlineData(180, 0x00FFFF)] // cyjan
    [InlineData(240, 0x0000FF)] // niebieski
    [InlineData(300, 0xFF00FF)] // magenta
    public void ToRgb_HitsThePureHuesExactly(int hue, int expected)
    {
        HsvColor.Create(hue, 100, 100).ToRgb().ShouldBe(expected);
    }

    [Fact]
    public void ToRgb_WithoutSaturation_GivesGrey()
    {
        var grey = HsvColor.Create(200, 0, 50).ToRgb();

        var red = (grey >> 16) & 0xFF;
        var green = (grey >> 8) & 0xFF;
        var blue = grey & 0xFF;

        red.ShouldBe(green);
        green.ShouldBe(blue);
    }

    [Fact]
    public void ToRgb_WithoutValue_IsBlackWhateverTheHue()
    {
        for (var hue = 0; hue < HsvColor.HueCount; hue++)
        {
            HsvColor.Create(hue, 100, 0).ToRgb().ShouldBe(0);
        }
    }

    /// <summary>
    /// Kolor musi zmieścić się w 24 bitach — parser manifestu po stronie C++ odrzuca
    /// wszystko powyżej <c>0xFFFFFF</c>, a odrzucony manifest to mecz, który nie wstaje.
    /// </summary>
    [Fact]
    public void ToRgb_NeverLeavesTheRangeTheGameServerAccepts()
    {
        for (var hue = 0; hue < HsvColor.HueCount; hue++)
        {
            var color = HsvColor.Create(hue, HsvColor.DefaultSaturation, HsvColor.DefaultValue)
                .ToRgb();

            color.ShouldBeInRange(0, 0xFFFFFF);
        }
    }
}
