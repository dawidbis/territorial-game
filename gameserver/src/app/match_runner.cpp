#include "app/match_runner.hpp"

#include "app/log.hpp"
#include "app/startup.hpp"
#include "net/listener.hpp"
#include "net/match_services.hpp"
#include "net/session_registry.hpp"
#include "sim/simulation.hpp"
#include "state/publisher.hpp"
#include "tick/match_clock.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <memory>
#include <optional>

namespace gs
{
namespace
{

/// Co ile tików proces mówi, że żyje. 50 tików to 5 sekund przy 10 Hz.
constexpr std::uint32_t heartbeat_every = 50;

/// Ustawia gaszenie procesu sygnałem.
///
/// Zestaw sygnałów zostaje u wołającego, bo musi przeżyć całą pętlę meczu — i dlatego, że
/// na końcu trzeba go anulować, inaczej `io_context` ma wciąż robotę i proces nie wraca
/// z `run()`.
void stop_on_signal(boost::asio::signal_set& signals, MatchClock& clock)
{
    signals.async_wait(
        [&clock](const boost::system::error_code& error, int signal_number)
        {
            // Anulowanie oczekiwania na sygnał przychodzi tą samą drogą co sygnał. Wtedy
            // korutyna może już kończyć pracę, więc `clock` nie ma prawa być tu dotknięty.
            if (error)
            {
                return;
            }

            log::info("Sygnał {} — kończę.", signal_number);

            clock.cancel();
        });
}

/// Otwiera nasłuch i puszcza pętlę przyjmowania połączeń.
///
/// Uchwyt akceptora wraca do wołającego, bo nasłuch trzeba umieć **zatrzymać**: inaczej po
/// końcu meczu `io_context` wciąż czeka na graczy, których nie ma już dokąd wpuścić, a proces
/// nie kończy pracy. Zamknięcie akceptora jest jedynym sposobem przerwania tamtej pętli.
std::shared_ptr<boost::asio::ip::tcp::acceptor> start_listening(
    const boost::asio::any_io_executor& executor,
    std::uint16_t port,
    MatchServices& services,
    MatchClock& clock)
{
    auto acceptor =
        std::make_shared<boost::asio::ip::tcp::acceptor>(listen_on_loopback(executor, port));

    boost::asio::co_spawn(
        executor,
        accept_connections(acceptor, services),
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
                log::error("Nasłuch przerwany: {}", exception.what());
            }

            // Proces, który nie przyjmuje graczy, nie ma po co tykać.
            clock.cancel();
        });

    return acceptor;
}

/// Limity cyklu życia procesu z uwzględnieniem skróconego okna dev.
///
/// Skrócone okno bezczynności jest narzędziem dev, nie strojeniem: na maszynie deweloperskiej
/// stoi jeden mecz naraz, więc dwuminutowe czekanie na powrót gracza blokuje **każde** lobby
/// otwarte w tym czasie. Twardego limitu meczu to nie dotyka.
LifetimeLimits limits_for(const Options& options)
{
    LifetimeLimits limits;

    if (options.idle_seconds != 0)
    {
        limits.join_grace = std::chrono::seconds{options.idle_seconds};
        limits.empty_grace = std::chrono::seconds{options.idle_seconds};
    }

    return limits;
}

} // namespace

boost::asio::awaitable<MatchOutcome> run_match(
    Options options,
    MatchServices& services,
    MatchClock& clock,
    MatchSetup& setup)
{
    const boost::asio::any_io_executor executor = co_await boost::asio::this_coro::executor;

    boost::asio::signal_set signals(executor, SIGINT, SIGTERM);

    stop_on_signal(signals, clock);

    const std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor =
        start_listening(executor, options.port, services, clock);

    MatchLifetime lifetime(limits_for(options), TickRates{});

    MatchPublisher publisher(
        services.sessions,
        setup.roster.actors(),
        setup.world,
        services.simulation);

    MatchOutcome outcome = MatchOutcome::running;

    while (const std::optional<Tick> tick = co_await clock.next())
    {
        services.simulation.tick(tick->number);

        if (tick->send)
        {
            publisher.publish(tick->number);

            if (tick->number % heartbeat_every == 0)
            {
                log::info("Tik {}; połączeń: {}.", tick->number, services.sessions.size());
            }
        }

        outcome = lifetime.observe(tick->number, services.sessions.size());

        if (outcome != MatchOutcome::running)
        {
            log::info("Koniec meczu: {}.", describe(outcome));

            break;
        }

        if (options.max_ticks != 0 && tick->number >= options.max_ticks)
        {
            log::info("Osiągnięto limit {} tików — kończę.", options.max_ticks);

            break;
        }
    }

    services.sessions.close_all();

    // Bez tych dwóch linii `io_context` ma wciąż robotę do wykonania — oczekiwanie na sygnał
    // i na kolejne połączenie — i proces nigdy nie wraca z `run()`.
    //
    // Wynik zamknięcia jest **świadomie porzucany**: to sprzątanie na wyjściu, a jedyną
    // możliwą reakcją na „nie udało się zamknąć akceptora" byłoby wypisanie tego do logu
    // procesu, który właśnie się kończy.
    boost::system::error_code ignored;
    static_cast<void>(acceptor->close(ignored));

    signals.cancel();

    log::info("Koniec: {} tików, {} przepadło.", clock.tick(), clock.skipped());

    co_return outcome;
}

} // namespace gs
