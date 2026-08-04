#include "net/listener.hpp"

#include "app/log.hpp"
#include "net/session.hpp"
#include "net/session_registry.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <memory>
#include <utility>

namespace gs
{

boost::asio::ip::tcp::acceptor listen_on_loopback(
    const boost::asio::any_io_executor& executor,
    std::uint16_t port)
{
    using boost::asio::ip::tcp;

    const tcp::endpoint endpoint{boost::asio::ip::make_address("127.0.0.1"), port};

    tcp::acceptor acceptor{executor};

    acceptor.open(endpoint.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(tcp::socket::max_listen_connections);

    return acceptor;
}

boost::asio::awaitable<void> accept_connections(
    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor,
    MatchServices& services)
{
    log::info(
        "Nasłuch na ws://127.0.0.1:{}/ws/match/{}.",
        acceptor->local_endpoint().port(),
        services.tickets.match());

    for (;;)
    {
        auto [error, socket] =
            co_await acceptor->async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));

        if (error)
        {
            if (error == boost::asio::error::operation_aborted)
            {
                co_return;
            }

            // Pojedyncze nieudane przyjęcie połączenia nie jest powodem, żeby przestać
            // przyjmować kolejne — mecz trwa dalej dla tych, którzy już weszli.
            log::warn("Nie udało się przyjąć połączenia: {}.", error.message());

            continue;
        }

        std::make_shared<Session>(std::move(socket), services)->start();
    }
}

} // namespace gs
