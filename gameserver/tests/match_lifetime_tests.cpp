#include "tick/match_lifetime.hpp"

#include "tick/match_clock.hpp"

#include <gtest/gtest.h>

#include <cstdint>

// Trzy sposoby, w jakie proces ma się skończyć sam. Bez nich pierwszy dzień z prawdziwym
// alokatorem zostawia na maszynie proces na każde lobby.
//
// Cały cykl życia liczy w tikach, więc testy nie czekają ani milisekundy — wystarczy podać
// numer tiku, który normalnie nadszedłby po dwóch minutach.
namespace
{

constexpr gs::TickRates rates{};

/// Ile tików mieści się w sekundzie przy domyślnym kroku symulacji.
constexpr std::uint32_t per_second = 10;

gs::MatchLifetime lifetime()
{
    return gs::MatchLifetime(gs::LifetimeLimits{}, rates);
}

} // namespace

TEST(MatchLifetimeTest, KeepsRunningWhileSomeoneIsConnected)
{
    gs::MatchLifetime match = lifetime();

    for (std::uint32_t tick = 1; tick <= 600 * per_second; tick += per_second)
    {
        ASSERT_EQ(match.observe(tick, 1), gs::MatchOutcome::running) << "tik " << tick;
    }
}

// Alokacja poszła w próżnię: proces wstał, a gracz nigdy nie doszedł. Kod wyjścia ma to
// odróżnić od normalnego końca meczu.
TEST(MatchLifetimeTest, GivesUpWhenNobodyEverJoins)
{
    gs::MatchLifetime match = lifetime();

    EXPECT_EQ(match.observe(119 * per_second, 0), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(120 * per_second, 0), gs::MatchOutcome::abandoned);
}

TEST(MatchLifetimeTest, EndsAfterTheLastPlayerLeaves)
{
    gs::MatchLifetime match = lifetime();

    EXPECT_EQ(match.observe(10, 1), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20, 0), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20 + 119 * per_second, 0), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20 + 120 * per_second, 0), gs::MatchOutcome::finished);
}

/// D14 punkt 2: okno reconnectu. Gracz, któremu mignęła sieć, ma do czego wrócić — a powrót
/// zeruje odliczanie, zamiast tylko je wstrzymywać.
TEST(MatchLifetimeTest, AReturningPlayerResetsTheCountdown)
{
    gs::MatchLifetime match = lifetime();

    EXPECT_EQ(match.observe(10, 1), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20, 0), gs::MatchOutcome::running);

    // Wrócił po minucie — od tej chwili odliczanie ma zacząć się od nowa.
    EXPECT_EQ(match.observe(20 + 60 * per_second, 1), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20 + 179 * per_second, 0), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20 + 181 * per_second, 0), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(20 + 300 * per_second, 0), gs::MatchOutcome::finished);
}

// D7 obiecuje mecze poniżej 25 minut i na tej obietnicy stoi strategia deployu. Pełna sala
// nie jest powodem, żeby jej nie dotrzymać.
TEST(MatchLifetimeTest, EnforcesTheHardTimeLimitEvenWithPlayersInside)
{
    gs::MatchLifetime match = lifetime();

    EXPECT_EQ(match.observe(29 * 60 * per_second, 8), gs::MatchOutcome::running);
    EXPECT_EQ(match.observe(30 * 60 * per_second, 8), gs::MatchOutcome::expired);
}

TEST(MatchLifetimeTest, DescribesEveryOutcome)
{
    EXPECT_NE(gs::describe(gs::MatchOutcome::abandoned), gs::describe(gs::MatchOutcome::finished));
    EXPECT_NE(gs::describe(gs::MatchOutcome::expired), gs::describe(gs::MatchOutcome::running));
}
