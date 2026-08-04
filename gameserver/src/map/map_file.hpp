#pragma once

#include "map/tmap.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gs
{

/// Plik terenu wczytany do pamięci razem z jego sumą kontrolną.
///
/// **Hash liczy proces, nie meta** (D13). Sensem `mapSha256` w `MatchInit` jest wykrycie,
/// że klient ma w cache'u inny teren niż serwer; wartość przepisana z bazy poświadczałaby
/// to, co meta *myśli* o pliku, a policzona z faktycznie wczytanych bajtów poświadcza teren,
/// na którym mecz naprawdę się toczy.
///
/// `mmap` z D13 tu nie ma i to jest decyzja 6.4: jedyna korzyść — współdzielona page cache —
/// pojawia się przy 250 procesach na jednym hoście, czyli razem z agentem. Do tego czasu
/// byłby to kod platformowy pisany na zapas. Gdy wejdzie, zmieni się wyłącznie to, skąd
/// pochodzi `bytes_`.
class MapFile
{
public:
    /// Kopiowanie jest zabronione, bo `view_` wskazuje w `bytes_`. Przenoszenie jest
    /// bezpieczne: przeniesiony wektor oddaje ten sam blok na stercie, więc widok dalej
    /// pokazuje te same bajty.
    MapFile(const MapFile&) = delete;
    MapFile& operator=(const MapFile&) = delete;

    MapFile(MapFile&&) noexcept = default;
    MapFile& operator=(MapFile&&) noexcept = default;

    ~MapFile() = default;

    static std::expected<MapFile, std::string> open(const std::filesystem::path& path);

    /// Ta sama droga bez dysku — dla testów i dla trybu, w którym teren przyjdzie skądinąd.
    static std::expected<MapFile, std::string> from_bytes(std::vector<std::uint8_t> bytes);

    const tmap::MapView& map() const noexcept
    {
        return view_;
    }

    /// SHA-256 **całego pliku**, czyli dokładnie tych bajtów, które pobierze klient.
    std::span<const std::uint8_t, 32> sha256() const noexcept
    {
        return digest_;
    }

    /// Suma kontrolna w postaci szesnastkowej — do logu i do adresu `/maps/{id}/{sha}/…`.
    std::string sha256_hex() const;

private:
    MapFile() = default;

    std::vector<std::uint8_t> bytes_;

    tmap::MapView view_;

    std::array<std::uint8_t, 32> digest_{};
};

} // namespace gs
