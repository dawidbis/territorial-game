using Microsoft.EntityFrameworkCore;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Infrastructure.Persistence;

public sealed class MetaDbContext(DbContextOptions<MetaDbContext> options) : DbContext(options)
{
    public DbSet<Player> Players => Set<Player>();

    /// <summary>
    /// Mecze. Uczestnicy są dostępni wyłącznie przez <c>Match.Participants</c> — nie ma
    /// scenariusza, w którym uczestnik bez meczu cokolwiek znaczy.
    /// </summary>
    public DbSet<Match> Matches => Set<Match>();

    protected override void OnModelCreating(ModelBuilder modelBuilder) =>
        modelBuilder.ApplyConfigurationsFromAssembly(typeof(MetaDbContext).Assembly);
}
