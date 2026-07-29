using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Infrastructure.Lobbies;
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

        // Katalog map jest bezstanowy i wpisany na sztywno, więc singleton.
        services.AddSingleton<IMapCatalog, InMemoryMapCatalog>();

        return services;
    }
}
