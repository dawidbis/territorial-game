using Microsoft.EntityFrameworkCore;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Matches;

namespace Territorial.Meta.Infrastructure.Persistence.Repositories;

internal sealed class EfMatchRepository(MetaDbContext dbContext) : IMatchRepository
{
    // Uczestnicy wchodzą do bazy razem z meczem — są jego częścią, nie osobnym agregatem.
    public void Add(Match match) => dbContext.Matches.Add(match);

    public Task<Match?> GetAsync(Guid id, CancellationToken cancellationToken) =>
        dbContext
            .Matches.Include(m => m.Participants)
            .SingleOrDefaultAsync(m => m.Id == id, cancellationToken);

    public async Task<IReadOnlyList<Match>> GetAllocatingAsync(
        CancellationToken cancellationToken
    ){
        // Bez uczestników: zamiatanie zmienia wyłącznie stan meczu, a przy starcie procesu
        // nie ma powodu ciągnąć rosterów meczów, które i tak nigdy nie wystartowały.
        return await dbContext
            .Matches.Where(m => m.State == MatchState.Allocating)
            .ToListAsync(cancellationToken);
    }

    public Task SaveChangesAsync(CancellationToken cancellationToken) =>
        dbContext.SaveChangesAsync(cancellationToken);
}
