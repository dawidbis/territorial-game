#include "app/log.hpp"
#include "app/options.hpp"
#include "tick/match_clock.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// Co ile tików proces mówi, że żyje. 50 tików to 5 sekund przy 10 Hz.
constexpr std::uint32_t heartbeat_every = 50;

/// Cały mecz w jednej pętli.
///
/// Opcje przez wartość, nie referencję: korutyna żyje dłużej niż wywołanie, które ją
/// utworzyło, więc referencja na argument jest tu klasycznym sposobem na wiszący wskaźnik.
boost::asio::awaitable<void> run_match(gs::Options options)
{
    boost::asio::any_io_executor executor = co_await boost::asio::this_coro::executor;

    gs::MatchClock clock(executor, gs::TickRates{});

    boost::asio::signal_set signals(executor, SIGINT, SIGTERM);

    signals.async_wait(
        [&clock](const boost::system::error_code& error, int signal_number)
        {
            // Anulowanie oczekiwania na sygnał przychodzi tą samą drogą co sygnał.
            // Wtedy korutyna może już kończyć pracę, więc `clock` nie ma prawa być
            // tu dotknięty.
            if (error)
            {
                return;
            }

            gs::log::info("Sygnał {} — kończę.", signal_number);

            clock.cancel();
        });

    while (const std::optional<gs::Tick> tick = co_await clock.next())
    {
        // Tu wejdzie krok symulacji, a przy `tick->send` budowa i rozesłanie snapshotu.
        // Dopóki ich nie ma, pętla dowodzi wyłącznie własnej regularności — i to jest
        // cały cel etapu E1.
        if (tick->send && tick->number % heartbeat_every == 0)
        {
            gs::log::info("Tik {}.", tick->number);
        }

        if (options.max_ticks != 0 && tick->number >= options.max_ticks)
        {
            gs::log::info("Osiągnięto limit {} tików — kończę.", options.max_ticks);

            break;
        }
    }

    // Bez tego oczekiwanie na sygnał zostaje robotą do wykonania i `io_context` nigdy
    // nie wraca z `run()`.
    signals.cancel();

    gs::log::info("Koniec: {} tików, {} przepadło.", clock.tick(), clock.skipped());
}

} // namespace

int main(int argc, char* argv[])
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

    gs::log::info(
        "Mecz {} — port {}, mapa '{}', ziarno {}, aktorów {}.",
        options->match_id,
        options->port,
        options->map_path,
        options->seed,
        options->max_actors);

    gs::log::info("Etap E1: zegar chodzi, gniazda jeszcze nie ma.");

    // Jeden `io_context` i jeden wątek (D8). Zegar meczu dzieli go z siecią, więc nie ma
    // tu żadnego stanu współdzielonego między wątkami — i nie ma go zyskać.
    boost::asio::io_context io;

    bool failed = false;

    boost::asio::co_spawn(
        io,
        run_match(*options),
        [&failed](const std::exception_ptr& error)
        {
            if (!error)
            {
                return;
            }

            failed = true;

            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                gs::log::error("Mecz przerwany błędem: {}", exception.what());
            }
        });

    io.run();

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
