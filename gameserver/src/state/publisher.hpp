#pragma once

#include "sim/roster.hpp"

#include <cstdint>
#include <span>

namespace gs
{

class Simulation;
class SessionRegistry;
class World;

/// Jedno okno wysyłki: co leci na drut po tiku, w jakiej kolejności i jak często.
///
/// Wydzielone z pętli meczu, bo to nie jest „kilka linii w pętli", tylko trzy niezależne
/// reguły, z których każda ma powód: snapshot idzie do wszystkich jednym buforem, stan gracza
/// osobno do każdego, a lista zmienionych kafelków kasuje się dopiero po wysyłce. Wymieszane
/// z odliczaniem końca meczu i logiem tętna czytały się jak jedna sprawa, którą nie są.
class MatchPublisher
{
public:
    MatchPublisher(
        SessionRegistry& sessions,
        std::span<const Actor> actors,
        World& world,
        const Simulation& simulation) noexcept;

    /// Rozsyła stan po tiku `tick`. Wołane wyłącznie w tikach z wysyłką (D3).
    void publish(std::uint32_t tick);

private:
    /// Co która wysyłka niesie `PublicState`. Przy 10 Hz daje to ranking raz na sekundę.
    ///
    /// Liczba idzie w parze z `TickRates::send_every`: ranking ma zostać przy 1 Hz niezależnie
    /// od tego, jak często lecą snapshoty — to lista stu graczy, a nie dane do animowania.
    static constexpr std::uint32_t public_state_every = 10;

    SessionRegistry& sessions_;

    std::span<const Actor> actors_;

    World& world_;

    const Simulation& simulation_;

    /// Ile okien wysyłki już poszło — z tego wychodzi rytm `PublicState`.
    std::uint32_t sends_ = 0;
};

} // namespace gs
