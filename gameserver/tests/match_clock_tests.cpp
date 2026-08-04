#include "tick/match_clock.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{

// Skrócony krok, żeby testy trwały milisekundy zamiast sekund. Właściwości, których
// pilnujemy — numeracja tików i podział na wysyłkowe — nie zależą od długości kroku.
constexpr std::chrono::milliseconds fast_step{2};

/// Zbiera pierwsze `count` tików.
///
/// Asercje `ASSERT_*` nie mogą wejść do korutyny — rozwijają się do `return`, a korutyna
/// wymaga `co_return`. Dlatego korutyna wyłącznie zbiera dane, a sprawdza je test.
std::vector<gs::Tick> collect(gs::TickRates rates, std::uint32_t count)
{
    boost::asio::io_context io;

    std::vector<gs::Tick> ticks;

    boost::asio::co_spawn(
        io,
        [&]() -> boost::asio::awaitable<void>
        {
            gs::MatchClock clock(io.get_executor(), rates);

            while (const std::optional<gs::Tick> tick = co_await clock.next())
            {
                ticks.push_back(*tick);

                if (tick->number >= count)
                {
                    break;
                }
            }
        },
        boost::asio::detached);

    io.run();

    return ticks;
}

} // namespace

TEST(MatchClockTest, NumbersTicksConsecutivelyFromOne)
{
    const std::vector<gs::Tick> ticks = collect(gs::TickRates{fast_step, 2}, 10);

    ASSERT_EQ(ticks.size(), 10u);

    for (std::size_t index = 0; index < ticks.size(); ++index)
    {
        EXPECT_EQ(ticks[index].number, index + 1);
    }
}

// Domyślne tempo jest **kontraktem z klientem**, a nie ustawieniem: klient animuje
// przejmowanie kafelków z tego, co przyszło ostatnią paczką, więc wysyłka rzadsza niż
// symulacja zamienia animację w zgadywanie. Świadome odstępstwo od D3 („send 5 Hz").
TEST(MatchClockTest, SendsOnEveryTickByDefault)
{
    const gs::TickRates rates;

    EXPECT_EQ(rates.send_every, 1u);
    EXPECT_EQ(rates.sim_period, std::chrono::milliseconds{100});
}

// Podział zostaje konfigurowalny, bo to on decyduje, ile ruchu wychodzi z serwera —
// powrót do wysyłki co drugi tik jest jedną wartością, nie przepisaniem pętli.
TEST(MatchClockTest, MarksEverySecondTickForSending)
{
    const std::vector<gs::Tick> ticks = collect(gs::TickRates{fast_step, 2}, 6);

    std::vector<std::uint32_t> sending;

    for (const gs::Tick& tick : ticks)
    {
        if (tick.send)
        {
            sending.push_back(tick.number);
        }
    }

    EXPECT_EQ(sending, (std::vector<std::uint32_t>{2, 4, 6}));
}

TEST(MatchClockTest, MarksEveryTickWhenAskedTo)
{
    const std::vector<gs::Tick> ticks = collect(gs::TickRates{fast_step, 1}, 3);

    ASSERT_EQ(ticks.size(), 3u);

    for (const gs::Tick& tick : ticks)
    {
        EXPECT_TRUE(tick.send);
    }
}

// Zatrzymanie musi obudzić zegar natychmiast, a nie po upływie kroku. Inaczej wyjście
// z meczu czekałoby na kolejny tik, a przy gaszeniu procesu (E3) na cały krok symulacji.
TEST(MatchClockTest, CancelWakesTheWaitingClock)
{
    boost::asio::io_context io;

    std::optional<gs::Tick> result;
    bool finished = false;

    // Krok celowo długi: gdyby anulowanie czekało na termin, test trwałby pół sekundy
    // i zwróciłby tik zamiast pustki.
    gs::MatchClock clock(io.get_executor(), gs::TickRates{std::chrono::milliseconds{500}, 2});

    boost::asio::co_spawn(
        io,
        [&]() -> boost::asio::awaitable<void>
        {
            result = co_await clock.next();
            finished = true;
        },
        boost::asio::detached);

    boost::asio::post(io, [&clock] { clock.cancel(); });

    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    io.run();

    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_TRUE(finished);
    EXPECT_FALSE(result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds{250});
}

TEST(MatchClockTest, CancelledClockStaysCancelled)
{
    boost::asio::io_context io;

    int results = 0;

    boost::asio::co_spawn(
        io,
        [&]() -> boost::asio::awaitable<void>
        {
            gs::MatchClock clock(io.get_executor(), gs::TickRates{fast_step, 2});

            clock.cancel();

            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (co_await clock.next())
                {
                    ++results;
                }
            }
        },
        boost::asio::detached);

    io.run();

    EXPECT_EQ(results, 0);
}
