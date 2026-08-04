#include "app/log.hpp"
#include "app/match_runner.hpp"
#include "app/options.hpp"
#include "app/startup.hpp"
#include "net/match_services.hpp"
#include "net/session_registry.hpp"
#include "sim/simulation.hpp"
#include "tick/match_clock.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>

#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// Wynik pojedynczego meczu widziany przez orkiestratora.
///
/// Kod wyjścia mówi mu, czy to było porzucenie, czy normalny koniec: „nikt nie przyszedł" jest
/// awarią alokacji, a nie meczem — i tylko po kodzie wyjścia da się to odróżnić bez czytania
/// logów.
bool failed_outcome(const std::exception_ptr& error, gs::MatchOutcome outcome)
{
    if (!error)
    {
        return outcome == gs::MatchOutcome::abandoned;
    }

    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::exception& exception)
    {
        gs::log::error("Mecz przerwany błędem: {}", exception.what());
    }

    return true;
}

} // namespace

/// Wejście procesu meczu: opcje → zasoby → pętla.
///
/// Trzy kroki i ani jednej linii logiki poza nimi. Wszystko, co dawniej robił tu na miejscu —
/// wczytywanie mapy, obsady i klucza, banner do logu, pętla tików — mieszka w `app/startup`
/// i `app/match_runner`, bo każda z tych rzeczy ma inny powód do zmiany.
int main(int argc, char* argv[])
try
{
    const std::vector<std::string_view> args(argv + 1, argv + argc);

    const std::expected<gs::Options, std::string> options = gs::parse_options(args);

    if (!options)
    {
        std::cerr << options.error() << "\n\n" << gs::usage_text();

        return EXIT_FAILURE;
    }

    if (options->help)
    {
        std::cout << gs::usage_text();

        return EXIT_SUCCESS;
    }

    std::expected<gs::MatchSetup, std::string> setup = gs::MatchSetup::open(*options);

    if (!setup)
    {
        std::cerr << setup.error() << "\n";

        return EXIT_FAILURE;
    }

    gs::log_match_summary(*options, *setup);

    // Jeden `io_context` i jeden wątek (D8). Zegar meczu dzieli go z siecią, więc nie ma tu
    // żadnego stanu współdzielonego między wątkami — i nie ma go zyskać.
    boost::asio::io_context io;

    gs::SessionRegistry sessions;

    // Zegar i usługi stoją tutaj, a nie w ramce korutyny meczu: sesje trzymają do nich
    // referencje i potrafią wznowić się jeszcze po tym, jak pętla meczu się skończy.
    gs::MatchClock clock(io.get_executor(), gs::TickRates{});

    // Symulacja stoi przed usługami, bo te trzymają do niej referencję — rozkaz gracza wchodzi
    // do niej wprost z korutyny sesji.
    gs::Simulation simulation(setup->world, setup->roster.actors(), options->seed);

    gs::MatchServices services{setup->tickets, sessions, setup->intro, clock, simulation};

    bool failed = false;

    boost::asio::co_spawn(
        io,
        gs::run_match(*options, services, clock, *setup),
        [&failed](const std::exception_ptr& error, gs::MatchOutcome outcome)
        { failed = failed_outcome(error, outcome); });

    io.run();

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
// Wyjątek, który dochodzi do `main`, kończy proces przez `std::terminate` — bez logu i bez
// kodu wyjścia, po którym orkiestrator odróżni awarię od normalnego końca meczu. Tu jest
// ostatnie miejsce, w którym da się powiedzieć, co się stało.
catch (const std::exception& exception)
{
    gs::log::error("Proces meczu przerwany: {}", exception.what());

    return EXIT_FAILURE;
}
catch (...)
{
    gs::log::error("Proces meczu przerwany wyjątkiem nieznanego typu.");

    return EXIT_FAILURE;
}
