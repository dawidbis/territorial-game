namespace Territorial.Meta.Api.Auth;

/// <summary>
/// Konfiguracja podpisu tokenów gracza.
/// </summary>
/// <remarks>
/// Klucz jest symetryczny (HS256), bo token wystawia i weryfikuje ten sam proces.
/// Asymetryczny Ed25519 z dokumentu (§5③) dotyczy czegoś innego — biletu meczowego,
/// który weryfikuje game-serwer bez kontaktu z meta.
/// </remarks>
public sealed class JwtOptions
{
    public const string SectionName = "Jwt";

    /// <summary>HS256 wymaga klucza nie krótszego niż długość skrótu, czyli 256 bitów.</summary>
    public const int MinSigningKeyLength = 32;

    /// <summary>Nigdy nie trafia do repozytorium — w dev z user-secrets, w produkcji z sekretów hosta.</summary>
    public string SigningKey { get; init; } = string.Empty;

    public string Issuer { get; init; } = "territorial-meta";

    public string Audience { get; init; } = "territorial-client";

    /// <summary>
    /// Czas życia tokenu gościa.
    /// </summary>
    /// <remarks>
    /// Trzydzieści dni, a nie kwadrans, bo token <b>jest</b> tożsamością gościa — zastępuje
    /// identyfikator trzymany dotąd w localStorage. Klient odświeża go przy każdym starcie
    /// aplikacji, więc aktywny gracz nigdy nie zbliża się do wygaśnięcia i cała maszyneria
    /// refresh-tokenów jest zbędna. Krótki TTL wraca razem z prawdziwym logowaniem.
    /// </remarks>
    public TimeSpan Lifetime { get; init; } = TimeSpan.FromDays(30);
}
