#include "sim/enclosure.hpp"

#include "map/map_file.hpp"
#include "map/tmap.hpp"
#include "map_fixtures.hpp"
#include "sim/simulation.hpp"
#include "sim/world.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Aneksja przez okrążenie. Wszystkie plansze są rysowane w teście znak po znaku, bo cała
// ta reguła jest o kształcie terytorium — a kształt zapisany rysunkiem czyta się od razu,
// zapisany listą indeksów nie czyta się wcale.
namespace
{

gs::MapFile load(const gs::tmap::Map& map)
{
    const std::expected<std::string, std::string> bytes = gs::tmap::encode(map);

    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error());

    std::expected<gs::MapFile, std::string> file =
        gs::MapFile::from_bytes(std::vector<std::uint8_t>(bytes->begin(), bytes->end()));

    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error());

    return std::move(*file);
}

/// Woda z rysunku idzie do terenu, bo tylko stamtąd `World` ją zna.
gs::tmap::Map terrain_of(const std::vector<std::string_view>& rows)
{
    const std::uint16_t width = static_cast<std::uint16_t>(rows.front().size());
    const std::uint16_t height = static_cast<std::uint16_t>(rows.size());

    gs::tmap::Map map = fixtures::plains_map(
        width,
        height,
        gs::tmap::Terrain::lowlands,
        {gs::tmap::Spawn{0, 0}, gs::tmap::Spawn{static_cast<std::uint16_t>(width - 1), 0}});

    for (std::uint16_t y = 0; y < height; ++y)
    {
        for (std::uint16_t x = 0; x < width; ++x)
        {
            if (rows[y][x] == '~')
            {
                map.terrain[static_cast<std::size_t>(y) * width + x] =
                    static_cast<std::uint8_t>(gs::tmap::Terrain::water);
            }
        }
    }

    return map;
}

/// Plansza z rysunku: `.` pustkowie, `~` woda, cyfra — slot właściciela.
struct Board
{
    explicit Board(const std::vector<std::string_view>& rows)
        : file(load(terrain_of(rows)))
        , world(file.map())
    {
        const std::uint16_t width = static_cast<std::uint16_t>(rows.front().size());

        for (std::uint16_t y = 0; y < rows.size(); ++y)
        {
            for (std::uint16_t x = 0; x < width; ++x)
            {
                const char cell = rows[y][x];

                if (cell == '.' || cell == '~')
                {
                    continue;
                }

                world.set_owner(
                    static_cast<std::uint32_t>(y) * width + x,
                    static_cast<std::uint8_t>(cell - '0'));
            }
        }

        world.clear_changed();
    }

    std::uint32_t at(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return y * world.width() + x;
    }

    gs::MapFile file;
    gs::World world;
};

} // namespace

// Kocioł w najczystszej postaci: jedno pole obrońcy otoczone z czterech stron.
TEST(EnclosureTest, AbsorbsAPocketCutOffFromTheRestOfTheTerritory)
{
    Board board({
        "22222",
        "21122",
        "22222",
    });

    const std::uint32_t captured = board.at(2, 1);

    // Warunek lokalny **nie** wystarcza: przejęty kafelek ma jednego sąsiada obrońcy, więc
    // niczego nie rozcina — usunięcie liścia zostawia resztę spójną. Fragment i tak zostaje
    // otoczony, a łapie to dopiero druga droga, bramkowana rozmiarem terytorium.
    EXPECT_FALSE(gs::may_split(board.world, captured, 1));

    board.world.set_owner(captured, 2);

    const std::vector<gs::Enclosure> found = gs::find_enclosures(board.world, captured, 1);

    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found.front().annexer, 2);
    ASSERT_EQ(found.front().tiles.size(), 1u);
    EXPECT_EQ(found.front().tiles.front(), board.at(1, 1));
}

// Jakikolwiek sąsiad wodny odbiera możliwość aneksji. Ten sam kocioł co wyżej, tyle że
// z jednym kafelkiem wody obok — zdobywa się go polem po polu, jak każdy inny.
TEST(EnclosureTest, WaterNextToThePocketBlocksAnnexation)
{
    Board board({
        "2~222",
        "21122",
        "22222",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    EXPECT_TRUE(gs::find_enclosures(board.world, captured, 1).empty());
}

// „Okrążony z każdej strony" znaczy **z każdej**: fragment stykający się z pustkowiem ma
// dokąd rosnąć, więc nie jest w kotle.
TEST(EnclosureTest, AnOutletToWastelandBlocksAnnexation)
{
    Board board({
        "2.222",
        "21122",
        "22222",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    EXPECT_TRUE(gs::find_enclosures(board.world, captured, 1).empty());
}

// Pustkowia nie wchłania się okrążeniem. Zamknięcie pierścienia wokół pustego pola dawałoby
// je za darmo i przestawiało tempo pierwszych minut meczu.
TEST(EnclosureTest, WastelandIsNeverAnnexed)
{
    Board board({
        "22222",
        "2.222",
        "22222",
    });

    // Pustkowie jest tu otoczone tak samo jak państwo w pierwszym teście — i to jest cały
    // sens tego przypadku.
    EXPECT_TRUE(gs::find_enclosures(board.world, board.at(2, 1), 0).empty());
}

// Fragment odcięty od reszty państwa, ale samo państwo żyje dalej gdzie indziej.
TEST(EnclosureTest, AbsorbsOnlyTheCutOffFragment)
{
    Board board({
        "2222222",
        "2112222",
        "2222211",
        "2222211",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    const std::vector<gs::Enclosure> found = gs::find_enclosures(board.world, captured, 1);

    ASSERT_EQ(found.size(), 1u);
    ASSERT_EQ(found.front().tiles.size(), 1u);
    EXPECT_EQ(found.front().tiles.front(), board.at(1, 1));
}

// Przewężenie, które **nie** rozcina: fale wychodzące z sąsiadów przejętego kafelka spotykają
// się naokoło. To jest test na spotkanie fal — pierwsza wersja algorytmu miała tu wspólny
// zbiór odwiedzonych i fala „wyczerpywała się", bo jej front zajęła sąsiadka, czyli spójne
// terytorium wyglądało na rozcięte.
TEST(EnclosureTest, LeavesTerritoryAloneWhenItStaysConnected)
{
    Board board({
        "2222222",
        "2111.22",
        "2111122",
        "2222222",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    EXPECT_TRUE(gs::find_enclosures(board.world, captured, 1).empty());
}

// Przesmyk potrafi rozpaść się na więcej niż dwa kawałki i wszystkie oprócz największego są
// kotłami. Wersja biorąca pierwszy znaleziony zostawiała resztę na kolejne tiki.
TEST(EnclosureTest, TakesEveryFragmentTheCaptureCutOff)
{
    Board board({
        "22122",
        "21112",
        "22222",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    const std::vector<gs::Enclosure> found = gs::find_enclosures(board.world, captured, 1);

    // Trzy pola slotu 1 dookoła przejętego kafelka, każde odcięte osobno. Górne dotyka
    // krawędzi mapy — a krawędź wyjściem nie jest, więc też idzie do kotła.
    ASSERT_EQ(found.size(), 3u);

    for (const gs::Enclosure& enclosure : found)
    {
        EXPECT_EQ(enclosure.annexer, 2);
        EXPECT_EQ(enclosure.tiles.size(), 1u);
    }
}

// Kocioł na styku dwóch wrogów bierze ten, kto pilnuje większości obwodu. Remis rozstrzyga
// niższy slot — porządek totalny jest tu warunkiem replayu, tak samo jak w kolejce podboju.
TEST(EnclosureTest, TheMajorityOfTheRingTakesThePocket)
{
    Board board({
        "33333",
        "31222",
        "22222",
    });

    const std::uint32_t captured = board.at(2, 1);

    board.world.set_owner(captured, 2);

    const std::vector<gs::Enclosure> found = gs::find_enclosures(board.world, captured, 1);

    ASSERT_EQ(found.size(), 1u);

    // Obwód kafelka (1,1): slot 3 z góry i z lewej, slot 2 z prawej i z dołu — remis,
    // więc bierze niższy slot.
    EXPECT_EQ(found.front().annexer, 2);
}

// Warunek lokalny decyduje, czy pełny rachunek ruszy przy przejęciu na froncie wielkiego
// państwa — a tam liczy się każdy zaoszczędzony przebieg.
TEST(EnclosureTest, TheLocalFilterAsksWhetherTheCaptureCouldSplitAnything)
{
    Board board({
        "2222",
        "2112",
        "2222",
    });

    // Jeden sąsiad obrońcy: usunięcie liścia nie rozspójnia niczego.
    EXPECT_FALSE(gs::may_split(board.world, board.at(2, 1), 1));
    EXPECT_FALSE(gs::may_split(board.world, board.at(1, 1), 1));

    Board isthmus({
        "2222",
        "1112",
        "2222",
    });

    // Dwaj sąsiedzi po przeciwnych stronach przesmyku: przejęcie może rozciąć terytorium.
    EXPECT_TRUE(gs::may_split(isthmus.world, isthmus.at(1, 1), 1));
}

// Cała droga, nie sam rachunek: rozkaz gracza, tik symulacji i wchłonięcie tego, co zostało
// odcięte. Slot 1 ma trzy pola zamknięte ze wszystkich stron przez slot 2, więc w tiku,
// w którym straci pierwsze z nich, ma stracić **wszystkie** — reszta jest kotłem.
TEST(EnclosureTest, TheSimulationAbsorbsThePocketInTheSameTickTheRingCloses)
{
    Board board({
        "2222222",
        "2211122",
        "2222222",
    });

    const std::vector<gs::Actor> actors{
        gs::Actor{1, "Ala", 0xFF0000, false},
        gs::Actor{2, "Bo", 0x00FF00, false}};

    gs::Simulation simulation(board.world, actors, 4242);

    ASSERT_EQ(simulation.order_attack(2, 1, 100), gs::OrderResult::accepted);

    ASSERT_EQ(board.world.tiles_of(1), 3u);

    std::vector<std::uint32_t> history;

    for (std::uint32_t tick = 1; tick <= 30 && board.world.tiles_of(1) > 0; ++tick)
    {
        simulation.tick(tick);

        history.push_back(board.world.tiles_of(1));
    }

    ASSERT_EQ(board.world.tiles_of(1), 0u);

    // Bez aneksji ciąg wyglądałby 3 → 2 → 1 → 0, bo każde pole trzeba by zdobyć osobno.
    // Sprawdzamy więc, że **stanu pośredniego nie było**: albo pełne trzy pola, albo zero.
    // Asercja nie zakłada, w którym tiku natarcie zdąży ruszyć — to jest sprawa tempa ataku,
    // a nie aneksji.
    for (const std::uint32_t remaining : history)
    {
        EXPECT_TRUE(remaining == 3u || remaining == 0u)
            << "terytorium miało zniknąć w całości, a nie polami; zostało " << remaining;
    }

    EXPECT_FALSE(simulation.player(1).alive);
}
