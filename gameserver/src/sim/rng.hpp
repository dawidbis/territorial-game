#pragma once

#include <cstdint>

namespace gs
{

/// PCG32 — jedyne źródło losowości w procesie meczu (§3.8 planu).
///
/// Własny generator, a nie `std::mt19937`, bo **replay wymaga, żeby ten sam ciąg wychodził
/// na każdej maszynie i przy każdej wersji biblioteki standardowej**. Rozkłady ze
/// standardu (`uniform_int_distribution`) nie mają zdefiniowanej implementacji, więc dwie
/// biblioteki dają z tego samego ziarna dwa różne mecze — i wychodzi to w ósmym miesiącu.
///
/// Wyłącznie arytmetyka na liczbach bez znaku: przepełnienie jest tu zdefiniowane
/// i zamierzone, w przeciwieństwie do typów ze znakiem.
///
/// **Strumień jest częścią ziarna.** Bot na slocie 7 losuje ze strumienia 7, więc jego nick
/// i kolor nie zależą od tego, ilu botów jest przed nim — dopisanie jednego nie przesuwa
/// wszystkich pozostałych. Bez tego każda zmiana liczby aktorów unieważniałaby replaye.
class Pcg32
{
public:
    Pcg32(std::uint64_t seed, std::uint64_t stream);

    std::uint32_t next() noexcept;

    /// Liczba z przedziału `[0, bound)` **bez obciążenia**.
    ///
    /// Zwykłe `next() % bound` faworyzuje niskie wartości, gdy `bound` nie dzieli 2³²;
    /// przy 254 slotach byłoby to widać jako boty tłoczące się w jednym kolorze.
    std::uint32_t below(std::uint32_t bound) noexcept;

private:
    std::uint64_t state_ = 0;

    /// Zawsze nieparzysty — tego wymaga generator liniowy, żeby miał pełny okres.
    std::uint64_t increment_ = 1;
};

} // namespace gs
