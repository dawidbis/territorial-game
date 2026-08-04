#include "state/snapshot.hpp"

#include "sim/economy.hpp"
#include "sim/simulation.hpp"
#include "sim/world.hpp"

#include <game.pb.h>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace gs
{
namespace
{

/// Tików symulacji na sekundę — przelicznik z „na tik" na „na sekundę" (D3).
constexpr double ticks_per_second = 10.0;

/// Ile milisekund trwa tik. Wszystko, co wychodzi do klienta jako czas, idzie w milisekundach.
constexpr double tick_ms = 1000.0 / ticks_per_second;

/// Pola `MyState` są 32-bitowe, a pula ludzi liczy się w `double`, złoto w `uint64`.
/// Obcięcie zamiast zawinięcia: pasek stanu ma pokazać wartość maksymalną, a nie zero.
std::uint32_t to_u32(double value) noexcept
{
    return static_cast<std::uint32_t>(
        std::clamp(value, 0.0, static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
}

} // namespace

std::shared_ptr<const std::string> build_snapshot(
    std::uint32_t tick,
    bool with_public_state,
    std::span<const Actor> actors,
    const World& world,
    const Simulation& simulation)
{
    game::ServerMsg message;

    game::Snapshot* snapshot = message.mutable_snapshot();
    snapshot->set_tick(tick);
    snapshot->set_is_keyframe(false);

    if (with_public_state)
    {
        for (const Actor& actor : actors)
        {
            game::PublicState* state = snapshot->add_others();

            state->set_slot(actor.slot);
            state->set_territory_tiles(world.tiles_of(actor.slot));
            state->set_population(to_u32(simulation.player(actor.slot).troops));
        }
    }

    const std::span<const std::uint32_t> changed = world.changed_tiles();

    if (changed.empty())
    {
        return std::make_shared<const std::string>(message.SerializeAsString());
    }

    // Sortowanie tutaj, a nie przy odnotowaniu zmiany: podbój idzie kolejnością kolejki
    // priorytetowej, więc kafelki wpadają na listę w porządku, który dla klienta nic nie
    // znaczy — a kodowanie różnicowe wymaga rosnących indeksów.
    std::vector<std::uint32_t> tiles(changed.begin(), changed.end());
    std::ranges::sort(tiles);

    // Slot → indeks grupy w wiadomości; −1 znaczy „grupy jeszcze nie ma". Tablica zamiast
    // mapy, bo slotów jest 256 z definicji (D12), a tędy przechodzi każda wysyłka.
    std::array<int, 256> group_of{};
    group_of.fill(-1);

    std::array<std::uint32_t, 256> previous{};

    for (const std::uint32_t tile : tiles)
    {
        const std::uint8_t owner = world.owner_at(tile);

        if (group_of[owner] < 0)
        {
            group_of[owner] = snapshot->deltas_size();

            game::TileDeltaGroup* group = snapshot->add_deltas();
            group->set_slot(owner);

            // Pierwszy indeks w grupie jest bezwzględny — nie ma od czego liczyć różnicy.
            group->add_index_deltas(tile);
        }
        else
        {
            snapshot->mutable_deltas(group_of[owner])->add_index_deltas(tile - previous[owner]);
        }

        previous[owner] = tile;
    }

    return std::make_shared<const std::string>(message.SerializeAsString());
}

std::shared_ptr<const std::string> build_my_state(
    const Simulation& simulation,
    const World& world,
    std::uint8_t slot)
{
    const PlayerState& player = simulation.player(slot);

    game::ServerMsg message;

    game::MyState* state = message.mutable_my_state();

    state->set_population(to_u32(player.troops));
    state->set_gold(to_u32(static_cast<double>(player.gold)));

    // Oba przychody **na sekundę**, nie na tik: gracz czyta je jako tempo, a tik jest
    // szczegółem symulacji, o którym interfejs nie ma powodu wiedzieć.
    state->set_pop_income(to_u32(player.last_gain * ticks_per_second));

    const double gold_rate =
        static_cast<double>(economy::gold_per_tick(player.cities, player.is_bot));

    state->set_gold_income(to_u32(gold_rate * ticks_per_second));

    state->set_attack_force(to_u32(simulation.attack_force(slot)));
    state->set_cities(player.cities);

    // Cena kolejnego miasta jedzie z serwera, zamiast być liczona po obu stronach: druga
    // implementacja tego samego wzoru rozjeżdża się przy pierwszej zmianie i objawia jako
    // przycisk, który obiecuje inną cenę, niż pobiera serwer.
    state->set_next_city_cost(to_u32(static_cast<double>(economy::city_cost(player.cities))));

    // Sufit jedzie razem z pulą, bo sama liczba ludzi nic nie mówi: przyrost hamuje
    // względem sufitu, a ten rośnie z każdym zdobytym kafelkiem.
    state->set_max_population(to_u32(economy::max_troops(world.tiles_of(slot), player.cities)));

    // Podatek w milisekundach, nie w tikach: pasek w interfejsie ma się wypełniać płynnie,
    // a przeliczenie tików na czas byłoby drugim miejscem, w którym klient musi znać
    // częstotliwość symulacji.
    state->set_tax_amount(to_u32(static_cast<double>(simulation.tax_due(slot))));
    state->set_tax_in_ms(to_u32(simulation.ticks_to_tax() * tick_ms));
    state->set_tax_period_ms(to_u32(economy::tax_interval_ticks * tick_ms));

    return std::make_shared<const std::string>(message.SerializeAsString());
}

} // namespace gs
