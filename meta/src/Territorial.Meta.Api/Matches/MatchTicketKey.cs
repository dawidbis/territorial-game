using System.Security.Cryptography;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Klucz podpisu biletów meczowych — ECDSA P-256 (§5③).
/// </summary>
/// <remarks>
/// <para>
/// Osobno od <see cref="MatchTicketService"/>, bo klucz i wystawianie mają różne cykle
/// życia i różne powody do zmiany: klucz wczytuje się raz, z sekretów, i trzeba go zamknąć;
/// bilety wystawia się tysiące razy. Rozdzielenie pozwala też testom podstawić własny klucz
/// bez dotykania konfiguracji.
/// </para>
/// <para>
/// Klucz <b>publiczny</b> zapisywany jest do pliku przy starcie, o ile ścieżka jest
/// skonfigurowana. To on jedzie do game-serwera i nie jest sekretem — a zapis przy starcie
/// gwarantuje, że plik odpowiada kluczowi, którym meta faktycznie podpisuje.
/// </para>
/// </remarks>
public sealed partial class MatchTicketKey : IDisposable
{
    /// <summary>Rozmiar klucza P-256 w bitach. Krzywa wybrana w §6.1 planu alokacji.</summary>
    private const int ExpectedKeySize = 256;

    private readonly ECDsa key;

    public MatchTicketKey(MatchOptions options, ILogger<MatchTicketKey> logger)
    {
        ArgumentNullException.ThrowIfNull(options);

        key = ECDsa.Create(ECCurve.NamedCurves.nistP256);

        if (string.IsNullOrWhiteSpace(options.TicketPrivateKeyPem))
        {
            // Klucz z konstruktora zostaje. Dopuszczalne wyłącznie w dev — poza nim start
            // zatrzymuje się wcześniej, przy walidacji konfiguracji.
            IsEphemeral = true;

            LogEphemeralKey(logger);
        }
        else
        {
            key.ImportFromPem(options.TicketPrivateKeyPem);

            if (key.KeySize != ExpectedKeySize)
            {
                throw new InvalidOperationException(
                    $"Klucz biletów ma {key.KeySize} bitów, a game-serwer weryfikuje wyłącznie "
                        + $"P-256 ({ExpectedKeySize} bitów). Podmień klucz albo zmień obie strony naraz."
                );
            }
        }

        if (!string.IsNullOrWhiteSpace(options.TicketPublicKeyPath))
        {
            ExportPublicKey(options.TicketPublicKeyPath, logger);
        }
    }

    /// <summary>Czy klucz powstał na czas życia procesu, zamiast przyjść z konfiguracji.</summary>
    public bool IsEphemeral { get; }

    /// <summary>Klucz do podpisywania. Nie opuszcza tego procesu.</summary>
    public ECDsa Signing => key;

    /// <summary>Klucz publiczny w formacie SPKI PEM — dokładnie to, co czyta OpenSSL.</summary>
    public string PublicKeyPem => key.ExportSubjectPublicKeyInfoPem();

    public void Dispose() => key.Dispose();

    private void ExportPublicKey(string path, ILogger<MatchTicketKey> logger)
    {
        var full = Path.GetFullPath(path);

        var directory = Path.GetDirectoryName(full);

        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        File.WriteAllText(full, PublicKeyPem);

        LogPublicKeyExported(logger, full);
    }

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Brak Match:TicketPrivateKeyPem — bilety podpisuje klucz wygenerowany na czas "
            + "życia procesu. Po restarcie meta wcześniejsze bilety przestaną być ważne."
    )]
    private static partial void LogEphemeralKey(ILogger logger);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Klucz publiczny biletów zapisany do {Path} — tę ścieżkę podaje się "
            + "game-serwerowi przez --ticket-key."
    )]
    private static partial void LogPublicKeyExported(ILogger logger, string path);
}
