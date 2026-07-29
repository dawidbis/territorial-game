using Microsoft.EntityFrameworkCore;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Infrastructure.Persistence.Repositories;

internal sealed class EfPlayerRepository(MetaDbContext dbContext) : IPlayerRepository
{
    public Task<Player?> GetAsync(Guid id, CancellationToken cancellationToken) =>
        dbContext.Players.SingleOrDefaultAsync(p => p.Id == id, cancellationToken);

    public void Add(Player player) => dbContext.Players.Add(player);

    public Task SaveChangesAsync(CancellationToken cancellationToken) =>
        dbContext.SaveChangesAsync(cancellationToken);
}