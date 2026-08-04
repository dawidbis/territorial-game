#pragma once

#include "meta/manifest.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gs
{

/// Aktor meczu — człowiek albo bot. Z punktu widzenia symulacji nie ma między nimi różnicy
/// (D12) i to jest cel: bot startuje na tym samym punkcie i podlega tym samym regułom.
struct Actor
{
    std::uint8_t slot = 0;

    std::string name;

    std::uint32_t color_rgb = 0;

    bool is_bot = false;
};

/// Pełna obsada meczu: ludzie z manifestu plus boty dobrane z ziarna do sufitu aktorów.
///
/// **Boty muszą wynikać z ziarna, nie z zegara ani z kolejności wywołań.** Replay odtwarza
/// mecz przez re-symulację (D10), więc wszystko, co ich dotyczy — nick, kolor, później także
/// decyzje — ma być funkcją ziarna i numeru slotu. Stąd nick i kolor bierze się z permutacji
/// wyliczonej raz z ziarna, a nie ze strumienia losowań idącego po kolei: dopisanie jednego
/// bota nie ma prawa przemalować wszystkich pozostałych.
class Roster
{
public:
    /// @param fill_bots Czy dopełnić obsadę botami do sufitu aktorów. `false` daje mecz
    ///                  wyłącznie z ludzi — mapa zostaje pustkowiem poza ich terytoriami.
    static Roster build(
        const std::vector<ManifestPlayer>& players,
        std::uint32_t max_actors,
        std::int64_t seed,
        bool fill_bots = true);

    std::span<const Actor> actors() const noexcept
    {
        return actors_;
    }

    std::size_t humans() const noexcept
    {
        return humans_;
    }

    std::size_t bots() const noexcept
    {
        return actors_.size() - humans_;
    }

private:
    std::vector<Actor> actors_;

    std::size_t humans_ = 0;
};

/// Kolor z koła barw w 256 krokach — pełne nasycenie i jasność, bo boty mają być widoczne
/// na mapie, a nie ładne.
///
/// To **nie jest** konwersja HSV, której §3.5 planu zabrania procesowi znać: tamta dotyczy
/// kolorów wybranych przez graczy i mieszka po stronie meta razem z domeną. Boty nie mają
/// domenowej reprezentacji koloru w ogóle — mają tylko wymóg bycia rozróżnialnymi.
std::uint32_t color_from_hue(std::uint32_t hue) noexcept;

} // namespace gs
