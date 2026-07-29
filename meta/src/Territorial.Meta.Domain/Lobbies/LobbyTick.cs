namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Co zegar zastał przy kolejnym spojrzeniu na lobby. Wartość steruje tym, czy
/// warto cokolwiek rozsyłać — <see cref="Idle"/> to zdecydowana większość wywołań.
/// </summary>
public enum LobbyTick
{
    /// <summary>Nic się nie stało. Żadnego broadcastu.</summary>
    Idle = 0,

    /// <summary>Okno dobiegło końca przy pustym lobby i wystartowało od nowa.</summary>
    WindowReset = 1,

    /// <summary>Lobby weszło w fazę startu. Roster jest od tej chwili zamrożony.</summary>
    Started = 2,
}
