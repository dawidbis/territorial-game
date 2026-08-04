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

    public async Task<Match?> GetLiveForPlayerAsync(
        Guid playerId,
        CancellationToken cancellationToken
    ){
        var live = await dbContext
            .Matches.Include(m => m.Participants)
            .Where(m =>
                m.State == MatchState.Live
                // `LeftAt == null`, bo wyjście jest nieodwracalne: gracz, który opuścił mecz,
                // ma nie być do niego wciągany przy następnym wejściu do aplikacji.
                && m.Participants.Any(p => p.PlayerId == playerId && p.LeftAt == null)
            )
            .ToListAsync(cancellationToken);

        // Porządkowanie w pamięci, a nie w zapytaniu, bo **SQLite nie sortuje po
        // DateTimeOffset** — próba kończy się wyjątkiem dopiero w czasie wykonania, nie przy
        // kompilacji. Kosztu tu nie ma: gracz jest w zero albo jednym żywym meczu, a wiele
        // naraz to stan, którego dziś nic nie tworzy. Bierzemy najnowszy zamiast zakładać,
        // że jest dokładnie jeden.
        return live.OrderByDescending(m => m.CreatedAt).FirstOrDefault();
    }

    public Task SaveChangesAsync(CancellationToken cancellationToken) =>
        dbContext.SaveChangesAsync(cancellationToken);
}
