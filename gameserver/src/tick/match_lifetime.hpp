#pragma once

#include "tick/match_clock.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gs
{

/// Trzy sposoby, w jakie proces meczu ma się skończyć sam.
///
/// Bez nich pierwszy dzień z prawdziwym alokatorem zostawia na maszynie proces na każde lobby,
/// a lobby otwiera się co kilka minut w nieskończoność.
struct LifetimeLimits
{
    /// Ile proces czeka na pierwszego gracza. Nikt nie przyszedł — alokacja poszła w próżnię.
    std::chrono::seconds join_grace{120};

    /// Ile proces czeka po odejściu ostatniego. Tyle trwa okno reconnectu z D14, więc gracz,
    /// któremu mignęła sieć, ma do czego wrócić.
    std::chrono::seconds empty_grace{120};

    /// D7 obiecuje mecze poniżej 25 minut i **na tej obietnicy stoi cała strategia deployu**
    /// („przestań alokować i poczekaj"). Obietnica bez egzekwowania jest tylko komentarzem.
    std::chrono::seconds hard_limit{30 * 60};
};

enum class MatchOutcome : std::uint8_t
{
    running,

    /// Nikt nie przyszedł. Kod wyjścia niezerowy, bo to jest awaria alokacji, a nie mecz.
    abandoned,

    /// Ostatni gracz wyszedł i nie wrócił w oknie reconnectu.
    finished,

    /// Twardy limit czasu meczu.
    expired,
};

std::string_view describe(MatchOutcome outcome) noexcept;

/// Pilnuje warunków zakończenia, licząc w tikach zamiast patrzeć na zegar.
///
/// Tik jest już związany z czasem rzeczywistym przez <see cref="MatchClock"/>, więc drugie
/// źródło czasu wnosiłoby tylko możliwość rozjechania się z pierwszym. Przy okazji cała ta
/// klasa daje się przetestować bez czekania i bez ani jednego timera.
class MatchLifetime
{
public:
    MatchLifetime(LifetimeLimits limits, TickRates rates);

    MatchOutcome observe(std::uint32_t tick, std::size_t connections) noexcept;

private:
    std::uint32_t join_grace_ticks_ = 0;

    std::uint32_t empty_grace_ticks_ = 0;

    std::uint32_t hard_limit_ticks_ = 0;

    bool anyone_joined_ = false;

    /// Tik, w którym zrobiło się pusto; zero znaczy „ktoś jest".
    std::uint32_t empty_since_ = 0;
};

} // namespace gs
