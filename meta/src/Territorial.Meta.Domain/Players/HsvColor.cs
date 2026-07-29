namespace Territorial.Meta.Domain.Players;

/// <summary>
/// Kolor gracza w przestrzeni HSV.
/// </summary>
public readonly record struct HsvColor
{
    public const int HueCount = 360;

    public const int MaxPercent = 100;

    public const int DefaultSaturation = 72;

    public const int DefaultValue = 88;

    private HsvColor(int hue, int saturation, int value)
    {
        Hue = hue;
        Saturation = saturation;
        Value = value;
    }

    public int Hue { get; }

    public int Saturation { get; }

    public int Value { get; }

    public static HsvColor Create(int hue, int saturation, int value)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(hue);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(hue, HueCount);
        ArgumentOutOfRangeException.ThrowIfNegative(saturation);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(saturation, MaxPercent);
        ArgumentOutOfRangeException.ThrowIfNegative(value);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(value, MaxPercent);

        return new HsvColor(hue, saturation, value);
    }
}
