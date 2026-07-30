using Microsoft.AspNetCore.SignalR;

namespace Territorial.Meta.Api.Auth;

/// <summary>
/// Mówi SignalR-owi, po czym rozpoznawać gracza w <c>Clients.User(...)</c>.
/// </summary>
/// <remarks>
/// <para>
/// Bez tego adresowanie po graczu <b>po cichu nic by nie robiło</b> — najgorszy rodzaj
/// awarii. Domyślny <c>DefaultUserIdProvider</c> czyta <c>ClaimTypes.NameIdentifier</c>,
/// a my mamy w tokenie <c>sub</c>, bo <c>MapInboundClaims = false</c> wyłącza przepisywanie
/// nazw claimów. Zwróciłby <c>null</c>, wysyłka trafiłaby w pustkę i nikt nie dostałby
/// żadnego błędu.
/// </para>
/// <para>
/// Alternatywa — sięgnięcie po rejestr połączeń z <c>CurrentLobby</c> — działa, ale
/// wystawia wewnętrzny stan lobby na zewnątrz i nie obsługuje graczy spoza lobby.
/// Ta wersja przy okazji dostarcza wiadomość na <b>wszystkie</b> karty gracza.
/// </para>
/// </remarks>
public sealed class PlayerUserIdProvider : IUserIdProvider
{
    public string? GetUserId(HubConnectionContext connection) =>
        connection.User?.GetPlayerId()?.ToString();
}
