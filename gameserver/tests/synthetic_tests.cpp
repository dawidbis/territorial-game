#include "synthetic.hpp"

#include "convert.hpp"
#include "map/tmap.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>

// Generator zapasowy: mapa z ziarna, dopóki nie powstanie pierwsza narysowana.
//
// Mapy są adresowane sumą kontrolną (D13), więc „to samo ziarno" musi znaczyć „ten sam plik
// co do bajtu" — także między Windowsem a Linuksem. Stąd w generatorze nie ma ani jednej
// operacji zmiennoprzecinkowej, a ten test jest jego jedyną realną gwarancją.
namespace
{

/// Mniejsza niż produkcyjne 2000×1000, żeby zestaw testów nie trwał minuty. Reguły są te
/// same, więc mapa musi przejść tę samą walidację.
gs::tmapgen::SyntheticRequest request(std::uint64_t seed)
{
    gs::tmapgen::SyntheticRequest wanted;

    wanted.id = "synthetic";
    wanted.width = 400;
    wanted.height = 300;
    wanted.max_actors = 12;
    wanted.seed = seed;

    return wanted;
}

std::string bytes_of(const gs::tmapgen::SyntheticRequest& wanted)
{
    const std::expected<gs::tmap::Map, std::string> map = gs::tmapgen::generate(wanted);

    EXPECT_TRUE(map.has_value()) << (map ? "" : map.error());

    if (!map)
    {
        return {};
    }

    const std::expected<std::string, std::string> encoded = gs::tmap::encode(*map);

    EXPECT_TRUE(encoded.has_value()) << (encoded ? "" : encoded.error());

    return encoded.value_or(std::string{});
}

} // namespace

TEST(SyntheticTest, ProducesAMapThatPassesItsOwnValidation)
{
    const std::expected<gs::tmap::Map, std::string> map = gs::tmapgen::generate(request(1));

    ASSERT_TRUE(map.has_value()) << (map ? "" : map.error());

    const std::expected<void, std::string> valid = gs::tmapgen::validate(*map, 12);

    EXPECT_TRUE(valid.has_value()) << (valid ? "" : valid.error());

    EXPECT_EQ(map->spawns.size(), 12u);
    EXPECT_EQ(map->terrain.size(), 400u * 300u);
}

TEST(SyntheticTest, GivesTheSameFileForTheSameSeed)
{
    EXPECT_EQ(bytes_of(request(7)), bytes_of(request(7)));
}

TEST(SyntheticTest, GivesADifferentFileForADifferentSeed)
{
    EXPECT_NE(bytes_of(request(7)), bytes_of(request(8)));
}

// Wszystkie cztery typy terenu muszą wystąpić, inaczej mapa nie ćwiczy tego, po co powstała:
// keyframe'a o realistycznej liczbie runów i tabeli kosztów z trzema progami.
TEST(SyntheticTest, UsesAllFourTerrainTypes)
{
    const std::expected<gs::tmap::Map, std::string> map = gs::tmapgen::generate(request(3));

    ASSERT_TRUE(map.has_value()) << (map ? "" : map.error());

    for (std::uint8_t type = 0; type < gs::tmap::terrain_type_count; ++type)
    {
        EXPECT_NE(std::ranges::find(map->terrain, type), map->terrain.end())
            << "brak terenu o kodzie " << static_cast<int>(type);
    }
}
