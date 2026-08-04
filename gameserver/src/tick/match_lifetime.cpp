#include "tick/match_lifetime.hpp"

#include <algorithm>

namespace gs
{
namespace
{

std::uint32_t to_ticks(std::chrono::seconds span, const TickRates& rates)
{
    const std::int64_t period = std::max<std::int64_t>(1, rates.sim_period.count());

    return static_cast<std::uint32_t>(std::max<std::int64_t>(1, span.count() * 1000 / period));
}

} // namespace

std::string_view describe(MatchOutcome outcome) noexcept
{
    switch (outcome)
    {
    case MatchOutcome::abandoned:
        return "nikt nie przyszedł";
    case MatchOutcome::finished:
        return "ostatni gracz nie wrócił";
    case MatchOutcome::expired:
        return "twardy limit czasu meczu";
    case MatchOutcome::running:
        break;
    }

    return "mecz trwa";
}

MatchLifetime::MatchLifetime(LifetimeLimits limits, TickRates rates)
    : join_grace_ticks_(to_ticks(limits.join_grace, rates))
    , empty_grace_ticks_(to_ticks(limits.empty_grace, rates))
    , hard_limit_ticks_(to_ticks(limits.hard_limit, rates))
{
}

MatchOutcome MatchLifetime::observe(std::uint32_t tick, std::size_t connections) noexcept
{
    // Twardy limit ma pierwszeństwo: mecz, który się nie kończy, jest problemem niezależnie
    // od tego, ilu graczy w nim siedzi.
    if (tick >= hard_limit_ticks_)
    {
        return MatchOutcome::expired;
    }

    if (connections > 0)
    {
        anyone_joined_ = true;
        empty_since_ = 0;

        return MatchOutcome::running;
    }

    if (!anyone_joined_)
    {
        return tick >= join_grace_ticks_ ? MatchOutcome::abandoned : MatchOutcome::running;
    }

    if (empty_since_ == 0)
    {
        empty_since_ = tick;
    }

    // Powrót w oknie zeruje licznik — to jest cała obsługa reconnectu po stronie cyklu życia
    // procesu (D14 punkt 2).
    return tick - empty_since_ >= empty_grace_ticks_ ? MatchOutcome::finished
                                                     : MatchOutcome::running;
}

} // namespace gs
