namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Cykl życia lobby. Przejścia są jednokierunkowe — lobby nigdy nie wraca do
/// wcześniejszego stanu, zamiast tego powstaje nowe. Patrz D7: ograniczony czas
/// życia zamiast migracji stanu.
/// </summary>
public enum LobbyState
{
    /// <summary>Zbiera graczy. Jedyny stan, w którym wolno dołączyć.</summary>
    Gathering = 0,

    /// <summary>Roster zamrożony, mecz jest oddawany game-serwerowi.</summary>
    Starting = 1,

    /// <summary>Mecz oddany. Lobby jest martwe i zostaje zastąpione nowym.</summary>
    Closed = 2,
}
