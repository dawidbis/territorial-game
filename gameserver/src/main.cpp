#include "app/log.hpp"
#include "app/options.hpp"
#include "meta/ticket.hpp"
#include "net/listener.hpp"
#include "net/session.hpp"
#include "tick/match_clock.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>

#include <game.pb.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// Co ile tików proces mówi, że żyje. 50 tików to 5 sekund przy 10 Hz.
constexpr std::uint32_t heartbeat_every = 50;

/// Buduje snapshot na dany tik.
///
/// Jeden bufor dla wszystkich — brak fog of war (§1) oznacza, że każdy gracz dostaje
/// identyczne bajty, więc serializacja dzieje się raz na tik, a nie raz na gracza.
/// W etapie E2 snapshot jest pusty: niesie numer tiku i nic poza nim, bo nie ma jeszcze
/// świata, który mógłby się zmienić.
std::shared_ptr<const std::string> build_snapshot(std::uint32_t tick)
{
    game::ServerMsg message;

    game::Snapshot* snapshot = message.mutable_snapshot();
    snapshot->set_tick(tick);
    snapshot->set_is_keyframe(false);

    return std::make_shared<const std::string>(message.SerializeAsString());
}

/// Cały mecz w jednej pętli.
///
/// Opcje przez wartość, nie referencję: korutyna żyje dłużej niż wywołanie, które ją
/// utworzyło. Weryfikator i rejestr przez referencję świadomie — oba żyją w `main`,
/// czyli dłużej niż `io_context`, który tę korutynę wznawia.
boost::asio::awaitable<void> run_match(
    gs::Options options,
    gs::TicketVerifier& tickets,
    gs::SessionRegistry& sessions)
{
    boost::asio::any_io_executor executor = co_await boost::asio::this_coro::executor;

    gs::MatchClock clock(executor, gs::TickRates{});

    boost::asio::signal_set signals(executor, SIGINT, SIGTERM);

    signals.async_wait(
        [&clock](const boost::system::error_code& error, int signal_number)
        {
            // Anulowanie oczekiwania na sygnał przychodzi tą samą drogą co sygnał. Wtedy
            // korutyna może już kończyć pracę, więc `clock` nie ma prawa być tu dotknięty.
            if (error)
            {
                return;
            }

            gs::log::info("Sygnał {} — kończę.", signal_number);

            clock.cancel();
        });

    // Nasłuch trzeba umieć zatrzymać, inaczej po końcu meczu `io_context` ma wciąż robotę
    // do wykonania i proces nie kończy pracy — czeka na graczy, których nie ma już dokąd
    // wpuścić. Zamknięcie akceptora jest jedynym sposobem przerwania tamtej pętli, więc
    // uchwyt zostaje tutaj.
    auto acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(
        gs::listen_on_loopback(executor, options.port));

    boost::asio::co_spawn(
        executor,
        gs::accept_connections(acceptor, tickets, sessions),
        [&clock](const std::exception_ptr& error)
        {
            if (!error)
            {
                return;
            }

            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                gs::log::error("Nasłuch przerwany: {}", exception.what());
            }

            // Proces, który nie przyjmuje graczy, nie ma po co tykać.
            clock.cancel();
        });

    while (const std::optional<gs::Tick> tick = co_await clock.next())
    {
        // Tu wejdzie krok symulacji.
        if (tick->send)
        {
            sessions.broadcast(build_snapshot(tick->number));
        }

        if (tick->send && tick->number % heartbeat_every == 0)
        {
            gs::log::info("Tik {}; połączeń: {}.", tick->number, sessions.size());
        }

        if (options.max_ticks != 0 && tick->number >= options.max_ticks)
        {
            gs::log::info("Osiągnięto limit {} tików — kończę.", options.max_ticks);

            break;
        }
    }

    sessions.close_all();

    // Bez tych dwóch linii `io_context` ma wciąż robotę do wykonania — oczekiwanie na
    // sygnał i na kolejne połączenie — i proces nigdy nie wraca z `run()`.
    boost::system::error_code ignored;
    acceptor->close(ignored);

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

    if (options->ticket_key_path.empty())
    {
        std::cerr << "Opcja --ticket-key jest wymagana: bez klucza publicznego meta proces "
                     "nie ma jak odróżnić biletu od zmyślonego ciągu.\n";

        return EXIT_FAILURE;
    }

    // Klucz wczytywany PRZED nasłuchem: proces, który i tak nikogo nie wpuści, ma paść
    // od razu, a nie przy pierwszym graczu.
    auto verifier = gs::TicketVerifier::from_pem_file(
        options->ticket_key_path,
        options->match_id,
        options->max_actors);

    if (!verifier)
    {
        std::cerr << verifier.error() << "\n";

        return EXIT_FAILURE;
    }

    gs::log::info(
        "Mecz {} — port {}, mapa '{}', ziarno {}, aktorów {}.",
        options->match_id,
        options->port,
        options->map_path,
        options->seed,
        options->max_actors);

    gs::log::info("Etap E2: gniazdo stoi, świata jeszcze nie ma.");

    // Jeden `io_context` i jeden wątek (D8). Zegar meczu dzieli go z siecią, więc nie ma
    // tu żadnego stanu współdzielonego między wątkami — i nie ma go zyskać.
    boost::asio::io_context io;

    gs::SessionRegistry sessions;

    bool failed = false;

    boost::asio::co_spawn(
        io,
        run_match(*options, *verifier, sessions),
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
