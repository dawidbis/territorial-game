using System.Security.Claims;
using Microsoft.IdentityModel.JsonWebTokens;

namespace Territorial.Meta.Api.Auth;

public static class ClaimsPrincipalExtensions
{
    /// <summary>
    /// Wyciąga tożsamość gracza z claima <c>sub</c>.
    /// </summary>
    /// <remarks>
    /// Działa identycznie w kontrolerze (<c>User</c>) i w hubie (<c>Context.User</c>) — to jest
    /// cały powód, dla którego tożsamość przeniosła się z nagłówka do tokenu. Przeglądarkowe
    /// <c>WebSocket</c> API nie pozwala ustawić własnych nagłówków na handshake'u, więc dawne
    /// <c>X-Player-Id</c> nigdy nie dotarłoby do SignalR.
    /// </remarks>
    /// <returns><c>null</c>, gdy żądanie jest anonimowe albo claim nie jest identyfikatorem GUID.</returns>
    public static Guid? GetPlayerId(this ClaimsPrincipal principal)
    {
        var value = principal.FindFirstValue(JwtRegisteredClaimNames.Sub);

        return Guid.TryParse(value, out var playerId) ? playerId : null;
    }
}
