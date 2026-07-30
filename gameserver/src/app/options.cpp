#include "app/options.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <system_error>

namespace gs
{
namespace
{

constexpr std::size_t guid_length = 36;

/// Czy wartość wygląda jak GUID w postaci kanonicznej.
///
/// Sprawdzane, bo `matchId` trafia wprost do porównania ze ścieżką URL-a. Wartość
/// dowolnego kształtu przeszłaby dalej i objawiła się dopiero jako mecz, do którego
/// nikt nie może wejść.
bool is_canonical_guid(std::string_view value)
{
    if (value.size() != guid_length)
    {
        return false;
    }

    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const char character = value[index];

        if (index == 8 || index == 13 || index == 18 || index == 23)
        {
            if (character != '-')
            {
                return false;
            }

            continue;
        }

        const bool hexadecimal = (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');

        if (!hexadecimal)
        {
            return false;
        }
    }

    return true;
}

/// Sprowadza GUID do jednej postaci, żeby porównanie ze ścieżką nie zależało od
/// wielkości liter. Ręcznie, bo `std::tolower` zależy od ustawień regionalnych.
std::string to_lower_ascii(std::string_view value)
{
    std::string lowered{value};

    for (char& character : lowered)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }

    return lowered;
}

template <typename T>
std::expected<T, std::string> parse_number(std::string_view name, std::string_view value)
{
    T parsed{};

    const char* first = value.data();
    const char* last = value.data() + value.size();

    const std::from_chars_result result = std::from_chars(first, last, parsed);

    if (result.ec != std::errc{} || result.ptr != last)
    {
        return std::unexpected(
            std::format("Opcja {} oczekuje liczby, a dostała '{}'.", name, value));
    }

    return parsed;
}

constexpr std::array value_options{
    std::string_view{"--match-id"},
    std::string_view{"--port"},
    std::string_view{"--map"},
    std::string_view{"--seed"},
    std::string_view{"--max-actors"},
    std::string_view{"--ticket-key"},
    std::string_view{"--manifest"},
    std::string_view{"--max-ticks"},
};

} // namespace

std::string_view usage_text()
{
    return R"(Serwer pojedynczego meczu gry terytorialnej.

  --match-id <guid>     mecz obsługiwany przez ten proces (wymagane)
  --port <1-65535>      port nasłuchu na pętli zwrotnej (wymagane)
  --map <ścieżka>       plik terenu .tmap
  --seed <liczba>       ziarno symulacji z meta
  --max-actors <1-254>  sufit aktorów, ludzie i boty razem (domyślnie 100)
  --ticket-key <plik>   klucz publiczny ECDSA P-256 do weryfikacji biletów
  --manifest <plik|->   źródło manifestu meczu, '-' to stdin (domyślnie -)
  --max-ticks <liczba>  zakończ po N tikach symulacji; 0 to praca do sygnału
  --help                ta pomoc
)";
}

std::expected<Options, std::string> parse_options(std::span<const std::string_view> args)
{
    Options options;

    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const std::string_view name = args[index];

        if (name == "--help" || name == "-h")
        {
            options.help = true;

            return options;
        }

        if (std::ranges::find(value_options, name) == value_options.end())
        {
            return std::unexpected(std::format("Nieznana opcja '{}'.", name));
        }

        if (index + 1 >= args.size())
        {
            return std::unexpected(std::format("Opcja {} wymaga wartości.", name));
        }

        const std::string_view value = args[++index];

        if (name == "--match-id")
        {
            if (!is_canonical_guid(value))
            {
                return std::unexpected(
                    std::format("Opcja --match-id oczekuje GUID-a, a dostała '{}'.", value));
            }

            options.match_id = to_lower_ascii(value);
        }
        else if (name == "--port")
        {
            const std::expected<std::uint32_t, std::string> port =
                parse_number<std::uint32_t>(name, value);

            if (!port)
            {
                return std::unexpected(port.error());
            }

            if (*port == 0 || *port > 65535)
            {
                return std::unexpected(
                    std::format("Opcja --port oczekuje wartości 1..65535, a dostała {}.", *port));
            }

            options.port = static_cast<std::uint16_t>(*port);
        }
        else if (name == "--map")
        {
            options.map_path = value;
        }
        else if (name == "--seed")
        {
            const std::expected<std::int64_t, std::string> seed =
                parse_number<std::int64_t>(name, value);

            if (!seed)
            {
                return std::unexpected(seed.error());
            }

            options.seed = *seed;
        }
        else if (name == "--max-actors")
        {
            const std::expected<std::uint32_t, std::string> max_actors =
                parse_number<std::uint32_t>(name, value);

            if (!max_actors)
            {
                return std::unexpected(max_actors.error());
            }

            if (*max_actors == 0 || *max_actors > max_actors_per_match)
            {
                return std::unexpected(std::format(
                    "Opcja --max-actors oczekuje wartości 1..{}, a dostała {}.",
                    max_actors_per_match,
                    *max_actors));
            }

            options.max_actors = *max_actors;
        }
        else if (name == "--ticket-key")
        {
            options.ticket_key_path = value;
        }
        else if (name == "--manifest")
        {
            options.manifest_path = value;
        }
        else if (name == "--max-ticks")
        {
            const std::expected<std::uint32_t, std::string> max_ticks =
                parse_number<std::uint32_t>(name, value);

            if (!max_ticks)
            {
                return std::unexpected(max_ticks.error());
            }

            options.max_ticks = *max_ticks;
        }
    }

    if (options.match_id.empty())
    {
        return std::unexpected(std::string{"Opcja --match-id jest wymagana."});
    }

    if (options.port == 0)
    {
        return std::unexpected(std::string{"Opcja --port jest wymagana."});
    }

    return options;
}

} // namespace gs
