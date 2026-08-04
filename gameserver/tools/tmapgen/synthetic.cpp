#include "synthetic.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <numeric>
#include <vector>

namespace gs::tmapgen
{
namespace
{

/// SplitMix64 — trzy linie, rozkład dobry na tyle, na ile potrzeba do rozrzucenia wysokości.
/// To **nie jest** RNG symulacji: tamten (PCG, §3.8 planu) jest jedynym źródłem losowości
/// w procesie meczu i nie ma prawa dzielić stanu z narzędziem.
std::uint64_t mix(std::uint64_t value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;

    return value ^ (value >> 31);
}

std::uint32_t lattice_value(std::uint64_t seed, std::uint32_t octave, std::int64_t x, std::int64_t y)
{
    const std::uint64_t hashed = mix(
        seed ^ (static_cast<std::uint64_t>(octave) << 56)
        ^ (static_cast<std::uint64_t>(x) * 0x100000001B3ULL)
        ^ (static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL));

    return static_cast<std::uint32_t>(hashed & 0xFFFF);
}

/// Wygładzenie 3t²−2t³ w stałym przecinku 16.16 — ten sam kształt co `smoothstep`, bez
/// ani jednej operacji zmiennoprzecinkowej.
std::int64_t smooth(std::int64_t t)
{
    // Trójka i szóstka jako `std::int64_t`: mnożenie w `int` przepełniłoby się przy dużych
    // `t`, a rozszerzenie do 64 bitów następowałoby dopiero po nim — czyli na już zepsutej
    // wartości. Determinizm generatora stoi na tym, że ta arytmetyka nie wychodzi z zakresu.
    constexpr std::int64_t one = 65536;

    return (t * t * (3 * one - 2 * t)) >> 32;
}

std::int64_t lerp(std::int64_t from, std::int64_t to, std::int64_t weight)
{
    return from + (((to - from) * weight) >> 16);
}

/// Suma trzech oktaw szumu wartościowego, przycięta lejkiem przy krawędziach.
///
/// Lejek nie jest kosmetyką: bez niego ląd dochodzi do brzegu mapy i rozpada się na osobne
/// kawałki przy pierwszym progowaniu, a wtedy „jeden kontynent" trzeba by wymuszać topieniem
/// połowy mapy.
std::vector<std::uint16_t> elevation_field(const SyntheticRequest& request)
{
    constexpr std::array<std::uint32_t, 3> cell_sizes{256, 64, 16};
    constexpr std::array<std::int64_t, 3> weights{4, 2, 1};

    const std::int64_t total_weight = std::accumulate(weights.begin(), weights.end(), std::int64_t{0});

    // Wymiary jako `uint32_t`, żeby w warunkach pętli stały obok liczników tego samego typu.
    // `uint16_t` awansuje w porównaniu do `int`, czyli do typu ze znakiem.
    const std::uint32_t width = request.width;
    const std::uint32_t height = request.height;

    std::vector<std::uint16_t> elevation(static_cast<std::size_t>(width) * height);

    const std::int64_t margin = std::min(width, height) / 6;

    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            std::int64_t sum = 0;

            for (std::uint32_t octave = 0; octave < cell_sizes.size(); ++octave)
            {
                const std::uint32_t cell = cell_sizes[octave];

                const std::int64_t cx = x / cell;
                const std::int64_t cy = y / cell;

                const std::int64_t tx = smooth(static_cast<std::int64_t>(x % cell) * 65536 / cell);
                const std::int64_t ty = smooth(static_cast<std::int64_t>(y % cell) * 65536 / cell);

                const std::int64_t top = lerp(
                    lattice_value(request.seed, octave, cx, cy),
                    lattice_value(request.seed, octave, cx + 1, cy),
                    tx);

                const std::int64_t bottom = lerp(
                    lattice_value(request.seed, octave, cx, cy + 1),
                    lattice_value(request.seed, octave, cx + 1, cy + 1),
                    tx);

                sum += weights[octave] * lerp(top, bottom, ty);
            }

            std::int64_t value = sum / total_weight;

            // Odległość do najbliższej krawędzi, przycięta do marginesu i przeskalowana
            // do 0..65536 — mnożnik, nie odejmowanie, żeby środek mapy został nietknięty.
            const std::int64_t distance = std::min(
                {static_cast<std::int64_t>(x),
                 static_cast<std::int64_t>(width - 1 - x),
                 static_cast<std::int64_t>(y),
                 static_cast<std::int64_t>(height - 1 - y)});

            if (margin > 0 && distance < margin)
            {
                value = (value * smooth(distance * 65536 / margin)) >> 16;
            }

            elevation[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint16_t>(std::clamp<std::int64_t>(value, 0, 65535));
        }
    }

    return elevation;
}

/// Najniższa wysokość, powyżej której leży mniej więcej `share` (w promilach) kafelków.
std::uint16_t quantile(const std::vector<std::uint16_t>& values, std::uint32_t share_per_mille)
{
    // Na stercie, nie na stosie: 65536 pozycji po osiem bajtów to pół megabajta, czyli
    // połowa domyślnego stosu wątku na Windowsie.
    std::vector<std::size_t> histogram(65536, 0);

    for (const std::uint16_t value : values)
    {
        ++histogram[value];
    }

    const std::size_t wanted = values.size() * share_per_mille / 1000;

    std::size_t above = 0;

    for (std::size_t level = 65536; level-- > 0;)
    {
        above += histogram[level];

        if (above >= wanted)
        {
            return static_cast<std::uint16_t>(level);
        }
    }

    return 0;
}

/// Topi wszystko poza największym spójnym obszarem lądu i oddaje jego rozmiar.
///
/// Bez tego mapa syntetyczna ma kilkadziesiąt wysepek, a walidacja „spawn poza głównym
/// kontynentem" odrzucałaby własny wynik generatora.
std::size_t keep_largest_continent(
    std::vector<std::uint8_t>& land,
    std::size_t width,
    std::size_t height)
{
    std::vector<std::int32_t> label(land.size(), -1);
    std::vector<std::size_t> sizes;
    std::vector<std::size_t> stack;

    for (std::size_t start = 0; start < land.size(); ++start)
    {
        if (land[start] == 0 || label[start] >= 0)
        {
            continue;
        }

        const std::int32_t current = static_cast<std::int32_t>(sizes.size());

        std::size_t size = 0;

        stack.push_back(start);
        label[start] = current;

        while (!stack.empty())
        {
            const std::size_t index = stack.back();
            stack.pop_back();

            ++size;

            const std::size_t x = index % width;
            const std::size_t y = index / width;

            const std::array<bool, 4> exists{x > 0, x + 1 < width, y > 0, y + 1 < height};
            const std::array<std::size_t, 4> neighbours{
                index - 1,
                index + 1,
                index - width,
                index + width};

            for (std::size_t side = 0; side < 4; ++side)
            {
                if (!exists[side] || land[neighbours[side]] == 0 || label[neighbours[side]] >= 0)
                {
                    continue;
                }

                label[neighbours[side]] = current;

                stack.push_back(neighbours[side]);
            }
        }

        sizes.push_back(size);
    }

    if (sizes.empty())
    {
        return 0;
    }

    const std::int32_t largest = static_cast<std::int32_t>(
        std::ranges::distance(sizes.begin(), std::ranges::max_element(sizes)));

    for (std::size_t index = 0; index < land.size(); ++index)
    {
        if (label[index] != largest)
        {
            land[index] = 0;
        }
    }

    return sizes[static_cast<std::size_t>(largest)];
}

std::uint64_t greatest_common_divisor(std::uint64_t left, std::uint64_t right)
{
    while (right != 0)
    {
        const std::uint64_t rest = left % right;

        left = right;
        right = rest;
    }

    return left;
}

/// Rozstawia punkty startowe po kontynencie, trzymając je z dala od siebie.
///
/// Kandydaci przeglądani są krokiem względnie pierwszym z rozmiarem listy — to daje
/// deterministyczny objazd całej listy bez tasowania miliona indeksów i bez ani jednego
/// powtórzenia. Minimalny dystans spada, dopóki nie uda się postawić wszystkich: lepszy
/// ciasny rozstaw niż mapa, której nie da się użyć.
std::vector<tmap::Spawn> place_spawns(
    const std::vector<std::size_t>& candidates,
    std::size_t width,
    std::uint32_t count,
    std::uint64_t seed)
{
    std::vector<tmap::Spawn> spawns;

    if (candidates.empty() || count == 0)
    {
        return spawns;
    }

    std::uint64_t step = mix(seed) % candidates.size();

    if (step < candidates.size() / 4)
    {
        step += candidates.size() / 4;
    }

    while (greatest_common_divisor(step, candidates.size()) != 1)
    {
        ++step;
    }

    const std::int64_t area_per_actor = static_cast<std::int64_t>(
        std::max<std::size_t>(1, candidates.size() / count));

    // Odległość liniowa, a nie powierzchniowa: pierwiastek z pola przypadającego na gracza.
    std::int64_t min_distance = 1;

    while (min_distance * min_distance < area_per_actor)
    {
        ++min_distance;
    }

    for (std::uint64_t attempt = 0; attempt < 8 && spawns.size() < count; ++attempt)
    {
        spawns.clear();

        std::size_t index = mix(seed + attempt) % candidates.size();

        for (std::size_t visited = 0; visited < candidates.size() && spawns.size() < count;
             ++visited)
        {
            const std::size_t tile = candidates[index];

            index = (index + step) % candidates.size();

            const std::int64_t x = static_cast<std::int64_t>(tile % width);
            const std::int64_t y = static_cast<std::int64_t>(tile / width);

            const bool crowded = std::ranges::any_of(
                spawns,
                [&](const tmap::Spawn& other)
                {
                    const std::int64_t dx = x - other.x;
                    const std::int64_t dy = y - other.y;

                    return dx * dx + dy * dy < min_distance * min_distance;
                });

            if (!crowded)
            {
                spawns.push_back(
                    tmap::Spawn{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
            }
        }

        min_distance = std::max<std::int64_t>(1, min_distance * 2 / 3);
    }

    return spawns;
}

} // namespace

std::expected<tmap::Map, std::string> generate(const SyntheticRequest& request)
{
    if (request.width == 0 || request.height == 0)
    {
        return std::unexpected(
            std::format("Mapa ma mieć wymiary {}×{}.", request.width, request.height));
    }

    const std::vector<std::uint16_t> elevation = elevation_field(request);

    // Progowanie z zapasem: część lądu przepadnie przy topieniu wysp, więc celujemy powyżej
    // środka dopuszczalnego przedziału i schodzimy, dopóki wynik w nim nie wyląduje.
    std::vector<std::uint8_t> land;
    std::size_t continent = 0;

    for (std::uint32_t target = 560; target <= 700; target += 20)
    {
        const std::uint16_t threshold = quantile(elevation, target);

        land.assign(elevation.size(), 0);

        for (std::size_t index = 0; index < elevation.size(); ++index)
        {
            land[index] = elevation[index] >= threshold ? 1u : 0u;
        }

        continent = keep_largest_continent(land, request.width, request.height);

        const std::uint32_t share = static_cast<std::uint32_t>(continent * 1000 / land.size());

        if (share >= 430 && share <= 570)
        {
            break;
        }
    }

    if (continent == 0)
    {
        return std::unexpected(std::string{"Generator nie znalazł ani jednego kafelka lądu."});
    }

    // Typ terenu z wysokości: te same progi dla całej mapy, żeby góry stały tam, gdzie
    // szum jest najwyższy, a nie tam, gdzie akurat wypadła granica kontynentu.
    std::vector<std::uint16_t> land_elevation;
    land_elevation.reserve(continent);

    std::vector<std::size_t> candidates;
    candidates.reserve(continent);

    for (std::size_t index = 0; index < land.size(); ++index)
    {
        if (land[index] != 0)
        {
            land_elevation.push_back(elevation[index]);
            candidates.push_back(index);
        }
    }

    const std::uint16_t highlands_from = quantile(land_elevation, 400);
    const std::uint16_t mountains_from = quantile(land_elevation, 120);

    tmap::Map map;
    map.id = request.id;
    map.width = request.width;
    map.height = request.height;
    map.terrain.resize(land.size());

    for (std::size_t index = 0; index < land.size(); ++index)
    {
        if (land[index] == 0)
        {
            map.terrain[index] = static_cast<std::uint8_t>(tmap::Terrain::water);

            continue;
        }

        const std::uint16_t value = elevation[index];

        // Trzy progi wysokości jako `if`, a nie jako warunek w warunku: zagnieżdżony operator
        // trójargumentowy czyta się tu jak jedno wyrażenie o dwóch znaczeniach naraz.
        tmap::Terrain terrain = tmap::Terrain::lowlands;

        if (value >= mountains_from)
        {
            terrain = tmap::Terrain::mountains;
        }
        else if (value >= highlands_from)
        {
            terrain = tmap::Terrain::highlands;
        }

        map.terrain[index] = static_cast<std::uint8_t>(terrain);
    }

    map.spawns = place_spawns(candidates, request.width, request.max_actors, request.seed);

    if (map.spawns.size() < request.max_actors)
    {
        return std::unexpected(std::format(
            "Generator postawił {} punktów startowych z {} — kontynent o {} kafelkach jest na "
            "tylu aktorów za mały.",
            map.spawns.size(),
            request.max_actors,
            continent));
    }

    return map;
}

} // namespace gs::tmapgen
