using Microsoft.Extensions.Logging.Abstractions;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Matches;

namespace Territorial.Meta.Api.IntegrationTests.Matches;

/// <summary>
/// Wystawianie biletów dla testów, które biletów nie badają.
/// </summary>
/// <remarks>
/// Klucz jest prawdziwy, ale generowany na czas testu i nigdzie niezapisywany: launcher
/// i kontroler mają sprawdzać, <b>komu</b> bilet trafia, a nie czym jest podpisany.
/// Kształtu samego biletu pilnuje <see cref="MatchTicketServiceTests"/>.
/// </remarks>
internal static class TestTickets
{
    public static MatchTicketService For(MatchOptions options)
    {
        var withoutSideEffects = new MatchOptions { TicketPublicKeyPath = string.Empty };

        var key = new MatchTicketKey(withoutSideEffects, NullLogger<MatchTicketKey>.Instance);

        return new MatchTicketService(options, key, TimeProvider.System);
    }
}
