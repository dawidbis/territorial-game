namespace Territorial.Meta.Application.Lobbies.Contracts;

/// <summary>
/// Pełny stan lobby w jednej chwili.
/// </summary>
/// <remarks>
/// Świadomie snapshot, a nie strumień zdarzeń <c>PlayerJoined</c>/<c>PlayerLeft</c>
/// (dokument §5②). Zdarzenia zmuszałyby klienta do utrzymywania własnego reducera, który
/// może rozjechać się ze stanem serwera — i wtedy trzeba dorabiać resync. Stan lobby ma
/// kilka kilobajtów, więc wysyłanie go w całości jest darmowe, a klient zostaje czystą
/// funkcją renderującą. To ta sama logika co D4 i D6, tylko zastosowana do lobby.
/// </remarks>
public sealed record LobbySnapshot(LobbyHeaderDto Header, IReadOnlyList<LobbyPlayerDto> Players);
