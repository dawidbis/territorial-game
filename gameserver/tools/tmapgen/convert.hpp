#pragma once

#include "map/tmap.hpp"
#include "png.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::tmapgen
{

/// Cztery kolory źródłowe, po jednym na typ terenu.
///
/// Czyste i skrajne, bo dobrane pod **precyzję rysowania**, a nie pod wygląd: w każdym
/// edytorze trafia się w nie bez pipety. Paleta wyświetlania jest osobną sprawą klienta.
struct TerrainColour
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    tmap::Terrain terrain = tmap::Terrain::water;
};

std::span<const TerrainColour> terrain_palette();

/// Wszystko, czego obrazek nie umie wyrazić — czyli zawartość pliku JSON obok niego.
///
/// Wymiarów ani sumy kontrolnej tu nie ma **celowo**: wychodzą z obrazka i z `.tmap`,
/// a wpisane drugi raz zaczęłyby kłamać przy pierwszej zmianie mapy.
struct Metadata
{
    std::string id;

    std::string name;

    std::uint32_t max_actors = 0;

    std::vector<tmap::Spawn> spawns;
};

std::expected<Metadata, std::string> parse_metadata(std::string_view json);

std::expected<Metadata, std::string> read_metadata_file(const std::filesystem::path& path);

/// Zamienia piksele na kody terenu.
///
/// Nieznany kolor to **błąd, nie najbliższe dopasowanie**: literówka w odcieniu dałaby
/// inaczej wyspę tam, gdzie miała być woda — i wyszłoby to dopiero w środku meczu.
std::expected<std::vector<std::uint8_t>, std::string> terrain_from_image(const png::Image& image);

/// Sprawdzenia, których obrazek sam z siebie nie przejdzie.
///
/// Każde z nich opisuje mapę, która **wczytuje się bez problemu** i psuje dopiero
/// w dwunastej minucie meczu — a wygląda wtedy jak błąd symulacji.
std::expected<void, std::string> validate(const tmap::Map& map, std::uint32_t max_actors);

} // namespace gs::tmapgen
