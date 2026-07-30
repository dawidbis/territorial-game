#pragma once

#include "meta/ticket.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace gs
{

class SessionRegistry;

/// Jedno połączenie gracza.
///
/// Cała sesja jest jedną korutyną czytaną z góry na dół: upgrade → `ClientHello` →
/// weryfikacja biletu → pętla wiadomości. Ramka korutyny **jest** stanem sesji, więc nie
/// ma osobnej maszyny stanów ani przekazywania danych między uchwytami.
///
/// Wysyłka idzie drugą korutyną, bo snapshot przychodzi z zegara meczu, a nie w odpowiedzi
/// na cokolwiek od gracza. Obie żyją na tym samym wątku (D8), więc kolejka nie potrzebuje
/// żadnej synchronizacji.
class Session : public std::enable_shared_from_this<Session>
{
public:
    /// Próg zerwania połączenia z niedomagającym klientem (D4).
    ///
    /// Przy ~1 KB na snapshot rosnąca kolejka znaczy, że klient i tak już nie żyje.
    /// Odbudowa łańcucha delt kosztowałaby keyframe i osobną ścieżkę w kodzie; rozłączenie
    /// kosztuje nic, a gracz wraca biletem.
    static constexpr std::size_t max_queued_bytes = 256 * 1024;

    Session(
        boost::asio::ip::tcp::socket socket,
        TicketVerifier& tickets,
        SessionRegistry& registry);

    /// Odpala korutynę sesji. Sesja żyje tak długo, jak ta korutyna.
    void start();

    /// Dokłada gotową ramkę do wysłania.
    ///
    /// Bufor jest **współdzielony** — snapshot jest identyczny dla wszystkich (brak fog of
    /// war), więc serializuje się go raz na tik i rozsyła ten sam wskaźnik.
    void send(std::shared_ptr<const std::string> frame);

    /// Zrywa połączenie natychmiast — dla klienta, który przestał nadążać albo wrócił
    /// pod tym samym slotem.
    void stop();

    /// Zamyka połączenie ramką „going away".
    ///
    /// Różnica jest widoczna po stronie gracza: zerwane gniazdo to kod 1006, czyli „coś
    /// się stało z siecią", a normalne zakończenie meczu to 1001. Klient ma prawo
    /// zareagować na te dwie sytuacje inaczej.
    void close_gracefully();

    std::uint8_t slot() const noexcept
    {
        return slot_;
    }

private:
    boost::asio::awaitable<void> run();

    boost::asio::awaitable<void> read_loop();

    boost::asio::awaitable<void> write_loop();

    /// Zamyka połączenie z kodem 1008 i powodem — używane, gdy bilet nie przeszedł.
    boost::asio::awaitable<void> reject(TicketError error);

    boost::beast::websocket::stream<boost::beast::tcp_stream> stream_;

    TicketVerifier& tickets_;

    SessionRegistry& registry_;

    /// Budzik kolejki wyjściowej: czeka „w nieskończoność", a `cancel()` go budzi.
    boost::asio::steady_timer pending_;

    std::deque<std::shared_ptr<const std::string>> queue_;

    std::size_t queued_bytes_ = 0;

    std::uint8_t slot_ = 0;

    bool registered_ = false;

    bool stopping_ = false;
};

/// Żywe sesje meczu.
///
/// Jedyne miejsce, które wie, komu wysłać snapshot. Bez muteksów i bez kopiowania bufora:
/// wszystko dzieje się na jednym wątku, a ramka jest współdzielona przez `shared_ptr`.
class SessionRegistry
{
public:
    void add(const std::shared_ptr<Session>& session);

    void remove(const Session* session);

    /// Rozsyła tę samą ramkę do wszystkich. Złożoność zależy od liczby graczy, nie kafelków.
    void broadcast(std::shared_ptr<const std::string> frame);

    /// Rozłącza wcześniejsze połączenie na tym samym slocie.
    ///
    /// To jest reconnect z D14 w najprostszej postaci: gracz, który wrócił, wypiera swoje
    /// poprzednie wcielenie. Bez tego odświeżenie strony zostawiałoby zombie trzymające slot.
    void drop_previous_on(std::uint8_t slot, const Session* keep);

    std::size_t size() const noexcept
    {
        return sessions_.size();
    }

    void close_all();

private:
    std::vector<std::shared_ptr<Session>> sessions_;
};

} // namespace gs
