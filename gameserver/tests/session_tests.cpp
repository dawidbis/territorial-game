#include "meta/ticket.hpp"
#include "net/listener.hpp"
#include "net/session.hpp"
#include "ticket_vectors.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <game.pb.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Testy całej drogi wejścia: gniazdo → upgrade → ClientHello → weryfikacja biletu →
// snapshot. Prawdziwy jest tu każdy element poza samą siecią rozległą: klient to Beast,
// bilety wystawiła meta, a weryfikacja idzie przez OpenSSL.
namespace
{
using namespace vectors;

namespace beast = boost::beast;
namespace websocket = beast::websocket;

using boost::asio::as_tuple;
using boost::asio::awaitable;
using boost::asio::use_awaitable;
using tcp = boost::asio::ip::tcp;

/// Ile test czeka na cokolwiek, zanim uzna, że to się już nie stanie.
constexpr std::chrono::seconds patience{5};

/// Co widział klient, który próbował wejść do meczu.
struct Attempt
{
    /// Czy serwer zgodził się na upgrade do WebSocketa.
    bool upgraded = false;

    /// Czy po `ClientHello` przyszedł snapshot — czyli czy bilet został przyjęty.
    bool admitted = false;

    std::uint32_t tick = 0;

    websocket::close_code closed_with = websocket::close_code::none;

    bool finished = false;
};

std::shared_ptr<const std::string> snapshot_frame(std::uint32_t tick)
{
    game::ServerMsg message;
    message.mutable_snapshot()->set_tick(tick);

    return std::make_shared<const std::string>(message.SerializeAsString());
}

awaitable<void> client(std::uint16_t port, std::string path, std::string ticket, Attempt& result)
{
    websocket::stream<beast::tcp_stream> stream(co_await boost::asio::this_coro::executor);

    const tcp::endpoint endpoint{boost::asio::ip::make_address("127.0.0.1"), port};

    if (const auto [error] = co_await beast::get_lowest_layer(stream).async_connect(
            endpoint,
            as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    if (const auto [error] =
            co_await stream.async_handshake("127.0.0.1", path, as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    result.upgraded = true;
    stream.binary(true);

    game::ClientMsg hello;
    hello.mutable_hello()->set_ticket(ticket);

    const std::string payload = hello.SerializeAsString();

    if (const auto [error, ignored] =
            co_await stream.async_write(boost::asio::buffer(payload), as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    beast::flat_buffer buffer;

    const auto [error, ignored] = co_await stream.async_read(buffer, as_tuple(use_awaitable));

    if (error)
    {
        // Serwer zamknął połączenie — powód siedzi w ramce zamknięcia.
        result.closed_with = static_cast<websocket::close_code>(stream.reason().code);

        co_return;
    }

    game::ServerMsg message;

    if (message.ParseFromString(beast::buffers_to_string(buffer.data())) && message.has_snapshot())
    {
        result.admitted = true;
        result.tick = message.snapshot().tick();
    }

    co_await stream.async_close(websocket::close_code::normal, as_tuple(use_awaitable));
}

/// Klient, który wchodzi do meczu i przestaje czytać.
///
/// Nie czyta **niczego** — nawet pierwszej ramki — więc bufory zapychają się po stronie
/// serwera. Tak wygląda karta uśpiona przez przeglądarkę albo łącze, które padło bez
/// zamknięcia gniazda.
awaitable<void> silent_client(std::uint16_t port, std::string path, std::string ticket)
{
    websocket::stream<beast::tcp_stream> stream(co_await boost::asio::this_coro::executor);

    const tcp::endpoint endpoint{boost::asio::ip::make_address("127.0.0.1"), port};

    if (const auto [error] = co_await beast::get_lowest_layer(stream).async_connect(
            endpoint,
            as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    if (const auto [error] =
            co_await stream.async_handshake("127.0.0.1", path, as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    stream.binary(true);

    game::ClientMsg hello;
    hello.mutable_hello()->set_ticket(ticket);

    const std::string payload = hello.SerializeAsString();

    co_await stream.async_write(boost::asio::buffer(payload), as_tuple(use_awaitable));

    // Milczymy do końca testu. Gniazdo zostaje otwarte, więc serwer nie ma jak zauważyć
    // niczego poza rosnącą kolejką.
    boost::asio::steady_timer forever(co_await boost::asio::this_coro::executor);
    forever.expires_after(patience);

    co_await forever.async_wait(as_tuple(use_awaitable));
}

/// Czeka, aż warunek stanie się prawdą. Odpytywanie, bo rejestr sesji nie ma powiadomień —
/// a w teście na jednym wątku milisekundowy takt jest tańszy niż zbudowanie ich na potrzeby
/// samego testu.
awaitable<bool> wait_until(std::function<bool()> condition, std::chrono::seconds limit)
{
    boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);

    const auto deadline = std::chrono::steady_clock::now() + limit;

    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            co_return false;
        }

        timer.expires_after(std::chrono::milliseconds{1});

        co_await timer.async_wait(as_tuple(use_awaitable));
    }

    co_return true;
}

/// Proces meczu postawiony na porcie efemerycznym.
class MatchUnderTest
{
public:
    MatchUnderTest()
        : verifier(make_verifier())
        , watchdog(io, patience)
    {
        auto acceptor = std::make_shared<tcp::acceptor>(gs::listen_on_loopback(io.get_executor(), 0));

        port_ = acceptor->local_endpoint().port();

        boost::asio::co_spawn(
            io,
            gs::accept_connections(std::move(acceptor), verifier, sessions),
            boost::asio::detached);

        // Bez tego zawieszony test wisiałby do końca świata zamiast czerwienić się po pięciu
        // sekundach.
        watchdog.async_wait(
            [this](const boost::system::error_code& error)
            {
                if (!error)
                {
                    io.stop();
                }
            });
    }

    std::uint16_t port() const noexcept
    {
        return port_;
    }

    /// Odpala klienta i odnotowuje, że skończył.
    void spawn_client(Attempt& result, std::string ticket, std::string path = {})
    {
        boost::asio::co_spawn(
            io,
            client(port_, path.empty() ? default_path() : std::move(path), std::move(ticket), result),
            [&result](const std::exception_ptr&) { result.finished = true; });
    }

    /// Uruchamia scenariusz i zatrzymuje pętlę, gdy ten się skończy.
    void run(std::function<awaitable<void>()> scenario)
    {
        boost::asio::co_spawn(io, scenario(), [this](const std::exception_ptr&) { io.stop(); });

        io.run();
    }

    std::string default_path() const
    {
        return "/match/" + std::string{match_id};
    }

    gs::TicketVerifier verifier;

    gs::SessionRegistry sessions;

    boost::asio::io_context io;

private:
    static gs::TicketVerifier make_verifier()
    {
        auto verifier = gs::TicketVerifier::from_pem(public_key_pem, std::string{match_id}, 100);

        EXPECT_TRUE(verifier.has_value());

        return std::move(*verifier);
    }

    boost::asio::steady_timer watchdog;

    std::uint16_t port_ = 0;
};

} // namespace

TEST(SessionTest, AdmitsAPlayerWithATicketFromMeta)
{
    MatchUnderTest match;

    Attempt player;
    match.spawn_client(player, std::string{valid_token});

    match.run(
        [&]() -> awaitable<void>
        {
            const bool joined = co_await wait_until(
                [&] { return match.sessions.size() == 1; },
                patience);

            EXPECT_TRUE(joined);

            match.sessions.broadcast(snapshot_frame(42));

            co_await wait_until([&] { return player.finished; }, patience);
        });

    EXPECT_TRUE(player.upgraded);
    EXPECT_TRUE(player.admitted);
    EXPECT_EQ(player.tick, 42u);
}

// Podpis wystawiony obcym kluczem — połączenie dochodzi do upgrade'u, ale dalej nie.
// Kod zamknięcia jest jeden dla wszystkich powodów odrzucenia, żeby nie podpowiadać,
// jak blisko celu był próbujący.
TEST(SessionTest, ClosesTheConnectionOnAForgedTicket)
{
    MatchUnderTest match;

    Attempt intruder;
    match.spawn_client(intruder, std::string{foreign_key_token});

    match.run([&]() -> awaitable<void>
              { co_await wait_until([&] { return intruder.finished; }, patience); });

    EXPECT_TRUE(intruder.upgraded);
    EXPECT_FALSE(intruder.admitted);
    EXPECT_EQ(intruder.closed_with, websocket::close_code::policy_error);
    EXPECT_EQ(match.sessions.size(), 0u);
}

// Proces obsługuje jeden mecz (D7). Żądanie pod inny identyfikator to pomyłka routingu,
// więc odpowiedź pada przed upgrade'em — nie ma czego negocjować.
TEST(SessionTest, RefusesTheUpgradeUnderAnotherMatchId)
{
    MatchUnderTest match;

    Attempt stray;
    match.spawn_client(stray, std::string{valid_token}, "/match/018f3a2b-0000-0000-0000-000000000000");

    match.run([&]() -> awaitable<void>
              { co_await wait_until([&] { return stray.finished; }, patience); });

    EXPECT_FALSE(stray.upgraded);
    EXPECT_EQ(match.sessions.size(), 0u);
}

/// D4: rosnąca kolejka wyjściowa znaczy, że klient już nie żyje.
///
/// Klient w tym teście **nie czyta nic** po wejściu — dokładnie tak wygląda karta, której
/// przeglądarka przestała obsługiwać, albo łącze, które padło bez zamknięcia gniazda.
/// Serwer ma go rozłączyć, a nie odkładać dla niego megabajty.
TEST(SessionTest, DropsAClientThatStopsReading)
{
    MatchUnderTest match;

    boost::asio::co_spawn(
        match.io,
        silent_client(match.port(), match.default_path(), std::string{valid_token}),
        boost::asio::detached);

    // Ramka grubo poniżej progu, ale kilkadziesiąt takich już go przekracza.
    const std::shared_ptr<const std::string> heavy =
        std::make_shared<const std::string>(32 * 1024, 'x');

    match.run(
        [&]() -> awaitable<void>
        {
            EXPECT_TRUE(co_await wait_until([&] { return match.sessions.size() == 1; }, patience));

            // Klient czeka na pierwszą ramkę i po niej już nie czyta, więc bufory zapychają
            // się gdzieś między nim a nami. Po przekroczeniu progu sesja ma zniknąć.
            while (match.sessions.size() == 1)
            {
                match.sessions.broadcast(heavy);

                boost::asio::steady_timer pause(co_await boost::asio::this_coro::executor);
                pause.expires_after(std::chrono::milliseconds{1});

                co_await pause.async_wait(as_tuple(use_awaitable));
            }
        });

    EXPECT_EQ(match.sessions.size(), 0u);
}

/// Reconnect z D14 w najprostszej postaci: gracz, który wrócił, wypiera swoje poprzednie
/// wcielenie. Bez tego odświeżenie strony zostawiałoby zombie trzymające slot do końca meczu.
TEST(SessionTest, ARejoiningPlayerReplacesHisEarlierConnection)
{
    MatchUnderTest match;

    Attempt first;
    Attempt second;

    // Ile sesji zostało w chwili, gdy powracający gracz był już w środku. Zdejmowane
    // w trakcie, bo po teście jest zero — drugi klient rozłącza się sam na koniec.
    std::size_t after_rejoin = 0;

    match.spawn_client(first, std::string{valid_token});

    match.run(
        [&]() -> awaitable<void>
        {
            EXPECT_TRUE(co_await wait_until([&] { return match.sessions.size() == 1; }, patience));

            // Ten sam slot, inny bilet — dokładnie to, co dzieje się po F5.
            match.spawn_client(second, std::string{second_token});

            EXPECT_TRUE(co_await wait_until([&] { return first.finished; }, patience));
            EXPECT_TRUE(co_await wait_until([&] { return match.sessions.size() == 1; }, patience));

            after_rejoin = match.sessions.size();

            match.sessions.broadcast(snapshot_frame(7));

            co_await wait_until([&] { return second.finished; }, patience);
        });

    EXPECT_TRUE(first.upgraded);
    EXPECT_FALSE(first.admitted);
    EXPECT_TRUE(second.admitted);
    EXPECT_EQ(after_rejoin, 1u);
}
