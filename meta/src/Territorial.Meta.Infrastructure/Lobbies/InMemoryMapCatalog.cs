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
    /// <summary>
    /// Mapa z trybu <c>--synthetic</c> konwertera — jedyna, dla której istnieje plik terenu.
    /// </summary>
    /// <remarks>
    /// Identyfikator zgadza się z nazwą pliku (<c>maps/synthetic.tmap</c>) i to jest cały
    /// powód, dla którego stoi tu <c>synthetic</c>, a nie zapowiadany <c>moon</c>: katalog
    /// obiecujący mapę, której nikt nie narysował, kończy się procesem padającym przy
    /// starcie. <c>moon</c> wraca w tej linii razem z pierwszym narysowanym obrazkiem.
    /// </remarks>
    private static readonly MapDefinition Synthetic = new("synthetic", "Testowa", MaxActors: 100);

    public MapDefinition ForNextLobby() => Synthetic;
}
