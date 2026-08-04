#include "sim/simulation.hpp"

#include "map/tmap.hpp"
#include "sim/economy.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace gs
{
namespace
{

/// Strumień PCG symulacji.
///
/// Z dala od strumieni 1..254, którymi obsada losuje nicki i kolory botów: te są funkcją
/// slotu i mają nią zostać, więc losowania podboju nie mogą się w nie wcinać.
constexpr std::uint64_t simulation_stream = 1'000'000;

/// Losowy dodatek do szerokości frontu przy liczeniu budżetu na tik — z przedziału [0, 4].
///
/// Pierwowzór dolicza go przy każdym tiku każdego natarcia (`borderSize() + nextInt(0, 5)`).
/// Przy wąskim froncie to jest różnica rzędu kilkudziesięciu procent budżetu, więc nie jest
/// to kosmetyka — bez tego natarcie przez przesmyk idzie miarowo zamiast szarpanym krokiem.
constexpr std::uint32_t front_jitter_bound = 5;

} // namespace

Simulation::Simulation(World& world, std::span<const Actor> actors, std::int64_t seed)
    : world_(world)
    , rng_(static_cast<std::uint64_t>(seed), simulation_stream)
{
    for (const Actor& actor : actors)
    {
        PlayerState& player = players_[actor.slot];

        player.troops = economy::initial_troops;
        player.is_bot = actor.is_bot;
        player.alive = true;
    }
}

void Simulation::tick(std::uint32_t tick)
{
    tick_ = tick;

    grow();

    collect_tax(tick);

    run_attacks(tick);
}

void Simulation::grow()
{
    for (std::size_t slot = 1; slot < players_.size(); ++slot)
    {
        PlayerState& player = players_[slot];

        if (!player.alive)
        {
            continue;
        }

        player.gold += economy::gold_per_tick(player.cities, player.is_bot);

        const double max =
            economy::max_troops(world_.tiles_of(static_cast<std::uint8_t>(slot)), player.cities);

        player.last_gain = economy::troop_gain(player.troops, max);
        player.troops += player.last_gain;
    }
}

void Simulation::collect_tax(std::uint32_t tick)
{
    // Tik zerowy pomijany świadomie: pobór na starcie meczu wziąłby dziesiątą część puli
    // początkowej, zanim gracz zdążył cokolwiek zrobić, i wyglądałby na karę za wejście.
    if (tick == 0 || tick % economy::tax_interval_ticks != 0)
    {
        return;
    }

    for (PlayerState& player : players_)
    {
        if (!player.alive)
        {
            continue;
        }

        player.gold += economy::tax_amount(player.troops);
    }
}

std::uint64_t Simulation::tax_due(std::uint8_t slot) const noexcept
{
    const PlayerState& player = players_[slot];

    return player.alive ? economy::tax_amount(player.troops) : 0;
}

std::uint32_t Simulation::ticks_to_tax() const noexcept
{
    return economy::tax_interval_ticks - tick_ % economy::tax_interval_ticks;
}

void Simulation::run_attacks(std::uint32_t tick)
{
    // Indeksem, a nie zakresowo: `advance` potrafi wykreślić gracza, a to gasi wszystkie
    // jego natarcia. Lista przy tym nie rośnie — rozkazy przychodzą między tikami, nie
    // w środku, bo ta pętla nie ma ani jednego punktu zawieszenia korutyny (D8). Pętla
    // zakresowa przeszłaby dziś tak samo, ale trzymałaby iterator na liście, którą wnętrze
    // pętli dotyka — a to jest różnica między „działa" a „nie da się zepsuć".
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (std::size_t index = 0; index < attacks_.size(); ++index)
    {
        Attack& attack = attacks_[index];

        if (attack.done)
        {
            continue;
        }

        if (!advance(attack, tick))
        {
            attack.done = true;
        }
    }

    std::erase_if(attacks_, [](const Attack& attack) { return attack.done; });
}

bool Simulation::advance(Attack& attack, std::uint32_t tick)
{
    // Wycofanie i podbój to dwa tryby tego samego natarcia i poza tym jednym „if" nie mają
    // ze sobą nic wspólnego — jedno odlicza, drugie zdobywa kafelki.
    return attack.retreating ? withdraw(attack) : conquer(attack, tick);
}

bool Simulation::withdraw(Attack& attack)
{
    // Odliczanie idzie tutaj, a nie osobną pętlą po natarciach: wycofujące się natarcie
    // wciąż jest natarciem — trzyma ludzi poza pulą i wciąż może zginąć w starciu
    // czołowym, zanim odliczy do zera.
    if (attack.retreat_countdown > 0)
    {
        --attack.retreat_countdown;

        return true;
    }

    give_back(attack, attack.target != tmap::wasteland_owner ? retreat_malus_percent : 0.0);

    return false;
}

bool Simulation::conquer(Attack& attack, std::uint32_t tick)
{
    const bool versus_player = attack.target != tmap::wasteland_owner;

    PlayerState& defender = players_[attack.target];

    double budget = attack_tiles_per_tick(
        attack.troops,
        versus_player ? defender.troops : 0.0,
        versus_player,
        attack.border.size() + rng_.below(front_jitter_bound));

    while (budget > 0.0)
    {
        if (attack.troops < 1.0)
        {
            // Natarcie wykrwawiło się pod bramą. Nie ma kogo oddawać do puli i **to jest
            // cena ataku** — inaczej przegrana ofensywa nic by nie kosztowała.
            attack.troops = 0.0;

            return false;
        }

        if (attack.frontier.empty())
        {
            // Nie ma czego zdobywać: front się urwał albo cel przestał istnieć. Ocalali
            // wracają bez kary, bo to nie jest decyzja gracza, tylko koniec roboty.
            give_back(attack, 0.0);

            return false;
        }

        const ConquerTile next = attack.frontier.top();

        attack.frontier.pop();
        attack.border.erase(next.tile);

        // Kafelek mógł w międzyczasie zmienić właściciela albo przestać stykać się
        // z natarciem — kolejka jest budowana z wyprzedzeniem i bywa nieaktualna.
        if (world_.owner_at(next.tile) != attack.target || !touches(world_, next.tile, attack.attacker))
        {
            continue;
        }

        const CombatSides sides{
            attack.troops,
            world_.tiles_of(attack.attacker),
            players_[attack.attacker].is_bot,
            versus_player ? defender.troops : 0.0,
            versus_player ? world_.tiles_of(attack.target) : 0u,
            versus_player && defender.is_bot,
            versus_player,
        };

        const AttackStep step = attack_step(sides, world_.terrain_at(next.tile));

        budget -= step.tiles_used;
        attack.troops -= step.attacker_loss;

        if (versus_player)
        {
            defender.troops = std::max(0.0, defender.troops - step.defender_loss);
        }

        // Front rozszerzany PRZED przejęciem — patrz `extend_front`.
        extend_front(world_, attack, rng_, next.tile, tick);

        world_.set_owner(next.tile, attack.attacker);

        if (versus_player && world_.tiles_of(attack.target) == 0)
        {
            eliminate(attack.target);
            give_back(attack, 0.0);

            return false;
        }
    }

    return true;
}

void Simulation::give_back(Attack& attack, double malus_percent)
{
    const double survivors = std::max(0.0, attack.troops) * (1.0 - malus_percent / 100.0);

    // Świadomie **bez** obcięcia do sufitu ludzi: sufit hamuje werbunek, a nie powroty.
    // Obcięcie kasowałoby armię za to, że pula akurat była pełna.
    players_[attack.attacker].troops += survivors;

    attack.troops = 0.0;
}

void Simulation::eliminate(std::uint8_t slot)
{
    PlayerState& player = players_[slot];

    player.alive = false;
    player.troops = 0.0;
    player.last_gain = 0.0;

    // Ludzie, których wykreślony gracz trzymał w polu, przepadają razem z nim — nie ma
    // dokąd ich cofnąć.
    for (Attack& attack : attacks_)
    {
        if (attack.attacker == slot)
        {
            attack.troops = 0.0;
            attack.done = true;
        }
    }
}

double Simulation::attack_force(std::uint8_t slot) const noexcept
{
    double total = 0.0;

    for (const Attack& attack : attacks_)
    {
        if (!attack.done && attack.attacker == slot)
        {
            total += attack.troops;
        }
    }

    return total;
}

OrderResult Simulation::order_attack(std::uint8_t slot, std::uint8_t target, std::uint32_t percent)
{
    PlayerState& attacker = players_[slot];

    if (!attacker.alive || target == slot || target == tmap::water_owner)
    {
        return OrderResult::invalid_target;
    }

    if (target != tmap::wasteland_owner && !players_[target].alive)
    {
        return OrderResult::invalid_target;
    }

    // Suwak w interfejsie ma skraje, więc zero i sto pięć znaczą to samo co one — a nie
    // „rozkaz do odrzucenia".
    const double share = std::clamp<double>(percent, 1.0, 100.0) / 100.0;

    double troops = std::floor(attacker.troops * share);

    if (troops < 1.0)
    {
        return OrderResult::invalid_target;
    }

    // Natarcie w odwrocie nie jest już „trwającą ofensywą": dokładanie do niego ludzi
    // odsyłałoby ich z powrotem razem z karą, zamiast posłać na kafelki. Taki rozkaz otwiera
    // nowe natarcie obok wycofywanego.
    const auto same_target = [slot, target](const Attack& attack)
    {
        return !attack.done && !attack.retreating && attack.attacker == slot
            && attack.target == target;
    };

    const auto existing = std::ranges::find_if(attacks_, same_target);

    Attack fresh;

    if (existing == attacks_.end())
    {
        fresh.attacker = slot;
        fresh.target = target;

        // Front budowany **przed** pobraniem ludzi z puli: rozkaz na cel, z którym nie ma
        // wspólnej granicy, ma nic nie kosztować.
        seed_front(world_, fresh, rng_, 0);

        if (fresh.border.empty())
        {
            return OrderResult::no_shared_border;
        }
    }

    attacker.troops -= troops;

    troops = absorb_counterattacks(slot, target, troops);

    if (troops < 1.0)
    {
        // Cała wysłana armia poległa w starciu czołowym. Rozkaz się wykonał — po prostu
        // nie został z niego nikt, kto mógłby ruszyć na kafelki.
        return OrderResult::accepted;
    }

    if (existing != attacks_.end())
    {
        // Drugi rozkaz na ten sam cel dokłada ludzi do trwającej ofensywy, zamiast otwierać
        // drugi front — dwa natarcia na tego samego przeciwnika biłyby się o te same
        // kafelki i każde liczyłoby stosunek sił, jakby było jedyne.
        existing->troops += troops;

        return OrderResult::accepted;
    }

    fresh.troops = troops;

    attacks_.push_back(std::move(fresh));

    return OrderResult::accepted;
}

double Simulation::absorb_counterattacks(std::uint8_t slot, std::uint8_t target, double troops)
{
    // Natarcia idące w przeciwne strony ścierają się od razu, zanim którekolwiek dojdzie
    // do kafelków. Bez tego obie armie mijałyby się na mapie i każda zdobywałaby puste
    // zaplecze przeciwnika — wygrywałby ten, kto ma szybszy palec, nie silniejszą armię.
    for (Attack& incoming : attacks_)
    {
        if (incoming.done || incoming.attacker != target || incoming.target != slot)
        {
            continue;
        }

        if (incoming.troops > troops)
        {
            incoming.troops -= troops;

            return 0.0;
        }

        troops -= incoming.troops;

        incoming.troops = 0.0;
        incoming.done = true;
    }

    return troops;
}

OrderResult Simulation::order_city(std::uint8_t slot)
{
    PlayerState& player = players_[slot];

    if (!player.alive)
    {
        return OrderResult::invalid_target;
    }

    const std::uint64_t cost = economy::city_cost(player.cities);

    if (player.gold < cost)
    {
        return OrderResult::not_enough_gold;
    }

    player.gold -= cost;
    ++player.cities;

    return OrderResult::accepted;
}

OrderResult Simulation::order_retreat(std::uint8_t slot, std::uint8_t target)
{
    for (Attack& attack : attacks_)
    {
        // Natarcie już wycofywane jest pomijane: powtórzony rozkaz przestawiałby odliczanie
        // od nowa, czyli klikanie „wycofaj" trzymałoby armię w polu w nieskończoność.
        if (attack.done || attack.retreating || attack.attacker != slot || attack.target != target)
        {
            continue;
        }

        attack.retreating = true;
        attack.retreat_countdown = retreat_delay_ticks;

        return OrderResult::accepted;
    }

    return OrderResult::invalid_target;
}

} // namespace gs
