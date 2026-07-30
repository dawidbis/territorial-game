using System.Globalization;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Infrastructure.Matches;

/// <summary>
/// Alokator, który niczego nie alokuje — oddaje adres wpisany w konfiguracji.
/// </summary>
/// <remarks>
/// <para>
/// Pozwala przejść całą ścieżkę „lobby dobija do terminu → mecz w bazie → bilety →
/// nowe lobby" bez jednej linii C++. Pod zwróconym adresem nikt nie nasłuchuje i to jest
/// świadome: etap 1 kończy się w chwili, gdy klient ma w ręku adres i bilet.
/// </para>
/// <para>
/// Adres publiczny buduje się tutaj, a nie w warstwie wyżej, bo to jedyne miejsce, które
/// wie, jak proces jest wystawiony. Wersje z prawdziwym procesem
/// (<c>LocalProcessMatchAllocator</c>, <c>AgentMatchAllocator</c>) podmieniają wyłącznie
/// tę klasę i jedną rejestrację w kontenerze.
/// </para>
/// </remarks>
internal sealed class FakeMatchAllocator(MatchOptions options) : IMatchAllocator
{
    public Task<MatchAllocation> AllocateAsync(
        MatchAllocationRequest request,
        CancellationToken cancellationToken
    ){
        var (host, port) = ParseEndpoint(options.FakeAllocatorEndpoint);

        var wsUrl = $"{options.MatchWebSocketBaseUrl.TrimEnd('/')}/{request.MatchId}";

        return Task.FromResult(new MatchAllocation(host, port, wsUrl));
    }

    private static (string Host, int Port) ParseEndpoint(string endpoint)
    {
        // Rozdzielenie po OSTATNIM dwukropku, żeby adres IPv6 nie rozpadł się na kawałki.
        var separator = endpoint.LastIndexOf(':');

        if (
            separator > 0
            && int.TryParse(
                endpoint.AsSpan(separator + 1),
                CultureInfo.InvariantCulture,
                out var port
            )
        )
        {
            return (endpoint[..separator], port);
        }

        throw new InvalidOperationException(
            $"Match:FakeAllocatorEndpoint ma wartość '{endpoint}', a oczekiwana jest postać host:port."
        );
    }
}
