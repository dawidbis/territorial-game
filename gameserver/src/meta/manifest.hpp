#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace gs
{

/// Jeden gracz z rostera zamrożonego przez lobby.
struct ManifestPlayer
{
    std::uint8_t slot = 0;

    std::string name;

    /// Kolor już po konwersji — **domena trzyma HSV, ale zamienia go meta** (§3.5 planu),
    /// żeby proces meczu nigdy nie dowiedział się o istnieniu tamtej przestrzeni.
    std::uint32_t color_rgb = 0;
};

/// Roster meczu podany procesowi przy starcie.
///
/// Idzie **stdinem, nie argumentami i nie plikiem** (decyzja 6.2): nicki graczy w argumentach
/// trafiłyby do listy procesów całej maszyny, a plik trzeba by sprzątać. Orkiestrator tylko
/// go przekazuje — nie rozumie i nie przechowuje.
///
/// Wariant „proces dopytuje meta przy starcie" odpadł, bo wprowadzałby zależność od meta
/// w ścieżce startu meczu — dokładnie to, czego zabrania §4.3 dokumentu architektury.
///
/// Format:
/// ```json
/// { "players": [ { "slot": 1, "name": "Ala", "colorRgb": 16711680 } ] }
/// ```
///
/// Botów w manifeście nie ma i nie będzie: nie mają wiersza w bazie, a ich nicki i kolory
/// wynikają z ziarna meczu — to warunek z §8, nie oszczędność.
std::expected<std::vector<ManifestPlayer>, std::string> parse_manifest(
    std::string_view json,
    std::uint32_t max_actors);

/// Czyta manifest ze standardowego wejścia albo z pliku; `-` oznacza stdin.
std::expected<std::string, std::string> read_manifest(std::string_view source);

} // namespace gs
