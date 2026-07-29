using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Players;

public interface IPlayerRepository
{
    Task<Player?> GetAsync(Guid id, CancellationToken cancellationToken);

    void Add(Player player);

    Task SaveChangesAsync(CancellationToken cancellationToken);
}