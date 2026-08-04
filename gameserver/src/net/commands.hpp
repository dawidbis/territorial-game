#pragma once

#include <game.pb.h>

#include <cstdint>

namespace gs
{

class Simulation;

/// Wykonuje rozkaz gracza i mówi, dlaczego symulacja go nie przyjęła.
///
/// Osobno od sesji, bo to jest **tłumaczenie protokołu na regułę gry**, a nie obsługa
/// połączenia: sesja czyta gniazdo i nie ma powodu wiedzieć, że slot jest bajtem ani że
/// „za mało złota" ma swój numer na drucie. Symetryczne do `state/snapshot.cpp`, które robi
/// to samo w drugą stronę.
///
/// @returns `REJECT_REASON_UNSPECIFIED`, gdy rozkaz przeszedł — udany rozkaz nie generuje
/// żadnej odpowiedzi, więc „brak powodu" znaczy tu „nie ma o czym mówić".
game::RejectReason execute_command(
    const game::Command& command,
    Simulation& simulation,
    std::uint8_t slot);

} // namespace gs
