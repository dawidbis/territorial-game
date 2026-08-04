#include "sim/world.hpp"

#include "map/map_file.hpp"
#include "map/tmap.hpp"
#include "map_fixtures.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <string>
#include <vector>

// Świat po wczytaniu mapy: woda jako 255 (D12), reszta jako pustkowie, punkty startowe
// przypisane po indeksie slotu.
namespace
{

gs::MapFile load(const gs::tmap::Map& map)
{
    const std::expected<std::string, std::string> bytes = gs::tmap::encode(map);

    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error());

    std::expected<gs::MapFile, std::string> file =
        gs::MapFile::from_bytes(std::vector<std::uint8_t>(bytes->begin(), bytes->end()));

    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error());

    return std::move(*file);
}

} // namespace

TEST(WorldTest, TurnsWaterIntoTheReservedOwnerAndLandIntoWasteland)
{
    const gs::MapFile file = load(fixtures::small_map());

    const gs::World world(file.map());

    ASSERT_EQ(world.owner().size(), 12u);

    EXPECT_EQ(world.owner()[0], gs::tmap::water_owner);
    EXPECT_EQ(world.owner()[5], gs::tmap::wasteland_owner);
    EXPECT_EQ(world.owner()[6], gs::tmap::wasteland_owner);
    EXPECT_EQ(world.land_tiles(), 2u);
}

// Indeks spawnu **jest** indeksem slotu (§3.6 planu), a sloty liczą się od jedynki, bo zero
// jest zarezerwowane na pustkowie. To jedna z tych reguł, które łatwo przesunąć o jeden
// i zauważyć dopiero po tym, jak gracz wyląduje na cudzym punkcie startowym.
TEST(WorldTest, PutsSlotOneOnTheFirstSpawn)
{
    // Plansza z zapasem: przy 52 kafelkach startowych na ciasnej mapie pierwszy postawiony
    // zajmuje punkt startowy drugiego i test mówiłby o czymś innym, niż zamierza.
    const gs::MapFile file = load(fixtures::plains_map(40, 40));

    gs::World world(file.map());

    ASSERT_TRUE(world.place_actor(1));
    ASSERT_TRUE(world.place_actor(2));

    // Spawny fixture'u stoją odsunięte od krawędzi o 8 kafelków.
    EXPECT_EQ(world.owner()[8 * 40 + 8], 1u);
    EXPECT_EQ(world.owner()[31 * 40 + 31], 2u);
}

// Kształt startowy: dysk o promieniu 4 liczony od styku czterech kafelków, czyli sylwetka
// z pierwowzoru. Test pilnuje jej **wiersz po wierszu**, bo sam licznik 52 przepuściłby
// dowolną inną figurę o tej samej powierzchni — a to kształt jest tym, co widać w grze.
TEST(WorldTest, GivesEachActorADiscShapedStartingTerritory)
{
    const gs::MapFile file = load(fixtures::plains_map(40, 40, gs::tmap::Terrain::lowlands,
        {gs::tmap::Spawn{20, 20}}));

    gs::World world(file.map());

    ASSERT_TRUE(world.place_actor(1));

    EXPECT_EQ(world.tiles_of(1), gs::World::starting_tiles);

    std::array<int, 40> per_row{};

    for (std::uint32_t tile = 0; tile < world.tile_count(); ++tile)
    {
        if (world.owner_at(tile) != 1u)
        {
            continue;
        }

        const int dx = static_cast<int>(tile % 40) - 20;
        const int dy = static_cast<int>(tile / 40) - 20;

        // Środek dysku leży na styku kafelków, więc warunek idzie po `d + 0,5` — tu
        // w podwojonych współrzędnych, żeby test nie zależał od zaokrągleń.
        EXPECT_LE((2 * dx + 1) * (2 * dx + 1) + (2 * dy + 1) * (2 * dy + 1), 64);

        ++per_row[static_cast<std::size_t>(tile / 40)];
    }

    // Osiem wierszy o szerokościach 4-6-8-8-8-8-6-4: ścięte rogi i płaskie boki.
    const std::array<int, 8> silhouette{4, 6, 8, 8, 8, 8, 6, 4};

    for (std::size_t index = 0; index < silhouette.size(); ++index)
    {
        EXPECT_EQ(per_row[16 + index], silhouette[index]) << "wiersz y=" << 16 + index;
    }

    EXPECT_EQ(per_row[15], 0);
    EXPECT_EQ(per_row[24], 0);
}

// Brzeg przycina terytorium startowe i to jest w porządku: alternatywą byłoby przesuwanie
// punktu startowego w głąb lądu, czyli rozjazd między mapą a tym, co mówi plik.
TEST(WorldTest, ClipsTheStartingTerritoryAgainstWaterAndEdges)
{
    const gs::MapFile file = load(fixtures::small_map());

    gs::World world(file.map());

    ASSERT_TRUE(world.place_actor(1));

    // Cała mapa ma dwa kafelki lądu i oba wpadają w dysk pierwszego gracza.
    EXPECT_EQ(world.tiles_of(1), 2u);

    // Drugi punkt startowy został przez to zajęty. Odmowa jest głośna świadomie: to znaczy
    // mapę, na której gracze nie mieszczą się obok siebie, a nie stan do przemilczenia.
    EXPECT_FALSE(world.place_actor(2));
}

// `PublicState` idzie co sekundę, a delty będą tysiącami na tik — dlatego terytorium liczy
// się przyrostowo, a nie przez przejście po dwóch milionach kafelków.
TEST(WorldTest, CountsTerritoryWhileItChanges)
{
    const gs::MapFile file = load(fixtures::plains_map(40, 40));

    gs::World world(file.map());

    EXPECT_EQ(world.territory()[gs::tmap::water_owner], 0u);
    EXPECT_EQ(world.territory()[gs::tmap::wasteland_owner], 1600u);

    ASSERT_TRUE(world.place_actor(1));

    EXPECT_EQ(world.territory()[1], gs::World::starting_tiles);
    EXPECT_EQ(world.territory()[gs::tmap::wasteland_owner], 1600u - gs::World::starting_tiles);
}

TEST(WorldTest, RefusesSlotsThatHaveNoSpawn)
{
    const gs::MapFile file = load(fixtures::small_map());

    gs::World world(file.map());

    EXPECT_FALSE(world.place_actor(0));
    EXPECT_FALSE(world.place_actor(3));
    EXPECT_FALSE(world.place_actor(gs::tmap::water_owner));
}

// Hash liczy proces z faktycznie wczytanych bajtów (D13) — inaczej `mapSha256` poświadczałby
// to, co meta *myśli* o pliku, zamiast terenu, na którym mecz się toczy.
TEST(WorldTest, HashesTheFileItActuallyRead)
{
    const gs::MapFile first = load(fixtures::small_map());

    gs::tmap::Map changed = fixtures::small_map();
    changed.terrain[5] = static_cast<std::uint8_t>(gs::tmap::Terrain::mountains);

    const gs::MapFile second = load(changed);

    EXPECT_EQ(first.sha256_hex().size(), 64u);
    EXPECT_NE(first.sha256_hex(), second.sha256_hex());
}
