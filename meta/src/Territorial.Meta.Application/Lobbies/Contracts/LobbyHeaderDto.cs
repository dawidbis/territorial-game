namespace Territorial.Meta.Application.Lobbies.Contracts;

/// <summary>
/// Wszystko, co strona główna musi wiedzieć o lobby — bez rostera.
/// </summary>
/// <remarks>
/// <para>
/// Nagłówek leci do <b>wszystkich</b> połączonych, roster tylko do tych, którzy dołączyli.
/// Przy stu graczach pełna lista to kilka kilobajtów; rozsyłanie jej każdemu, kto tylko
/// otworzył stronę główną, byłoby setkami kilobajtów na sekundę bez żadnego pożytku.
/// </para>
/// <para>
/// <c>State</c> i <c>Mode</c> są tekstem, a nie liczbą: kontrakt sieciowy pozostaje
/// czytelny, dopisanie wartości do enuma niczego nie przesuwa, a domena nie musi znać
/// atrybutów serializacji.
/// </para>
/// <para>
/// <c>StartsAt</c> to chwila startu; <c>null</c> oznacza zatrzymane odliczanie i wtedy —
/// i tylko wtedy — niepuste jest <c>FrozenSeconds</c> z wartością, na której licznik stoi.
/// Klient odejmuje chwilę startu lokalnie, dzięki czemu licznik nie wymaga wiadomości
/// co sekundę.
/// </para>
/// <para>
/// <c>ServerNow</c> to czas serwera w momencie zbudowania snapshotu. Klient wylicza z niego
/// przesunięcie własnego zegara, więc odliczanie jest odporne na źle ustawiony czas
/// w systemie gracza.
/// </para>
/// </remarks>
public sealed record LobbyHeaderDto(
    Guid LobbyId,
    string State,
    string MapId,
    string MapName,
    string Mode,
    int PlayerCount,
    int MaxPlayers,
    int BotCount,
    DateTimeOffset? StartsAt,
    int? FrozenSeconds,
    DateTimeOffset ServerNow
);
