#include "sim/attack.hpp"

#include "sim/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace gs
{
namespace
{

/// Od ilu kafelków obrońcy zaczyna działać hamulec na wielkie państwa.
constexpr double defense_debuff_midpoint = 150'000.0;

/// Tempo narastania hamulca: po 50 000 kafelków sigmoida pokonuje połowę drogi.
///
/// `std::numbers::ln2`, a nie `std::log(2.0)`: to samo, ale policzone w czasie kompilacji,
/// więc stała jest `constexpr`, a nie inicjalizowana przy starcie procesu.
constexpr double defense_debuff_rate = std::numbers::ln2 / 50'000.0;

/// Powyżej tylu kafelków atakujący zaczyna tracić na sprawności.
constexpr double large_attacker_tiles = 100'000.0;

/// Mnożnik strat atakującego, gdy broni się bot.
///
/// **0,7, a nie 0,8**: tyle stoi w źródle pierwowzoru (`mag *= 0.7` w `attackLogic`). Wiki
/// pierwowzoru podaje 0,8 i myli się — tak samo jak w wykładniku wielkiego atakującego niżej.
constexpr double bot_defense_debuff = 0.7;

double sigmoid(double x, double rate, double midpoint) noexcept
{
    return 1.0 / (1.0 + std::exp(-rate * (x - midpoint)));
}

double clamp(double value, double low, double high) noexcept
{
    return std::max(low, std::min(high, value));
}

/// Górna granica losowania w priorytecie podboju — wartość z przedziału [0, 6].
///
/// Siedem, nie osiem: pierwowzór woła `nextInt(0, 7)`, a to u niego przedział **domknięty
/// z lewej i otwarty z prawej**. Wiki pierwowzoru pisze „[0,7]" i myli się o jeden.
constexpr std::uint32_t priority_roll_bound = 7;

/// Mnożnik terenu w priorytecie kolejki — inny niż ten w walce, bo odpowiada na inne
/// pytanie: nie „ile to kosztuje", tylko „w jakiej kolejności to brać".
double priority_terrain(tmap::Terrain terrain) noexcept
{
    switch (terrain)
    {
    case tmap::Terrain::highlands:
        return 1.5;
    case tmap::Terrain::mountains:
        return 2.0;
    case tmap::Terrain::lowlands:
        return 1.0;
    case tmap::Terrain::water:
        break;
    }

    return 0.0;
}

} // namespace

bool touches(const World& world, std::uint32_t tile, std::uint8_t slot) noexcept
{
    std::array<std::uint32_t, 4> neighbors{};

    const std::uint32_t count = world.neighbors4(tile, neighbors);

    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (world.owner_at(neighbors[index]) == slot)
        {
            return true;
        }
    }

    return false;
}

void seed_front(const World& world, Attack& attack, Pcg32& rng, std::uint32_t tick)
{
    const std::size_t tiles = world.tile_count();

    for (std::size_t index = 0; index < tiles; ++index)
    {
        const std::uint32_t tile = static_cast<std::uint32_t>(index);

        if (world.owner_at(tile) != attack.target || !touches(world, tile, attack.attacker))
        {
            continue;
        }

        attack.border.insert(tile);

        attack.frontier.push(
            {tile,
             conquer_priority(world, tile, attack.attacker, rng.below(priority_roll_bound), tick)});
    }
}

void extend_front(
    const World& world,
    Attack& attack,
    Pcg32& rng,
    std::uint32_t tile,
    std::uint32_t tick)
{
    std::array<std::uint32_t, 4> neighbors{};

    const std::uint32_t count = world.neighbors4(tile, neighbors);

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const std::uint32_t neighbor = neighbors[index];

        if (!world.is_land(neighbor) || world.owner_at(neighbor) != attack.target)
        {
            continue;
        }

        // Zbiór granicy liczy szerokość frontu — i tylko tyle.
        attack.border.insert(neighbor);

        attack.frontier.push(
            {neighbor,
             conquer_priority(
                 world,
                 neighbor,
                 attack.attacker,
                 rng.below(priority_roll_bound),
                 tick)});
    }
}

TerrainCost terrain_cost(tmap::Terrain terrain) noexcept
{
    switch (terrain)
    {
    case tmap::Terrain::highlands:
        return {100.0, 20.0};
    case tmap::Terrain::mountains:
        return {120.0, 25.0};
    case tmap::Terrain::lowlands:
    case tmap::Terrain::water:
        break;
    }

    return {80.0, 16.5};
}

double conquer_priority(
    const World& world,
    std::uint32_t tile,
    std::uint8_t attacker,
    std::uint32_t roll,
    std::uint32_t tick) noexcept
{
    std::array<std::uint32_t, 4> neighbors{};

    const std::uint32_t count = world.neighbors4(tile, neighbors);

    std::uint32_t mine = 0;

    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (world.owner_at(neighbors[index]) == attacker)
        {
            ++mine;
        }
    }

    const double terrain = priority_terrain(world.terrain_at(tile));

    return (static_cast<double>(roll) + 10.0) * (1.0 - static_cast<double>(mine) * 0.5 + terrain / 2.0)
        + static_cast<double>(tick);
}

AttackStep attack_step(const CombatSides& sides, tmap::Terrain terrain) noexcept
{
    const TerrainCost cost = terrain_cost(terrain);

    // Zero ludzi po stronie atakującego znaczy atak, który zaraz i tak zostanie zdjęty;
    // dzielenie przez niego dałoby nieskończoność i zatrułoby stan gry.
    const double attacker_troops = std::max(1.0, sides.attacker_troops);

    if (!sides.defender_is_player)
    {
        // Pustkowie nie ma kto bronić: koszt jest stały i zależy wyłącznie od terenu.
        // Bot płaci połowę, żeby wypełnianie mapy botami nie trwało całego meczu.
        const double loss = sides.attacker_is_bot ? cost.defense / 10.0 : cost.defense / 5.0;

        const double used = clamp((2000.0 * std::max(10.0, cost.speed)) / attacker_troops, 5.0, 100.0);

        return {loss, 0.0, used};
    }

    // Bot broni się gorzej od człowieka i to jedyne odstępstwo od wspólnych reguł: bez
    // niego pierwsze minuty meczu schodzą na przegryzaniu się przez sąsiadów-wypełniaczy.
    // Warunek jest po obu stronach jak w pierwowzorze: bot bijący bota nie dostaje ulgi.
    const bool soft_defender = sides.defender_is_bot && !sides.attacker_is_bot;

    const double defense = soft_defender ? cost.defense * bot_defense_debuff : cost.defense;

    const double defender_tiles = static_cast<double>(sides.defender_tiles);
    const double attacker_tiles = static_cast<double>(sides.attacker_tiles);

    const double scale = 1.0 - sigmoid(defender_tiles, defense_debuff_rate, defense_debuff_midpoint);

    // Wielki obrońca broni się i porusza gorzej — inaczej państwo, które raz urosło, jest
    // nie do ruszenia samą masą.
    const double defender_debuff = 0.7 + 0.3 * scale;

    double attacker_bonus = 1.0;
    double attacker_speed_bonus = 1.0;

    if (attacker_tiles > large_attacker_tiles)
    {
        // Pierwiastek jest w pierwowzorze i nie jest pomyłką przepisania:
        // `Math.sqrt(100_000 / tiles) ** 0.7`, czyli faktycznie wykładnik 0,35. Wersja
        // prędkościowa niżej pierwiastka nie ma — i to też jest z oryginału.
        attacker_bonus = std::pow(std::sqrt(large_attacker_tiles / attacker_tiles), 0.7);
        attacker_speed_bonus = std::pow(large_attacker_tiles / attacker_tiles, 0.6);
    }

    const double defender_loss =
        defender_tiles > 0.0 ? sides.defender_troops / defender_tiles : 0.0;

    const double by_ratio = clamp(sides.defender_troops / attacker_troops, 0.6, 2.0) * defense * 0.8
        * defender_debuff * attacker_bonus;

    const double by_garrison = 1.3 * defender_loss * (defense / 100.0);

    // Dwa wzory zamiast jednego: sam stosunek sił ignoruje, ile obrońca faktycznie trzymał
    // na kafelku, a sama obsada ignoruje przewagę liczebną. Ważone 60/40 na korzyść tego
    // pierwszego, bo to on decyduje o tym, czy natarcie w ogóle ma sens.
    const double attacker_loss = 0.6 * by_ratio + 0.4 * by_garrison;

    const double used = clamp(sides.defender_troops / (5.0 * attacker_troops), 0.2, 1.5) * cost.speed
        * defender_debuff * attacker_speed_bonus;

    return {attacker_loss, defender_loss, used};
}

double attack_tiles_per_tick(
    double attacker_troops,
    double defender_troops,
    bool defender_is_player,
    std::size_t border_tiles) noexcept
{
    const double front = static_cast<double>(border_tiles);

    if (!defender_is_player)
    {
        return front * 2.0;
    }

    const double defenders = std::max(1.0, defender_troops);

    return clamp((5.0 * attacker_troops) / defenders * 2.0, 0.01, 0.5) * front * 3.0;
}

} // namespace gs
