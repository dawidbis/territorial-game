using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text.Json;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Wystawia bilety wpuszczające gracza na konkretny slot konkretnego meczu.
/// </summary>
/// <remarks>
/// <para>
/// <b>Etap 1: bilet NIE JEST podpisany.</b> Nie ma jeszcze game-serwera, więc nie ma kto
/// go weryfikować, a kryptografia bez odbiorcy dawałaby wyłącznie złudzenie ochrony.
/// Wartość jest nieprzezroczystym ciągiem o docelowym ładunku (§5③:
/// <c>playerId, matchId, slot, nonce</c>) i docelowym TTL — dzięki temu etap 2 podmienia
/// wyłącznie sposób zamknięcia ładunku, a nie kontrakt wiadomości ani kod klienta.
/// </para>
/// <para>
/// Docelowo <b>ECDSA P-256</b> (ES256), czyli podpis asymetryczny — w odróżnieniu od tokenu
/// gracza (HS256), który wystawia i weryfikuje ten sam proces. Bilet musi zweryfikować
/// game-serwer bez kontaktu z meta (§4.3), więc klucz prywatny zostaje w sekretach hosta,
/// a publiczny wchodzi do obrazu game-serwera. Krzywa wybrana świadomie zamiast Ed25519:
/// .NET nie ma Ed25519 w BCL, a OpenSSL i tak będzie po stronie C++.
/// </para>
/// <para>
/// Jednorazowość rozwiązuje się sama dzięki D7: jeden proces obsługuje jeden mecz, więc
/// zużyte <c>nonce</c> pamięta u siebie. Meta nie musi o tym wiedzieć.
/// </para>
/// </remarks>
public sealed class MatchTicketService(MatchOptions options, TimeProvider timeProvider)
{
    /// <summary>Długość jednorazowej części biletu. 128 bitów z CSPRNG — nie do zgadnięcia.</summary>
    private const int NonceBytes = 16;

    private sealed record TicketPayload(Guid PlayerId, Guid MatchId, byte Slot, string Nonce);

    public MatchTicket Issue(Guid playerId, Guid matchId, byte slot)
    {
        var expiresAt =
            timeProvider.GetUtcNow() + TimeSpan.FromSeconds(options.TicketLifetimeSeconds);

        var nonce = Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(NonceBytes));

        var payload = new TicketPayload(playerId, matchId, slot, nonce);

        return new MatchTicket(
            Base64Url.EncodeToString(JsonSerializer.SerializeToUtf8Bytes(payload)),
            expiresAt
        );
    }
}
