#include "sim/economy.hpp"

#include <gtest/gtest.h>

#include <cmath>

// Ekonomia: sufit ludzi, przyrost i cena miasta. Wzory są tu kontraktem z interfejsem
// gracza — pasek stanu pokazuje dokładnie te liczby, więc pomyłka w wykładniku nie jest
// „lekkim rozjechaniem balansu", tylko liczbą, która nie zgadza się z niczym.
namespace
{

using namespace gs::economy;

} // namespace

TEST(EconomyTest, MaxTroopsCountsLandWithDiminishingReturns)
{
    // 2 × (1^0,6 × 1000 + 50 000) = 102 000 na jednym kafelku.
    EXPECT_NEAR(max_troops(1, 0), 102'000.0, 0.5);

    const double small = max_troops(1'000, 0);
    const double twice = max_troops(2'000, 0);

    // Dwa razy większe państwo ma mniej niż dwa razy większy sufit — na tym polega
    // wykładnik 0,6 i to jest jedyny hamulec na kuli śnieżnej.
    EXPECT_LT(twice, 2.0 * small);
    EXPECT_GT(twice, small);
}

TEST(EconomyTest, EachCityRaisesTheCeilingByAQuarterMillion)
{
    EXPECT_NEAR(max_troops(100, 3) - max_troops(100, 0), 750'000.0, 0.5);
}

TEST(EconomyTest, TroopGainPeaksAroundFortyTwoPercentOfTheCeiling)
{
    constexpr double ceiling = 1'000'000.0;

    double best = 0.0;
    double best_at = 0.0;

    for (int percent = 1; percent < 100; ++percent)
    {
        const double troops = ceiling * percent / 100.0;
        const double gain = troop_gain(troops, ceiling);

        if (gain > best)
        {
            best = gain;
            best_at = percent;
        }
    }

    // Wniosek ze wzoru, nie z przypadku: po tym punkcie werbunek zwalnia, więc trzymanie
    // pełnej puli jest gorsze niż jej wydawanie. Interfejs obiecuje graczowi te 42%.
    EXPECT_NEAR(best_at, 42.0, 1.0);
}

TEST(EconomyTest, TroopGainStopsAtTheCeiling)
{
    EXPECT_DOUBLE_EQ(troop_gain(1'000.0, 1'000.0), 0.0);
    EXPECT_DOUBLE_EQ(troop_gain(2'000.0, 1'000.0), 0.0);

    // Przy suficie tuż nad pułapem przyrost nie ma prawa go przeskoczyć.
    EXPECT_LE(troop_gain(999.0, 1'000.0), 1.0);
}

TEST(EconomyTest, CitiesAddToTheGoldIncome)
{
    EXPECT_EQ(gold_per_tick(0, false), human_gold_per_tick);
    EXPECT_EQ(gold_per_tick(0, true), bot_gold_per_tick);

    // Miasto dokłada 10 na tik, czyli 100 na sekundę — pierwsze zwraca się w 1250 sekund.
    EXPECT_EQ(gold_per_tick(3, false), human_gold_per_tick + 30);
    EXPECT_EQ(gold_per_tick(3, true), bot_gold_per_tick + 30);
}

TEST(EconomyTest, TaxTakesATenthOfThePopulationThePlayerSees)
{
    // Przykład z projektu mechaniki: gracz z 376 tysiącami ludzi na pasku oddaje 37,6 tysiąca
    // złota. Na pasku widać `troops / population_per_unit`, więc w jednostkach symulacji jest
    // to 3 760 000 — i **to** rozróżnienie jest tu jedyną rzeczą, którą da się pomylić.
    EXPECT_EQ(tax_amount(3'760'000.0), 37'600u);

    EXPECT_EQ(tax_amount(0.0), 0u);
    EXPECT_EQ(tax_amount(-5.0), 0u);
}

TEST(EconomyTest, TaxIsWorthRoughlyTheBaseIncomeOverItsPeriod)
{
    // Trzydzieści sekund bazowego przychodu to 30 000 złota. Podatek od 300 tysięcy ludzi
    // z paska daje tyle samo — a to jest cały sens tej mechaniki: dopiero od tego progu
    // trzymanie ludzi w domu konkuruje z ich wydawaniem.
    const std::uint64_t base = human_gold_per_tick * tax_interval_ticks;

    EXPECT_EQ(tax_amount(300'000.0 * population_per_unit), base);
}

TEST(EconomyTest, CityCostDoublesUntilItHitsTheCap)
{
    EXPECT_EQ(city_cost(0), 125'000u);
    EXPECT_EQ(city_cost(1), 250'000u);
    EXPECT_EQ(city_cost(2), 500'000u);
    EXPECT_EQ(city_cost(3), 1'000'000u);

    // Od czwartego miasta cena stoi — bez sufitu podwajanie wychodzi poza zakres złota
    // w kilkunastu krokach i miasta miałyby sens wyłącznie na starcie.
    EXPECT_EQ(city_cost(4), max_city_cost);
    EXPECT_EQ(city_cost(40), max_city_cost);
}
