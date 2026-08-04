#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Format pliku terenu `.tmap` (D13) — jedyne miejsce, które wie, jak te bajty leżą.
///
/// Nagłówek dzielą **konwerter i czytnik**: format z dwiema niezależnymi implementacjami
/// rozjeżdża się przy pierwszej zmianie i wychodzi to dopiero na produkcji. Dlatego ta
/// jednostka nie zależy od niczego poza standardową biblioteką — `tmapgen` linkuje ją bez
/// Boosta i bez protobufa.
namespace gs::tmap
{

/// Cztery typy terenu, bo tyle wynika z mechaniki: w v1 patrzy na teren jedna reguła —
/// koszt przejęcia kafelka — a trzy poziomy lądu to trzy progi kosztu. Wody nie dzielimy
/// na ocean i jeziora, bo to rozróżnienie zaczyna znaczyć dopiero przy transporcie wodnym.
enum class Terrain : std::uint8_t
{
    water = 0,
    lowlands = 1,
    highlands = 2,
    mountains = 3,
};

/// Zapisane w nagłówku, żeby plik z innej wersji reguł dał się odrzucić przy wczytaniu,
/// a nie zinterpretować jako teren o nieznanym kodzie.
inline constexpr std::uint8_t terrain_type_count = 4;

inline constexpr std::uint16_t format_version = 1;

/// Rezerwacje z D12: `0` to pustkowie, `255` to woda, `1..254` to sloty aktorów.
inline constexpr std::uint8_t wasteland_owner = 0;
inline constexpr std::uint8_t water_owner = 255;

/// Punkt startowy w kafelkach. Indeks na liście **jest** przypisaniem do slotu (§3.6 planu):
/// kto stoi na slocie 7, ten zaczyna na spawnie 7. Nie ma losowania, więc nie ma czego
/// odtwarzać w replayu (D10).
struct Spawn
{
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

/// Mapa gotowa do zapisania. Używana przez `tmapgen`; serwer czyta `MapView`.
struct Map
{
    /// Identyfikator z katalogu map, np. `moon`. Ten sam, który idzie w `MatchInit.map_id`
    /// i w adresie terenu po stronie klienta.
    std::string id;

    std::uint16_t width = 0;
    std::uint16_t height = 0;

    std::vector<Spawn> spawns;

    /// `width * height` bajtów, wiersz po wierszu. Wartości to kody `Terrain`.
    std::vector<std::uint8_t> terrain;
};

/// Odczytana mapa **bez kopiowania terenu**.
///
/// `id` i `terrain` wskazują w bufor przekazany do `decode` — bufor musi żyć dłużej niż ten
/// widok. Tak wygląda seam pod `mmap` z D13: gdy teren zacznie przychodzić z mapowania
/// pliku zamiast z wektora, zmieni się wyłącznie to, skąd pochodzi bufor.
struct MapView
{
    std::string_view id;

    std::uint16_t width = 0;
    std::uint16_t height = 0;

    /// Kopiowane, bo jest ich najwyżej kilkaset, a wskaźnik w bufor o dowolnym wyrównaniu
    /// byłby albo niezdefiniowanym zachowaniem, albo osobnym akcesorem na każde pole.
    std::vector<Spawn> spawns;

    std::span<const std::uint8_t> terrain;

    std::size_t tile_count() const noexcept
    {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    /// Indeks kafelka w układzie wiersz po wierszu — ten sam, którym adresuje `owner[]`.
    std::size_t index_of(Spawn spawn) const noexcept
    {
        return static_cast<std::size_t>(spawn.y) * static_cast<std::size_t>(width) + spawn.x;
    }
};

/// Układ pliku, little-endian:
///
/// ```
///  0   4  "TMAP"
///  4   2  wersja formatu
///  6   2  szerokość
///  8   2  wysokość
/// 10   1  liczba typów terenu
/// 11   1  długość identyfikatora
/// 12   2  liczba punktów startowych
/// 14   2  rezerwa
/// 16   4  offset terenu
/// 20   …  identyfikator (ASCII)
///      …  punkty startowe, po 2 × u16
/// offset terenu: width × height bajtów
/// ```
///
/// **Dwa odstępstwa od nagłówka opisanego w D13** i oba mają powód. Po pierwsze, punkty
/// startowe i identyfikator jadą w tym samym pliku co teren: to one przechodzą walidację
/// konwertera, więc rozdzielone na drugi plik prędzej czy później rozjadą się z siatką.
/// Po drugie, offset terenu stoi wprost w nagłówku zamiast być liczony z pozostałych pól —
/// dzięki temu klient (`new Uint8Array(buf, offset)`) nigdy nie musi wiedzieć o sekcjach,
/// których nie czyta.
inline constexpr std::size_t header_size = 20;

/// Największa mapa, jaką format opisuje. Wymiary są u16, więc to sufit z definicji;
/// nazwany, bo konwerter musi go sprawdzić, zanim obetnie wartość po cichu.
inline constexpr std::uint32_t max_dimension = 65535;

/// Serializuje mapę. Nie waliduje sensowności terenu — od tego jest `tmapgen`; sprawdza
/// wyłącznie to, czego format fizycznie nie uniesie.
std::expected<std::string, std::string> encode(const Map& map);

/// Czyta plik z pamięci.
///
/// Każde sprawdzenie zamyka inną drogę do awarii w dwunastej minucie meczu: zły rozmiar
/// dałby odczyt poza tablicą, nieznany kod terenu — cichy błąd w tabeli kosztów, a spawn
/// na wodzie gracza, którego nikt nie może zaatakować.
std::expected<MapView, std::string> decode(std::span<const std::uint8_t> bytes);

} // namespace gs::tmap
