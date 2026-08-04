#pragma once

#include "sim/roster.hpp"

#include <game.pb.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gs
{

/// Wszystko, co gracz musi wiedzieć o meczu, zanim zobaczy pierwszy kafelek.
struct MatchDescription
{
    /// Identyfikator z katalogu map — ten sam, którym klient adresuje teren.
    std::string map_id;

    /// SHA-256 pliku terenu, policzone z wczytanych bajtów (D13).
    std::array<std::uint8_t, 32> map_sha256{};

    std::uint16_t map_width = 0;

    std::uint16_t map_height = 0;

    std::uint32_t tick_rate = 0;

    std::int64_t seed = 0;
};

/// Dwie wiadomości, które dostaje każdy wchodzący: `MatchInit` i keyframe.
///
/// Obie są zbudowane raz i trzymane jako gotowe wiadomości. Keyframe dlatego, że policzenie
/// runów to przejście po dwóch milionach bajtów, a `MatchInit` dlatego, że lista slotów jest
/// identyczna dla wszystkich — przy wejściu dokłada się do niej wyłącznie `your_slot`.
class MatchIntro
{
public:
    MatchIntro(
        MatchDescription description,
        std::span<const Actor> actors,
        std::span<const std::uint8_t> owner);

    std::shared_ptr<const std::string> init_for(std::uint8_t slot) const;

    /// Keyframe ze znacznikiem bieżącego tiku.
    ///
    /// Numer tiku jest jedyną rzeczą, która zmienia się między wejściami, dopóki nie ma
    /// symulacji — dlatego wchodzi dopiero przy serializacji, a runy są policzone raz.
    /// Gdy świat zacznie się zmieniać, ta sama metoda będzie przebudowywać runy; sygnatura
    /// jest już taka, jak trzeba.
    std::shared_ptr<const std::string> keyframe_at(std::uint32_t tick) const;

    int run_count() const noexcept
    {
        return keyframe_.runs_size();
    }

    /// Rozmiar keyframe'a na drucie — do sprawdzenia budżetu z §7 dokumentu architektury.
    std::size_t keyframe_bytes() const;

private:
    MatchDescription description_;

    /// Gotowa wiadomość bez `your_slot` — jedynego pola, które różni graczy między sobą.
    game::MatchInit init_;

    game::Snapshot keyframe_;
};

} // namespace gs
