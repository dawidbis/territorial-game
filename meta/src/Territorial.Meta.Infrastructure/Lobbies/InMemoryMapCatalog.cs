using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Infrastructure.Lobbies;

/// <summary>
/// Katalog map wpisany na sztywno. Docelowo tabela <c>MapDefinition</c> z dokumentu §4.2 —
/// do tego czasu jedna pozycja wystarcza, a interfejs sprawia, że podmiana nie dotknie
/// niczego poza tym plikiem i rejestracją w kontenerze.
/// </summary>
internal sealed class InMemoryMapCatalog : IMapCatalog
{
    private static readonly MapDefinition Moon = new("moon", "Moon", MaxActors: 100);

    public MapDefinition ForNextLobby() => Moon;
}
