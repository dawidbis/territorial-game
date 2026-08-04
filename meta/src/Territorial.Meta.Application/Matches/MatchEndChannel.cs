using System.Threading.Channels;

namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Wiadomość „proces tego meczu zgasł" w drodze od alokatora do warstwy, która ma bazę.
/// </summary>
/// <remarks>
/// <para>
/// Kanał, a nie wywołanie wprost, z tego samego powodu co przy starcie meczu: notyfikacja
/// przychodzi ze <b>zdarzenia procesu potomnego</b>, czyli z wątku puli, w dowolnej chwili
/// i bez żadnego kontekstu żądania. Zapis do bazy z takiego miejsca musiałby sam sobie
/// zrobić scope, sam łapać wyjątki i sam decydować, co robi z anulowaniem hosta — a to jest
/// dokładnie ta odpowiedzialność, którą ma warstwa czytająca ten kanał.
/// </para>
/// <para>
/// Alokator, który procesów nie stawia (produkcyjny agent), po prostu tu nie pisze. Kanał
/// nie zakłada, że mecz kończy się wyjściem procesu — zakłada tylko, że <b>jeśli</b> ktoś
/// to zauważy, ma gdzie o tym powiedzieć.
/// </para>
/// </remarks>
public sealed class MatchEndChannel
{
    // Wielu piszących: każdy proces meczu gaśnie na własnym wątku puli i nikt ich nie
    // synchronizuje. Jeden czytający, bo po drugiej stronie stoi jedna usługa w tle.
    private readonly Channel<MatchEndedNotice> channel = Channel.CreateUnbounded<MatchEndedNotice>(
        new UnboundedChannelOptions { SingleReader = true, SingleWriter = false }
    );

    /// <summary>Wpisuje notyfikację i natychmiast wraca. Wołane z uchwytu zdarzenia procesu.</summary>
    public void Enqueue(MatchEndedNotice notice) => channel.Writer.TryWrite(notice);

    public IAsyncEnumerable<MatchEndedNotice> ReadAllAsync(CancellationToken cancellationToken) =>
        channel.Reader.ReadAllAsync(cancellationToken);
}

/// <summary>
/// Koniec jednego meczu widziany od strony procesu.
/// </summary>
/// <param name="MatchId">Mecz, którego proces zgasł.</param>
/// <param name="ExitCode">Kod wyjścia procesu; <c>null</c>, gdy systemu nie dało się o niego zapytać.</param>
public sealed record MatchEndedNotice(Guid MatchId, int? ExitCode);
