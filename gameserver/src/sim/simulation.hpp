#pragma once

#include "sim/attack.hpp"
#include "sim/rng.hpp"
#include "sim/roster.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gs
{

class World;

/// Stan jednego aktora poza mapą. Terytorium mieszka w <see cref="World"/> i jest liczone
/// tam — dwa liczniki tego samego rozjeżdżają się przy pierwszym przeoczonym przejęciu.
struct PlayerState
{
    double troops = 0.0;

    std::uint64_t gold = 0;

    std::uint32_t cities = 0;

    bool is_bot = false;

    /// Czy slot jest w obsadzie i jeszcze żyje.
    bool alive = false;

    /// Przyrost z ostatniego tiku — wyłącznie do paska stanu, żaden wzór go nie czyta.
    double last_gain = 0.0;
};

/// Wynik rozkazu. Odwzorowanie na `RejectReason` robi warstwa sieciowa: symulacja nie zna
/// protokołu i nie ma powodu, żeby go poznać.
enum class OrderResult : std::uint8_t
{
    accepted,
    invalid_target,
    not_enough_gold,
    no_shared_border,
};

/// Symulacja meczu: ekonomia, natarcia i podbój.
///
/// Wszystko dzieje się w jednym wątku (D8) i wyłącznie w <see cref="tick"/> albo w rozkazach
/// wołanych z tej samej pętli — nie ma tu żadnego stanu współdzielonego i nie ma go zyskać.
///
/// Losowość idzie z jednego strumienia PCG zasianego ziarnem meczu. Kolejność losowań zależy
/// od kolejności rozkazów, a ta jest dokładnie tym, co zapisuje log komend — więc replay
/// (D10) ma komplet: ziarno plus log odtwarza mecz kafelek w kafelek.
class Simulation
{
public:
    /// Kara w procentach za wycofanie natarcia z ataku na gracza.
    ///
    /// Wycofanie ma być decyzją, nie odruchem: bez kary opłacałoby się cofać armię przy
    /// każdej niekorzystnej wymianie i wysyłać ją ponownie sekundę później.
    static constexpr double retreat_malus_percent = 25.0;

    /// Ile tików mija między rozkazem wycofania a powrotem ludzi do puli — dwie sekundy
    /// przy 10 Hz.
    ///
    /// Podbój staje natychmiast, ale armia jest przez ten czas **poza pulą**: nie broni,
    /// nie przyrasta i nie da się jej zawrócić. Bez tej zwłoki wycofanie byłoby teleportem
    /// spod ognia prosto do obrony.
    static constexpr std::uint32_t retreat_delay_ticks = 20;

    Simulation(World& world, std::span<const Actor> actors, std::int64_t seed);

    /// Jeden krok symulacji: ekonomia, potem natarcia.
    void tick(std::uint32_t tick);

    /// Wysyła część ludzi gracza na wskazany cel.
    ///
    /// `percent` z zakresu 1..100 jest obcinany do niego, a nie odrzucany: to wartość
    /// z suwaka w interfejsie, więc zero i sto pięć znaczą to samo co skraje suwaka.
    OrderResult order_attack(std::uint8_t slot, std::uint8_t target, std::uint32_t percent);

    /// Stawia miasto. Miasta są na razie **wyłącznie licznikiem** — podnoszą sufit ludzi,
    /// ale nie stoją na mapie, więc nie da się ich zdobyć ani zniszczyć.
    OrderResult order_city(std::uint8_t slot);

    /// Zleca wycofanie natarcia; zdejmie je najbliższy tik.
    ///
    /// Nie ma jeszcze rozkazu w protokole, więc dziś woła to tylko test. Mechanika istnieje,
    /// bo bez niej wysłanie armii jest nieodwracalne i pierwsza pomyłka kończy mecz gracza.
    OrderResult order_retreat(std::uint8_t slot, std::uint8_t target);

    const PlayerState& player(std::uint8_t slot) const noexcept
    {
        return players_[slot];
    }

    /// Ludzie związani w natarciach tego gracza — do paska stanu.
    double attack_force(std::uint8_t slot) const noexcept;

    /// Złoto, które przyniesie temu graczowi najbliższy pobór podatku.
    ///
    /// Liczone z bieżącej puli, więc wartość drga razem z werbunkiem i natarciami — i o to
    /// chodzi: pasek podatku ma pokazywać, ile gracz **straci**, wysyłając ludzi teraz.
    std::uint64_t tax_due(std::uint8_t slot) const noexcept;

    /// Ile tików zostało do najbliższego poboru.
    std::uint32_t ticks_to_tax() const noexcept;

    std::size_t active_attacks() const noexcept
    {
        return attacks_.size();
    }

private:
    void grow();

    /// Ściąga podatek od wszystkich żywych, jeśli ten tik jest tikiem poboru.
    void collect_tax(std::uint32_t tick);

    void run_attacks(std::uint32_t tick);

    /// Jeden tik jednego natarcia. `false` znaczy, że atak się skończył.
    ///
    /// Sam rozstrzyga tylko, w którym z dwóch trybów natarcie jest — cała robota siedzi
    /// w <see cref="withdraw"/> albo w <see cref="conquer"/>.
    bool advance(Attack& attack, std::uint32_t tick);

    /// Tik natarcia w odwrocie: odliczanie, a na końcu powrót ludzi do puli.
    bool withdraw(Attack& attack);

    /// Tik natarcia zdobywającego: budżet kafelków na ten tik i pętla przejęć.
    bool conquer(Attack& attack, std::uint32_t tick);

    /// Ściera świeżo wysłaną armię z natarciami idącymi w przeciwną stronę.
    ///
    /// @returns ilu ludzi zostało po starciu czołowym.
    double absorb_counterattacks(std::uint8_t slot, std::uint8_t target, double troops);

    /// Oddaje ocalałych do puli gracza i kończy natarcie.
    void give_back(Attack& attack, double malus_percent);

    /// Wykreśla gracza, który stracił ostatni kafelek.
    void eliminate(std::uint8_t slot);

    World& world_;

    Pcg32 rng_;

    /// Ostatni przetworzony tik. Trzymany wyłącznie po to, żeby `MyState` umiał powiedzieć,
    /// ile zostało do poboru — symulacja sama numeru tiku do niczego nie potrzebuje.
    std::uint32_t tick_ = 0;

    std::array<PlayerState, 256> players_{};

    std::vector<Attack> attacks_;
};

} // namespace gs
