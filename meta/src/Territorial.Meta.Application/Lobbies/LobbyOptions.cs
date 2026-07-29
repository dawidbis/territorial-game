namespace Territorial.Meta.Application.Lobbies;

public sealed class LobbyOptions
{
    public const string SectionName = "Lobby";

    /// <summary>Długość okna zbierania graczy.</summary>
    public int GatheringSeconds { get; init; } = 60;

    /// <summary>
    /// Czy licznik faktycznie odlicza.
    /// </summary>
    /// <remarks>
    /// Domyślnie wyłączony, bo nie ma jeszcze game-serwera, któremu można oddać mecz —
    /// licznik stoi na pełnym oknie i nigdy nie dobija do zera. Pełna cykliczna logika
    /// jest zaimplementowana i przetestowana, więc uruchomienie jej to zmiana tej jednej
    /// wartości w konfiguracji, bez dotykania kodu.
    /// </remarks>
    public bool CountdownEnabled { get; init; }

    /// <summary>
    /// Ile sekund gracz zostaje w rosterze po utracie ostatniego połączenia.
    /// </summary>
    /// <remarks>
    /// Bez karencji odświeżenie strony (F5) wypisuje gracza z lobby i wpisuje z powrotem
    /// ułamek sekundy później, co u wszystkich pozostałych objawia się mignięciem listy.
    /// Przerwa w sieci wygląda tak samo. Kilka sekund pokrywa oba przypadki, a gracza,
    /// który naprawdę zamknął kartę, opóźnia nieistotnie.
    /// </remarks>
    public int DisconnectGraceSeconds { get; init; } = 5;
}
