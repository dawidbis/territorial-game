#include "net/commands.hpp"

#include "map/tmap.hpp"
#include "sim/simulation.hpp"

namespace gs
{
namespace
{

/// Tłumaczy wynik symulacji na powód z protokołu.
///
/// Odwzorowanie stoi tutaj, a nie w symulacji, bo to warstwa sieciowa zna protokół —
/// symulacja nie ma powodu wiedzieć, że ktokolwiek ją o cokolwiek pyta po drucie.
game::RejectReason to_reject_reason(OrderResult result) noexcept
{
    switch (result)
    {
    case OrderResult::invalid_target:
        return game::REJECT_REASON_INVALID_TARGET;
    case OrderResult::not_enough_gold:
        return game::REJECT_REASON_NOT_ENOUGH_GOLD;
    case OrderResult::no_shared_border:
        return game::REJECT_REASON_NO_SHARED_BORDER;
    case OrderResult::accepted:
        break;
    }

    return game::REJECT_REASON_UNSPECIFIED;
}

/// Atak: cel z protokołu na slot symulacji.
OrderResult attack(const game::AttackOrder& order, Simulation& simulation, std::uint8_t slot)
{
    const std::uint32_t target = order.target_slot();

    // Sloty są bajtem (D12), a pole w protokole ma trzydzieści dwa bity. Wartość spoza
    // zakresu to nie „nieznany gracz", tylko klient mówiący coś, czego nie da się
    // zinterpretować — i lepiej, żeby dowiedział się tego wprost.
    if (target > tmap::water_owner)
    {
        return OrderResult::invalid_target;
    }

    return simulation.order_attack(
        slot,
        static_cast<std::uint8_t>(target),
        order.population_pct());
}

} // namespace

game::RejectReason execute_command(
    const game::Command& command,
    Simulation& simulation,
    std::uint8_t slot)
{
    switch (command.order_case())
    {
    case game::Command::kAttack:
        return to_reject_reason(attack(command.attack(), simulation, slot));

    case game::Command::kBuild:
        // `tile_index` jest na razie ignorowany: miasta podnoszą sufit ludzi, ale nie stoją
        // na mapie. Pole zostaje w protokole, bo postawienie ich na kafelkach nie ma zmieniać
        // kształtu rozkazu.
        return to_reject_reason(simulation.order_city(slot));

    case game::Command::ORDER_NOT_SET:
        break;
    }

    return game::REJECT_REASON_UNKNOWN_ORDER;
}

} // namespace gs
