#pragma once

#include "map/tmap.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

/// Mapa 4×3 używana przez testy formatu, świata i keyframe'a.
///
/// Tak mała celowo: wszystkie oczekiwania da się wypisać ręcznie, więc test mówi, co ma
/// wyjść, zamiast liczyć to drugą implementacją tego samego algorytmu.
///
/// ```
///  w w w w
///  w l l w      l — niziny, w — woda
///  w w w w
/// ```
namespace fixtures
{

inline gs::tmap::Map small_map()
{
    constexpr std::uint8_t water = static_cast<std::uint8_t>(gs::tmap::Terrain::water);
    constexpr std::uint8_t land = static_cast<std::uint8_t>(gs::tmap::Terrain::lowlands);

    gs::tmap::Map map;

    map.id = "test";
    map.width = 4;
    map.height = 3;

    map.terrain = std::vector<std::uint8_t>{
        water, water, water, water,
        water, land,  land,  water,
        water, water, water, water};

    map.spawns = {gs::tmap::Spawn{1, 1}, gs::tmap::Spawn{2, 1}};

    return map;
}

/// Prostokąt samego lądu; punkty startowe domyślnie w przeciwległych rogach.
///
/// Plansza do testów podboju: bez wody każdy kafelek jest zdobywalny, więc test mówi
/// o regułach ataku, a nie o kształcie brzegu. `terrain` pozwala sprawdzić, że wyżyny
/// i góry faktycznie kosztują więcej niż niziny.
///
/// **Rozmiar ma znaczenie od czasu, gdy aktor startuje z 52 kafelkami** (`World::place_actor`):
/// na ciasnej planszy pierwszy postawiony zajmuje punkt startowy drugiego. Testy, które
/// potrzebują dwóch osobnych państw, podają rozmiar z zapasem albo własne spawny.
inline gs::tmap::Map plains_map(
    std::uint16_t width,
    std::uint16_t height,
    gs::tmap::Terrain terrain = gs::tmap::Terrain::lowlands,
    std::vector<gs::tmap::Spawn> spawns = {})
{
    gs::tmap::Map map;

    map.id = "plains";
    map.width = width;
    map.height = height;

    map.terrain.assign(
        static_cast<std::size_t>(width) * height,
        static_cast<std::uint8_t>(terrain));

    // Domyślne punkty startowe stoją **odsunięte od krawędzi**, a nie w samych rogach:
    // róg przycina dysk startowy do ćwiartki i test mówiłby o krawędzi mapy zamiast o tym,
    // co zamierza. Odsunięcie jest przycięte do rozmiaru planszy, żeby małe mapy używane
    // do testów czystych funkcji nadal miały dwa różne spawny.
    const int inset = std::min(8, std::min<int>(width, height) / 3);

    map.spawns = spawns.empty()
        ? std::vector<gs::tmap::Spawn>{
              gs::tmap::Spawn{
                  static_cast<std::uint16_t>(inset),
                  static_cast<std::uint16_t>(inset)},
              gs::tmap::Spawn{
                  static_cast<std::uint16_t>(width - 1 - inset),
                  static_cast<std::uint16_t>(height - 1 - inset)}}
        : std::move(spawns);

    return map;
}

/// Korytarz o wysokości jednego kafelka z graczami na obu końcach.
///
/// Terytorium startowe degeneruje się tu do odcinka, więc oba państwa **stykają się
/// dokładnie w jednym miejscu** — a to jest cała plansza potrzebna do testów wspólnej
/// granicy, anihilacji i eliminacji.
inline gs::tmap::Map duel_map()
{
    // Osiem kafelków to dwa odcinki po cztery: dysk startowy sięga czterech pól w bok,
    // więc korytarz tej długości wypełnia się co do kafelka i bez luki pośrodku.
    return plains_map(
        8,
        1,
        gs::tmap::Terrain::lowlands,
        {gs::tmap::Spawn{0, 0}, gs::tmap::Spawn{7, 0}});
}

} // namespace fixtures
