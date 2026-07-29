namespace Territorial.Meta.Api.Auth;

/// <summary>Wystawiony token wraz z chwilą wygaśnięcia, żeby klient nie musiał go parsować.</summary>
public readonly record struct PlayerToken(string Value, DateTimeOffset ExpiresAt);
