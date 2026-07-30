using System.Threading.Channels;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.Matches;

/// <summary>
/// Przekazanie zamrożonego rostera z zegara do launchera.
/// </summary>
/// <remarks>
/// <para>
/// Istnieje po to, żeby zegar mógł oddać robotę i wrócić do tykania. Alokacja to I/O
/// z ponowieniami — wykonana w tiku zatrzymywałaby rozgłaszanie stanu <b>dla całego
/// serwisu</b> na czas rozmowy z orkiestratorem.
/// </para>
/// <para>
/// Kanał nieograniczony, bo pisze do niego wyłącznie zegar i najwyżej raz na cykl lobby.
/// Kanał z limitem musiałby odpowiedzieć na pytanie „co zrobić z odrzuconym startem",
/// a przy jednym producencie i jednym starcie na minutę to pytanie nie ma prawa paść.
/// </para>
/// </remarks>
public sealed class MatchStartChannel
{
    private readonly Channel<MatchStartRequest> channel =
        Channel.CreateUnbounded<MatchStartRequest>(
            new UnboundedChannelOptions { SingleReader = true, SingleWriter = true }
        );

    /// <summary>Wpisuje żądanie i natychmiast wraca. Wołane z tiku zegara.</summary>
    public void Enqueue(MatchStartRequest request) => channel.Writer.TryWrite(request);

    public IAsyncEnumerable<MatchStartRequest> ReadAllAsync(CancellationToken cancellationToken) =>
        channel.Reader.ReadAllAsync(cancellationToken);
}
