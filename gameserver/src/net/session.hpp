#pragma once

#include "meta/ticket.hpp"
#include "net/match_services.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace game
{
class Command;
} // namespace game

namespace gs
{

/// Jedno połączenie gracza.
///
/// Cała sesja jest jedną korutyną czytaną z góry na dół: upgrade → `ClientHello` →
/// weryfikacja biletu → pętla wiadomości. Ramka korutyny **jest** stanem sesji, więc nie
/// ma osobnej maszyny stanów ani przekazywania danych między uchwytami. Każdy z tych czterech
/// kroków ma własną metodę i własny powód, żeby się nie udać — jedna stulinijkowa korutyna
/// znaczyła, że „gdzie tu jest sprawdzanie biletu" wymagało przeczytania obsługi HTTP.
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
    static constexpr std::size_t max_queued_bytes = std::size_t{256} * 1024;

    Session(boost::asio::ip::tcp::socket socket, MatchServices& services);

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
    /// Cała sesja z lotu ptaka: cztery kroki, każdy w swojej metodzie.
    boost::asio::awaitable<void> run();

    /// Żądanie HTTP, sprawdzenie ścieżki i podniesienie połączenia do WebSocketa.
    ///
    /// @returns `false`, gdy nie ma czego podnosić — wtedy sesja kończy się bez śladu.
    boost::asio::awaitable<bool> accept_websocket();

    /// Pierwsza ramka: `ClientHello` z biletem.
    ///
    /// @returns `false`, gdy bilet nie przeszedł. Połączenie jest wtedy już zamknięte kodem
    /// `policy_error` — jednym dla wszystkich powodów, żeby nie podpowiadać próbującemu,
    /// jak blisko celu jest.
    boost::asio::awaitable<bool> authenticate();

    /// Wpisuje sesję do rejestru i wysyła graczowi to, co widzi zaraz po wejściu.
    void join();

    /// Wypisuje sesję z rejestru. Idempotentne — wołane także po zerwanym połączeniu.
    void leave();

    boost::asio::awaitable<void> read_loop();

    boost::asio::awaitable<void> write_loop();

    /// Wykonuje rozkaz gracza i odsyła powód, jeśli symulacja go nie przyjęła.
    ///
    /// Odrzucenie **musi** wracać do gracza: rozkaz, który zniknął bez śladu, wygląda
    /// z drugiej strony dokładnie tak samo jak zerwana sieć.
    void handle_command(const game::Command& command);

    /// Zamyka połączenie z kodem 1008 i powodem — używane, gdy bilet nie przeszedł.
    boost::asio::awaitable<void> reject(TicketError error);

    boost::beast::websocket::stream<boost::beast::tcp_stream> stream_;

    MatchServices& services_;

    /// Budzik kolejki wyjściowej: czeka „w nieskończoność", a `cancel()` go budzi.
    boost::asio::steady_timer pending_;

    std::deque<std::shared_ptr<const std::string>> queue_;

    std::size_t queued_bytes_ = 0;

    std::uint8_t slot_ = 0;

    /// Gracz z biletu — wyłącznie do logu wejścia.
    ///
    /// Trzymany, bo weryfikacja biletu i wpisanie do rejestru to od teraz dwa kroki, a numer
    /// slotu nie mówi w logu nic o tym, kto pod nim siedzi.
    std::string player_id_;

    bool registered_ = false;

    bool stopping_ = false;
};

} // namespace gs
