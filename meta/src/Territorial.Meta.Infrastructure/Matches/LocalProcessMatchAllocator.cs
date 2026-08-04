using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using Microsoft.Extensions.Logging;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Infrastructure.Matches;

/// <summary>
/// Alokator, który stawia proces meczu na tej samej maszynie co meta.
/// </summary>
/// <remarks>
/// <para>
/// Wersja deweloperska tego, co na produkcji robi agent (§9 dokumentu architektury):
/// wybór maszyny znika, bo maszyna jest jedna, ale reszta kontraktu zostaje ta sama —
/// alokacja albo oddaje adres działającego procesu, albo rzuca. Stan pośredni „proces
/// wstał, ale jeszcze nie przyjmuje" nie ma prawa wyjść na zewnątrz, bo bilety idą do
/// graczy natychmiast po powrocie stąd.
/// </para>
/// <para>
/// Proces jest świadomie <b>osierocany</b>. Gasi się sam po trzech warunkach z §3.7 planu
/// serwera gry, więc meta nie musi go pilnować, a restart meta nie ma prawa zerwać
/// trwającego meczu — dokładnie to gwarantuje §4.3 dokumentu architektury.
/// </para>
/// <para>
/// <b>Osierocony nie znaczy niezauważony.</b> Uchwyt zostaje otwarty wyłącznie po to, żeby
/// dostać zdarzenie wyjścia i powiedzieć o nim przez <see cref="MatchEndChannel"/>. Bez tego
/// wiersz meczu zostaje na zawsze w stanie <c>Live</c>, meta wydaje bilety do procesu,
/// którego nie ma, a gracz wracający pod link ląduje na ekranie „tego meczu już nie ma".
/// Uchwyt nie jest kontrolą nad procesem: nikt go stąd nie zabija i restart meta nadal
/// niczego nie zrywa — traci tylko obserwację, dokładnie tak jak przed tą zmianą.
/// </para>
/// </remarks>
internal sealed partial class LocalProcessMatchAllocator(
    MatchOptions options,
    MatchEndChannel ended,
    ILogger<LocalProcessMatchAllocator> logger
) : IMatchAllocator
{
    /// <summary>Co ile pytamy, czy proces już nasłuchuje.</summary>
    private const int ProbeIntervalMilliseconds = 50;

    public async Task<MatchAllocation> AllocateAsync(
        MatchAllocationRequest request,
        CancellationToken cancellationToken
    ){
        ArgumentNullException.ThrowIfNull(request);

        var binary = ResolveBinary();
        var map = ResolveMap(request.MapId);
        var ticketKey = ResolveTicketKey();

        // Port wybierany PRZED startem, a nie sprawdzany po nim. Gdyby stał na nim jeszcze
        // proces poprzedniego meczu, sonda gotowości zameldowałaby sukces w pierwszej
        // próbie, a gracze trafiliby do meczu, którego ich bilety nie dotyczą.
        var port = await ReserveFreePortAsync(cancellationToken);

        var process = Start(binary, map, ticketKey, port, options, request);

        try
        {
            await SendManifestAsync(process, request.Manifest, cancellationToken);

            await WaitUntilListeningAsync(process, port, cancellationToken);
        }
        catch
        {
            Terminate(process);

            throw;
        }

        LogProcessStarted(logger, request.MatchId, process.Id, port);

        WatchForExit(process, request.MatchId);

        return new MatchAllocation(IPAddress.Loopback.ToString(), port, BuildWsUrl(port, request.MatchId));
    }

    /// <summary>
    /// Melduje koniec meczu, gdy zgaśnie jego proces.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Uchwyt procesu żyje do zdarzenia i jest w nim zwalniany — to jedyne miejsce, w którym
    /// wolno go zamknąć, bo <c>Dispose</c> przed <c>Exited</c> zabiera zdarzenie razem
    /// z uchwytem.
    /// </para>
    /// <para>
    /// Wyścig z <c>HasExited</c> jest obsłużony <b>po</b> podpięciu uchwytu, nie przed:
    /// proces, który zgasł między startem a tą linią, nie wywoła już zdarzenia, a sprawdzenie
    /// przed subskrypcją zostawiałoby okno, w którym nie zrobi tego ani jedno, ani drugie.
    /// Podwójne zgłoszenie jest nieszkodliwe — po drugiej stronie stoi <c>MarkCompleted</c>,
    /// które przy meczu już zamkniętym po prostu nic nie robi.
    /// </para>
    /// </remarks>
    private void WatchForExit(Process process, Guid matchId)
    {
        process.Exited += (_, _) =>
        {
            int? code = null;

            try
            {
                code = process.ExitCode;
            }
            catch (Exception exception)
            {
                // Kod wyjścia jest informacją do logu, nie warunkiem zamknięcia meczu.
                LogExitCodeUnavailable(logger, matchId, exception);
            }

            LogProcessExited(logger, matchId, code);

            ended.Enqueue(new MatchEndedNotice(matchId, code));

            process.Dispose();
        };

        process.EnableRaisingEvents = true;

        if (process.HasExited)
        {
            ended.Enqueue(new MatchEndedNotice(matchId, null));
        }
    }

    /// <summary>
    /// Publiczny adres meczu: baza, numer procesu w puli i identyfikator meczu.
    /// </summary>
    /// <remarks>
    /// Segment <c>gsN</c> jest adresem <b>dla proxy dev-servera</b>, nie dla procesu meczu —
    /// ten zna wyłącznie <c>/ws/match/{matchId}</c> i wszystko inne odrzuca, więc proxy ten
    /// segment zdejmuje. Bierze się stąd, że dev-server nie umie wybierać celu per żądanie:
    /// każdy port puli ma u niego własny statyczny wpis, a coś w ścieżce musi powiedzieć,
    /// o który chodzi. Numer wpisu, nie port — D9 zostaje w mocy.
    /// </remarks>
    private string BuildWsUrl(int port, Guid matchId) =>
        $"{options.MatchWebSocketBaseUrl.TrimEnd('/')}/gs{options.SlotOfPort(port)}/{matchId}";

    private static Process Start(
        string binary,
        string map,
        string ticketKey,
        int port,
        MatchOptions options,
        MatchAllocationRequest request
    ){
        var info = new ProcessStartInfo(binary)
        {
            UseShellExecute = false,
            RedirectStandardInput = true,
            // Kodowanie jawnie, bo domyślne bierze się ze strony kodowej konsoli — a po
            // drugiej stronie potoku stoi parser JSON-a oczekujący UTF-8. Bez BOM-u:
            // ten wywróciłby parsowanie na pierwszym bajcie.
            StandardInputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
        };

        // Wyjście procesu świadomie NIE jest przechwytywane. W dev logi meczu mają lecieć
        // do tej samej konsoli co logi meta, a przechwycony i nieczytany potok zapycha się
        // po kilkudziesięciu kilobajtach i zawiesza proces w połowie meczu.

        info.ArgumentList.Add("--match-id");
        info.ArgumentList.Add(request.MatchId.ToString());

        info.ArgumentList.Add("--port");
        info.ArgumentList.Add(port.ToString(CultureInfo.InvariantCulture));

        info.ArgumentList.Add("--map");
        info.ArgumentList.Add(map);

        info.ArgumentList.Add("--seed");
        info.ArgumentList.Add(request.Seed.ToString(CultureInfo.InvariantCulture));

        info.ArgumentList.Add("--max-actors");
        info.ArgumentList.Add(request.MaxActors.ToString(CultureInfo.InvariantCulture));

        info.ArgumentList.Add("--ticket-key");
        info.ArgumentList.Add(ticketKey);

        if (options.MatchIdleSeconds > 0)
        {
            info.ArgumentList.Add("--idle-seconds");
            info.ArgumentList.Add(options.MatchIdleSeconds.ToString(CultureInfo.InvariantCulture));
        }

        // Podawane tylko przy wyłączeniu: proces domyślnie dopełnia obsadę botami, więc
        // milczenie znaczy tu dokładnie to samo co „--bots 1".
        if (!options.FillWithBots)
        {
            info.ArgumentList.Add("--bots");
            info.ArgumentList.Add("0");
        }

        // Roster idzie stdinem, a nie argumentem: nicki graczy trafiłyby do listy procesów
        // całej maszyny (decyzja 6.2 planu serwera gry).
        info.ArgumentList.Add("--manifest");
        info.ArgumentList.Add("-");

        return Process.Start(info)
            ?? throw new InvalidOperationException(
                $"System nie uruchomił procesu z '{binary}' i nie podał powodu."
            );
    }

    private static async Task SendManifestAsync(
        Process process,
        string manifest,
        CancellationToken cancellationToken
    ){
        await process.StandardInput.WriteAsync(manifest.AsMemory(), cancellationToken);

        // Zamknięcie wejścia jest częścią kontraktu, nie sprzątaniem: proces czyta stdin
        // do końca strumienia i bez tego stanąłby na zawsze, zanim otworzy gniazdo.
        process.StandardInput.Close();
    }

    private async Task WaitUntilListeningAsync(
        Process process,
        int port,
        CancellationToken cancellationToken
    ){
        var timeout = TimeSpan.FromMilliseconds(Math.Max(0, options.ReadinessTimeoutMilliseconds));

        var waiting = Stopwatch.StartNew();

        while (true)
        {
            // Najpierw martwy proces, potem sonda. Odwrotna kolejność meldowałaby gotowość
            // procesu, który właśnie padł, gdyby port zdążył zająć ktoś inny.
            if (process.HasExited)
            {
                throw new InvalidOperationException(
                    $"Proces meczu zakończył się kodem {process.ExitCode}, zanim zaczął "
                        + "nasłuchiwać. Powód wypisał na swoje wyjście."
                );
            }

            if (await CanConnectAsync(port, cancellationToken))
            {
                return;
            }

            if (waiting.Elapsed >= timeout)
            {
                throw new TimeoutException(
                    $"Proces meczu nie zaczął nasłuchiwać na porcie {port} w ciągu "
                        + $"{options.ReadinessTimeoutMilliseconds} ms."
                );
            }

            await Task.Delay(ProbeIntervalMilliseconds, cancellationToken);
        }
    }

    /// <summary>
    /// Czy na porcie ktoś już przyjmuje połączenia.
    /// </summary>
    /// <remarks>
    /// Sonda otwiera i natychmiast zamyka połączenie TCP. Proces meczu przyjmuje je,
    /// czeka na żądanie HTTP, dostaje koniec strumienia i cicho kończy sesję — nie ma tu
    /// nic do posprzątania po żadnej ze stron.
    /// </remarks>
    private static async Task<bool> CanConnectAsync(int port, CancellationToken cancellationToken)
    {
        using var probe = new TcpClient();

        try
        {
            await probe.ConnectAsync(IPAddress.Loopback, port, cancellationToken);

            return true;
        }
        catch (SocketException)
        {
            // Nikt jeszcze nie nasłuchuje. To nie jest awaria, tylko odpowiedź „jeszcze nie".
            return false;
        }
    }

    /// <summary>
    /// Wybiera z puli port, na którym nikt nie nasłuchuje — czekając, aż taki się znajdzie.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>Pula, a nie jeden port.</b> Przy jednym porcie na maszynie stał jeden mecz naraz,
    /// a to nie jest okno przejściowe: trwa tyle, ile cała tamta rozgrywka. Gracz, który
    /// z meczu wyszedł i wrócił do kolejki, nie mógł zacząć następnego, dopóki grali w tamtym
    /// pozostali — każde lobby otwarte w tym czasie kończyło się komunikatem o awarii.
    /// </para>
    /// <para>
    /// Czekanie zostaje na wypadek zajętej <b>całej</b> puli i na okno bezczynności procesu,
    /// który właśnie gaśnie. Ograniczone <see cref="MatchOptions.PortWaitMilliseconds"/>, bo
    /// po drugiej stronie stoi zamrożony roster.
    /// </para>
    /// <para>
    /// Rezerwacja nie potrzebuje własnego zamka: alokacje idą po kolei, jedna korutyna
    /// launchera, a wracamy stąd dopiero wtedy, gdy proces poprzedniego meczu już nasłuchuje
    /// — więc następne wywołanie widzi jego port jako zajęty.
    /// </para>
    /// </remarks>
    private async Task<int> ReserveFreePortAsync(CancellationToken cancellationToken)
    {
        var timeout = TimeSpan.FromMilliseconds(Math.Max(0, options.PortWaitMilliseconds));

        var waiting = Stopwatch.StartNew();

        while (true)
        {
            if (FirstFreePort() is { } port)
            {
                return port;
            }

            if (waiting.Elapsed >= timeout)
            {
                throw new InvalidOperationException(
                    $"Wszystkie porty puli {options.GameServerPort}–"
                        + $"{options.GameServerPort + Math.Max(1, options.GameServerPortCount) - 1} "
                        + $"są zajęte od {options.PortWaitMilliseconds} ms — stoją na nich procesy "
                        + "trwających meczów. Zwiększ Match:GameServerPortCount albo poczekaj, aż "
                        + "któryś się skończy."
                );
            }

            await Task.Delay(ProbeIntervalMilliseconds, cancellationToken);
        }
    }

    /// <summary>Pierwszy port puli bez nasłuchu albo <c>null</c>, gdy zajęta jest cała.</summary>
    private int? FirstFreePort()
    {
        var listeners = IPGlobalProperties.GetIPGlobalProperties().GetActiveTcpListeners();

        // Jedno pytanie do systemu na cały przebieg, a nie jedno na port: lista i tak
        // przychodzi w całości, a pula ma kilka pozycji.
        var taken = listeners.Select(endpoint => endpoint.Port).ToHashSet();

        foreach (var port in options.GameServerPorts)
        {
            if (!taken.Contains(port))
            {
                return port;
            }
        }

        return null;
    }

    private string ResolveBinary()
    {
        if (string.IsNullOrWhiteSpace(options.GameServerPath))
        {
            throw new InvalidOperationException(
                "Match:GameServerPath jest pusty, a alokator ma uruchomić proces meczu."
            );
        }

        var full = Path.GetFullPath(options.GameServerPath);

        if (!File.Exists(full))
        {
            throw new InvalidOperationException(
                $"Nie ma binarki game-serwera pod '{full}'. Zbuduj cel 'gameserver' albo "
                    + "popraw Match:GameServerPath."
            );
        }

        return full;
    }

    private string ResolveMap(string mapId)
    {
        var full = Path.GetFullPath(Path.Combine(options.MapsRoot, $"{mapId}.tmap"));

        if (!File.Exists(full))
        {
            throw new InvalidOperationException(
                $"Nie ma pliku terenu '{full}'. Plik .tmap jest artefaktem konwersji i nie "
                    + $"leży w repozytorium — zrób go poleceniem: tmapgen --synthetic --out {full}."
            );
        }

        return full;
    }

    private string ResolveTicketKey()
    {
        if (string.IsNullOrWhiteSpace(options.TicketPublicKeyPath))
        {
            throw new InvalidOperationException(
                "Match:TicketPublicKeyPath jest pusty, więc nie ma czego podać procesowi "
                    + "przez --ticket-key. Bez klucza proces nie odróżni biletu od zmyślonego ciągu."
            );
        }

        var full = Path.GetFullPath(options.TicketPublicKeyPath);

        if (!File.Exists(full))
        {
            throw new InvalidOperationException(
                $"Nie ma klucza publicznego biletów pod '{full}'. Zapisuje go meta przy starcie — "
                    + "jeśli pliku nie ma, start się nie udał albo ścieżka wskazuje gdzie indziej."
            );
        }

        return full;
    }

    private void Terminate(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch (Exception exception)
        {
            // Proces mógł właśnie zniknąć sam. Jesteśmy na ścieżce obsługi awarii, więc
            // wyjątek stąd przesłoniłby przyczynę, dla której się tu trafiło.
            LogTerminationFailed(logger, exception);
        }
        finally
        {
            process.Dispose();
        }
    }

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Proces meczu {MatchId} stoi: pid {ProcessId}, nasłuch na 127.0.0.1:{Port}."
    )]
    private static partial void LogProcessStarted(
        ILogger logger,
        Guid matchId,
        int processId,
        int port
    );

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Nie udało się zatrzymać procesu meczu po nieudanej alokacji."
    )]
    private static partial void LogTerminationFailed(ILogger logger, Exception exception);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Proces meczu {MatchId} zgasł (kod {ExitCode}). Mecz zostaje zamknięty."
    )]
    private static partial void LogProcessExited(ILogger logger, Guid matchId, int? exitCode);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Nie udało się odczytać kodu wyjścia procesu meczu {MatchId}."
    )]
    private static partial void LogExitCodeUnavailable(
        ILogger logger,
        Guid matchId,
        Exception exception
    );
}
