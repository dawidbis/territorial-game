#include "sim/roster.hpp"

#include "meta/manifest.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Obsada meczu: ludzie z manifestu plus boty dobrane z ziarna.
//
// Wszystko, co dotyczy botów, musi wynikać z ziarna — replay odtwarza mecz przez
// re-symulację (D10), więc bot, którego nick zależałby od kolejności wywołań albo od zegara,
// byłby innym botem przy każdym odtworzeniu.
namespace
{

constexpr std::uint32_t max_actors = 16;

gs::ManifestPlayer human(std::uint8_t slot, std::string name)
{
    return gs::ManifestPlayer{slot, std::move(name), 0x112233};
}

const gs::Actor& actor_on(const gs::Roster& roster, std::uint8_t slot)
{
    static const gs::Actor missing;

    const auto found = std::ranges::find_if(
        roster.actors(),
        [slot](const gs::Actor& actor) { return actor.slot == slot; });

    if (found == roster.actors().end())
    {
        ADD_FAILURE() << "brak aktora na slocie " << int{slot};

        return missing;
    }

    return *found;
}

} // namespace

TEST(RosterTest, FillsEveryFreeSlotWithABot)
{
    const gs::Roster roster = gs::Roster::build({human(3, "Ala")}, max_actors, 42);

    EXPECT_EQ(roster.actors().size(), max_actors);
    EXPECT_EQ(roster.humans(), 1u);
    EXPECT_EQ(roster.bots(), max_actors - 1);

    EXPECT_FALSE(actor_on(roster, 3).is_bot);
    EXPECT_EQ(actor_on(roster, 3).name, "Ala");
    EXPECT_EQ(actor_on(roster, 3).color_rgb, 0x112233u);

    EXPECT_TRUE(actor_on(roster, 4).is_bot);
}

// `MatchInit` idzie do wszystkich tym samym buforem, więc lista posortowana po kolejności
// z manifestu zależałaby od tego, kto pierwszy dołączył do lobby.
TEST(RosterTest, SortsActorsBySlot)
{
    const gs::Roster roster =
        gs::Roster::build({human(9, "Ala"), human(2, "Bob")}, max_actors, 42);

    EXPECT_TRUE(std::ranges::is_sorted(
        roster.actors(),
        [](const gs::Actor& left, const gs::Actor& right) { return left.slot < right.slot; }));

    EXPECT_EQ(roster.actors()[0].slot, 1);
}

TEST(RosterTest, GivesTheSameBotsForTheSameSeed)
{
    const gs::Roster first = gs::Roster::build({}, max_actors, 7);
    const gs::Roster second = gs::Roster::build({}, max_actors, 7);

    for (std::uint8_t slot = 1; slot <= max_actors; ++slot)
    {
        EXPECT_EQ(actor_on(first, slot).name, actor_on(second, slot).name);
        EXPECT_EQ(actor_on(first, slot).color_rgb, actor_on(second, slot).color_rgb);
    }
}

TEST(RosterTest, GivesDifferentBotsForADifferentSeed)
{
    const gs::Roster first = gs::Roster::build({}, max_actors, 7);
    const gs::Roster second = gs::Roster::build({}, max_actors, 8);

    bool anything_differs = false;

    for (std::uint8_t slot = 1; slot <= max_actors; ++slot)
    {
        anything_differs = anything_differs || actor_on(first, slot).name != actor_on(second, slot).name;
    }

    EXPECT_TRUE(anything_differs);
}

/// Sedno konstrukcji: nick i kolor bota zależą **od slotu i ziarna**, a nie od tego, ilu
/// aktorów jest przed nim. Bez tego dołączenie jednego człowieka przemalowałoby cały mecz,
/// a replay sprzed zmiany przestałby się zgadzać.
TEST(RosterTest, AddingAHumanDoesNotChangeTheOtherBots)
{
    const gs::Roster without = gs::Roster::build({}, max_actors, 99);
    const gs::Roster with = gs::Roster::build({human(5, "Ala")}, max_actors, 99);

    for (std::uint8_t slot = 1; slot <= max_actors; ++slot)
    {
        if (slot == 5)
        {
            continue;
        }

        EXPECT_EQ(actor_on(without, slot).name, actor_on(with, slot).name) << "slot " << int{slot};
        EXPECT_EQ(actor_on(without, slot).color_rgb, actor_on(with, slot).color_rgb);
    }
}

// Przy losowaniu niezależnym kolizja nicków byłaby regułą (paradoks dnia urodzin), dlatego
// nicki i kolory idą z permutacji przestrzeni 256 wartości.
TEST(RosterTest, GivesEveryActorADistinctNameAndColour)
{
    const gs::Roster roster = gs::Roster::build({}, 254, 3);

    std::set<std::string> names;
    std::set<std::uint32_t> colours;

    for (const gs::Actor& actor : roster.actors())
    {
        names.insert(actor.name);
        colours.insert(actor.color_rgb);
    }

    EXPECT_EQ(names.size(), roster.actors().size());
    EXPECT_EQ(colours.size(), roster.actors().size());
}

TEST(RosterTest, TurnsHueIntoAColourOnTheFullWheel)
{
    // Dwa punkty, które wypadają dokładnie na wierzchołkach sześciu odcinków.
    EXPECT_EQ(gs::color_from_hue(0), 0xFF0000u);
    EXPECT_EQ(gs::color_from_hue(128), 0x00FFFFu);

    // Koło się zamyka.
    EXPECT_EQ(gs::color_from_hue(256), gs::color_from_hue(0));

    std::set<std::uint32_t> colours;

    for (std::uint32_t hue = 0; hue < 256; ++hue)
    {
        const std::uint32_t colour = gs::color_from_hue(hue);

        colours.insert(colour);

        // Pełne nasycenie i jasność: przynajmniej jeden kanał na maksimum. Kolor bota ma
        // być widoczny na mapie, nie ładny — a przygaszony ginie pod przyciemnieniem terenu.
        const bool saturated = (colour >> 16) == 255 || ((colour >> 8) & 0xFF) == 255
            || (colour & 0xFF) == 255;

        EXPECT_TRUE(saturated) << "odcień " << hue;
    }

    EXPECT_EQ(colours.size(), 256u);
}
