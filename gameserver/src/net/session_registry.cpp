#include "net/session_registry.hpp"

#include <algorithm>
#include <vector>

namespace gs
{

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

void SessionRegistry::broadcast(const std::shared_ptr<const std::string>& frame)
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
