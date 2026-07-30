#include "net/session.hpp"

#include "app/log.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <game.pb.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace gs
{
namespace
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using boost::asio::as_tuple;
using boost::asio::use_awaitable;

/// Ile czasu klient ma na dojście od gniazda do `ClientHello`.
///
/// Krótko, bo połączenie bez biletu nie ma po co zajmować slotu ani deskryptora — a to
/// jest najtańszy sposób, w jaki ktoś mógłby próbować zająć proces meczu.
constexpr std::chrono::seconds handshake_timeout{10};

/// Cisza po tym czasie oznacza martwe połączenie. Beast pinguje sam po połowie tego okna.
constexpr std::chrono::seconds idle_timeout{30};

/// Czy ścieżka żądania to `/match/{matchId}` tego procesu.
///
/// Sprawdzane **przed** upgrade'em: proces obsługuje jeden mecz (D7), więc żądanie pod inny
/// identyfikator trafiło tu przez pomyłkę routingu i nie ma czego negocjować.
bool matches_path(std::string_view target, std::string_view match_id)
{
    constexpr std::string_view prefix = "/match/";

    if (!target.starts_with(prefix))
    {
        return false;
    }

    std::string_view id = target.substr(prefix.size());

    // Ucięcie ewentualnego query stringu — proxy potrafi coś dokleić.
    if (const std::size_t question = id.find('?'); question != std::string_view::npos)
    {
        id = id.substr(0, question);
    }

    if (id.size() != match_id.size())
    {
        return false;
    }

    return std::equal(
        id.begin(),
        id.end(),
        match_id.begin(),
        [](char left, char right)
        {
            const char a = left >= 'A' && left <= 'Z' ? static_cast<char>(left + 32) : left;
            const char b = right >= 'A' && right <= 'Z' ? static_cast<char>(right + 32) : right;

            return a == b;
        });
}

} // namespace

Session::Session(
    boost::asio::ip::tcp::socket socket,
    TicketVerifier& tickets,
    SessionRegistry& registry)
    : stream_(std::move(socket))
    , tickets_(tickets)
    , registry_(registry)
    , pending_(stream_.get_executor(), boost::asio::steady_timer::time_point::max())
{
}

void Session::start()
{
    boost::asio::co_spawn(
        stream_.get_executor(),
        [self = shared_from_this()] { return self->run(); },
        boost::asio::detached);
}

boost::asio::awaitable<void> Session::run()
{
    stream_.binary(true);

    websocket::stream_base::timeout timeouts{};
    timeouts.handshake_timeout = handshake_timeout;
    timeouts.idle_timeout = idle_timeout;
    // Beast pinguje sam i sam zrywa martwe połączenia. Przeglądarka odpowiada na ramkę ping
    // automatycznie, więc detekcja po tej stronie jest darmowa (§5⑤).
    timeouts.keep_alive_pings = true;

    stream_.set_option(timeouts);

    beast::flat_buffer buffer;
    http::request<http::empty_body> request;

    beast::get_lowest_layer(stream_).expires_after(handshake_timeout);

    if (const auto [error, ignored] =
            co_await http::async_read(stream_.next_layer(), buffer, request, as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    if (!matches_path(request.target(), tickets_.match()))
    {
        log::warn(
            "Żądanie pod ścieżkę {} — ten proces obsługuje mecz {}.",
            request.target(),
            tickets_.match());

        http::response<http::empty_body> not_found{http::status::not_found, request.version()};
        not_found.keep_alive(false);
        not_found.prepare_payload();

        co_await http::async_write(stream_.next_layer(), not_found, as_tuple(use_awaitable));

        co_return;
    }

    // Termin ustawiony na czas handshake'u trzeba **zdjąć**, zanim WebSocket przejmie
    // połączenie: od tej chwili terminami zarządza sam Beast (`idle_timeout` i pingi).
    // Zostawiony działa dalej i zrywa każde połączenie po dziesięciu sekundach — a że
    // wygląda to jak zerwanie sieci, w testach jednostkowych nie widać tego wcale.
    beast::get_lowest_layer(stream_).expires_never();

    if (const auto [error] = co_await stream_.async_accept(request, as_tuple(use_awaitable)); error)
    {
        co_return;
    }

    buffer.clear();

    if (const auto [error, ignored] = co_await stream_.async_read(buffer, as_tuple(use_awaitable));
        error)
    {
        co_return;
    }

    game::ClientMsg hello;

    if (!hello.ParseFromString(beast::buffers_to_string(buffer.data()))
        || hello.msg_case() != game::ClientMsg::kHello)
    {
        co_await reject(TicketError::malformed);

        co_return;
    }

    auto ticket = tickets_.verify(hello.hello().ticket(), std::chrono::system_clock::now());

    if (!ticket)
    {
        co_await reject(ticket.error());

        co_return;
    }

    slot_ = ticket->slot;

    // Powrót gracza wypiera jego poprzednie połączenie — bez tego odświeżenie strony
    // zostawiałoby zombie trzymające slot (D14).
    registry_.drop_previous_on(slot_, this);
    registry_.add(shared_from_this());
    registered_ = true;

    log::info(
        "Gracz {} wszedł na slot {}; połączeń: {}.",
        ticket->player_id,
        slot_,
        registry_.size());

    boost::asio::co_spawn(
        stream_.get_executor(),
        [self = shared_from_this()] { return self->write_loop(); },
        boost::asio::detached);

    co_await read_loop();

    stopping_ = true;
    pending_.cancel();

    if (registered_)
    {
        registry_.remove(this);
        registered_ = false;

        log::info("Slot {} rozłączony; połączeń: {}.", slot_, registry_.size());
    }
}

boost::asio::awaitable<void> Session::read_loop()
{
    for (;;)
    {
        beast::flat_buffer buffer;

        const auto [error, ignored] = co_await stream_.async_read(buffer, as_tuple(use_awaitable));

        if (error)
        {
            co_return;
        }

        game::ClientMsg message;

        if (!message.ParseFromString(beast::buffers_to_string(buffer.data())))
        {
            // Śmieci w strumieniu binarnym to albo zły klient, albo zła wersja protokołu.
            // W obu przypadkach dalsze czytanie nie ma sensu.
            co_return;
        }

        switch (message.msg_case())
        {
        case game::ClientMsg::kPing:
        {
            // Odsyłamy znacznik bez zmian — RTT liczy klient, bo przeglądarka nie daje
            // JavaScriptowi dostępu do natywnych ramek ping/pong (§5⑤).
            game::ServerMsg pong;
            pong.mutable_pong()->set_client_time_ms(message.ping().client_time_ms());

            send(std::make_shared<const std::string>(pong.SerializeAsString()));

            break;
        }
        case game::ClientMsg::kCommand:
            // Tu wejdzie kolejka komend. Etap E2 nie ma jeszcze czego nimi poruszyć.
            break;
        case game::ClientMsg::kHello:
        case game::ClientMsg::MSG_NOT_SET:
            break;
        }
    }
}

boost::asio::awaitable<void> Session::write_loop()
{
    for (;;)
    {
        if (queue_.empty())
        {
            if (stopping_)
            {
                co_return;
            }

            // Termin jest w nieskończoności — budzi nas wyłącznie `cancel()` z `send`.
            co_await pending_.async_wait(as_tuple(use_awaitable));

            continue;
        }

        const std::shared_ptr<const std::string> frame = queue_.front();

        const auto [error, ignored] =
            co_await stream_.async_write(boost::asio::buffer(*frame), as_tuple(use_awaitable));

        queue_.pop_front();
        queued_bytes_ -= frame->size();

        if (error)
        {
            co_return;
        }
    }
}

boost::asio::awaitable<void> Session::reject(TicketError error)
{
    log::warn("Bilet odrzucony — {}.", describe(error));

    // Jeden kod dla wszystkich powodów: gracz i tak dostaje „bilet odrzucony", a różnicowanie
    // powiedziałoby próbującemu, jak blisko celu jest.
    co_await stream_.async_close(
        websocket::close_reason{websocket::close_code::policy_error, "ticket"},
        as_tuple(use_awaitable));
}

void Session::send(std::shared_ptr<const std::string> frame)
{
    if (stopping_ || !frame)
    {
        return;
    }

    if (queued_bytes_ + frame->size() > max_queued_bytes)
    {
        // Klient nie nadąża (D4). Odbudowa łańcucha delt kosztowałaby keyframe i osobną
        // ścieżkę w kodzie; rozłączenie kosztuje nic, a gracz wraca biletem.
        log::warn(
            "Slot {} nie nadąża: {} B w kolejce wyjściowej. Rozłączam.",
            slot_,
            queued_bytes_);

        stop();

        return;
    }

    queued_bytes_ += frame->size();
    queue_.push_back(std::move(frame));

    pending_.cancel();
}

void Session::stop()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;

    pending_.cancel();

    boost::system::error_code ignored;
    beast::get_lowest_layer(stream_).socket().close(ignored);
}

void Session::close_gracefully()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;

    pending_.cancel();

    boost::asio::co_spawn(
        stream_.get_executor(),
        [self = shared_from_this()]() -> boost::asio::awaitable<void>
        {
            // Błąd nie ma tu znaczenia: jeśli ramka zamknięcia nie doszła, to znaczy,
            // że po drugiej stronie i tak nikogo już nie ma.
            co_await self->stream_.async_close(
                websocket::close_code::going_away,
                as_tuple(use_awaitable));
        },
        boost::asio::detached);
}

void SessionRegistry::add(const std::shared_ptr<Session>& session)
{
    sessions_.push_back(session);
}

void SessionRegistry::remove(const Session* session)
{
    std::erase_if(
        sessions_,
        [session](const std::shared_ptr<Session>& candidate) { return candidate.get() == session; });
}

void SessionRegistry::broadcast(std::shared_ptr<const std::string> frame)
{
    // Iteracja po żywej liście jest bezpieczna: `send` może najwyżej zamknąć gniazdo,
    // a wypisanie z rejestru dzieje się dopiero, gdy korutyna sesji się obudzi.
    for (const std::shared_ptr<Session>& session : sessions_)
    {
        session->send(frame);
    }
}

void SessionRegistry::drop_previous_on(std::uint8_t slot, const Session* keep)
{
    for (const std::shared_ptr<Session>& session : sessions_)
    {
        if (session.get() != keep && session->slot() == slot)
        {
            session->stop();
        }
    }
}

void SessionRegistry::close_all()
{
    // Kopia, bo zamykanie sesji kończy ich korutyny, a te wypisują się z tej listy.
    const std::vector<std::shared_ptr<Session>> closing = sessions_;

    for (const std::shared_ptr<Session>& session : closing)
    {
        session->close_gracefully();
    }
}

} // namespace gs
