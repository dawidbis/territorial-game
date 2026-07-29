using Territorial.Meta.Application.Players.Contracts;

namespace Territorial.Meta.Api.Contracts;

/// <summary>
/// Odpowiedź na „kim jestem" — profil razem ze świeżym tokenem.
/// </summary>
/// <remarks>
/// Token wraca przy każdym wywołaniu, więc samo wejście na stronę odnawia sesję.
/// Dzięki temu klient nie potrzebuje refresh-tokenu ani osobnego endpointu na odświeżanie.
/// </remarks>
public sealed record PlayerSessionResponse(
    PlayerProfileDto Player,
    string AccessToken,
    DateTimeOffset ExpiresAt
);
