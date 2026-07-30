namespace Territorial.Meta.Domain.Matches;

/// <summary>
/// Rezerwacje i pojemność slotów aktora z D12.
/// </summary>
/// <remarks>
/// <para>
/// Tablica kafelków w game-serwerze trzyma <b>slot</b> (u8), a nie identyfikator aktora —
/// stąd twarde 254 miejsca na mecz i dwie wartości zarezerwowane. Meta musi znać te same
/// liczby, bo to ona sloty przydziela.
/// </para>
/// <para>
/// Sufit jest właściwością <b>meczu</b>, nie systemu: sloty żyją tyle, co jeden mecz,
/// więc 254 nie ogranicza liczby graczy w serwisie.
/// </para>
/// </remarks>
public static class ActorSlot
{
    /// <summary>Kafelek niczyj.</summary>
    public const byte Wilderness = 0;

    /// <summary>Kafelek nieprzejezdny.</summary>
    public const byte Water = 255;

    public const byte FirstActor = 1;

    public const byte LastActor = 254;

    /// <summary>Ilu aktorów — ludzi i botów razem — zmieści się w jednym meczu.</summary>
    public const int MaxActorsPerMatch = LastActor - FirstActor + 1;

    /// <summary>
    /// Slot dla gracza stojącego na danej pozycji rostera (liczonej od zera).
    /// </summary>
    /// <remarks>
    /// Cała reguła przypisania sprowadza się do tej jednej funkcji: ludzie biorą sloty
    /// <c>1..N</c> w kolejności rostera, boty dopełniają <c>N+1..MaxActors</c>. Kolejność
    /// rostera jest stabilna (<c>JoinedAt</c>, potem <c>PlayerId</c>), więc przypisanie da
    /// się odtworzyć — czego wymaga determinizm z D10.
    /// </remarks>
    public static byte ForRosterPosition(int position)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(position);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(position, MaxActorsPerMatch);

        return (byte)(FirstActor + position);
    }
}
