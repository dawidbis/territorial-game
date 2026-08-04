using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Domain.Matches;

/// <summary>
/// Człowiek na konkretnym slocie konkretnego meczu.
/// </summary>
/// <remarks>
/// <para>
/// Nick i kolor są <b>kopiowane</b> z rostera, dokładnie tak jak w <c>LobbyPlayer</c>, ale
/// z mocniejszych powodów: game-serwer potrzebuje ich do <c>SlotInfo</c> w <c>MatchInit</c>,
/// a historia meczów powinna pokazywać nick używany wtedy, nie dzisiejszy. Referencja do
/// profilu dałaby przeciwnie — zmiana nicku przepisywałaby historię.
/// </para>
/// <para>
/// Boty nie mają tu wiersza. Zajmują sloty <c>N+1..MaxActors</c>, które wynikają z liczby
/// ludzi i sufitu mapy, więc nie ma czego zapisywać ani z czym synchronizować.
/// </para>
/// </remarks>
public sealed class MatchParticipant
{
    private MatchParticipant()
    {
        // Konstruktor dla EF Core. Wszystkie właściwości są typami wartościowymi,
        // więc nie ma tu żadnego "= null!".
    }

    internal MatchParticipant(
        Guid matchId,
        Guid playerId,
        byte slot,
        Nickname nickname,
        HsvColor color
    ){
        MatchId = matchId;
        PlayerId = playerId;
        Slot = slot;
        Nickname = nickname;
        Color = color;
    }

    public Guid MatchId { get; private set; }

    public Guid PlayerId { get; private set; }

    /// <summary>Slot z zakresu 1..254 (D12). Zapisany, a nie wyliczany ponownie.</summary>
    /// <remarks>
    /// Reguła przypisania może się kiedyś zmienić, a stare replaye muszą zostać czytelne —
    /// dlatego slot jest faktem zapisanym razem z meczem, nie funkcją bieżącego kodu.
    /// </remarks>
    public byte Slot { get; private set; }

    public Nickname Nickname { get; private set; }

    public HsvColor Color { get; private set; }

    /// <summary>
    /// Chwila jawnego opuszczenia meczu; <c>null</c>, dopóki gracz w nim jest.
    /// </summary>
    /// <remarks>
    /// Wiersz **zostaje** — gracz był w tym meczu i historia ma to pokazywać. Znika za to
    /// z odpowiedzi na pytanie „w czym gram" i przestaje dostawać bilety, więc wyjście jest
    /// nieodwracalne. Slotu nie zwalniamy: proces meczu wciąż nim dysponuje, a terytorium
    /// zajęte przez gracza nie przestaje istnieć dlatego, że ten zamknął przeglądarkę.
    /// </remarks>
    public DateTimeOffset? LeftAt { get; private set; }

    public bool HasLeft => LeftAt is not null;

    /// <summary>Odnotowuje wyjście. Powtórne wywołanie nic nie zmienia — liczy się pierwsze.</summary>
    internal void Leave(DateTimeOffset now) => LeftAt ??= now;
}
