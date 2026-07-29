using Territorial.Meta.Application.Players.Contracts;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Players;

internal static class PlayerMappings
{
    public static PlayerProfileDto ToDto(this Player player) =>
        new(
            player.Id,
            player.Nickname.Value,
            new HsvColorDto(player.Color.Hue, player.Color.Saturation, player.Color.Value)
        );
}
