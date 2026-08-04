#pragma once

#include <cstdint>

namespace gs::economy
{

/// Ile złota dostaje człowiek w jednym tiku symulacji (10 Hz), czyli 1000 na sekundę.
inline constexpr std::uint64_t human_gold_per_tick = 100;

/// Bot dostaje połowę. To jedyne miejsce poza obroną, w którym symulacja odróżnia bota od
/// człowieka — poza nim reguły są wspólne (D12) i mają takie zostać.
inline constexpr std::uint64_t bot_gold_per_tick = 50;

/// Ile złota na tik dokłada jedno miasto, czyli 100 na sekundę.
///
/// Miasto jest **jedyną inwestycją w meczu**, więc musi zwracać się w czasie, który da się
/// przeżyć: pierwsze kosztuje 125 000 i zwraca się w 1250 sekund. Późniejsze zwracają się
/// wolniej, bo cena rośnie, a przychód nie — i o to chodzi, żeby nie dało się kupić
/// zwycięstwa samą kumulacją.
inline constexpr std::uint64_t city_gold_per_tick = 10;

/// Ile jednostek symulacji przypada na jednego człowieka pokazywanego graczowi.
///
/// Ta sama stała siedzi w `client/src/core/format.ts` i **musi** siedzieć w obu miejscach:
/// symulacja liczy w jednostkach dziesięć razy drobniejszych, żeby przyrost na tik miał
/// sensowną rozdzielczość. Podatek jest pierwszym wzorem, który tego przelicznika używa
/// — stawka jest podana od populacji **z paska**, bo to ją gracz widzi, kiedy decyduje,
/// czy opłaca mu się trzymać ludzi w domu.
inline constexpr double population_per_unit = 10.0;

/// Co ile tików skarbiec ściąga podatek — trzydzieści sekund przy 10 Hz.
inline constexpr std::uint32_t tax_interval_ticks = 300;

/// Jaka część żywej populacji wpada wtedy do skarbca.
///
/// Dziesiąta część populacji z paska co pół minuty stawia podatek mniej więcej na równi
/// z bazowym przychodem (30 000 złota na cykl): przy 300 tysiącach ludzi to 30 tysięcy
/// złota. Dzięki temu **ludzie trzymani w domu zaczynają na siebie zarabiać**, a wybór
/// między kolejnym natarciem a odłożeniem na miasto przestaje być oczywisty.
inline constexpr double tax_rate = 0.1;

/// Ludzie, z którymi aktor zaczyna mecz.
///
/// **Wartość do wyważenia, nie wzór.** Start z zera znaczyłby pierwszą minutę na przyroście
/// rzędu dziesięciu ludzi na tik, czyli mecz zaczynający się od czekania.
inline constexpr double initial_troops = 25'000.0;

/// Cena pierwszego miasta; każde kolejne kosztuje dwa razy tyle.
inline constexpr std::uint64_t first_city_cost = 125'000;

/// Sufit ceny miasta — od dziewiątego cena przestaje rosnąć.
///
/// Bez sufitu podwajanie wychodzi poza zakres złota w kilkunastu krokach, a miasta miałyby
/// sens wyłącznie na początku meczu.
inline constexpr std::uint64_t max_city_cost = 1'000'000;

/// Sufit ludzi: ziemia plus miasta.
///
/// Wykładnik `0,6` sprawia, że terytorium daje malejący zwrot — dwa razy większe państwo ma
/// półtora raza większy sufit, nie dwa. Bez tego pierwszy gracz, który urośnie, wygrywa
/// resztę meczu samym rozmiarem.
double max_troops(std::uint32_t tiles, std::uint32_t cities) noexcept;

/// Przyrost ludzi w jednym tiku.
///
/// Iloczyn dwóch czynników: bazy rosnącej z liczby ludzi (`troops^0,73 / 4`) i hamulca
/// `1 − troops/max`, który zeruje przyrost przy suficie. Maksimum wypada koło **42 %**
/// zapełnienia — po tym punkcie werbunek zwalnia, więc trzymanie pełnej puli jest gorsze
/// niż jej wydawanie.
///
/// Nigdy nie przekracza `max − troops`, więc sufit jest twardy także przy dużym kroku.
double troop_gain(double troops, double max) noexcept;

/// Cena kolejnego miasta przy `cities` już postawionych.
std::uint64_t city_cost(std::uint32_t cities) noexcept;

/// Przychód złota na tik: baza zależna od tego, kto gra, plus miasta.
std::uint64_t gold_per_tick(std::uint32_t cities, bool is_bot) noexcept;

/// Podatek od podanej puli ludzi — złoto, które zabierze najbliższy pobór.
///
/// Liczony wyłącznie z ludzi **w puli**: armia w polu nie płaci, bo jej tam nie ma. To jest
/// cała cena natarcia poza samymi stratami — wysłanie wszystkich ludzi zeruje pobór.
std::uint64_t tax_amount(double troops) noexcept;

} // namespace gs::economy
