#pragma once

#include "map/map_file.hpp"
#include "meta/ticket.hpp"
#include "sim/roster.hpp"
#include "sim/world.hpp"
#include "state/match_intro.hpp"

#include <expected>
#include <string>

namespace gs
{

struct Options;

/// Wszystko, co proces musi wczytać i policzyć, **zanim** wpuści pierwszego gracza.
///
/// Jedna struktura zamiast siedmiu luźnych zmiennych w `main`, bo te rzeczy nie są luźne:
/// świat jest widokiem w bajty pliku mapy, keyframe jest zdjęciem świata po postawieniu
/// aktorów, a roster decyduje, kto gdzie stanął. Kolejność pól **jest** kolejnością zależności
/// i zarazem odwrotną kolejnością niszczenia — mapa musi przeżyć świat, bo trzyma jego teren.
///
/// Całość powstaje przed nasłuchem i to jest decyzja, nie kolejność przypadkowa: proces, który
/// i tak nikogo nie wpuści — bo nie ma klucza, mapy albo punktów startowych — ma paść od razu.
/// Meta zgłasza wtedy nieudaną alokację i otwiera nowe lobby, czyli ścieżką, która już istnieje.
/// Padnięcie przy pierwszym graczu byłoby meczem, który meta uważa za żywy.
///
/// Przeniesienie tej struktury **może rzucić** i nie da się tego zmienić: `MatchIntro` trzyma
/// gotowe wiadomości protobuf, a ich konstruktor przenoszący potrafi alokować. Dzieje się to
/// dokładnie raz, przy zwrocie z `open`, i wyjątek stamtąd jest równoważny nieudanemu startowi
/// procesu — czyli sytuacji, którą i tak obsługujemy.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct MatchSetup
{
    /// Bajty pliku terenu razem z ich sumą kontrolną. Musi stać pierwszy: `world` pokazuje
    /// w jego bufor (patrz `MapFile` — przeniesienie jest bezpieczne, zniszczenie nie).
    MapFile map;

    World world;

    Roster roster;

    /// `MatchInit` i keyframe zbudowane raz, po postawieniu wszystkich aktorów.
    MatchIntro intro;

    TicketVerifier tickets;

    /// Wczytuje mapę, obsadę i klucz biletów; stawia aktorów na punktach startowych.
    ///
    /// @returns komunikat dla `stderr`, jeśli którykolwiek z tych kroków nie ma prawa się
    /// udać. Każdy powód jest zdaniem, nie kodem błędu — po drugiej stronie stoi człowiek
    /// czytający log procesu, który przed chwilą nie wstał.
    static std::expected<MatchSetup, std::string> open(const Options& options);
};

/// Wypisuje do logu to, czym proces zaraz zacznie grać: mecz, mapę, obsadę i keyframe.
///
/// Osobno od `MatchSetup::open`, bo wczytywanie i opowiadanie o wczytanym to dwie różne
/// rzeczy — a bez tego bloku `main` znów robiłby jedno i drugie.
void log_match_summary(const Options& options, const MatchSetup& setup);

} // namespace gs
