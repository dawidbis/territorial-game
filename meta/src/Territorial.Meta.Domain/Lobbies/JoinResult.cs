namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Wynik próby dołączenia. Rozróżnienie <see cref="Joined"/> od <see cref="AlreadyJoined"/>
/// nie jest kosmetyką: tylko pierwszy przypadek zmienia roster, więc tylko on uzasadnia
/// rozesłanie snapshotu wszystkim. Reszta to odpowiedzi, które klient musi umieć pokazać.
/// </summary>
public enum JoinResult
{
    /// <summary>Gracz wszedł do rostera — stan lobby się zmienił.</summary>
    Joined = 0,

    /// <summary>Gracz już był w lobby. Odświeżono nick i kolor, kolejność bez zmian.</summary>
    AlreadyJoined = 1,

    /// <summary>Komplet aktorów. Trzeba poczekać na następne lobby.</summary>
    Full = 2,

    /// <summary>Lobby jest już w fazie startu — dołączenia są zamknięte.</summary>
    NotGathering = 3,

    /// <summary>Tożsamość z żądania nie wskazuje istniejącego gracza — sesja jest nieaktualna.</summary>
    /// <remarks>
    /// <para>
    /// Jedyna wartość, której <see cref="Lobby"/> nie produkuje: o tym, że gracza nie ma
    /// w bazie, wie dopiero warstwa wystawiająca hub. Mieszka tu razem z pozostałymi, bo to
    /// jeden kontrakt odpowiedzi dla klienta, a nie sygnatura jednej metody.
    /// </para>
    /// <para>
    /// Wcześniej ten przypadek wracał jako <see cref="NotGathering"/>, więc gracz z nieaktualną
    /// sesją czytał komunikat o fazie startu — stan naprawialny wyglądał jak cudzy problem
    /// z czasem. Osobna wartość pozwala klientowi odnowić tożsamość i wrócić bez klikania.
    /// </para>
    /// </remarks>
    UnknownPlayer = 4,
}
