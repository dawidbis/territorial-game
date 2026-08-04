#include "sim/rng.hpp"

namespace gs
{
namespace
{

constexpr std::uint64_t multiplier = 6364136223846793005ULL;

} // namespace

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream)
    : increment_((stream << 1U) | 1U)
{
    // Kolejność z referencyjnej implementacji PCG: krok, dodanie ziarna, krok. Ziarno
    // wmieszane bez pierwszego kroku zostawiałoby widoczną zależność pierwszej liczby
    // od ziarna.
    next();

    state_ += seed;

    next();
}

std::uint32_t Pcg32::next() noexcept
{
    const std::uint64_t previous = state_;

    state_ = previous * multiplier + increment_;

    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
    const std::uint32_t rotation = static_cast<std::uint32_t>(previous >> 59U);

    return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
}

std::uint32_t Pcg32::below(std::uint32_t bound) noexcept
{
    if (bound == 0)
    {
        return 0;
    }

    // Odrzucamy najniższe `2³² mod bound` wyników — reszta dzieli się na równe kubełki.
    const std::uint32_t threshold = (0U - bound) % bound;

    for (;;)
    {
        const std::uint32_t drawn = next();

        if (drawn >= threshold)
        {
            return drawn % bound;
        }
    }
}

} // namespace gs
