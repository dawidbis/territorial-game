#include "state/publisher.hpp"

#include "net/session_registry.hpp"
#include "sim/world.hpp"
#include "state/snapshot.hpp"

namespace gs
{

MatchPublisher::MatchPublisher(
    SessionRegistry& sessions,
    std::span<const Actor> actors,
    World& world,
    const Simulation& simulation) noexcept
    : sessions_(sessions)
    , actors_(actors)
    , world_(world)
    , simulation_(simulation)
{
}

void MatchPublisher::publish(std::uint32_t tick)
{
    ++sends_;

    sessions_.broadcast(
        build_snapshot(tick, sends_ % public_state_every == 1, actors_, world_, simulation_));

    // Stan gracza idzie **po** snapshocie: pasek pokazuje pulę ludzi z tego samego kroku,
    // w którym mapa się przesunęła, a nie z poprzedniego.
    sessions_.send_each([this](std::uint8_t slot)
                        { return build_my_state(simulation_, world_, slot); });

    // Lista zmian należy do okna wysyłki, nie do tiku: przy wysyłce rzadszej niż symulacja (D3)
    // kafelek przejęty i odbity w tym samym oknie ma pojechać raz.
    world_.clear_changed();
}

} // namespace gs
