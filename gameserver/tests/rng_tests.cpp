#include "sim/rng.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

// PCG32 — jedyne źródło losowości w procesie meczu.
//
// Testy nie sprawdzają jakości rozkładu, bo tę gwarantuje sam algorytm. Sprawdzają to, na
// czym stoi replay: że ten sam ciąg wychodzi z tego samego ziarna i że strumienie są od
// siebie niezależne.
namespace
{

std::vector<std::uint32_t> draw(std::uint64_t seed, std::uint64_t stream, int count)
{
    gs::Pcg32 rng(seed, stream);

    std::vector<std::uint32_t> drawn;
    drawn.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        drawn.push_back(rng.next());
    }

    return drawn;
}

} // namespace

TEST(RngTest, GivesTheSameSequenceForTheSameSeedAndStream)
{
    EXPECT_EQ(draw(42, 1, 32), draw(42, 1, 32));
}

TEST(RngTest, GivesADifferentSequenceForADifferentSeed)
{
    EXPECT_NE(draw(42, 1, 32), draw(43, 1, 32));
}

// Strumienie są tym, co pozwala botowi na slocie 7 losować niezależnie od reszty. Gdyby
// dawały ten sam ciąg, wszystkie boty byłyby identyczne.
TEST(RngTest, GivesADifferentSequencePerStream)
{
    EXPECT_NE(draw(42, 1, 32), draw(42, 2, 32));
}

TEST(RngTest, StaysInsideTheBound)
{
    gs::Pcg32 rng(7, 0);

    for (int index = 0; index < 10000; ++index)
    {
        EXPECT_LT(rng.below(254), 254u);
    }
}

TEST(RngTest, TreatsAZeroBoundAsZero)
{
    gs::Pcg32 rng(7, 0);

    EXPECT_EQ(rng.below(0), 0u);
}

// Odrzucanie reszty ma dawać rozkład bez faworyzowania niskich wartości. Przy dziesięciu
// kubełkach i stu tysiącach losowań odchylenie ponad 20% od średniej znaczyłoby, że coś
// w kodzie odrzucania jest odwrotnie.
TEST(RngTest, SpreadsDrawsAcrossTheWholeRange)
{
    gs::Pcg32 rng(1234, 5);

    std::array<int, 10> buckets{};

    constexpr int draws = 100000;

    for (int index = 0; index < draws; ++index)
    {
        ++buckets[rng.below(static_cast<std::uint32_t>(buckets.size()))];
    }

    constexpr int expected = draws / static_cast<int>(buckets.size());

    for (const int count : buckets)
    {
        EXPECT_GT(count, expected * 8 / 10);
        EXPECT_LT(count, expected * 12 / 10);
    }
}
