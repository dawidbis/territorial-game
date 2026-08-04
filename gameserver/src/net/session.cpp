#include "net/session.hpp"

#include "app/log.hpp"
#include "net/commands.hpp"
#include "net/session_registry.hpp"
#include "state/match_intro.hpp"
#include "tick/match_clock.hpp"

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

/// Czy ścieżka żądania to `/ws/match/{matchId}` tego procesu.
///
/// Sprawdzane **przed** upgrade'em: proces obsługuje jeden mecz (D7), więc żądanie pod inny
/// identyfikator trafiło tu przez pomyłkę routingu i nie ma czego negocjować.
///
/// **Prefiks `/ws/` nie jest ozdobą.** Klient ma własną trasę `/match/{matchId}` — zwykłą
/// stronę SPA — i dopóki gniazdo stało pod tym samym adresem, odświeżenie strony w trakcie
/// meczu wysyłało żądanie dokumentu HTML tam, gdzie stoi WebSocket. Wspólne wejście na 443
/// routuje po ścieżce (D9), więc dwie różne rzeczy nie mogą dzielić jednej.
bool matches_path(std::string_view target, std::string_view match_id)
{
    constexpr std::string_view prefix = "/ws/match/";

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

Session::Session(boost::asio::ip::tcp::socket socket, MatchServices& services)
    : stream_(std::move(socket))
    , services_(services)
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
    if (!co_await accept_websocket())
    {
        co_return;
    }

    if (!co_await authenticate())
    {
        co_return;
    }

    join();

    co_await read_loop();

    stopping_ = true;
    pending_.cancel();

    leave();
}

boost::asio::awaitable<bool> Session::accept_websocket()
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
        co_return false;
    }

    if (!matches_path(request.target(), services_.tickets.match()))
    {
        log::warn(
            "Żądanie pod ścieżkę {} — ten proces obsługuje mecz {}.",
            request.target(),
            services_.tickets.match());

        http::response<http::empty_body> not_found{http::status::not_found, request.version()};
        not_found.keep_alive(false);
        not_found.prepare_payload();

        co_await http::async_write(stream_.next_layer(), not_found, as_tuple(use_awaitable));

        co_return false;
    }

    // Termin ustawiony na czas handshake'u trzeba **zdjąć**, zanim WebSocket przejmie
    // połączenie: od tej chwili terminami zarządza sam Beast (`idle_timeout` i pingi).
    // Zostawiony działa dalej i zrywa każde połączenie po dziesięciu sekundach — a że
    // wygląda to jak zerwanie sieci, w testach jednostkowych nie widać tego wcale.
    beast::get_lowest_layer(stream_).expires_never();

    const auto [error] = co_await stream_.async_accept(request, as_tuple(use_awaitable));

    co_return !error;
}

boost::asio::awaitable<bool> Session::authenticate()
{
    beast::flat_buffer buffer;

    if (const auto [error, ignored] = co_await stream_.async_read(buffer, as_tuple(use_awaitable));
        error)
    {
        co_return false;
    }

    game::ClientMsg hello;

    if (!hello.ParseFromString(beast::buffers_to_string(buffer.data()))
        || hello.msg_case() != game::ClientMsg::kHello)
    {
        co_await reject(TicketError::malformed);

        co_return false;
    }

    const std::expected<Ticket, TicketError> ticket =
        services_.tickets.verify(hello.hello().ticket(), std::chrono::system_clock::now());

    if (!ticket)
    {
        co_await reject(ticket.error());

        co_return false;
    }

    slot_ = ticket->slot;
    player_id_ = ticket->player_id;

    co_return true;
}

void Session::join()
{
    // Powrót gracza wypiera jego poprzednie połączenie — bez tego odświeżenie strony
    // zostawiałoby zombie trzymające slot (D14).
    services_.sessions.drop_previous_on(slot_, this);
    services_.sessions.add(shared_from_this());
    registered_ = true;

    log::info(
        "Gracz {} wszedł na slot {}; połączeń: {}.",
        player_id_,
        slot_,
        services_.sessions.size());

    // Kolejność jest częścią kontraktu: klient buduje paletę i tablicę właścicieli z
    // `MatchInit`, a dopiero potem ma czym wypełnić ją keyframe'em. Obie idą do kolejki,
    // zanim ruszy pętla wysyłki — pierwsze, co gracz zobaczy, to mapa.
    send(services_.intro.init_for(slot_));
    send(services_.intro.keyframe_at(services_.clock.tick()));

    boost::asio::co_spawn(
        stream_.get_executor(),
        [self = shared_from_this()] { return self->write_loop(); },
        boost::asio::detached);
}

void Session::leave()
{
    if (!registered_)
    {
        return;
    }

    services_.sessions.remove(this);
    registered_ = false;

    log::info("Slot {} rozłączony; połączeń: {}.", slot_, services_.sessions.size());
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
            handle_command(message.command());
            break;
        case game::ClientMsg::kHello:
        case game::ClientMsg::MSG_NOT_SET:
            break;
        }
    }
}

void Session::handle_command(const game::Command& command)
{
    const game::RejectReason reason = execute_command(command, services_.simulation, slot_);

    // Przyjęty rozkaz nie generuje żadnej odpowiedzi: jego skutek gracz zobaczy w najbliższym
    // snapshocie. Odrzucony **musi** wrócić — rozkaz, który zniknął bez śladu, wygląda
    // z drugiej strony dokładnie tak samo jak zerwana sieć.
    if (reason == game::REJECT_REASON_UNSPECIFIED)
    {
        return;
    }

    game::ServerMsg rejected;
    rejected.mutable_rejected()->set_seq(command.seq());
    rejected.mutable_rejected()->set_reason(reason);

    send(std::make_shared<const std::string>(rejected.SerializeAsString()));
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

    // Wynik świadomie porzucany: to jest zrywanie połączenia, a jedyną możliwą reakcją na
    // „nie udało się zamknąć gniazda" byłoby zamknięcie go jeszcze raz.
    boost::system::error_code ignored;
    static_cast<void>(beast::get_lowest_layer(stream_).socket().close(ignored));
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

} // namespace gs
