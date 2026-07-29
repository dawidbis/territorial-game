using Microsoft.EntityFrameworkCore;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Infrastructure.Persistence;

public sealed class MetaDbContext(DbContextOptions<MetaDbContext> options) : DbContext(options)
{
    public DbSet<Player> Players => Set<Player>();

    protected override void OnModelCreating(ModelBuilder modelBuilder) =>
        modelBuilder.ApplyConfigurationsFromAssembly(typeof(MetaDbContext).Assembly);
}
