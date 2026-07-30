using Territorial.Meta.Domain.Matches;

namespace Territorial.Meta.Application.Matches;

public interface IMatchRepository
{
    void Add(Match match);

    /// <summary>
    /// Mecz razem z uczestnikami.
    /// </summary>
    /// <remarks>
    /// Uczestnicy dociągani są zawsze, bo jedyne pytanie, które ktoś zadaje meczowi po
    /// identyfikatorze, brzmi „czy gracz X jest w nim i na jakim slocie". Osobne zapytanie
    /// o listę byłoby drugim round-tripem po dane, których i tak potrzebuje każdy wołający.
    /// </remarks>
    Task<Match?> GetAsync(Guid id, CancellationToken cancellationToken);

    /// <summary>
    /// Mecze, które utknęły w trakcie alokacji.
    /// </summary>
    /// <remarks>
    /// Wołane raz, przy starcie procesu. Alokacja żyje w pamięci jednej instancji, więc po
    /// restarcie <b>żaden</b> wiersz w tym stanie nie ma już swojego launchera — nie ma tu
    /// wyścigu z trwającą alokacją.
    /// </remarks>
    Task<IReadOnlyList<Match>> GetAllocatingAsync(CancellationToken cancellationToken);

    Task SaveChangesAsync(CancellationToken cancellationToken);
}
