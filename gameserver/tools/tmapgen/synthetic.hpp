#pragma once

#include "map/tmap.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace gs::tmapgen
{

struct SyntheticRequest
{
    std::string id = "synthetic";

    std::uint16_t width = 2000;

    std::uint16_t height = 1000;

    std::uint32_t max_actors = 100;

    std::uint64_t seed = 1;
};

/// Mapa wygenerowana z ziarna — **nie udaje mapy do grania**.
///
/// Istnieje po to, żeby etap E3 miał jakiekolwiek bajty, zanim powstanie pierwsza narysowana
/// mapa: keyframe o realistycznym rozmiarze, na którym da się sprawdzić budżet z §7. Balans
/// opiera się na ręcznie postawionych punktach startowych (§4.2 dokumentu architektury),
/// więc generacja proceduralna nie jest i nie będzie drogą do map produkcyjnych.
///
/// Cała arytmetyka jest całkowitoliczbowa, żeby to samo ziarno dawało bajt w bajt ten sam
/// plik na Windowsie i na Linuksie. Mapa jest adresowana sumą kontrolną (D13), więc dwie
/// maszyny generujące „tę samą" mapę odrobinę inaczej serwowałyby dwa różne assety.
std::expected<tmap::Map, std::string> generate(const SyntheticRequest& request);

} // namespace gs::tmapgen
