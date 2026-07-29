using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Application.Lobbies;

/// <summary>
/// Źródło map. Dziś jedna pozycja wpisana na sztywno, docelowo tabela z dokumentu §4.2.
/// Interfejs istnieje po to, żeby <see cref="CurrentLobby"/> miało kogo zapytać
/// „na jakiej mapie gramy następnym razem" i nie musiało tego wiedzieć samo.
/// </summary>
public interface IMapCatalog
{
    MapDefinition ForNextLobby();
}
