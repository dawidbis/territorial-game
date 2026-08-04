#include "tick/match_clock.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <utility>

namespace gs
{

// Egzekutor przez stałą referencję, nie przez wartość: `steady_timer` i tak przyjmuje go
// referencją, więc przeniesienie nic nie przenosi, a kopia w parametrze kosztuje przy każdym
// utworzeniu zegara.
MatchClock::MatchClock(const boost::asio::any_io_executor& executor, TickRates rates)
    : timer_(executor)
    , rates_(rates)
    , next_deadline_(Clock::now() + rates.sim_period)
{
    // Zero znaczyłoby dzielenie przez zero przy wyborze tików wysyłkowych. Naprawiamy
    // po cichu, bo to konfiguracja wewnętrzna, a nie dane od gracza.
    if (rates_.send_every == 0)
    {
        rates_.send_every = 1;
    }
}

boost::asio::awaitable<std::optional<Tick>> MatchClock::next()
{
    if (cancelled_)
    {
        co_return std::nullopt;
    }

    const Clock::time_point now = Clock::now();

    if (next_deadline_ > now)
    {
        timer_.expires_at(next_deadline_);

        // `as_tuple` zamiast wyjątku: anulowanie zegara jest normalnym końcem meczu,
        // a nie sytuacją wyjątkową, więc nie ma powodu, żeby leciało wyjątkiem.
        const auto [error] =
            co_await timer_.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));

        if (error)
        {
            co_return std::nullopt;
        }
    }
    else if (const Clock::duration behind = now - next_deadline_;
             behind > max_catch_up * rates_.sim_period)
    {
        const auto lost = behind / rates_.sim_period;

        skipped_ += static_cast<std::uint32_t>(lost);
        next_deadline_ += lost * rates_.sim_period;
    }

    // Spóźnienie mniejsze niż sufit nie trafia w żadną z gałęzi wyżej — i o to chodzi:
    // tik wraca natychmiast, a pętla wywołująca nadrabia zaległość własnym obrotem.

    next_deadline_ += rates_.sim_period;
    ++tick_;

    co_return Tick{tick_, tick_ % rates_.send_every == 0};
}

void MatchClock::cancel()
{
    cancelled_ = true;

    timer_.cancel();
}

} // namespace gs
