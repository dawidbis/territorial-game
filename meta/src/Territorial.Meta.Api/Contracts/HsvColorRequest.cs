using System.ComponentModel.DataAnnotations;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Api.Contracts;

public sealed class HsvColorRequest
{
    // Granice pochodzą ze stałych domenowych, nie z literałów — dzięki temu
    // atrybuty i HsvColor.Create nie mogą się rozjechać.
    [Range(0, HsvColor.HueCount - 1)]
    public int Hue { get; init; }

    [Range(0, HsvColor.MaxPercent)]
    public int Saturation { get; init; }

    [Range(0, HsvColor.MaxPercent)]
    public int Value { get; init; }
}
