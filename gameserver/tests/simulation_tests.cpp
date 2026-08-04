#include "sim/simulation.hpp"

#include "map/map_file.hpp"
#include "map/tmap.hpp"
#include "map_fixtures.hpp"
#include "sim/economy.hpp"
#include "sim/world.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <expected>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

// Pełny przebieg symulacji na małej planszy: ekonomia, podbój i przypadki brzegowe
// zarządzania natarciami. Mapa jest samym lądem, żeby test mówił o regułach ataku,
// a nie o kształcie brzegu.
namespace
{

constexpr std::uint8_t first = 1;
constexpr std::uint8_t second = 2;

const std::vector<gs::Actor> two_humans{
    gs::Actor{first, "Ala", 0xFF0000, false},
    gs::Actor{second, "Bo", 0x00FF00, false}};

gs::MapFile load(const gs::tmap::Map& map)
{
    const std::expected<std::string, std::string> bytes = gs::tmap::encode(map);

    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error());

    std::expected<gs::MapFile, std::string> file =
        gs::MapFile::from_bytes(std::vector<std::uint8_t>(bytes->begin(), bytes->end()));

    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error());

    return std::move(*file);
}

/// Plansza z obydwoma aktorami postawionymi na punktach startowych.
struct Board
{
    explicit Board(gs::tmap::Map map)
        : file(load(map))
        , world(file.map())
        , simulation(world, two_humans, 1234)
    {
        EXPECT_TRUE(world.place_actor(first));
        EXPECT_TRUE(world.place_actor(second));
    }

    void run(std::uint32_t ticks)
    {
        for (std::uint32_t tick = 1; tick <= ticks; ++tick)
        {
            simulation.tick(tick);
        }
    }

    gs::MapFile file;
    gs::World world;
    gs::Simulation simulation;
};

} // namespace

TEST(SimulationTest, ActorsStartWithTroopsAndGrowEveryTick)
{
    Board board(fixtures::plains_map(40, 40));

    EXPECT_DOUBLE_EQ(board.simulation.player(first).troops, gs::economy::initial_troops);

    board.run(10);

    EXPECT_GT(board.simulation.player(first).troops, gs::economy::initial_troops);
    EXPECT_GT(board.simulation.player(first).last_gain, 0.0);
}

TEST(SimulationTest, GoldAccruesPerTickAndBotsGetHalf)
{
    const std::vector<gs::Actor> mixed{
        gs::Actor{first, "Ala", 0xFF0000, false},
        gs::Actor{second, "Kadar", 0x00FF00, true}};

    const gs::MapFile file = load(fixtures::plains_map(40, 40));

    gs::World world(file.map());
    gs::Simulation simulation(world, mixed, 7);

    ASSERT_TRUE(world.place_actor(first));
    ASSERT_TRUE(world.place_actor(second));

    for (std::uint32_t tick = 1; tick <= 10; ++tick)
    {
        simulation.tick(tick);
    }

    EXPECT_EQ(simulation.player(first).gold, 10 * gs::economy::human_gold_per_tick);
    EXPECT_EQ(simulation.player(second).gold, 10 * gs::economy::bot_gold_per_tick);
}

TEST(SimulationTest, TaxLandsInTheTreasuryOnceEveryPeriod)
{
    Board board(fixtures::plains_map(40, 40));

    // Tikami wprost, a nie przez `run`: ta metoda zaczyna liczyć od jedynki przy każdym
    // wywołaniu, a tu chodzi o **jedno** przejście przez tik poboru.
    for (std::uint32_t tick = 1; tick < gs::economy::tax_interval_ticks; ++tick)
    {
        board.simulation.tick(tick);
    }

    const std::uint64_t before = board.simulation.player(first).gold;

    // Tik przed poborem: w skarbcu jest wyłącznie przychód bazowy.
    EXPECT_EQ(before, (gs::economy::tax_interval_ticks - 1) * gs::economy::human_gold_per_tick);

    const std::uint64_t due = board.simulation.tax_due(first);

    ASSERT_GT(due, 0u);

    board.simulation.tick(gs::economy::tax_interval_ticks);

    const std::uint64_t after = board.simulation.player(first).gold;

    // Zapowiedź liczona jest przed werbunkiem z tego tiku, a pobór po nim, więc kwoty
    // różnią się o podatek od jednego przyrostu. Tolerancja jest na to i tylko na to —
    // gdyby pobór poszedł dwa razy albo w ogóle, różnica byłaby stukrotna.
    EXPECT_NEAR(
        static_cast<double>(after - before - gs::economy::human_gold_per_tick),
        static_cast<double>(due),
        static_cast<double>(due) * 0.01 + 1.0);
}

TEST(SimulationTest, TaxSkipsTheDeadAndTheArmyInTheField)
{
    Board board(fixtures::plains_map(40, 40));

    // Cała pula wychodzi w pole: ludzi w domu nie ma, więc nie ma od czego brać podatku.
    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 100),
        gs::OrderResult::accepted);

    EXPECT_LT(board.simulation.player(first).troops, 1.0);
    EXPECT_EQ(board.simulation.tax_due(first), 0u);

    // Slot spoza obsady nie żyje i nie płaci — `tax_due` nie ma prawa go dotknąć.
    EXPECT_EQ(board.simulation.tax_due(200), 0u);
}

TEST(SimulationTest, TicksToTaxCountsDownAndResetsAfterCollection)
{
    Board board(fixtures::plains_map(40, 40));

    board.run(1);

    EXPECT_EQ(board.simulation.ticks_to_tax(), gs::economy::tax_interval_ticks - 1);

    board.run(gs::economy::tax_interval_ticks);

    // Tik poboru jest zarazem pierwszym tikiem kolejnego cyklu, więc licznik pokazuje pełen
    // okres — bez tego pasek w interfejsie zatrzymywałby się na zerze na jedną wysyłkę.
    EXPECT_EQ(board.simulation.ticks_to_tax(), gs::economy::tax_interval_ticks);
}

TEST(SimulationTest, AttackingWastelandTakesGroundAndSpendsTroops)
{
    Board board(fixtures::plains_map(40, 40));

    const std::uint32_t start = board.world.tiles_of(first);

    ASSERT_EQ(start, gs::World::starting_tiles);

    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 50),
        gs::OrderResult::accepted);

    const double kept = board.simulation.player(first).troops;

    // Połowa puli wyszła w pole, więc w domu zostaje reszta.
    EXPECT_NEAR(kept, gs::economy::initial_troops / 2.0, 1.0);
    EXPECT_GT(board.simulation.attack_force(first), 0.0);

    board.run(20);

    EXPECT_GT(board.world.tiles_of(first), start);
    EXPECT_GT(board.world.changed_tiles().size(), 0u);
}

TEST(SimulationTest, AttackWithoutASharedBorderCostsNothing)
{
    // Punkty startowe w przeciwległych rogach czterdziestu na czterdzieści — terytoria
    // startowe dzieli kilkadziesiąt kafelków pustkowia.
    Board board(fixtures::plains_map(40, 40));

    EXPECT_EQ(
        board.simulation.order_attack(first, second, 50),
        gs::OrderResult::no_shared_border);

    // Odrzucony rozkaz nie ma prawa zabrać ludzi z puli.
    EXPECT_DOUBLE_EQ(board.simulation.player(first).troops, gs::economy::initial_troops);
    EXPECT_EQ(board.simulation.active_attacks(), 0u);
}

TEST(SimulationTest, AttackOnYourselfOrOnWaterIsRejected)
{
    Board board(fixtures::plains_map(40, 40));

    EXPECT_EQ(board.simulation.order_attack(first, first, 50), gs::OrderResult::invalid_target);
    EXPECT_EQ(
        board.simulation.order_attack(first, gs::tmap::water_owner, 50),
        gs::OrderResult::invalid_target);
}

TEST(SimulationTest, SecondOrderOnTheSameTargetJoinsTheOffensive)
{
    Board board(fixtures::plains_map(40, 40));

    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 25),
        gs::OrderResult::accepted);

    const double sent = board.simulation.attack_force(first);

    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 25),
        gs::OrderResult::accepted);

    // Jedno natarcie, nie dwa: dwa fronty na tego samego przeciwnika biłyby się o te same
    // kafelki, a każde liczyłoby stosunek sił, jakby było jedyne.
    EXPECT_EQ(board.simulation.active_attacks(), 1u);
    EXPECT_GT(board.simulation.attack_force(first), sent);
}

TEST(SimulationTest, OpposingAttacksAnnihilateEachOther)
{
    // Korytarz: oba państwa stykają się dokładnie w jednym miejscu.
    Board board(fixtures::duel_map());

    const std::uint32_t held = board.world.tiles_of(first);

    ASSERT_GT(held, 0u);
    ASSERT_EQ(board.world.tiles_of(second), held);

    ASSERT_EQ(board.simulation.order_attack(first, second, 100), gs::OrderResult::accepted);
    ASSERT_EQ(board.simulation.active_attacks(), 1u);

    // Obrońca odpowiada tym samym. Armie ścierają się od razu, zanim którakolwiek dojdzie
    // do kafelków — obie były równe, więc znoszą się bez reszty.
    ASSERT_EQ(board.simulation.order_attack(second, first, 100), gs::OrderResult::accepted);

    board.run(1);

    EXPECT_EQ(board.simulation.active_attacks(), 0u);
    EXPECT_EQ(board.world.tiles_of(first), held);
    EXPECT_EQ(board.world.tiles_of(second), held);
}

TEST(SimulationTest, WastelandFrontStaysCompactInsteadOfFraying)
{
    // Punkt startowy **na środku** i mapa z zapasem: przy domyślnych spawnach plama dobija
    // do krawędzi, a wtedy obwód mierzy kształt planszy, nie kształt natarcia.
    Board board(fixtures::plains_map(
        120,
        120,
        gs::tmap::Terrain::lowlands,
        {gs::tmap::Spawn{60, 60}, gs::tmap::Spawn{5, 5}}));

    // Połowa puli, bo strzępienie zaczyna się dopiero przy froncie, który ma się gdzie
    // rozjechać: przy małym natarciu obie wersje kopca dają tę samą zwartą plamę.
    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 50),
        gs::OrderResult::accepted);

    board.run(60);

    const double area = static_cast<double>(board.world.tiles_of(first));

    std::uint32_t perimeter = 0;

    for (std::size_t index = 0; index < board.world.tile_count(); ++index)
    {
        const std::uint32_t tile = static_cast<std::uint32_t>(index);

        if (board.world.owner_at(tile) != first)
        {
            continue;
        }

        std::array<std::uint32_t, 4> neighbors{};

        const std::uint32_t count = board.world.neighbors4(tile, neighbors);

        for (std::uint32_t at = 0; at < count; ++at)
        {
            if (board.world.owner_at(neighbors[at]) != first)
            {
                ++perimeter;

                break;
            }
        }
    }

    // Natarcie ma się rozlewać zwartą plamą, a nie mackami. Miarą jest obwód porównany
    // z obwodem koła o tej samej powierzchni — kopiec bez ponownego wstawiania kafelków
    // dawał tu ponad dwukrotność, bo zamrożony priorytet zostawiał niedomknięte kieszenie.
    //
    // To nie jest test estetyki: budżet kafelków na tik jest **proporcjonalny do szerokości
    // frontu**, więc postrzępione natarcie samo się rozpędza i rozjeżdża tempo z pierwowzorem.
    const double ideal = 2.0 * std::sqrt(std::numbers::pi * area);

    // Zmierzone: 102 przy zwartej plamie z 834 kafelków (obwód idealnego koła to 102,4),
    // a 247 przy kopcu z dedupem. Próg 1,3 rozdziela te dwa światy z zapasem w obie strony.
    EXPECT_LT(static_cast<double>(perimeter), ideal * 1.3);
}

TEST(SimulationTest, RetreatReturnsThreeQuartersOfTheForceAfterTheDelay)
{
    Board board(fixtures::duel_map());

    ASSERT_EQ(board.simulation.order_attack(first, second, 100), gs::OrderResult::accepted);

    const double sent = board.simulation.attack_force(first);

    ASSERT_GT(sent, 0.0);
    ASSERT_EQ(board.simulation.order_retreat(first, second), gs::OrderResult::accepted);

    board.run(gs::Simulation::retreat_delay_ticks);

    // Armia jest w drodze: natarcie stoi na liście, a pula gracza wciąż jej nie widzi.
    EXPECT_EQ(board.simulation.active_attacks(), 1u);
    EXPECT_NEAR(board.simulation.attack_force(first), sent, sent * 0.001);

    const double waiting = board.simulation.player(first).troops;

    EXPECT_LT(waiting, sent * 0.1);

    board.run(1);

    EXPECT_EQ(board.simulation.active_attacks(), 0u);

    // Wycofanie ma być decyzją, nie odruchem: wraca 75%, reszta zostaje na drodze.
    // Tolerancja pokrywa przyrost z tego jednego tiku.
    EXPECT_NEAR(board.simulation.player(first).troops - waiting, sent * 0.75, sent * 0.01);
}

TEST(SimulationTest, RetreatCannotBeRestartedToKeepTheArmyInTheField)
{
    Board board(fixtures::duel_map());

    ASSERT_EQ(board.simulation.order_attack(first, second, 100), gs::OrderResult::accepted);
    ASSERT_EQ(board.simulation.order_retreat(first, second), gs::OrderResult::accepted);

    board.run(gs::Simulation::retreat_delay_ticks - 1);

    // Powtórzony rozkaz nie ma czego przestawić — inaczej klikanie „wycofaj" trzymałoby
    // armię poza pulą w nieskończoność, a przy tym poza zasięgiem obrony.
    EXPECT_EQ(board.simulation.order_retreat(first, second), gs::OrderResult::invalid_target);

    board.run(2);

    EXPECT_EQ(board.simulation.active_attacks(), 0u);
}

TEST(SimulationTest, RetreatFromWastelandCostsNothing)
{
    Board board(fixtures::plains_map(40, 40));

    ASSERT_EQ(
        board.simulation.order_attack(first, gs::tmap::wasteland_owner, 100),
        gs::OrderResult::accepted);

    const double sent = board.simulation.attack_force(first);

    ASSERT_EQ(
        board.simulation.order_retreat(first, gs::tmap::wasteland_owner),
        gs::OrderResult::accepted);

    board.run(gs::Simulation::retreat_delay_ticks);

    const double waiting = board.simulation.player(first).troops;

    board.run(1);

    // Nikt tam nie stał, więc odwrót nie jest ucieczką spod ognia — kary nie ma.
    EXPECT_NEAR(board.simulation.player(first).troops - waiting, sent, sent * 0.005);
}

TEST(SimulationTest, LosingTheLastTileEliminatesThePlayer)
{
    Board board(fixtures::duel_map());

    ASSERT_TRUE(board.simulation.player(second).alive);

    ASSERT_EQ(board.simulation.order_attack(first, second, 100), gs::OrderResult::accepted);

    board.run(200);

    EXPECT_EQ(board.world.tiles_of(second), 0u);
    EXPECT_FALSE(board.simulation.player(second).alive);
    EXPECT_DOUBLE_EQ(board.simulation.player(second).troops, 0.0);

    // Zwycięzca dostaje ocalałych z powrotem: nie ma już czego zdobywać.
    EXPECT_EQ(board.simulation.active_attacks(), 0u);
    EXPECT_GT(board.simulation.player(first).troops, 0.0);
}

TEST(SimulationTest, CitiesCostGoldAndRaiseTheCeiling)
{
    Board board(fixtures::plains_map(40, 40));

    EXPECT_EQ(board.simulation.order_city(first), gs::OrderResult::not_enough_gold);

    // 125 000 złota to 1250 tików samego przychodu bazowego, a po drodze dochodzą jeszcze
    // cztery pobory podatku — dlatego stan skarbca odczytujemy, zamiast go zakładać.
    board.run(1250);

    const std::uint64_t saved = board.simulation.player(first).gold;

    ASSERT_GE(saved, gs::economy::city_cost(0));

    ASSERT_EQ(board.simulation.order_city(first), gs::OrderResult::accepted);

    EXPECT_EQ(board.simulation.player(first).cities, 1u);
    EXPECT_EQ(board.simulation.player(first).gold, saved - gs::economy::city_cost(0));

    // Drugie miasto kosztuje dwa razy tyle, więc od ręki się nie da.
    EXPECT_EQ(board.simulation.order_city(first), gs::OrderResult::not_enough_gold);
}

TEST(SimulationTest, SameSeedAndOrdersGiveTheSameMap)
{
    // Replay (D10) stoi na tym, że ziarno plus log komend odtwarza mecz kafelek w kafelek.
    // Bez tego nie ma po co zapisywać ani jednego, ani drugiego.
    const auto play = [](std::vector<std::uint8_t>& owners)
    {
        Board board(fixtures::plains_map(40, 40));

        EXPECT_EQ(
            board.simulation.order_attack(first, gs::tmap::wasteland_owner, 60),
            gs::OrderResult::accepted);

        board.run(40);

        owners.assign(board.world.owner().begin(), board.world.owner().end());
    };

    std::vector<std::uint8_t> left;
    std::vector<std::uint8_t> right;

    play(left);
    play(right);

    EXPECT_EQ(left, right);
}
