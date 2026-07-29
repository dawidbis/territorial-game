using Territorial.Meta.Application.Players.Contracts;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Players;

/// <summary>
/// Zwraca profil odwiedzającego. Gdy klient nie przedstawia żadnej tożsamości
/// albo wskazuje gracza, którego już nie ma — zakłada nowego.
/// </summary>
public sealed class GetOrCreatePlayer(IPlayerRepository players, TimeProvider timeProvider)
{
    public async Task<PlayerProfileDto> ExecuteAsync(
        Guid? playerId,
        CancellationToken cancellationToken
    ){
        var now = timeProvider.GetUtcNow();

        if (playerId is { } id)
        {
            var existing = await players.GetAsync(id, cancellationToken);

            if (existing is not null)
            {
                // Touch zwraca false, dopóki nie minie LastSeenPrecision —
                // dzięki temu typowe wejście na stronę nie generuje UPDATE-a.
                if (existing.Touch(now))
                {
                    await players.SaveChangesAsync(cancellationToken);
                }

                return existing.ToDto();
            }
        }

        var player = Player.CreateGuest(now);

        players.Add(player);
        await players.SaveChangesAsync(cancellationToken);

        return player.ToDto();
    }
}
