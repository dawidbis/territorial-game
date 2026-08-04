#include "convert.hpp"

#include "map/tmap.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Konwersja pary plików źródłowych i walidacja mapy.
//
// Walidacja jest tu najważniejsza: każda z tych reguł opisuje mapę, która **wczytuje się bez
// problemu**, a psuje mecz w dwunastej minucie — i wygląda wtedy jak błąd symulacji.
namespace
{

constexpr std::uint8_t water = static_cast<std::uint8_t>(gs::tmap::Terrain::water);
constexpr std::uint8_t lowlands = static_cast<std::uint8_t>(gs::tmap::Terrain::lowlands);

/// Mapa 10×10 z lądem w lewej połowie: 50% lądu, czyli w środku dopuszczalnego przedziału.
gs::tmap::Map half_land_map()
{
    gs::tmap::Map map;

    map.id = "test";
    map.width = 10;
    map.height = 10;
    map.terrain.assign(100, water);

    for (std::uint16_t y = 0; y < 10; ++y)
    {
        for (std::uint16_t x = 0; x < 5; ++x)
        {
            map.terrain[y * 10u + x] = lowlands;
        }
    }

    map.spawns = {gs::tmap::Spawn{0, 0}, gs::tmap::Spawn{4, 9}};

    return map;
}

gs::png::Image single_pixel(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    gs::png::Image image;

    image.width = 1;
    image.height = 1;
    image.rgb = {red, green, blue};

    return image;
}

} // namespace

TEST(ConvertTest, MapsEverySourceColourToItsTerrain)
{
    for (const gs::tmapgen::TerrainColour& colour : gs::tmapgen::terrain_palette())
    {
        const std::expected<std::vector<std::uint8_t>, std::string> terrain =
            gs::tmapgen::terrain_from_image(single_pixel(colour.red, colour.green, colour.blue));

        ASSERT_TRUE(terrain.has_value()) << (terrain ? "" : terrain.error());
        ASSERT_EQ(terrain->size(), 1u);
        EXPECT_EQ((*terrain)[0], static_cast<std::uint8_t>(colour.terrain));
    }
}

// Cicha interpretacja zamieniłaby literówkę w odcieniu w wyspę tam, gdzie miała być woda.
TEST(ConvertTest, RefusesAColourThatIsNotInThePalette)
{
    EXPECT_FALSE(gs::tmapgen::terrain_from_image(single_pixel(0x00, 0x00, 0xFE)).has_value());
}

TEST(ConvertTest, ReadsTheDescriptionFile)
{
    const std::expected<gs::tmapgen::Metadata, std::string> metadata = gs::tmapgen::parse_metadata(
        R"({"id":"moon","name":"Moon","maxActors":2,"spawns":[[412,233],[1620,880]]})");

    ASSERT_TRUE(metadata.has_value()) << (metadata ? "" : metadata.error());

    EXPECT_EQ(metadata->id, "moon");
    EXPECT_EQ(metadata->name, "Moon");
    EXPECT_EQ(metadata->max_actors, 2u);
    ASSERT_EQ(metadata->spawns.size(), 2u);
    EXPECT_EQ(metadata->spawns[1].x, 1620u);
    EXPECT_EQ(metadata->spawns[1].y, 880u);
}

TEST(ConvertTest, RefusesADescriptionWithAMissingField)
{
    EXPECT_FALSE(gs::tmapgen::parse_metadata(R"({"id":"moon","maxActors":2,"spawns":[]})")
                     .has_value());
}

TEST(ConvertTest, RefusesMoreActorsThanAMatchHolds)
{
    EXPECT_FALSE(
        gs::tmapgen::parse_metadata(R"({"id":"m","name":"M","maxActors":255,"spawns":[]})")
            .has_value());
}

TEST(ConvertTest, AcceptsAMapThatBreaksNoRule)
{
    EXPECT_TRUE(gs::tmapgen::validate(half_land_map(), 2).has_value());
}

// Boty startują dokładnie tak samo jak ludzie, więc spawnów musi być co najmniej tylu,
// ilu aktorów mieści mecz.
TEST(ConvertTest, RefusesFewerSpawnsThanActors)
{
    EXPECT_FALSE(gs::tmapgen::validate(half_land_map(), 3).has_value());
}

TEST(ConvertTest, RefusesAMapThatIsMostlyLand)
{
    gs::tmap::Map map = half_land_map();
    map.terrain.assign(100, lowlands);

    EXPECT_FALSE(gs::tmapgen::validate(map, 2).has_value());
}

TEST(ConvertTest, RefusesAMapThatIsMostlyWater)
{
    gs::tmap::Map map = half_land_map();

    for (std::uint16_t y = 0; y < 10; ++y)
    {
        for (std::uint16_t x = 2; x < 5; ++x)
        {
            map.terrain[y * 10u + x] = water;
        }
    }

    EXPECT_FALSE(gs::tmapgen::validate(map, 2).has_value());
}

// Wyspa bez połączenia z głównym lądem to gracz, którego nikt nie zaatakuje i który sam
// nigdzie nie wyjdzie — czyli slot wycięty z meczu, ale liczony do wyniku.
TEST(ConvertTest, RefusesASpawnOutsideTheMainContinent)
{
    gs::tmap::Map map = half_land_map();

    // Wysepka po drugiej stronie mapy, oddzielona od kontynentu pasem wody.
    map.terrain[9 * 10u + 9] = lowlands;
    map.spawns[1] = gs::tmap::Spawn{9, 9};

    EXPECT_FALSE(gs::tmapgen::validate(map, 2).has_value());
}

TEST(ConvertTest, RefusesASpawnOnWater)
{
    gs::tmap::Map map = half_land_map();
    map.spawns[1] = gs::tmap::Spawn{9, 0};

    EXPECT_FALSE(gs::tmapgen::validate(map, 2).has_value());
}

// Kafelek ma jednego właściciela (D12), więc dwóch aktorów na jednym punkcie startowym
// znaczy, że jeden z nich zniknie z planszy — i wygląda to jak bot, który nic nie robi.
TEST(ConvertTest, RefusesTwoSpawnsOnTheSameTile)
{
    gs::tmap::Map map = half_land_map();
    map.spawns[1] = map.spawns[0];

    EXPECT_FALSE(gs::tmapgen::validate(map, 2).has_value());
}
