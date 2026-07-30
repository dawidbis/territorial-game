namespace Territorial.Meta.Domain.Matches;

/// <summary>
/// Cykl życia meczu. Przejścia są jednokierunkowe — mecz nigdy nie wraca do
/// wcześniejszego stanu.
/// </summary>
/// <remarks>
/// Stan zerowy to <see cref="Allocating"/>, a nie sztuczne "None": mecz zapisywany jest
/// do bazy <b>przed</b> rozmową z orkiestratorem, więc od pierwszej chwili istnienia jest
/// w trakcie alokacji. Dzięki temu restart meta w środku alokacji zostawia po sobie ślad,
/// który da się zamieść.
/// </remarks>
public enum MatchState
{
    /// <summary>Zapisany, czeka na endpoint od orkiestratora.</summary>
    Allocating = 0,

    /// <summary>Game-serwer stoi i przyjmuje graczy.</summary>
    Live = 1,

    /// <summary>Mecz się skończył i wynik został przyjęty.</summary>
    /// <remarks>
    /// Przejście dochodzi razem z odbiorem wyniku od game-serwera (plan alokacji, etap 4).
    /// Wartość jest tu od początku, bo to kontrakt kolumny w bazie, a nie kod do dopisania.
    /// </remarks>
    Completed = 2,

    /// <summary>Alokacja się nie udała. Gracze wracają do nowego lobby.</summary>
    Failed = 3,
}
