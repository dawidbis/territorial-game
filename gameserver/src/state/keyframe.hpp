#pragma once

#include <cstdint>
#include <span>

namespace game
{
class Snapshot;
}

namespace gs
{

/// Zapisuje całą tablicę właścicieli jako runy RLE w układzie wiersz po wierszu (D4).
///
/// **Pustkowia nie jadą.** Klient zeruje `owner[]` i nakłada na to runy, więc kafelki niczyje
/// są opisane samą swoją nieobecnością — a `start_delta` z §6 dokumentu architektury dostaje
/// wtedy sens, który miał mieć: jest długością przerwy, a nie zawsze zerem. Na mapie w połowie
/// pokrytej wodą to różnica rzędu dwukrotności rozmiaru keyframe'a.
///
/// Woda jedzie, mimo że klient ma ją też w terenie. To nie jest przeoczenie: dzięki temu
/// keyframe opisuje `owner[]` **w całości**, więc klient nie musi łączyć dwóch źródeł, żeby
/// wiedzieć, kto jest właścicielem kafelka. Ładna konsekwencja uboczna: keyframe szkieletu
/// nie jest pusty, więc ścieżka daje się zmierzyć, zanim pojawi się terytorium.
void build_keyframe(std::span<const std::uint8_t> owner, game::Snapshot& snapshot);

} // namespace gs
