#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace gs
{

/// Symulacja 10 Hz, wysyłka **też 10 Hz**.
///
/// D3 dokumentu architektury mówi „send 5 Hz" i to **świadome odstępstwo**: klient animuje
/// przejmowanie kafelków, a animacja odtwarza to, co przyszło ostatnią paczką. Przy wysyłce
/// co drugi tik ruch frontu docierał skokami po 200 ms, więc animacja albo zostawała w tyle
/// o pół kroku, albo musiała zgadywać przyszłość.
///
/// Cena jest realna i policzona: znika naturalna deduplikacja (kafelek przejęty dwa razy
/// w oknie jechał raz) i podwaja się liczba ramek. Sam wolumen kafelków rośnie mniej niż
/// dwukrotnie — zmiany na tik są te same, dzielą się tylko na więcej paczek — ale narzut
/// nagłówka ramki i `PublicState` już tak. Wrócić do `2` to jedna linia.
struct TickRates
{
    std::chrono::milliseconds sim_period{100};

    std::uint32_t send_every = 1;
};

struct Tick
{
    /// Numer tiku symulacji; pierwszy ma numer 1.
    std::uint32_t number = 0;

    /// Czy po tym kroku symulacji leci snapshot.
    bool send = false;
};

/// Zegar meczu: stały krok napędzany `steady_timer` na tym samym `io_context`, co sieć.
///
/// Zegar **nie woła niczyich uchwytów** — oddaje kolejny tik temu, kto o niego poprosi:
///
/// ```
/// while (const std::optional<Tick> tick = co_await clock.next())
/// {
///     simulate(*tick);
/// }
/// ```
///
/// Wersja z `std::function` odwracałaby sterowanie: zatrzymanie meczu musiałoby wracać
/// do zegara osobną ścieżką i być sprawdzane w środku nadrabiania zaległości. Tutaj
/// „koniec meczu" to zwykły `break`, a pętla symulacji czyta się z góry na dół.
///
/// Jeden wątek i zero mutexów zostaje w mocy (D8) — korutyna wznawia się na tym samym
/// executorze, na którym stoi sieć.
class MatchClock
{
public:
    using Clock = boost::asio::steady_timer::clock_type;

    /// O ile kroków wolno spóźnić się, zanim zegar zrezygnuje z nadrabiania.
    ///
    /// Nadrabianie bez sufitu to spirala śmierci: im dłużej trwa, tym więcej jest do
    /// nadrobienia. Po przekroczeniu sufitu zegar **przeskakuje** — mecz zostaje przy
    /// czasie rzeczywistym, a strata jest policzona i widoczna w <see cref="skipped"/>.
    static constexpr std::uint32_t max_catch_up = 5;

    MatchClock(const boost::asio::any_io_executor& executor, TickRates rates);

    /// Czeka do najbliższego terminu i oddaje tik.
    ///
    /// Spóźnienie mniejsze niż sufit wraca **bez czekania** — to jest nadrabianie.
    /// Pusty wynik znaczy „zegar zatrzymany" i jest jedynym sposobem, w jaki ta pętla
    /// się kończy.
    boost::asio::awaitable<std::optional<Tick>> next();

    /// Zatrzymuje zegar; oczekujące `next()` wraca puste, nie czekając na termin.
    void cancel();

    std::uint32_t tick() const noexcept
    {
        return tick_;
    }

    /// Ile tików przepadło przez przekroczenie sufitu nadrabiania. Zero przez cały mecz
    /// jest warunkiem, żeby wierzyć w cokolwiek, co ta pętla policzyła.
    std::uint32_t skipped() const noexcept
    {
        return skipped_;
    }

private:
    boost::asio::steady_timer timer_;

    TickRates rates_;

    Clock::time_point next_deadline_;

    std::uint32_t tick_ = 0;

    std::uint32_t skipped_ = 0;

    bool cancelled_ = false;
};

} // namespace gs
