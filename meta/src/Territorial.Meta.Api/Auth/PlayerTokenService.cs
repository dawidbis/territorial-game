using System.Text;
using Microsoft.IdentityModel.JsonWebTokens;
using Microsoft.IdentityModel.Tokens;

namespace Territorial.Meta.Api.Auth;

/// <summary>
/// Wystawia tokeny tożsamości gracza.
/// </summary>
/// <remarks>
/// W tokenie siedzi <b>wyłącznie</b> <c>sub</c>. Kusi, żeby dołożyć nick i kolor i oszczędzić
/// zapytanie do bazy przy dołączaniu do lobby, ale wtedy zmiana nicku nie byłaby widoczna
/// w rosterze aż do wygaśnięcia tokenu. Dołączenie zdarza się rzadko, więc jedno zapytanie
/// jest tańsze niż nieaktualne dane pokazywane innym graczom.
/// </remarks>
public sealed class PlayerTokenService(JwtOptions options, TimeProvider timeProvider)
{
    private readonly SigningCredentials credentials = new(
        new SymmetricSecurityKey(Encoding.UTF8.GetBytes(options.SigningKey)),
        SecurityAlgorithms.HmacSha256
    );

    private readonly JsonWebTokenHandler handler = new();

    public PlayerToken Issue(Guid playerId)
    {
        var now = timeProvider.GetUtcNow();
        var expiresAt = now + options.Lifetime;

        var descriptor = new SecurityTokenDescriptor
        {
            Issuer = options.Issuer,
            Audience = options.Audience,
            IssuedAt = now.UtcDateTime,
            NotBefore = now.UtcDateTime,
            Expires = expiresAt.UtcDateTime,
            SigningCredentials = credentials,
            Claims = new Dictionary<string, object>
            {
                [JwtRegisteredClaimNames.Sub] = playerId.ToString(),
                [JwtRegisteredClaimNames.Jti] = Guid.CreateVersion7(now).ToString(),
            },
        };

        return new PlayerToken(handler.CreateToken(descriptor), expiresAt);
    }
}
