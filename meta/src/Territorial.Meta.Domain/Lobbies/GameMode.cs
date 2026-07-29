namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Tryb rozgrywki. Wartość zerowa jest trybem domyślnym, a nie sztucznym "None" —
/// lobby bez trybu nie istnieje, więc <c>default</c> ma tu sensowne znaczenie.
/// </summary>
public enum GameMode
{
    /// <summary>Każdy na każdego. Jedyny tryb w v1.</summary>
    Ffa = 0,
}
