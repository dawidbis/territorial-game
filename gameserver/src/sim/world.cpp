#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace gs
{
namespace
{

/// Promień dysku startowego w kafelkach.
///
/// Cztery, jak w pierwowzorze (`euclDistFN(tile, 4, true)`). Kluczowe jest to `true`: warunek
/// liczy odległość od punktu przesuniętego o **pół kafelka**, więc dysk wychodzi parzysty —
/// osiem pól w najszerszym miejscu, ścięte rogi, żadnej osi symetrii przez środek kafelka.
/// Nieparzysty wariant dałby siedem pól i inny kształt narożnika.
constexpr int start_radius = 4;

struct Offset
{
    int dx = 0;
    int dy = 0;
};

/// Kwadrat odległości kafelka od środka dysku przesuniętego o pół pola.
///
/// Liczony w podwojonych współrzędnych (`2·d + 1`), więc całą arytmetykę robimy na `int`
/// i kształt nie zależy od tego, jak kompilator zaokrągli `double`. Warunek dysku
/// `(d + 0,5)² ≤ r²` jest tu równoważny `(2d + 1)² ≤ (2r)²`.
constexpr int centred_dist2(Offset offset) noexcept
{
    const int x = 2 * offset.dx + 1;
    const int y = 2 * offset.dy + 1;

    return x * x + y * y;
}

/// Kolejność dobierania kafelków startowych.
///
/// Dysk rosnący od środka: najbliższe kafelki pierwsze. Ma to znaczenie tylko wtedy, gdy
/// część kandydatów odpada — spawn przy brzegu traci wtedy **skraj dysku**, a nie losowe
/// pola ze środka, więc terytorium zostaje spójne i wygląda jak przycięte koło.
const std::vector<Offset>& start_offsets()
{
    static const std::vector<Offset> offsets = []
    {
        std::vector<Offset> all;

        // Zakres niesymetryczny, bo środek dysku leży na styku czterech kafelków: przy
        // promieniu 4 to kolumny i wiersze od -4 do 3, razem osiem na osiem minus rogi.
        for (int dy = -start_radius; dy < start_radius; ++dy)
        {
            for (int dx = -start_radius; dx < start_radius; ++dx)
            {
                const Offset offset{dx, dy};

                if (centred_dist2(offset) <= 4 * start_radius * start_radius)
                {
                    all.push_back(offset);
                }
            }
        }

        std::ranges::sort(
            all,
            [](Offset left, Offset right)
            {
                const int near_left = centred_dist2(left);
                const int near_right = centred_dist2(right);

                if (near_left != near_right)
                {
                    return near_left < near_right;
                }

                // Domknięcie porządku, żeby kolejność nie zależała od implementacji
                // sortowania — kształt startowy ma być funkcją mapy, nie kompilatora.
                return left.dy != right.dy ? left.dy < right.dy : left.dx < right.dx;
            });

        return all;
    }();

    return offsets;
}

} // namespace

World::World(const tmap::MapView& map)
    : terrain_(map.terrain)
    , spawns_(map.spawns)
    , width_(map.width)
    , height_(map.height)
{
    owner_.resize(map.tile_count());
    changed_mark_.assign(map.tile_count(), false);

    for (std::size_t index = 0; index < owner_.size(); ++index)
    {
        const bool water = map.terrain[index] == static_cast<std::uint8_t>(tmap::Terrain::water);

        owner_[index] = water ? tmap::water_owner : tmap::wasteland_owner;

        land_tiles_ += water ? 0u : 1u;
    }

    territory_[tmap::water_owner] = static_cast<std::uint32_t>(owner_.size() - land_tiles_);
    territory_[tmap::wasteland_owner] = static_cast<std::uint32_t>(land_tiles_);
}

std::uint32_t World::neighbors4(std::uint32_t tile, std::array<std::uint32_t, 4>& out)
    const noexcept
{
    const std::uint32_t x = tile % width_;
    const std::uint32_t y = tile / width_;

    std::uint32_t count = 0;

    if (x > 0)
    {
        out[count++] = tile - 1;
    }

    if (x + 1 < width_)
    {
        out[count++] = tile + 1;
    }

    if (y > 0)
    {
        out[count++] = tile - width_;
    }

    if (y + 1 < height_)
    {
        out[count++] = tile + width_;
    }

    return count;
}

void World::set_owner(std::uint32_t tile, std::uint8_t slot)
{
    if (owner_[tile] == slot)
    {
        return;
    }

    assign_owner(tile, slot);

    if (!changed_mark_[tile])
    {
        changed_mark_[tile] = true;
        changed_.push_back(tile);
    }
}

void World::clear_changed() noexcept
{
    for (const std::uint32_t tile : changed_)
    {
        changed_mark_[tile] = false;
    }

    changed_.clear();
}

void World::assign_owner(std::uint32_t tile, std::uint8_t slot) noexcept
{
    --territory_[owner_[tile]];
    ++territory_[slot];

    owner_[tile] = slot;
}

bool World::place_actor(std::uint8_t slot)
{
    if (slot == tmap::wasteland_owner || slot == tmap::water_owner
        || static_cast<std::size_t>(slot) > spawns_.size())
    {
        return false;
    }

    // Spawny są indeksowane od zera, sloty od jedynki — zero jest zarezerwowane na
    // pustkowie (D12), więc slot 1 startuje na pierwszym punkcie z pliku.
    const tmap::Spawn spawn = spawns_[slot - 1u];

    const std::size_t centre = static_cast<std::size_t>(spawn.y) * width_ + spawn.x;

    // Dwóch aktorów na jednym kafelku znaczyłoby dwa spawny w tym samym punkcie — mapa
    // przechodzi walidację, a mecz zaczyna się od gracza, którego nie ma na planszy.
    if (owner_[centre] != tmap::wasteland_owner)
    {
        return false;
    }

    std::uint32_t claimed = 0;

    for (const Offset offset : start_offsets())
    {
        const int x = static_cast<int>(spawn.x) + offset.dx;
        const int y = static_cast<int>(spawn.y) + offset.dy;

        // Bez zawijania: dysk przy krawędzi mapy jest przycięty, a nie przeniesiony na
        // drugą stronę świata.
        if (x < 0 || y < 0 || std::cmp_greater_equal(x, width_)
            || std::cmp_greater_equal(y, height_))
        {
            continue;
        }

        const std::uint32_t tile =
            static_cast<std::uint32_t>(y) * width_ + static_cast<std::uint32_t>(x);

        // Woda i cudze terytorium odpadają tym samym warunkiem — jedno i drugie ma
        // w `owner_` wartość różną od pustkowia (D12).
        if (owner_[tile] != tmap::wasteland_owner)
        {
            continue;
        }

        assign_owner(tile, slot);

        if (++claimed == starting_tiles)
        {
            break;
        }
    }

    return true;
}

} // namespace gs
