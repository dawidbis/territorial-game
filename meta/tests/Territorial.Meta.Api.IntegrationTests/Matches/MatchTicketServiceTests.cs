using System.Buffers.Text;
using System.Security.Cryptography;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Time.Testing;
using Microsoft.IdentityModel.JsonWebTokens;
using Microsoft.IdentityModel.Tokens;
using Shouldly;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.IntegrationTests.Matches;

/// <summary>
/// Testy biletu meczowego — tego, co game-serwer dostaje do weryfikacji.
/// </summary>
/// <remarks>
/// Kryptografia jest tu prawdziwa, bo to ona jest przedmiotem testu. Sprawdzany jest nie tyle
/// „czy się podpisało", ile <b>kształt</b> wyniku: rozmiar podpisu, komplet claimów i to, że
/// zmiana jednego znaku ładunku unieważnia bilet. Druga strona tego kontraktu żyje w C++
/// i nie ma jak zapytać, więc kontrakt musi być przybity tutaj.
/// </remarks>
public class MatchTicketServiceTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);

    private static readonly Guid PlayerId = Guid.Parse("018f3a2b-5c7d-7e91-9a2b-3c4d5e6f7a8b");
    private static readonly Guid MatchId = Guid.Parse("018f3a2b-5c7d-7e91-9a2b-000000000001");

    /// <summary>Pusta ścieżka klucza publicznego — testy nie mają po co pisać po dysku.</summary>
    private static MatchOptions NoExport(string privateKeyPem = "") =>
        new() { TicketPublicKeyPath = string.Empty, TicketPrivateKeyPem = privateKeyPem };

    private static (MatchTicketService Tickets, MatchTicketKey Key) Build(MatchOptions options)
    {
        var key = new MatchTicketKey(options, NullLogger<MatchTicketKey>.Instance);

        return (new MatchTicketService(options, key, new FakeTimeProvider(Now)), key);
    }

    private static async Task<bool> VerifiesWithAsync(string ticket, string publicKeyPem)
    {
        using var publicKey = ECDsa.Create();
        publicKey.ImportFromPem(publicKeyPem);

        var result = await new JsonWebTokenHandler().ValidateTokenAsync(
            ticket,
            new TokenValidationParameters
            {
                IssuerSigningKey = new ECDsaSecurityKey(publicKey),
                ValidateIssuerSigningKey = true,
                ValidateIssuer = false,
                ValidateAudience = false,
                // Czas w teście jest zamrożony, a walidacja życia patrzyłaby na zegar systemowy.
                ValidateLifetime = false,
            }
        );

        return result.IsValid;
    }

    [Fact]
    public async Task Issue_ProducesATicketThatVerifiesWithTheExportedPublicKey()
    {
        var (tickets, key) = Build(NoExport());

        var ticket = tickets.Issue(PlayerId, MatchId, slot: 7);

        (await VerifiesWithAsync(ticket.Value, key.PublicKeyPem)).ShouldBeTrue();
    }

    /// <summary>
    /// Najważniejszy test tego pliku.
    /// </summary>
    /// <remarks>
    /// W JWS podpis ES256 to <b>surowe R‖S, 64 bajty</b> (RFC 7518), a nie struktura DER.
    /// Strona C++ musi z tych dwóch połówek złożyć <c>ECDSA_SIG</c>, zanim poda go OpenSSL-owi
    /// — gdyby .NET zaczął kiedyś wystawiać DER, weryfikacja padłaby po stronie, której nie
    /// widać z tego repozytorium.
    /// </remarks>
    [Fact]
    public void Issue_SignsWithRawConcatenatedSignatureNotDer()
    {
        var (tickets, _) = Build(NoExport());

        var parts = tickets.Issue(PlayerId, MatchId, slot: 7).Value.Split('.');

        parts.Length.ShouldBe(3);
        Base64Url.DecodeFromChars(parts[2]).Length.ShouldBe(64);
    }

    [Fact]
    public void Issue_CarriesExactlyWhatTheGameServerNeeds()
    {
        var (tickets, _) = Build(NoExport());

        var token = new JsonWebToken(tickets.Issue(PlayerId, MatchId, slot: 254).Value);

        token.Alg.ShouldBe("ES256");
        token.GetClaim("playerId").Value.ShouldBe(PlayerId.ToString());
        token.GetClaim("matchId").Value.ShouldBe(MatchId.ToString());
        token.GetClaim("slot").Value.ShouldBe("254");
        token.GetClaim("nonce").Value.ShouldNotBeNullOrWhiteSpace();
    }

    [Fact]
    public void Issue_ExpiresAfterTheConfiguredLifetime()
    {
        var options = NoExport();
        var (tickets, _) = Build(options);

        var ticket = tickets.Issue(PlayerId, MatchId, slot: 1);

        ticket.ExpiresAt.ShouldBe(Now.AddSeconds(options.TicketLifetimeSeconds));
        new JsonWebToken(ticket.Value).ValidTo.ShouldBe(ticket.ExpiresAt.UtcDateTime);
    }

    /// <summary>Dwa bilety dla tego samego gracza muszą się różnić — inaczej powtórka jest darmowa.</summary>
    [Fact]
    public void Issue_UsesAFreshNonceEveryTime()
    {
        var (tickets, _) = Build(NoExport());

        var first = new JsonWebToken(tickets.Issue(PlayerId, MatchId, slot: 1).Value);
        var second = new JsonWebToken(tickets.Issue(PlayerId, MatchId, slot: 1).Value);

        first.GetClaim("nonce").Value.ShouldNotBe(second.GetClaim("nonce").Value);
    }

    [Fact]
    public async Task Issue_ProducesATicketThatDoesNotVerifyAfterOneCharacterOfThePayloadChanges()
    {
        var (tickets, key) = Build(NoExport());

        var parts = tickets.Issue(PlayerId, MatchId, slot: 7).Value.Split('.');

        // Podmiana jednego znaku ładunku — dokładnie to, co zrobiłby ktoś, kto chce wejść
        // na cudzy slot bez klucza.
        var tampered = string.Join('.', parts[0], parts[1][..^1] + (parts[1][^1] == 'A' ? 'B' : 'A'), parts[2]);

        (await VerifiesWithAsync(tampered, key.PublicKeyPem)).ShouldBeFalse();
    }

    [Fact]
    public async Task Issue_UsesTheConfiguredKeyInsteadOfGeneratingOne()
    {
        using var configured = ECDsa.Create(ECCurve.NamedCurves.nistP256);

        var (tickets, key) = Build(NoExport(configured.ExportPkcs8PrivateKeyPem()));

        key.IsEphemeral.ShouldBeFalse();
        (await VerifiesWithAsync(
            tickets.Issue(PlayerId, MatchId, slot: 1).Value,
            configured.ExportSubjectPublicKeyInfoPem()
        )).ShouldBeTrue();
    }

    [Fact]
    public void Key_GeneratesAnEphemeralKeyWhenNothingIsConfigured()
    {
        var (_, key) = Build(NoExport());

        key.IsEphemeral.ShouldBeTrue();
        key.PublicKeyPem.ShouldContain("BEGIN PUBLIC KEY");
    }

    [Fact]
    public void Key_RefusesACurveTheGameServerCannotVerify()
    {
        using var wrongCurve = ECDsa.Create(ECCurve.NamedCurves.nistP384);

        var options = NoExport(wrongCurve.ExportPkcs8PrivateKeyPem());

        Should
            .Throw<InvalidOperationException>(
                () => new MatchTicketKey(options, NullLogger<MatchTicketKey>.Instance)
            )
            .Message.ShouldContain("P-256");
    }

    /// <summary>
    /// Plik z kluczem publicznym to jedyne, co łączy meta z weryfikacją po stronie C++ —
    /// zapis przy starcie jest tym, co gwarantuje, że nie rozjedzie się z kluczem prywatnym.
    /// </summary>
    [Fact]
    public void Key_WritesThePublicKeyWhereTheGameServerExpectsIt()
    {
        var path = Path.Combine(Path.GetTempPath(), $"ticket-{Guid.NewGuid():N}.pub");

        try
        {
            using var key = new MatchTicketKey(
                new MatchOptions { TicketPublicKeyPath = path },
                NullLogger<MatchTicketKey>.Instance
            );

            var written = File.ReadAllText(path);

            written.ShouldBe(key.PublicKeyPem);

            using var reloaded = ECDsa.Create();
            Should.NotThrow(() => reloaded.ImportFromPem(written));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
