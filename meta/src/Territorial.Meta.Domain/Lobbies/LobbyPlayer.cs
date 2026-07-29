using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Gracz w rosterze lobby.
/// </summary>
/// <remarks>
/// <para>
/// Nick i kolor są <b>kopiowane</b> z profilu w momencie dołączenia, a nie trzymane
/// referencją. Lobby żyje kilkadziesiąt sekund, więc rozjazd jest nieistotny, a kopia
/// zdejmuje z rostera zależność od bazy przy każdym broadcaście.
/// </para>
/// <para>
/// <c>JoinedAt</c> wyznacza kolejność w rosterze. Bez tego lista skakałaby graczom
/// przed oczami przy każdym dołączeniu, bo kolejność słownika nie jest gwarantowana.
/// </para>
/// </remarks>
public sealed record LobbyPlayer(
    Guid PlayerId,
    Nickname Nickname,
    HsvColor Color,
    DateTimeOffset JoinedAt
);
