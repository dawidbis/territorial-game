#include "map/tmap.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace gs::tmap
{
namespace
{

constexpr std::array<char, 4> magic{'T', 'M', 'A', 'P'};

/// Zapis i odczyt idą jawnie bajt po bajcie, a nie przez `memcpy` struktury. Format
/// przechodzi między maszynami i między C++ a JavaScriptem, więc kolejność bajtów jest
/// częścią kontraktu, a nie właściwością procesora, na którym akurat stoi konwerter.
void put_u16(std::string& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void put_u32(std::string& out, std::uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

std::expected<std::string, std::string> encode(const Map& map)
{
    if (map.width == 0 || map.height == 0)
    {
        return std::unexpected(
            std::format("Mapa ma wymiary {}×{} — obie muszą być większe od zera.", map.width, map.height));
    }

    if (map.id.empty() || map.id.size() > 255)
    {
        return std::unexpected(std::format(
            "Identyfikator mapy ma {} znaków, a format mieści 1..255.",
            map.id.size()));
    }

    if (map.spawns.size() > max_dimension)
    {
        return std::unexpected(std::format(
            "Mapa ma {} punktów startowych, a format mieści najwyżej {}.",
            map.spawns.size(),
            max_dimension));
    }

    const std::size_t tiles = static_cast<std::size_t>(map.width) * map.height;

    if (map.terrain.size() != tiles)
    {
        return std::unexpected(std::format(
            "Siatka terenu ma {} kafelków, a wymiary {}×{} zapowiadają {}.",
            map.terrain.size(),
            map.width,
            map.height,
            tiles));
    }

    const std::uint32_t terrain_offset =
        static_cast<std::uint32_t>(header_size + map.id.size() + map.spawns.size() * 4);

    std::string out;
    out.reserve(terrain_offset + tiles);

    out.append(magic.data(), magic.size());
    put_u16(out, format_version);
    put_u16(out, map.width);
    put_u16(out, map.height);
    out.push_back(static_cast<char>(terrain_type_count));
    out.push_back(static_cast<char>(map.id.size()));
    put_u16(out, static_cast<std::uint16_t>(map.spawns.size()));
    put_u16(out, 0);
    put_u32(out, terrain_offset);

    out.append(map.id);

    for (const Spawn& spawn : map.spawns)
    {
        put_u16(out, spawn.x);
        put_u16(out, spawn.y);
    }

    out.append(reinterpret_cast<const char*>(map.terrain.data()), map.terrain.size());

    return out;
}

std::expected<MapView, std::string> decode(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < header_size)
    {
        return std::unexpected(std::format(
            "Plik ma {} bajtów, a sam nagłówek zajmuje {}.",
            bytes.size(),
            header_size));
    }

    if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
    {
        return std::unexpected(std::string{"To nie jest plik .tmap — brak sygnatury 'TMAP'."});
    }

    if (const std::uint16_t version = read_u16(bytes, 4); version != format_version)
    {
        return std::unexpected(std::format(
            "Plik jest w wersji formatu {}, a ten proces czyta {}. Przepuść mapę przez "
            "tmapgen jeszcze raz.",
            version,
            format_version));
    }

    MapView view;
    view.width = read_u16(bytes, 6);
    view.height = read_u16(bytes, 8);

    if (view.width == 0 || view.height == 0)
    {
        return std::unexpected(
            std::format("Nagłówek zapowiada mapę {}×{}.", view.width, view.height));
    }

    if (const std::uint8_t types = bytes[10]; types != terrain_type_count)
    {
        return std::unexpected(std::format(
            "Plik opisuje {} typów terenu, a ta wersja reguł zna {}.",
            types,
            terrain_type_count));
    }

    const std::size_t id_length = bytes[11];
    const std::size_t spawn_count = read_u16(bytes, 12);
    const std::size_t terrain_offset = read_u32(bytes, 16);

    // Offset stoi w nagłówku dla wygody klienta, ale zaufać mu nie można: sfałszowany
    // wskazywałby w środek terenu albo poza plik. Liczymy go z pozostałych pól i porównujemy.
    if (const std::size_t expected = header_size + id_length + spawn_count * 4;
        terrain_offset != expected)
    {
        return std::unexpected(std::format(
            "Nagłówek zapowiada teren pod offsetem {}, a sekcje przed nim kończą się na {}.",
            terrain_offset,
            expected));
    }

    const std::size_t tiles = view.tile_count();

    if (bytes.size() != terrain_offset + tiles)
    {
        return std::unexpected(std::format(
            "Plik ma {} bajtów, a nagłówek zapowiada {} ({} nagłówka i sekcji + {}×{} terenu).",
            bytes.size(),
            terrain_offset + tiles,
            terrain_offset,
            view.width,
            view.height));
    }

    view.id = std::string_view(
        reinterpret_cast<const char*>(bytes.data() + header_size),
        id_length);

    if (view.id.empty())
    {
        return std::unexpected(std::string{"Plik nie niesie identyfikatora mapy."});
    }

    view.spawns.reserve(spawn_count);

    for (std::size_t index = 0; index < spawn_count; ++index)
    {
        const std::size_t offset = header_size + id_length + index * 4;

        const Spawn spawn{read_u16(bytes, offset), read_u16(bytes, offset + 2)};

        if (spawn.x >= view.width || spawn.y >= view.height)
        {
            return std::unexpected(std::format(
                "Punkt startowy {} stoi na [{}, {}], poza mapą {}×{}.",
                index,
                spawn.x,
                spawn.y,
                view.width,
                view.height));
        }

        view.spawns.push_back(spawn);
    }

    view.terrain = bytes.subspan(terrain_offset, tiles);

    // Przejście po dwóch milionach bajtów kosztuje ułamek milisekundy i zamyka najgorszą
    // z możliwych dróg: nieznany kod terenu wszedłby do tabeli kosztów jako indeks poza
    // tablicą, a objawiłby się w środku meczu.
    for (std::size_t index = 0; index < tiles; ++index)
    {
        if (view.terrain[index] >= terrain_type_count)
        {
            return std::unexpected(std::format(
                "Kafelek {} ma typ terenu {}, a zdefiniowane są 0..{}.",
                index,
                view.terrain[index],
                terrain_type_count - 1));
        }
    }

    for (std::size_t index = 0; index < view.spawns.size(); ++index)
    {
        if (view.terrain[view.index_of(view.spawns[index])]
            == static_cast<std::uint8_t>(Terrain::water))
        {
            return std::unexpected(std::format(
                "Punkt startowy {} stoi na wodzie [{}, {}].",
                index,
                view.spawns[index].x,
                view.spawns[index].y));
        }
    }

    return view;
}

} // namespace gs::tmap
