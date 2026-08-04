#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <memory>

namespace gs
{

struct MatchServices;

/// Otwiera nasłuch na pętli zwrotnej.
///
/// **Nie na `0.0.0.0`.** Proces mówi gołym `ws://`, bo TLS terminuje proxy (D9) — wystawiony
/// wprost na świat byłby nieszyfrowanym wejściem do meczu. To jedna linia i jedno z tych
/// miejsc, w których domyślna wartość jest niebezpieczna.
///
/// Port `0` oznacza „dowolny wolny" — tak robią testy, żeby nie walczyć o stały numer.
///
/// Rzuca, gdy portu nie da się zająć: proces, który nie nasłuchuje, nie ma po co żyć.
boost::asio::ip::tcp::acceptor listen_on_loopback(
    const boost::asio::any_io_executor& executor,
    std::uint16_t port);

/// Przyjmuje połączenia i oddaje każde nowej sesji.
///
/// Akceptor przychodzi gotowy, bo to wołający decyduje, gdzie proces stoi — dzięki temu
/// test może związać port efemeryczny i dowiedzieć się, jaki dostał.
///
/// Współdzielony wskaźnik, a nie referencja: **zamknięcie akceptora jest jedynym sposobem
/// zatrzymania tej pętli**, a robi to wołający, gdy mecz się kończy. Bez tego `io_context`
/// ma wciąż robotę i proces nie kończy pracy, mimo że nie ma już czego przyjmować.
boost::asio::awaitable<void> accept_connections(
    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor,
    MatchServices& services);

} // namespace gs
