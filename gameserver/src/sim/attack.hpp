#pragma once

#include "map/tmap.hpp"
#include "sim/rng.hpp"

#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

namespace gs
{

class World;

/// Kafelek czekający w kolejce podboju.
struct ConquerTile
{
    std::uint32_t tile = 0;

    double priority = 0.0;
};

/// Porządek kolejki: najpierw najniższy priorytet, przy remisie niższy indeks kafelka.
///
/// **Rozstrzygnięcie remisu nie jest kosmetyką.** Kolejność elementów równych względem
/// komparatora nie jest w standardzie określona, więc dwie implementacje kopca zdejmowałyby
/// je inaczej i ten sam mecz rozszedłby się przy pierwszym remisie — a priorytety są tu
/// sumą kilku wartości z małego zbioru, więc remisy są regułą, nie wyjątkiem. Przy porządku
/// totalnym ciąg zdejmowanych elementów jest wyznaczony jednoznacznie i replay (D10) ma się
/// o co oprzeć.
struct ByPriority
{
    bool operator()(const ConquerTile& left, const ConquerTile& right) const noexcept
    {
        if (left.priority != right.priority)
        {
            return left.priority > right.priority;
        }

        return left.tile > right.tile;
    }
};

using ConquerQueue = std::priority_queue<ConquerTile, std::vector<ConquerTile>, ByPriority>;

/// Jedno trwające natarcie.
///
/// Ludzie wysłani do ataku **wychodzą z puli gracza** i żyją tutaj: nie przyrastają, nie
/// bronią własnego terytorium i wracają wyłącznie przez wycofanie. Bez tego atak byłby
/// darmowy, a jedyną sensowną strategią byłoby atakowanie wszystkim, co się ma.
///
/// Przeniesienie może rzucić: front to zbiór mieszający i kopiec, a żadne z nich nie ma
/// przenoszenia bez wyjątku. Natarcia są przenoszone wyłącznie przy powiększaniu wektora
/// `attacks_` — czyli w miejscu, w którym brak pamięci i tak kończy mecz.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Attack
{
    std::uint8_t attacker = 0;

    /// Slot obrońcy albo `tmap::wasteland_owner` dla pustkowia.
    std::uint8_t target = 0;

    double troops = 0.0;

    /// Ustawione przez rozkaz wycofania. Natarcie przestaje zdobywać **natychmiast**, ale
    /// ludzie wracają dopiero po odliczeniu poniżej.
    bool retreating = false;

    /// Ile tików zostało do powrotu ludzi do puli; czytane wyłącznie przy `retreating`.
    std::uint32_t retreat_countdown = 0;

    /// Natarcie skończone i czeka na wykreślenie z listy.
    ///
    /// Flaga zamiast usunięcia w miejscu, bo koniec jednego natarcia potrafi zakończyć inne
    /// — wykreślenie gracza gasi wszystkie jego ofensywy — a to znaczy modyfikowanie listy
    /// w trakcie przechodzenia po niej.
    bool done = false;

    /// Kafelki wroga stykające się z natarciem. Rozmiar tego zbioru **jest** szerokością
    /// frontu, od której zależy, ile kafelków atak zdąży w jednym tiku.
    std::unordered_set<std::uint32_t> border;

    ConquerQueue frontier;
};

/// Modyfikatory terenu w walce.
struct TerrainCost
{
    /// Baza strat atakującego — im wyżej, tym drożej się zdobywa.
    double defense = 0.0;

    /// Baza kosztu prędkości; wyższa znaczy mniej kafelków w tiku.
    double speed = 0.0;
};

TerrainCost terrain_cost(tmap::Terrain terrain) noexcept;

/// Czy kafelek styka się bokiem z terytorium tego slotu.
///
/// Bez zawijania na krawędziach — mapa jest prostokątem, nie torusem (patrz `World::neighbors4`).
bool touches(const World& world, std::uint32_t tile, std::uint8_t slot) noexcept;

/// Buduje front natarcia z kafelków celu stykających się z atakującym.
///
/// Przejście po całej mapie, bo granice graczy nie są nigdzie trzymane. Przy rozkazie co
/// kilkanaście sekund na gracza to kilka milisekund raz na natarcie — gdy zacznie przeszkadzać,
/// właściwą odpowiedzią jest zbiór kafelków granicznych per slot, a nie przyspieszanie tego
/// przejścia.
void seed_front(const World& world, Attack& attack, Pcg32& rng, std::uint32_t tick);

/// Dokłada do frontu sąsiadów właśnie zdobywanego kafelka.
///
/// Wołane **przed** przepisaniem właściciela, jak w pierwowzorze: w chwili liczenia priorytetu
/// zdobywany kafelek należy jeszcze do celu, więc nie liczy się jako własny sąsiad. Zamiana
/// kolejności nie zmienia tempa, tylko kształt frontu — ale zmienia go w każdym podboju, więc
/// mecz rozjeżdża się z pierwowzorem od pierwszego kafelka.
///
/// **Kopiec nie ma dedupu i mieć nie może**: kafelek wraca do niego po każdym zdobytym sąsiedzie,
/// za każdym razem ze świeżo policzonym priorytetem, a ten spada o pół za każde własne pole
/// dookoła. Zamrożenie priorytetu na chwili pierwszego odkrycia zostawia domkniętą kieszeń na
/// końcu kolejki: front rozłazi się na strzępy, a że budżet na tik jest proporcjonalny do jego
/// szerokości, postrzępione natarcie samo się rozpędza. Nieaktualne wpisy zdejmuje pętla podboju
/// za darmo — nie kosztują budżetu.
void extend_front(
    const World& world,
    Attack& attack,
    Pcg32& rng,
    std::uint32_t tile,
    std::uint32_t tick);

/// Priorytet kafelka w kolejce podboju; niższy idzie pierwszy.
///
/// Trzy składniki i każdy ma powód. **Losowość** [0,6] rozmywa front, żeby nie przesuwał
/// się idealnie prostą linią. **Liczba własnych sąsiadów** obniża priorytet o połowę za
/// każdego — kafelek otoczony z trzech stron zdobywa się przed tym stykającym się jednym
/// bokiem, więc natarcie domyka kieszenie zamiast zostawiać dziury. **Teren** działa
/// odwrotnie: góry (2,0) odkładają się na później niż równiny (1,0). Bieżący tik na końcu
/// sprawia, że kafelki dołożone później nie wyprzedzają tych czekających od dawna.
double conquer_priority(
    const World& world,
    std::uint32_t tile,
    std::uint8_t attacker,
    std::uint32_t roll,
    std::uint32_t tick) noexcept;

/// Strony starcia w postaci, jakiej potrzebuje arytmetyka walki.
struct CombatSides
{
    double attacker_troops = 0.0;

    std::uint32_t attacker_tiles = 0;

    bool attacker_is_bot = false;

    double defender_troops = 0.0;

    std::uint32_t defender_tiles = 0;

    bool defender_is_bot = false;

    /// `false` znaczy pustkowie — nie ma kto ginąć po drugiej stronie.
    bool defender_is_player = false;
};

/// Rozliczenie przejęcia jednego kafelka.
struct AttackStep
{
    double attacker_loss = 0.0;

    double defender_loss = 0.0;

    /// Ile z budżetu kafelków na tik pochłonęło to przejęcie.
    double tiles_used = 0.0;
};

/// Straty obu stron i koszt prędkości przy przejęciu jednego kafelka.
///
/// Straty obrońcy to jego ludzie rozłożeni równo na jego kafelki — traci tyle, ile „stało"
/// na utraconym polu, niezależnie od tego, kto go zdobył. Straty atakującego są wypadkową
/// dwóch wzorów: jednego od stosunku sił, drugiego od tego, ile obrońca miał na kafelku.
/// Sama pierwsza część robiła z ogromnych imperiów maszynę, której nic nie zatrzyma;
/// dlatego powyżej stu tysięcy kafelków dochodzą hamulce skali po obu stronach.
AttackStep attack_step(const CombatSides& sides, tmap::Terrain terrain) noexcept;

/// Budżet kafelków na jeden tik.
///
/// Zależy od stosunku sił **i od szerokości frontu**: ta sama armia rozlewa się szybciej po
/// długiej granicy niż przez przesmyk. Sufit `0,5` na kafelek granicy sprawia, że nawet
/// dziesięciokrotna przewaga nie zdobywa państwa w jednym tiku.
double attack_tiles_per_tick(
    double attacker_troops,
    double defender_troops,
    bool defender_is_player,
    std::size_t border_tiles) noexcept;

} // namespace gs
