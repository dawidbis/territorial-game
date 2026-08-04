#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace gs
{

/// Wszystko, co proces dostaje z zewnątrz w chwili startu.
///
/// Proces obsługuje dokładnie jeden mecz (D7), więc żadna z tych wartości nie zmienia
/// się przez całe jego życie — stąd zwykła struktura zamiast konfiguracji z możliwością
/// przeładowania. Roster nie jest tutaj: przychodzi manifestem na stdin (plan §3.5),
/// bo nicki graczy nie mają prawa trafić do listy procesów maszyny.
struct Options
{
    /// Mecz, którego dotyczy ten proces. Ta sama wartość musi stać w ścieżce upgrade'u
    /// WebSocketa i w bilecie — inaczej połączenie leci do niewłaściwej instancji.
    std::string match_id;

    /// Port w sieci wewnętrznej. Nasłuch idzie na pętlę zwrotną, bo przed procesem stoi
    /// proxy terminujące TLS (D9).
    std::uint16_t port = 0;

    /// Plik terenu w formacie `.tmap` (D13). Wczytywany dopiero w etapie E3.
    std::string map_path;

    /// Ziarno symulacji z meta. Bez zapisanego ziarna replay z D10 nie istnieje.
    std::int64_t seed = 0;

    /// Sufit aktorów — ludzie i boty razem (D12).
    std::uint32_t max_actors = 100;

    /// Klucz publiczny ECDSA P-256 do weryfikacji biletów. Wczytywany w etapie E2.
    std::string ticket_key_path;

    /// Źródło manifestu: `-` oznacza stdin i to jest wariant docelowy.
    std::string manifest_path = "-";

    /// Okno bezczynności w sekundach: ile proces czeka na pierwszego gracza i ile po odejściu
    /// ostatniego. 0 znaczy „wartość domyślna", czyli 120 s z D14.
    ///
    /// Istnieje **wyłącznie dla dev**, gdzie na maszynie stoi jeden mecz naraz i port jest
    /// wpisany w proxy klienta. Dwuminutowe okno znaczy tam, że po każdym meczu przez dwie
    /// minuty nie da się zacząć następnego — a to jest różnica między „można grać" a „nie
    /// można". Na produkcji zostaje 120 s, bo tyle trwa okno reconnectu obiecane graczowi.
    std::uint32_t idle_seconds = 0;

    /// Czy dopełnić obsadę botami do sufitu aktorów.
    ///
    /// Wyłączone daje mecz wyłącznie z ludzi: reszta mapy zostaje pustkowiem, które każdy
    /// może zająć. Przełącznik, a nie usunięcie kodu, bo boty wracają — dziś nie podejmują
    /// jeszcze żadnych decyzji, więc mecz z dziewięćdziesięcioma dziewięcioma z nich jest
    /// meczem z dziewięćdziesięcioma dziewięcioma nieruchomymi celami.
    bool fill_bots = true;

    /// Zatrzymanie po N tikach symulacji; 0 znaczy „do sygnału".
    ///
    /// Wyłącznie dla CI i ręcznego dymnego testu: proces, który sam kończy pracę, daje
    /// się sprawdzić bez zabijania go z zewnątrz i bez zgadywania, jak długo czekać.
    std::uint32_t max_ticks = 0;

    /// Wypisz pomoc i zakończ. Nie jest błędem, więc nie może iść kanałem błędów.
    bool help = false;
};

/// Ilu aktorów zmieści się w jednym meczu (D12) — ta sama liczba, którą zna meta.
inline constexpr std::uint32_t max_actors_per_match = 254;

/// Parsuje argumenty wiersza poleceń.
///
/// Oczekuje argumentów **bez** nazwy programu. Nieznana opcja jest błędem, a nie
/// ostrzeżeniem: literówka w wywołaniu z orkiestratora musi zatrzymać proces od razu,
/// zamiast po cichu uruchamiać mecz z domyślną wartością.
std::expected<Options, std::string> parse_options(std::span<const std::string_view> args);

/// Treść pomocy, jedno źródło dla `--help` i dla komunikatów o błędach.
std::string_view usage_text();

} // namespace gs
