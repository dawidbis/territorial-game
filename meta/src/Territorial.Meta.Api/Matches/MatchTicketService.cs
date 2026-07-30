using System.Buffers.Text;
using System.Security.Cryptography;
using Microsoft.IdentityModel.JsonWebTokens;
using Microsoft.IdentityModel.Tokens;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Wystawia bilety wpuszczające gracza na konkretny slot konkretnego meczu.
/// </summary>
/// <remarks>
/// <para>
/// JWT podpisany <b>ECDSA P-256</b> (ES256), czyli asymetrycznie — w odróżnieniu od tokenu
/// gracza (HS256), który wystawia i weryfikuje ten sam proces. Bilet weryfikuje game-serwer
/// bez kontaktu z meta (§4.3), więc klucz prywatny zostaje w sekretach hosta, a publiczny
/// jedzie do procesu meczu. Krzywa wybrana zamiast Ed25519, bo .NET nie ma Ed25519 w BCL,
/// a po stronie C++ i tak jest OpenSSL.
/// </para>
/// <para>
/// Ładunek jest minimalny (§5③): <c>playerId</c>, <c>matchId</c>, <c>slot</c>, <c>nonce</c>
/// i czas wygaśnięcia. Bez <c>iss</c> i <c>aud</c> — game-serwer sprawdza <c>matchId</c>
/// względem meczu, który obsługuje, a to jest mocniejsze niż dopasowanie stałego napisu.
/// </para>
/// <para>
/// Jednorazowość rozwiązuje się sama dzięki D7: jeden proces obsługuje jeden mecz, więc
/// zużyte <c>nonce</c> pamięta u siebie. Meta nie musi o tym wiedzieć.
/// </para>
/// </remarks>
public sealed class MatchTicketService
{
    /// <summary>Długość jednorazowej części biletu. 128 bitów z CSPRNG — nie do zgadnięcia.</summary>
    private const int NonceBytes = 16;

    private readonly MatchOptions options;
    private readonly TimeProvider timeProvider;
    private readonly SigningCredentials credentials;
    private readonly JsonWebTokenHandler handler = new();

    public MatchTicketService(MatchOptions options, MatchTicketKey key, TimeProvider timeProvider)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(options);

        this.options = options;
        this.timeProvider = timeProvider;

        credentials = new SigningCredentials(
            new ECDsaSecurityKey(key.Signing),
            SecurityAlgorithms.EcdsaSha256
        );
    }

    public MatchTicket Issue(Guid playerId, Guid matchId, byte slot)
    {
        var now = timeProvider.GetUtcNow();
        var expiresAt = now + TimeSpan.FromSeconds(options.TicketLifetimeSeconds);

        var descriptor = new SecurityTokenDescriptor
        {
            IssuedAt = now.UtcDateTime,
            NotBefore = now.UtcDateTime,
            Expires = expiresAt.UtcDateTime,
            SigningCredentials = credentials,
            Claims = new Dictionary<string, object>
            {
                ["playerId"] = playerId.ToString(),
                ["matchId"] = matchId.ToString(),
                // Liczba, nie napis: po stronie C++ oszczędza to konwersję i całą klasę
                // pytań o to, co zrobić z "007".
                ["slot"] = (int)slot,
                ["nonce"] = Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(NonceBytes)),
            },
        };

        return new MatchTicket(handler.CreateToken(descriptor), expiresAt);
    }
}
