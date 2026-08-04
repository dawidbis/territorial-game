namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Wszystko, co orkiestrator musi wiedzieć, żeby postawić proces meczu.
/// </summary>
/// <remarks>
/// <para>
/// Poza <see cref="Manifest"/> nie ma tu ani nicków, ani kolorów, ani niczego z modelu
/// gracza: orkiestrator ma wybrać maszynę i port. Im mniej wie, tym mniej trzeba w nim
/// zmieniać przy każdej zmianie modelu gracza.
/// </para>
/// <para>
/// <see cref="Manifest"/> tej zasady nie łamie, bo dla orkiestratora jest <b>nieprzezroczysty</b> —
/// przepisuje go na stdin procesu i nigdy do niego nie zagląda (plan serwera gry, §3.5).
/// </para>
/// </remarks>
/// <param name="MatchId">Mecz, którego dotyczy proces; ta sama wartość stoi w ścieżce WebSocketa.</param>
/// <param name="MapId">Identyfikator mapy — proces szuka po nim pliku terenu.</param>
/// <param name="MaxActors">Sufit aktorów: ludzie i boty razem.</param>
/// <param name="Seed">Ziarno symulacji. Bez niego nie ma replayu.</param>
/// <param name="Manifest">Roster w formacie, który czyta wyłącznie proces meczu.</param>
public sealed record MatchAllocationRequest(
    Guid MatchId,
    string MapId,
    int MaxActors,
    long Seed,
    string Manifest
);
