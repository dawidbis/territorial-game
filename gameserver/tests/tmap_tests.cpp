#include "map/tmap.hpp"

#include "map_fixtures.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Format pliku terenu: co przez niego przechodzi bez zmian i co ma zostać odrzucone.
//
// Każde odrzucenie opisuje plik, który **wczytałby się bez awarii** i zepsuł mecz później:
// nieznany kod terenu wszedłby do tabeli kosztów jako indeks poza tablicą, a spawn na wodzie
// dałby gracza, którego nikt nie może zaatakować.
namespace
{

std::vector<std::uint8_t> encoded(const gs::tmap::Map& map)
{
    const std::expected<std::string, std::string> bytes = gs::tmap::encode(map);

    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error());

    return std::vector<std::uint8_t>(bytes->begin(), bytes->end());
}

} // namespace

TEST(TmapTest, RoundTripsAMapWithoutTouchingIt)
{
    const gs::tmap::Map source = fixtures::small_map();

    const std::vector<std::uint8_t> bytes = encoded(source);

    const std::expected<gs::tmap::MapView, std::string> view = gs::tmap::decode(bytes);

    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error());

    EXPECT_EQ(view->id, source.id);
    EXPECT_EQ(view->width, source.width);
    EXPECT_EQ(view->height, source.height);
    ASSERT_EQ(view->spawns.size(), source.spawns.size());
    EXPECT_EQ(view->spawns[0].x, 1u);
    EXPECT_EQ(view->spawns[0].y, 1u);
    EXPECT_TRUE(std::equal(
        view->terrain.begin(),
        view->terrain.end(),
        source.terrain.begin(),
        source.terrain.end()));
}

// Teren zaczyna się dokładnie tam, gdzie mówi nagłówek — od tego zależy jednolinijkowy
// odczyt po stronie przeglądarki (D13).
TEST(TmapTest, PutsTerrainWhereTheHeaderPromises)
{
    const gs::tmap::Map source = fixtures::small_map();

    const std::vector<std::uint8_t> bytes = encoded(source);

    const std::size_t expected_offset =
        gs::tmap::header_size + source.id.size() + source.spawns.size() * 4;

    ASSERT_EQ(bytes.size(), expected_offset + source.terrain.size());
    EXPECT_EQ(bytes[expected_offset + 5], source.terrain[5]);
}

TEST(TmapTest, RefusesAFileWithoutTheSignature)
{
    std::vector<std::uint8_t> bytes = encoded(fixtures::small_map());
    bytes[1] = 'X';

    EXPECT_FALSE(gs::tmap::decode(bytes).has_value());
}

TEST(TmapTest, RefusesAFileFromAnotherFormatVersion)
{
    std::vector<std::uint8_t> bytes = encoded(fixtures::small_map());
    bytes[4] = static_cast<std::uint8_t>(gs::tmap::format_version + 1);

    EXPECT_FALSE(gs::tmap::decode(bytes).has_value());
}

// Plik urwany przy kopiowaniu ma paść przy wczytaniu, a nie przy pierwszym odczycie kafelka
// spoza tablicy.
TEST(TmapTest, RefusesAFileShorterThanItsHeaderPromises)
{
    std::vector<std::uint8_t> bytes = encoded(fixtures::small_map());
    bytes.pop_back();

    EXPECT_FALSE(gs::tmap::decode(bytes).has_value());
}

TEST(TmapTest, RefusesAnUnknownTerrainCode)
{
    const gs::tmap::Map source = fixtures::small_map();

    std::vector<std::uint8_t> bytes = encoded(source);

    const std::size_t terrain_offset =
        gs::tmap::header_size + source.id.size() + source.spawns.size() * 4;

    bytes[terrain_offset] = gs::tmap::terrain_type_count;

    EXPECT_FALSE(gs::tmap::decode(bytes).has_value());
}

TEST(TmapTest, RefusesASpawnOnWater)
{
    gs::tmap::Map source = fixtures::small_map();
    source.spawns[0] = gs::tmap::Spawn{0, 0};

    EXPECT_FALSE(gs::tmap::decode(encoded(source)).has_value());
}

TEST(TmapTest, RefusesASpawnOutsideTheMap)
{
    gs::tmap::Map source = fixtures::small_map();
    source.spawns[0] = gs::tmap::Spawn{4, 1};

    EXPECT_FALSE(gs::tmap::decode(encoded(source)).has_value());
}

// Kodowanie sprawdza wyłącznie to, czego format fizycznie nie uniesie — reszta należy
// do walidacji w `tmapgen`.
TEST(TmapTest, RefusesToEncodeAGridThatDoesNotMatchTheDimensions)
{
    gs::tmap::Map source = fixtures::small_map();
    source.terrain.pop_back();

    EXPECT_FALSE(gs::tmap::encode(source).has_value());
}
