using Territorial.Meta.Application.Lobbies.Contracts;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Domain.Lobbies;

namespace Territorial.Meta.Application.Lobbies;

/// <summary>
/// Co zegar ma zrobić po spojrzeniu na lobby.
/// </summary>
/// <remarks>
/// <para>
/// <c>Snapshot</c> jest niepusty dokładnie wtedy, gdy stan zmienił się od poprzedniego
/// tiku i jest co rozgłosić. <c>Start</c> — dokładnie wtedy, gdy <c>Tick</c> to
/// <see cref="LobbyTick.Started"/>; niesie roster zamrożony w tej samej chwili, w której
/// lobby przestało przyjmować graczy.
/// </para>
/// <para>
/// Rekord zamiast krotki, bo pól są już trzy i dwa z nich są opcjonalne — nazwy przy
/// wywołaniu są tu warte więcej niż zwięzłość.
/// </para>
/// </remarks>
public sealed record LobbyTickResult(
    LobbyTick Tick,
    LobbySnapshot? Snapshot,
    MatchStartRequest? Start
);
