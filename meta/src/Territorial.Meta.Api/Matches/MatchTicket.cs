namespace Territorial.Meta.Api.Matches;

/// <summary>Wystawiony bilet meczowy wraz z chwilą wygaśnięcia, żeby klient nie musiał go parsować.</summary>
public readonly record struct MatchTicket(string Value, DateTimeOffset ExpiresAt);
