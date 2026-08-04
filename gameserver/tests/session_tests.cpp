#include "map/map_file.hpp"
#include "map/tmap.hpp"
#include "map_fixtures.hpp"
#include "meta/ticket.hpp"
#include "net/listener.hpp"
#include "net/match_services.hpp"
#include "net/session_registry.hpp"
#include "sim/simulation.hpp"
#include "sim/world.hpp"
#include "state/match_intro.hpp"
#include "tick/match_clock.hpp"
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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

    /// Czy po `ClientHello` przyszedł `MatchInit` — czyli czy bilet został przyjęty.
    bool admitted = false;

    std::uint32_t your_slot = 0;

    std::uint32_t map_width = 0;

    std::string map_id;

    std::size_t map_sha256_length = 0;

    /// Obsada z `MatchInit` — nick własnego slotu i to, czy w liście są też boty.
    int slot_count = 0;

    std::string own_name;

    bool saw_a_bot = false;

    /// Ile runów niósł keyframe. Zero znaczy „mapa nie doszła", a nie „mapa jest pusta":
    /// woda daje runy nawet wtedy, gdy nikt jeszcze nie ma ani jednego kafelka.
    int keyframe_runs = -1;

    /// Pierwszy zwykły snapshot po keyframie.
    bool saw_snapshot = false;

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

    // Czytamy do pierwszego zwykłego snapshotu, bo przed nim jadą dwie wiadomości powitalne:
    // `MatchInit` i keyframe. Klient, który zakłada, że pierwsza ramka to snapshot, przegapiłby
    // całą mapę.
    while (!result.saw_snapshot)
    {
        beast::flat_buffer buffer;

        const auto [error, ignored] = co_await stream.async_read(buffer, as_tuple(use_awaitable));

        if (error)
        {
            // Serwer zamknął połączenie — powód siedzi w ramce zamknięcia.
            result.closed_with = static_cast<websocket::close_code>(stream.reason().code);

            co_return;
        }

        game::ServerMsg message;

        if (!message.ParseFromString(beast::buffers_to_string(buffer.data())))
        {
            co_return;
        }

        if (message.has_init())
        {
            result.admitted = true;
            result.your_slot = message.init().your_slot();
            result.map_width = message.init().map_width();
            result.map_id = message.init().map_id();
            result.map_sha256_length = message.init().map_sha256().size();
            result.slot_count = message.init().slots_size();

            for (const game::SlotInfo& slot : message.init().slots())
            {
                if (slot.slot() == result.your_slot)
                {
                    result.own_name = slot.name();
                }

                result.saw_a_bot = result.saw_a_bot || slot.is_bot();
            }
        }
        else if (message.has_snapshot() && message.snapshot().is_keyframe())
        {
            result.keyframe_runs = message.snapshot().runs_size();
        }
        else if (message.has_snapshot())
        {
            result.saw_snapshot = true;
            result.tick = message.snapshot().tick();
        }
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

/// Świat testowy: pięć kafelków, z czego dwa niczyje. Keyframe ma z tego wyjść dwa runy —
/// tyle wystarczy, żeby sprawdzić, że mapa w ogóle jedzie przez gniazdo.
const std::vector<std::uint8_t> test_owner{255, 255, 0, 0, 255};

/// Obsada: człowiek na slocie 7 (to jego bilet niosą wektory testowe) i jeden bot.
const std::vector<gs::Actor> test_actors{
    gs::Actor{7, "Ala", 0xFF0000, false},
    gs::Actor{8, "Kadar", 0x00FF00, true}};

/// Mapa dla symulacji, którą trzymają usługi sesji.
///
/// Osobna od `test_owner` powyżej i tak ma zostać: tamten wektor opisuje keyframe wysyłany
/// przez gniazdo, a ten świat istnieje wyłącznie po to, żeby rozkaz gracza miał na czym
/// wylądować. Testy tej mechaniki stoją w `simulation_tests.cpp`.
gs::MapFile test_map()
{
    const std::expected<std::string, std::string> bytes = gs::tmap::encode(fixtures::small_map());

    EXPECT_TRUE(bytes.has_value());

    std::expected<gs::MapFile, std::string> file =
        gs::MapFile::from_bytes(std::vector<std::uint8_t>(bytes->begin(), bytes->end()));

    EXPECT_TRUE(file.has_value());

    return std::move(*file);
}

gs::MatchDescription test_description()
{
    gs::MatchDescription description;

    description.map_id = "test";
    description.map_sha256.fill(0xAB);
    description.map_width = 5;
    description.map_height = 1;
    description.tick_rate = 10;
    description.seed = 42;

    return description;
}

/// Proces meczu postawiony na porcie efemerycznym.
class MatchUnderTest
{
public:
    MatchUnderTest()
        : verifier(make_verifier())
        , clock(io.get_executor(), gs::TickRates{})
        , intro(test_description(), test_actors, test_owner)
        , map(test_map())
        , world(map.map())
        , simulation(world, test_actors, 42)
        , services{verifier, sessions, intro, clock, simulation}
        , watchdog(io, patience)
    {
        auto acceptor = std::make_shared<tcp::acceptor>(gs::listen_on_loopback(io.get_executor(), 0));

        port_ = acceptor->local_endpoint().port();

        boost::asio::co_spawn(
            io,
            gs::accept_connections(std::move(acceptor), services),
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
        return "/ws/match/" + std::string{match_id};
    }

    // Kolejność deklaracji jest kolejnością konstruowania: zegar bierze executor z `io`,
    // a usługi trzymają referencje do wszystkiego powyżej.
    boost::asio::io_context io;

    gs::TicketVerifier verifier;

    gs::SessionRegistry sessions;

    gs::MatchClock clock;

    gs::MatchIntro intro;

    /// Plik trzyma bufor, w który patrzy `MapView` świata — musi go przeżyć.
    gs::MapFile map;

    gs::World world;

    gs::Simulation simulation;

    gs::MatchServices services;

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

/// Kryterium etapu E3 po stronie gniazda: gracz dostaje mapę, zanim cokolwiek zacznie się
/// zmieniać. Dwie wiadomości, w tej kolejności — `MatchInit` z opisem mapy, potem keyframe
/// z tablicą właścicieli. Keyframe nie jest pusty, bo woda też ma właściciela (D12).
TEST(SessionTest, SendsTheMapBeforeTheFirstSnapshot)
{
    MatchUnderTest match;

    Attempt player;
    match.spawn_client(player, std::string{valid_token});

    match.run(
        [&]() -> awaitable<void>
        {
            EXPECT_TRUE(co_await wait_until([&] { return match.sessions.size() == 1; }, patience));

            match.sessions.broadcast(snapshot_frame(1));

            co_await wait_until([&] { return player.finished; }, patience);
        });

    EXPECT_TRUE(player.admitted);
    EXPECT_EQ(player.map_id, "test");
    EXPECT_EQ(player.map_width, 5u);
    EXPECT_EQ(player.map_sha256_length, 32u);
    EXPECT_EQ(player.your_slot, 7u);

    // Dwa runy wody rozdzielone dwoma kafelkami pustkowia — patrz `test_owner`.
    EXPECT_EQ(player.keyframe_runs, 2);

    // Roster jedzie w tej samej wiadomości: gracz ma z czego zbudować paletę, zanim
    // zobaczy pierwszy kafelek cudzego terytorium.
    EXPECT_EQ(player.slot_count, 2);
    EXPECT_EQ(player.own_name, "Ala");
    EXPECT_TRUE(player.saw_a_bot);
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
    match.spawn_client(
        stray,
        std::string{valid_token},
        "/ws/match/018f3a2b-0000-0000-0000-000000000000");

    match.run([&]() -> awaitable<void>
              { co_await wait_until([&] { return stray.finished; }, patience); });

    EXPECT_FALSE(stray.upgraded);
    EXPECT_EQ(match.sessions.size(), 0u);
}

/// Trasa SPA klienta to `/match/{matchId}`, a gniazdo stoi pod `/ws/match/{matchId}` —
/// i te dwie ścieżki muszą pozostać rozłączne. Dopóki dzieliły jedną, odświeżenie strony
/// w trakcie meczu wysyłało żądanie dokumentu HTML na WebSocket, a gracz zamiast mapy
/// dostawał komunikat o brakującym nagłówku upgrade'u.
TEST(SessionTest, RefusesTheUpgradeUnderTheSinglePageAppPath)
{
    MatchUnderTest match;

    Attempt stray;
    match.spawn_client(stray, std::string{valid_token}, "/match/" + std::string{match_id});

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

    // Pierwsze wcielenie weszło i zobaczyło mapę — i właśnie dlatego test jest o czymś:
    // wyparte zostało połączenie, które **działało**, a nie takie, które i tak nie doszło.
    EXPECT_TRUE(first.admitted);
    EXPECT_FALSE(first.saw_snapshot);

    EXPECT_TRUE(second.admitted);
    EXPECT_TRUE(second.saw_snapshot);
    EXPECT_EQ(after_rejoin, 1u);
}
