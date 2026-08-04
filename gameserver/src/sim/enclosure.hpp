#pragma once

#include <cstdint>
#include <vector>

namespace gs
{

class World;

/// Kocioł: kafelki odcięte od reszty państwa, gotowe do wchłonięcia.
struct Enclosure
{
    /// Kafelki do przepisania. Zawsze należą do jednego gracza.
    std::vector<std::uint32_t> tiles;

    /// Kto je przejmuje — właściciel większości pól na obwodzie kotła.
    std::uint8_t annexer = 0;

};

/// Czy przejęcie tego kafelka **mogło** rozciąć terytorium obrońcy.
///
/// Warunek konieczny, nie wystarczający: przejęty kafelek musi mieć co najmniej dwóch sąsiadów
/// obrońcy, których nie łączy droga wewnątrz otoczenia 3×3. Osiem porównań, a odsiewa niemal
/// wszystkie przejęcia — front jest zwykle łukiem, nie kleszczami, więc pełne przeszukiwanie
/// uruchamia się kilka razy na mecz zamiast setki razy na tik.
bool may_split(const World& world, std::uint32_t captured, std::uint8_t defender) noexcept;

/// Szuka kotła wokół świeżo przejętego kafelka.
///
/// **Reguły są trzy i wszystkie są regułami gry, nie optymalizacjami:**
///
/// 1. Kocioł to kafelki gracza bez lądowego połączenia z resztą jego terytorium.
/// 2. **Jakikolwiek sąsiad wodny odbiera możliwość aneksji** — morze, jezioro i rzeka znaczą
///    tu to samo. Odcięty fragment nad wodą zdobywa się polem po polu, jak każdy inny.
/// 3. Pustkowie nie jest wchłaniane nigdy; okrążenie dotyczy wyłącznie państw.
///
/// Koszt to rozmiar **mniejszego** z rozciętych fragmentów: przeszukiwanie idzie z każdego
/// sąsiada obrońcy naraz, krok w krok, i kończy się w chwili, gdy pierwsze z nich się wyczerpie.
/// Kocioł jest z definicji tym mniejszym, więc typowy rachunek to kilkanaście pól.
///
/// @returns wszystkie odcięte fragmenty, jakie powstały przy tym przejęciu — przesmyk potrafi
/// rozpaść się na więcej niż dwa kawałki, a wszystkie oprócz największego są kotłami. Pusto,
/// gdy nic nie zostało odcięte albo gdy odcięte fragmenty mają wyjście.
std::vector<Enclosure> find_enclosures(
    const World& world,
    std::uint32_t captured,
    std::uint8_t defender);

/// Sufit rozmiaru kotła przy rozcięciu terytorium.
///
/// Zabezpieczenie przed przypadkiem patologicznym, nie regułą gry: przy okrążeniu połowy mapy
/// przeszukiwanie kosztowałoby tyle, co przejście po niej całej. Może być hojny, bo rozcięcie
/// płaci rozmiarem **mniejszego** kawałka — marsz krok w krok zatrzymuje się, gdy w ruchu
/// zostaje sam trzon terytorium.
inline constexpr std::size_t max_enclosure_tiles = 65'536;

/// Do jakiej wielkości państwa pytamy, czy jest okrążone w całości.
///
/// Ten przypadek jest **dużo droższy** i dlatego ma osobną, dużo ciaśniejszą granicę: nic się
/// tam nie rozcina, więc nie ma mniejszego kawałka, którym można zapłacić — jest wyłącznie
/// przejście po całym terytorium obrońcy. Bez tej granicy gracz z sześćdziesięcioma tysiącami
/// pól dostawałby taki przemarsz **co tik**, w którym cokolwiek stracił, a to jest zwyczajna
/// wielkość w połowie meczu.
///
/// Tysiąc pól to dwadzieścia terytoriów startowych. Kto jest większy, nie zostaje okrążony
/// przez zaskoczenie — najpierw musi zostać zjedzony do tego rozmiaru, a wtedy reguła wraca.
inline constexpr std::uint32_t max_surrounded_player_tiles = 1'024;

} // namespace gs
