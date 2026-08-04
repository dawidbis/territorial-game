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

    /// <summary>
    /// Żywy mecz, w którym gra ten gracz — albo <c>null</c>, gdy w żadnym nie gra.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Pytanie zadaje klient przy każdym wejściu do aplikacji: mecz jest stanem wyłącznym,
    /// więc gracz z trwającym meczem ma do niego wrócić, a nie oglądać lobby. Bez tego
    /// zapytania wiedza o meczu ginie razem z pamięcią karty, a odświeżenie strony gdziekolwiek
    /// poza adresem meczu wyglądałoby jak wypisanie z rozgrywki.
    /// </para>
    /// <para>
    /// Uczestnicy dociągani, bo wołający i tak potrzebuje slotu do wystawienia biletu.
    /// Teoretycznie graczy może być w kilku meczach naraz — praktycznie nie, bo lobby
    /// przyjmuje jedno wejście, a mecze kończą się same. Bierzemy najnowszy i nie udajemy,
    /// że rozstrzygamy tu regułę, której nikt jeszcze nie potrzebował.
    /// </para>
    /// </remarks>
    Task<Match?> GetLiveForPlayerAsync(Guid playerId, CancellationToken cancellationToken);

    Task SaveChangesAsync(CancellationToken cancellationToken);
}
