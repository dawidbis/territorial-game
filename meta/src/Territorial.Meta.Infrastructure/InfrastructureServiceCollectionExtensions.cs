using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Infrastructure.Lobbies;
using Territorial.Meta.Infrastructure.Matches;
using Territorial.Meta.Infrastructure.Persistence;
using Territorial.Meta.Infrastructure.Persistence.Repositories;

namespace Territorial.Meta.Infrastructure;

public static class InfrastructureServiceCollectionExtensions
{
    // connectionString przychodzi z zewnątrz, zamiast być odczytywany tu z IConfiguration —
    // Infrastructure nie musi wtedy znać nazw kluczy konfiguracyjnych ani ciągnąć
    // pakietu Microsoft.Extensions.Configuration.Abstractions.
    public static IServiceCollection AddInfrastructure(
        this IServiceCollection services,
        string connectionString
    ){
        services.AddDbContext<MetaDbContext>(options => options.UseSqlite(connectionString));

        services.AddScoped<IPlayerRepository, EfPlayerRepository>();
        services.AddScoped<IMatchRepository, EfMatchRepository>();

        // Katalog map jest bezstanowy i wpisany na sztywno, więc singleton.
        services.AddSingleton<IMapCatalog, InMemoryMapCatalog>();

        // Alokator wybierany konfiguracją, a nie środowiskiem: brak zbudowanej binarki C++
        // nie ma zamieniać dev-a w serwis, w którym każde lobby kończy się awarią. Reszta
        // systemu i tak widzi wyłącznie IMatchAllocator.
        services.AddSingleton<IMatchAllocator>(provider =>
        {
            var matchOptions = provider.GetRequiredService<MatchOptions>();

            if (matchOptions.UsesLocalProcess)
            {
                return new LocalProcessMatchAllocator(
                    matchOptions,
                    provider.GetRequiredService<MatchEndChannel>(),
                    provider.GetRequiredService<ILogger<LocalProcessMatchAllocator>>()
                );
            }

            if (
                !string.Equals(
                    matchOptions.Allocator,
                    MatchOptions.FakeAllocator,
                    StringComparison.OrdinalIgnoreCase
                )
            ){
                throw new InvalidOperationException(
                    $"Match:Allocator ma wartość '{matchOptions.Allocator}', a znane są "
                        + $"'{MatchOptions.FakeAllocator}' i '{MatchOptions.LocalProcessAllocator}'."
                );
            }

            return new FakeMatchAllocator(matchOptions);
        });

        return services;
    }
}
