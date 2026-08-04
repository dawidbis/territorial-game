#pragma once

namespace gs
{

class MatchClock;
class MatchIntro;
class SessionRegistry;
class Simulation;
class TicketVerifier;

/// Wszystko, co sesja dostaje z zewnątrz — poza własnym gniazdem.
///
/// Zbiorczo, a nie osobnymi argumentami, bo lista rośnie z każdym etapem: po rosterze doszła
/// symulacja, po niej dojdzie zapis wyniku. Wszystko tu żyje w `main`, czyli dłużej niż
/// którakolwiek sesja, i wszystko jest na jednym wątku (D8) — stąd gołe referencje.
///
/// Osobny nagłówek, bo to jest **spis zależności**, a nie kawałek implementacji sesji: czyta
/// go nasłuch, pętla meczu i testy, a żadne z nich nie potrzebuje wiedzieć, jak wygląda
/// pojedyncze połączenie. Same deklaracje zapowiadające, więc dołączenie kosztuje tyle co nic.
struct MatchServices
{
    TicketVerifier& tickets;

    SessionRegistry& sessions;

    /// `MatchInit` i keyframe — dwie wiadomości, które gracz dostaje zaraz po bilecie.
    const MatchIntro& intro;

    /// Skąd sesja bierze numer tiku dla keyframe'a. Wchodzący w połowie meczu ma dostać
    /// bieżący numer, a nie zero — inaczej klient uzna, że cofnął się w czasie.
    const MatchClock& clock;

    /// Rozkazy gracza trafiają tu wprost, bez kolejki.
    ///
    /// Kolejka miałaby sens, gdyby symulacja chodziła na innym wątku — nie chodzi (D8).
    /// Rozkaz wykonuje się między tikami, w korutynie sesji, i to jest cała synchronizacja.
    Simulation& simulation;
};

} // namespace gs
