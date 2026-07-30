namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Port do orkiestratora procesów meczu (§9 dokumentu architektury).
/// </summary>
/// <remarks>
/// <para>
/// Implementacje wchodzą po kolei, bez zmian w warstwie wyżej: atrapa na czas, gdy nie ma
/// jeszcze binarki C++, lokalny proces w dev z binarką, agent na maszynie w produkcji.
/// </para>
/// <para>
/// Metoda <b>rzuca</b>, gdy alokacja się nie udała — brak pojemności i milczący agent są
/// dla wołającego tym samym zdarzeniem. Ponowienia i decyzja o oznaczeniu meczu jako
/// nieudanego należą do wołającego, bo tylko on wie, komu i co o tym powiedzieć.
/// </para>
/// </remarks>
public interface IMatchAllocator
{
    Task<MatchAllocation> AllocateAsync(
        MatchAllocationRequest request,
        CancellationToken cancellationToken
    );
}
