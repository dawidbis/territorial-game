#include "convert.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <sstream>
#include <utility>
#include <vector>

namespace gs::tmapgen
{
namespace
{

constexpr std::array<TerrainColour, 4> palette{
    TerrainColour{0x00, 0x00, 0xFF, tmap::Terrain::water},
    TerrainColour{0x00, 0xFF, 0x00, tmap::Terrain::lowlands},
    TerrainColour{0xFF, 0xFF, 0x00, tmap::Terrain::highlands},
    TerrainColour{0x80, 0x80, 0x80, tmap::Terrain::mountains},
};

/// Przy dwóch milionach kafelków i 254 aktorach to jest ~4–5 tysięcy kafelków na gracza.
/// Mapa w 80% lądowa daje mecz bez linii brzegowej, a w 20% — sto wysepek bez sąsiadów.
constexpr double min_land_share = 0.40;
constexpr double max_land_share = 0.60;

std::string to_hex(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    return std::format("#{:02X}{:02X}{:02X}", red, green, blue);
}

/// Numeruje spójne obszary lądu i oddaje etykiety oraz numer największego z nich.
///
/// Sąsiedztwo czteropolowe, bo takie ma atak: idzie wyłącznie wzdłuż granicy lądowej,
/// a nie po przekątnej. Stos jawny zamiast rekurencji — przy dwóch milionach kafelków
/// wersja rekurencyjna przepełnia stos na pierwszym kontynencie.
struct Continents
{
    std::vector<std::int32_t> label;

    std::int32_t largest = -1;

    std::size_t largest_size = 0;
};

Continents find_continents(const tmap::Map& map)
{
    const std::size_t tiles = map.terrain.size();

    // Wymiary jako `size_t`, bo indeksy kafelków też nimi są. `uint16_t` w porównaniu
    // awansuje do `int`, więc każde `x + 1 < map.width` mieszałoby typ ze znakiem z typem
    // bez znaku — zależnie od kompilatora i flag to jest ostrzeżenie albo cisza.
    const std::size_t width = map.width;
    const std::size_t height = map.height;

    Continents result;
    result.label.assign(tiles, -1);

    std::vector<std::size_t> stack;

    std::int32_t next_label = 0;

    for (std::size_t start = 0; start < tiles; ++start)
    {
        if (map.terrain[start] == static_cast<std::uint8_t>(tmap::Terrain::water)
            || result.label[start] >= 0)
        {
            continue;
        }

        const std::int32_t label = next_label++;

        std::size_t size = 0;

        stack.push_back(start);
        result.label[start] = label;

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
                if (!exists[side])
                {
                    continue;
                }

                const std::size_t neighbour = neighbours[side];

                if (result.label[neighbour] >= 0
                    || map.terrain[neighbour] == static_cast<std::uint8_t>(tmap::Terrain::water))
                {
                    continue;
                }

                result.label[neighbour] = label;

                stack.push_back(neighbour);
            }
        }

        if (size > result.largest_size)
        {
            result.largest_size = size;
            result.largest = label;
        }
    }

    return result;
}

} // namespace

std::span<const TerrainColour> terrain_palette()
{
    return palette;
}

std::expected<Metadata, std::string> parse_metadata(std::string_view json)
{
    boost::system::error_code error;

    const boost::json::value parsed = boost::json::parse(json, error);

    if (error)
    {
        return std::unexpected(std::format("Plik opisu nie jest poprawnym JSON-em: {}.", error.message()));
    }

    if (!parsed.is_object())
    {
        return std::unexpected(std::string{"Plik opisu musi być obiektem JSON."});
    }

    const boost::json::object& root = parsed.get_object();

    Metadata metadata;

    for (const std::string_view field : {"id", "name", "maxActors", "spawns"})
    {
        if (!root.contains(field))
        {
            return std::unexpected(std::format("W pliku opisu brakuje pola '{}'.", field));
        }
    }

    if (!root.at("id").is_string() || !root.at("name").is_string())
    {
        return std::unexpected(std::string{"Pola 'id' i 'name' muszą być tekstem."});
    }

    metadata.id = root.at("id").get_string().c_str();
    metadata.name = root.at("name").get_string().c_str();

    if (metadata.id.empty())
    {
        return std::unexpected(std::string{"Pole 'id' jest puste."});
    }

    if (!root.at("maxActors").is_int64())
    {
        return std::unexpected(std::string{"Pole 'maxActors' musi być liczbą całkowitą."});
    }

    const std::int64_t max_actors = root.at("maxActors").get_int64();

    if (max_actors < 1 || max_actors > 254)
    {
        return std::unexpected(std::format(
            "Pole 'maxActors' ma wartość {}, a mecz mieści 1..254 aktorów (D12).",
            max_actors));
    }

    metadata.max_actors = static_cast<std::uint32_t>(max_actors);

    if (!root.at("spawns").is_array())
    {
        return std::unexpected(std::string{"Pole 'spawns' musi być tablicą par [x, y]."});
    }

    const boost::json::array& spawns = root.at("spawns").get_array();

    metadata.spawns.reserve(spawns.size());

    for (std::size_t index = 0; index < spawns.size(); ++index)
    {
        const boost::json::value& entry = spawns[index];

        if (!entry.is_array() || entry.get_array().size() != 2 || !entry.get_array()[0].is_int64()
            || !entry.get_array()[1].is_int64())
        {
            return std::unexpected(
                std::format("Punkt startowy {} nie jest parą liczb [x, y].", index));
        }

        const std::int64_t x = entry.get_array()[0].get_int64();
        const std::int64_t y = entry.get_array()[1].get_int64();

        if (x < 0 || y < 0 || std::cmp_greater(x, tmap::max_dimension)
            || std::cmp_greater(y, tmap::max_dimension))
        {
            return std::unexpected(std::format(
                "Punkt startowy {} stoi na [{}, {}], a współrzędne mieszczą się w 0..{}.",
                index,
                x,
                y,
                tmap::max_dimension));
        }

        metadata.spawns.push_back(
            tmap::Spawn{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
    }

    return metadata;
}

std::expected<Metadata, std::string> read_metadata_file(const std::filesystem::path& path)
{
    const std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected(
            std::format("Nie udało się otworzyć pliku opisu '{}'.", path.string()));
    }

    std::ostringstream content;
    content << file.rdbuf();

    std::expected<Metadata, std::string> metadata = parse_metadata(content.str());

    if (!metadata)
    {
        return std::unexpected(std::format("Plik opisu '{}': {}", path.string(), metadata.error()));
    }

    return metadata;
}

std::expected<std::vector<std::uint8_t>, std::string> terrain_from_image(const png::Image& image)
{
    if (image.width > tmap::max_dimension || image.height > tmap::max_dimension)
    {
        return std::unexpected(std::format(
            "Obrazek ma {}×{} pikseli, a format mieści mapy do {}×{}.",
            image.width,
            image.height,
            tmap::max_dimension,
            tmap::max_dimension));
    }

    std::vector<std::uint8_t> terrain(static_cast<std::size_t>(image.width) * image.height);

    for (std::size_t index = 0; index < terrain.size(); ++index)
    {
        const std::uint8_t red = image.rgb[index * 3];
        const std::uint8_t green = image.rgb[index * 3 + 1];
        const std::uint8_t blue = image.rgb[index * 3 + 2];

        const auto match = std::ranges::find_if(
            palette,
            [red, green, blue](const TerrainColour& colour)
            { return colour.red == red && colour.green == green && colour.blue == blue; });

        if (match == palette.end())
        {
            std::string known;

            for (const TerrainColour& colour : palette)
            {
                known += (known.empty() ? "" : ", ") + to_hex(colour.red, colour.green, colour.blue);
            }

            return std::unexpected(std::format(
                "Piksel [{}, {}] ma kolor {}, a mapa zna wyłącznie {}.",
                index % image.width,
                index / image.width,
                to_hex(red, green, blue),
                known));
        }

        terrain[index] = static_cast<std::uint8_t>(match->terrain);
    }

    return terrain;
}

std::expected<void, std::string> validate(const tmap::Map& map, std::uint32_t max_actors)
{
    if (map.spawns.size() < max_actors)
    {
        return std::unexpected(std::format(
            "Mapa ma {} punktów startowych, a przewiduje {} aktorów. Boty startują dokładnie "
            "tak samo jak ludzie, więc część z nich nie miałaby gdzie stanąć.",
            map.spawns.size(),
            max_actors));
    }

    const std::size_t land = static_cast<std::size_t>(std::ranges::count_if(
        map.terrain,
        [](std::uint8_t tile) { return tile != static_cast<std::uint8_t>(tmap::Terrain::water); }));

    const double share = static_cast<double>(land) / static_cast<double>(map.terrain.size());

    if (share < min_land_share || share > max_land_share)
    {
        return std::unexpected(std::format(
            "Ląd zajmuje {:.1f}% mapy, a przedział to {:.0f}–{:.0f}%.",
            share * 100.0,
            min_land_share * 100.0,
            max_land_share * 100.0));
    }

    const Continents continents = find_continents(map);

    std::vector<std::size_t> occupied;
    occupied.reserve(map.spawns.size());

    for (std::size_t index = 0; index < map.spawns.size(); ++index)
    {
        const tmap::Spawn spawn = map.spawns[index];
        const std::size_t tile = static_cast<std::size_t>(spawn.y) * map.width + spawn.x;

        // Dwa punkty startowe na jednym kafelku to dwóch aktorów w jednym miejscu. Kafelek
        // ma jednego właściciela (D12), więc jeden z nich po prostu zniknąłby z planszy —
        // i wyglądałoby to jak bot, który nic nie robi.
        if (const auto twin = std::ranges::find(occupied, tile); twin != occupied.end())
        {
            return std::unexpected(std::format(
                "Punkty startowe {} i {} stoją na tym samym kafelku [{}, {}].",
                std::ranges::distance(occupied.begin(), twin),
                index,
                spawn.x,
                spawn.y));
        }

        occupied.push_back(tile);

        if (map.terrain[tile] == static_cast<std::uint8_t>(tmap::Terrain::water))
        {
            return std::unexpected(
                std::format("Punkt startowy {} stoi na wodzie [{}, {}].", index, spawn.x, spawn.y));
        }

        // Wyspa bez połączenia z głównym lądem to gracz, którego nikt nie zaatakuje i który
        // sam nigdzie nie wyjdzie — czyli slot wycięty z meczu, ale liczony do wyniku.
        if (continents.label[tile] != continents.largest)
        {
            return std::unexpected(std::format(
                "Punkt startowy {} [{}, {}] leży poza głównym kontynentem.",
                index,
                spawn.x,
                spawn.y));
        }
    }

    return {};
}

} // namespace gs::tmapgen
