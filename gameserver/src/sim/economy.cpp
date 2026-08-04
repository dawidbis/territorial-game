#include "sim/economy.hpp"

#include <algorithm>
#include <cmath>

namespace gs::economy
{

double max_troops(std::uint32_t tiles, std::uint32_t cities) noexcept
{
    const double land = std::pow(static_cast<double>(tiles), 0.6) * 1000.0 + 50'000.0;

    return 2.0 * land + static_cast<double>(cities) * 250'000.0;
}

double troop_gain(double troops, double max) noexcept
{
    if (max <= 0.0 || troops >= max)
    {
        return 0.0;
    }

    const double base = 10.0 + std::pow(std::max(0.0, troops), 0.73) / 4.0;

    const double gain = base * (1.0 - troops / max);

    // Hamulec zbliża przyrost do zera przy suficie, ale go nie gwarantuje przy dużym kroku —
    // a sufit ma być twardy, bo to on wymusza wydawanie ludzi zamiast ich zbierania.
    return std::min(gain, max - troops);
}

std::uint64_t city_cost(std::uint32_t cities) noexcept
{
    // Podwojenie liczone przesunięciem, ale dopiero po odcięciu wykładników, przy których
    // i tak obowiązuje sufit — `1 << 60` to niezdefiniowane zachowanie, nie duża liczba.
    constexpr std::uint32_t max_doublings = 8;

    if (cities >= max_doublings)
    {
        return max_city_cost;
    }

    return std::min(first_city_cost << cities, max_city_cost);
}

std::uint64_t gold_per_tick(std::uint32_t cities, bool is_bot) noexcept
{
    const std::uint64_t base = is_bot ? bot_gold_per_tick : human_gold_per_tick;

    return base + static_cast<std::uint64_t>(cities) * city_gold_per_tick;
}

std::uint64_t tax_amount(double troops) noexcept
{
    if (troops <= 0.0)
    {
        return 0;
    }

    return static_cast<std::uint64_t>(troops / population_per_unit * tax_rate);
}

} // namespace gs::economy
