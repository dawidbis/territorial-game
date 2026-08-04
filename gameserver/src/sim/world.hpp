#pragma once

#include "map/tmap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gs
{

/// Stan świata: kto jest właścicielem którego kafelka.
///
/// Teren i właściciele leżą w dwóch tablicach, a nie w jednej strukturze na kafelek. Teren
/// jest niezmienny i przychodzi z pliku (D13), właściciele zmieniają się co tik — złączone
/// znaczyłyby, że każda delta ciągnie do cache'u bajty, których nikt nie zmienia.
///
/// Woda wchodzi do `owner[]` jako 255 (D12) i **to jest cała reprezentacja nieprzejezdności**:
/// symulacja nie musi zaglądać do terenu, żeby wiedzieć, że kafelka nie da się przejąć,
/// a keyframe opisuje ją tym samym mechanizmem co terytorium.
class World
{
public:
    explicit World(const tmap::MapView& map);

    std::span<const std::uint8_t> owner() const noexcept
    {
        return owner_;
    }

    std::uint16_t width() const noexcept
    {
        return width_;
    }

    std::uint16_t height() const noexcept
    {
        return height_;
    }

    /// Ile kafelków jest lądem — liczone raz przy wczytaniu, bo `tmapgen` sprawdza tym
    /// samym udział lądu, a serwer wypisuje go do logu przy starcie.
    std::size_t land_tiles() const noexcept
    {
        return land_tiles_;
    }

    std::size_t spawn_count() const noexcept
    {
        return spawns_.size();
    }

    /// Ile kafelków ma każdy właściciel — indeksowane slotem, więc pozycja 0 to pustkowie,
    /// a 255 to woda (D12).
    ///
    /// Liczone przyrostowo przy każdej zmianie właściciela, a nie przez przejście po dwóch
    /// milionach kafelków: `PublicState` idzie co sekundę, a delty będą tysiącami na tik.
    std::span<const std::uint32_t> territory() const noexcept
    {
        return territory_;
    }

    std::uint32_t tiles_of(std::uint8_t slot) const noexcept
    {
        return territory_[slot];
    }

    std::size_t tile_count() const noexcept
    {
        return owner_.size();
    }

    std::uint8_t owner_at(std::uint32_t tile) const noexcept
    {
        return owner_[tile];
    }

    /// Typ terenu kafelka.
    ///
    /// Teren nie jest kopiowany — to widok w bufor pliku, który żyje przez cały mecz (D13).
    /// Kopia kosztowałaby drugie dwa megabajty i nie dałaby niczego: nikt go nie zmienia.
    tmap::Terrain terrain_at(std::uint32_t tile) const noexcept
    {
        return static_cast<tmap::Terrain>(terrain_[tile]);
    }

    bool is_land(std::uint32_t tile) const noexcept
    {
        return owner_[tile] != tmap::water_owner;
    }

    /// Sąsiedzi w czterech kierunkach; zwraca ile ich zapisano.
    ///
    /// Bez zawijania na krawędziach: mapa jest prostokątem, nie torusem, więc kafelek przy
    /// lewej krawędzi **nie** sąsiaduje z prawą. Zawinięcie zrobiłoby z natarcia na skraju
    /// desant na drugim końcu świata.
    ///
    /// `std::array`, a nie goła tablica w parametrze: ta druga rozpada się do wskaźnika, więc
    /// rozmiar „4" w sygnaturze jest wyłącznie komentarzem — kompilator przyjąłby też tablicę
    /// dwuelementową i wyszłoby to dopiero jako zapis poza nią.
    std::uint32_t neighbors4(std::uint32_t tile, std::array<std::uint32_t, 4>& out) const noexcept;

    /// Przepisuje kafelek nowemu właścicielowi i odnotowuje zmianę dla delt.
    ///
    /// Liczniki terytorium idą przyrostowo, bo `PublicState` czyta je co sekundę, a pełne
    /// przeliczenie to przejście po dwóch milionach kafelków.
    void set_owner(std::uint32_t tile, std::uint8_t slot);

    /// Kafelki, które zmieniły właściciela od ostatniego `clear_changed()`.
    ///
    /// Lista bez powtórzeń, bo snapshot leci co drugi tik symulacji (D3): kafelek przejęty
    /// i odbity w tym samym oknie ma pojechać raz, z właścicielem obowiązującym na koniec.
    std::span<const std::uint32_t> changed_tiles() const noexcept
    {
        return changed_;
    }

    void clear_changed() noexcept;

    /// Ile kafelków dostaje aktor na starcie.
    ///
    /// Nie jest to pokrętło do kręcenia: tyle liczy **dysk o promieniu 4 liczony od styku
    /// czterech kafelków**, czyli kształt startowy z pierwowzoru. Zmiana tej liczby bez
    /// zmiany promienia w `world.cpp` obcięłaby dysk w połowie ostatniego pierścienia.
    static constexpr std::uint32_t starting_tiles = 52;

    /// Stawia aktora na jego punkcie startowym i przydziela mu terytorium startowe.
    ///
    /// Indeks spawnu to indeks slotu (§3.6 planu) — slot 7 startuje na siódmym punkcie
    /// z pliku. Terytorium to **dysk** o promieniu 4 wokół tego punktu, dobierany od środka
    /// na zewnątrz. Kafelki wody i zajęte przez kogoś innego są pomijane, więc spawn przy
    /// brzegu dostaje mniej niż
    /// <see cref="starting_tiles"/> i to jest w porządku.
    ///
    /// Zwraca `false`, gdy slot jest poza zakresem `1..spawn_count()` albo gdy sam punkt
    /// startowy jest już zajęty — wołający ma wtedy manifest niezgodny z mapą.
    bool place_actor(std::uint8_t slot);

private:
    /// Przepisuje właściciela razem z licznikami, **bez** odnotowania zmiany.
    ///
    /// Osobno od <see cref="set_owner"/>, bo obsadzanie punktów startowych dzieje się przed
    /// pierwszym tikiem: te kafelki opisuje keyframe, więc trafione też do delt jechałyby
    /// dwa razy.
    void assign_owner(std::uint32_t tile, std::uint8_t slot) noexcept;

    std::vector<std::uint8_t> owner_;

    std::span<const std::uint8_t> terrain_;

    std::vector<tmap::Spawn> spawns_;

    std::vector<std::uint32_t> changed_;

    /// Czy kafelek jest już na liście zmian. Bitowo — przy dwóch milionach kafelków to
    /// ćwierć megabajta, a wariant ze zbiorem mieszającym kosztowałby alokację na przejęcie.
    std::vector<bool> changed_mark_;

    std::array<std::uint32_t, 256> territory_{};

    std::uint16_t width_ = 0;

    std::uint16_t height_ = 0;

    std::size_t land_tiles_ = 0;
};

} // namespace gs
