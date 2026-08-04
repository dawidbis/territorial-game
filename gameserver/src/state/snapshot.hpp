#pragma once

#include "sim/roster.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gs
{

class Simulation;
class World;

/// Zwykły snapshot rozsyłany wszystkim.
///
/// Jeden bufor dla całego meczu — brak fog of war (§1 planu) znaczy, że każdy gracz dostaje
/// identyczne bajty, więc serializacja dzieje się raz na tik, a nie raz na gracza.
///
/// Delty niosą kafelki, które zmieniły właściciela od poprzedniej wysyłki — **bez powtórzeń
/// i posortowane**, pogrupowane po nowym właścicielu. Grupa płaci za właściciela raz,
/// a indeksy w niej jadą różnicami: ekspansja jest przestrzennie ciągła, więc różnica
/// prawie zawsze mieści się w jednym bajcie varinta (D5). Pierwszy indeks w grupie jest
/// bezwzględny — nie ma od czego liczyć różnicy.
///
/// `PublicState` dokłada się **co piąty snapshot**, czyli raz na sekundę przy wysyłce 5 Hz:
/// ranking nie zmienia się na tyle szybko, żeby opłacało się go wysyłać pięć razy częściej,
/// a przy stu aktorach to jest różnica rzędu kilkuset bajtów na tik.
///
/// Populacja w `PublicState` jest jawna dla wszystkich, bo mecz nie ma mgły wojny (§1 planu):
/// klient podpisuje nią cudze terytoria na mapie, a ukrywanie liczby, którą i tak widać po
/// tempie ekspansji, kosztowałoby więcej niż daje.
std::shared_ptr<const std::string> build_snapshot(
    std::uint32_t tick,
    bool with_public_state,
    std::span<const Actor> actors,
    const World& world,
    const Simulation& simulation);

/// Stan jednego gracza — jedyna wiadomość, której treść zależy od odbiorcy.
///
/// Dlatego jest osobnym wariantem `ServerMsg`, a nie polem w snapshocie: wspólny bufor
/// broadcastu przestałby być wspólny, gdyby niósł czyjąkolwiek pulę ludzi.
std::shared_ptr<const std::string> build_my_state(
    const Simulation& simulation,
    const World& world,
    std::uint8_t slot);

} // namespace gs
