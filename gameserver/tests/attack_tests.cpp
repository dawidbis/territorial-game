#include "sim/attack.hpp"

#include "map/map_file.hpp"
#include "map/tmap.hpp"
#include "map_fixtures.hpp"
#include "sim/world.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

// Arytmetyka natarcia: priorytet kolejki podboju i rozliczenie pojedynczego kafelka.
// Wszystko tu jest czystą funkcją, więc testy mówią wprost, co ma wyjść — bez zegara,
// bez gniazda i bez losowania (te są parametrem, nie zależnością).
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

constexpr std::uint8_t attacker_slot = 1;

} // namespace

TEST(AttackTest, TerrainCostRisesWithAltitude)
{
    EXPECT_DOUBLE_EQ(gs::terrain_cost(gs::tmap::Terrain::lowlands).defense, 80.0);
    EXPECT_DOUBLE_EQ(gs::terrain_cost(gs::tmap::Terrain::highlands).defense, 100.0);
    EXPECT_DOUBLE_EQ(gs::terrain_cost(gs::tmap::Terrain::mountains).defense, 120.0);

    EXPECT_LT(
        gs::terrain_cost(gs::tmap::Terrain::lowlands).speed,
        gs::terrain_cost(gs::tmap::Terrain::mountains).speed);
}

TEST(AttackTest, PriorityFavoursTilesSurroundedByTheAttacker)
{
    const gs::MapFile file = load(fixtures::plains_map(5, 5));

    gs::World world(file.map());

    // Kafelek 12 to środek planszy. Najpierw nie sąsiaduje z nikim…
    const double alone = gs::conquer_priority(world, 12, attacker_slot, 0, 0);

    // …a teraz z trzech stron.
    world.set_owner(11, attacker_slot);
    world.set_owner(7, attacker_slot);
    world.set_owner(17, attacker_slot);

    const double surrounded = gs::conquer_priority(world, 12, attacker_slot, 0, 0);

    // Niższy priorytet znaczy „wcześniej z kolejki": otoczona kieszeń domyka się przed
    // kafelkiem stykającym się jednym bokiem.
    EXPECT_LT(surrounded, alone);
}

TEST(AttackTest, PriorityPutsMountainsBehindPlains)
{
    const gs::MapFile plains = load(fixtures::plains_map(5, 5, gs::tmap::Terrain::lowlands));
    const gs::MapFile mountains = load(fixtures::plains_map(5, 5, gs::tmap::Terrain::mountains));

    const gs::World flat(plains.map());
    const gs::World rough(mountains.map());

    EXPECT_LT(
        gs::conquer_priority(flat, 12, attacker_slot, 0, 0),
        gs::conquer_priority(rough, 12, attacker_slot, 0, 0));
}

TEST(AttackTest, PriorityKeepsLatecomersBehindTilesWaitingSinceEarlier)
{
    const gs::MapFile file = load(fixtures::plains_map(5, 5));

    const gs::World world(file.map());

    // Ten sam kafelek, ten sam rzut, inny tik: dołożony później nie wyprzedza tych,
    // które czekają w kolejce od dawna.
    EXPECT_LT(
        gs::conquer_priority(world, 12, attacker_slot, 0, 10),
        gs::conquer_priority(world, 12, attacker_slot, 0, 200));
}

TEST(AttackTest, TakingWastelandCostsTerrainOnlyAndKillsNobody)
{
    gs::CombatSides sides;
    sides.attacker_troops = 10'000.0;
    sides.attacker_tiles = 1;
    sides.defender_is_player = false;

    const gs::AttackStep step = gs::attack_step(sides, gs::tmap::Terrain::lowlands);

    // Nizina kosztuje człowieka 80/5 ludzi na kafelek, a pustkowia nie ma kto bronić.
    EXPECT_DOUBLE_EQ(step.attacker_loss, 16.0);
    EXPECT_DOUBLE_EQ(step.defender_loss, 0.0);
    EXPECT_GT(step.tiles_used, 0.0);
}

TEST(AttackTest, BotsFillWastelandTwiceAsCheaply)
{
    gs::CombatSides human;
    human.attacker_troops = 10'000.0;
    human.defender_is_player = false;

    gs::CombatSides bot = human;
    bot.attacker_is_bot = true;

    EXPECT_DOUBLE_EQ(
        gs::attack_step(bot, gs::tmap::Terrain::lowlands).attacker_loss,
        gs::attack_step(human, gs::tmap::Terrain::lowlands).attacker_loss / 2.0);
}

TEST(AttackTest, DefenderLosesItsGarrisonSpreadOverItsTiles)
{
    gs::CombatSides sides;
    sides.attacker_troops = 10'000.0;
    sides.attacker_tiles = 100;
    sides.defender_troops = 50'000.0;
    sides.defender_tiles = 500;
    sides.defender_is_player = true;

    const gs::AttackStep step = gs::attack_step(sides, gs::tmap::Terrain::lowlands);

    // Obrońca traci tylu ludzi, ilu „stało" na utraconym kafelku — 50 000 / 500.
    EXPECT_DOUBLE_EQ(step.defender_loss, 100.0);
    EXPECT_GT(step.attacker_loss, 0.0);
}

TEST(AttackTest, AttackingUphillCostsMore)
{
    gs::CombatSides sides;
    sides.attacker_troops = 10'000.0;
    sides.attacker_tiles = 100;
    sides.defender_troops = 10'000.0;
    sides.defender_tiles = 100;
    sides.defender_is_player = true;

    EXPECT_LT(
        gs::attack_step(sides, gs::tmap::Terrain::lowlands).attacker_loss,
        gs::attack_step(sides, gs::tmap::Terrain::mountains).attacker_loss);
}

TEST(AttackTest, DefendingBotsAreSofterThanHumans)
{
    gs::CombatSides human;
    human.attacker_troops = 10'000.0;
    human.attacker_tiles = 100;
    human.defender_troops = 10'000.0;
    human.defender_tiles = 100;
    human.defender_is_player = true;

    gs::CombatSides bot = human;
    bot.defender_is_bot = true;

    EXPECT_LT(
        gs::attack_step(bot, gs::tmap::Terrain::lowlands).attacker_loss,
        gs::attack_step(human, gs::tmap::Terrain::lowlands).attacker_loss);
}

TEST(AttackTest, HugeDefendersDefendWorseThanTheirSizeSuggests)
{
    gs::CombatSides small;
    small.attacker_troops = 1'000'000.0;
    small.attacker_tiles = 1'000;
    small.defender_troops = 1'000'000.0;
    small.defender_tiles = 1'000;
    small.defender_is_player = true;

    gs::CombatSides huge = small;
    huge.defender_tiles = 400'000;

    // Ta sama armia po obu stronach, ale obrońca rozlany na czterysta tysięcy kafelków
    // kosztuje atakującego mniej — inaczej państwo, które raz urosło, jest nie do ruszenia.
    EXPECT_LT(
        gs::attack_step(huge, gs::tmap::Terrain::lowlands).attacker_loss,
        gs::attack_step(small, gs::tmap::Terrain::lowlands).attacker_loss);
}

TEST(AttackTest, HugeAttackersGetTheirPenaltyStraightFromTheOriginalFormula)
{
    gs::CombatSides at_threshold;
    at_threshold.attacker_troops = 1'000'000.0;
    at_threshold.attacker_tiles = 100'000;
    at_threshold.defender_tiles = 1'000;
    at_threshold.defender_is_player = true;

    gs::CombatSides four_times = at_threshold;
    four_times.attacker_tiles = 400'000;

    // Obrońca bez ludzi zeruje człon od obsady kafelka, więc iloraz strat pokazuje sam
    // hamulec skali: `sqrt(100 000 / kafelki)^0,7`, czyli efektywnie wykładnik 0,35.
    // Pierwiastek jest w pierwowzorze — wiki go gubi i podaje 0,7. Ten test istnieje po to,
    // żeby nikt (łącznie z asystentem) nie „poprawił" tego z powrotem.
    EXPECT_NEAR(
        gs::attack_step(four_times, gs::tmap::Terrain::lowlands).attacker_loss
            / gs::attack_step(at_threshold, gs::tmap::Terrain::lowlands).attacker_loss,
        std::pow(0.25, 0.35),
        1e-9);
}

TEST(AttackTest, TilesPerTickGrowWithTheFrontAndStayClamped)
{
    // Pustkowie: dwa kafelki na każdy kafelek granicy, bez stosunku sił.
    EXPECT_DOUBLE_EQ(gs::attack_tiles_per_tick(10'000.0, 0.0, false, 7), 14.0);

    // Przeciwko graczowi mnożnik jest obcięty do 0,5, więc nawet stukrotna przewaga nie
    // zdobywa państwa w jednym tiku.
    EXPECT_DOUBLE_EQ(gs::attack_tiles_per_tick(1'000'000.0, 1.0, true, 10), 0.5 * 10 * 3);

    // …i do 0,01 od dołu, żeby beznadziejne natarcie w ogóle się posuwało.
    EXPECT_DOUBLE_EQ(gs::attack_tiles_per_tick(1.0, 1'000'000.0, true, 10), 0.01 * 10 * 3);
}

TEST(AttackTest, ConquerQueueBreaksTiesByTileIndex)
{
    gs::ConquerQueue queue;

    queue.push({7, 1.0});
    queue.push({3, 1.0});
    queue.push({5, 1.0});

    // Remisy są tu regułą, nie wyjątkiem — priorytet to suma kilku wartości z małego
    // zbioru. Bez rozstrzygnięcia po indeksie kolejność zależałaby od implementacji kopca
    // i ten sam mecz rozszedłby się między dwiema bibliotekami standardowymi.
    EXPECT_EQ(queue.top().tile, 3u);
    queue.pop();
    EXPECT_EQ(queue.top().tile, 5u);
    queue.pop();
    EXPECT_EQ(queue.top().tile, 7u);
}
