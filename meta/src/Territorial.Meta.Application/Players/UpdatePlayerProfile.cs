using Territorial.Meta.Application.Players.Contracts;

namespace Territorial.Meta.Application.Players;

public sealed class UpdatePlayerProfile(IPlayerRepository players, TimeProvider timeProvider)
{
    /// <returns><c>null</c>, jeśli gracza o takim identyfikatorze nie ma.</returns>
    public async Task<PlayerProfileDto?> ExecuteAsync(
        UpdatePlayerProfileCommand command,
        CancellationToken cancellationToken
    ){
        var player = await players.GetAsync(command.PlayerId, cancellationToken);

        if (player is null)
        {
            return null;
        }

        player.Rename(command.Nickname);
        player.ChangeColor(command.Color);
        player.Touch(timeProvider.GetUtcNow());

        await players.SaveChangesAsync(cancellationToken);

        return player.ToDto();
    }
}