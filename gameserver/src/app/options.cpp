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

/// Wczytywanie jednej wartości z wiersza poleceń — nazwa opcji, jej wartość i pierwszy błąd.
///
/// Powstało z powtórzenia: każda opcja liczbowa miała ten sam czteroliniowy rytuał — sparsuj,
/// sprawdź błąd, sprawdź zakres, przypisz — a różnice między opcjami ginęły w tym powtórzeniu.
/// Tutaj każda z nich jest jedną linią, a rytuał stoi raz.
///
/// Błąd jest **zapamiętywany, nie zwracany**, żeby wołający nie musiał sprawdzać go po każdym
/// przypisaniu. Pętla opcji sprawdza go raz na obrót i wychodzi natychmiast, więc pierwsza
/// pomyłka wciąż zatrzymuje parsowanie w tym samym miejscu co wcześniej.
class ValueReader
{
public:
    ValueReader(std::string_view name, std::string_view value) noexcept
        : name_(name)
        , value_(value)
    {
    }

    /// Wartość dosłownie, bez sprawdzania — ścieżki i tryby manifestu.
    [[nodiscard]] std::string_view text() const noexcept
    {
        return value_;
    }

    /// Liczba z domkniętego przedziału `[low, high]`.
    ///
    /// Zakres jest częścią sygnatury, a nie osobnym `if` po przypisaniu: opcja bez granic to
    /// opcja, przez którą proces wstaje w stanie, którego nikt nie przewidział.
    template <typename Field, typename Bound>
    void number(Field& target, Bound low, Bound high)
    {
        const std::expected<Bound, std::string> parsed = parse_number<Bound>(name_, value_);

        if (!parsed)
        {
            error_ = parsed.error();

            return;
        }

        if (*parsed < low || *parsed > high)
        {
            error_ = std::format(
                "Opcja {} oczekuje wartości {}..{}, a dostała {}.",
                name_,
                low,
                high,
                *parsed);

            return;
        }

        target = static_cast<Field>(*parsed);
    }

    /// Liczba bez ograniczeń — ziarno i liczniki, dla których każda wartość jest legalna.
    template <typename Field>
    void number(Field& target)
    {
        const std::expected<Field, std::string> parsed = parse_number<Field>(name_, value_);

        if (!parsed)
        {
            error_ = parsed.error();

            return;
        }

        target = *parsed;
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return !error_.empty();
    }

    std::string take_error() noexcept
    {
        return std::move(error_);
    }

private:
    std::string_view name_;

    std::string_view value_;

    std::string error_;
};

constexpr std::array value_options{
    std::string_view{"--match-id"},
    std::string_view{"--port"},
    std::string_view{"--map"},
    std::string_view{"--seed"},
    std::string_view{"--max-actors"},
    std::string_view{"--ticket-key"},
    std::string_view{"--manifest"},
    std::string_view{"--bots"},
    std::string_view{"--idle-seconds"},
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
  --bots <0|1>          czy dopełnić obsadę botami do sufitu aktorów (domyślnie 1)
  --idle-seconds <sek>  okno bezczynności: czekanie na pierwszego gracza i po odejściu
                        ostatniego; 0 to wartość domyślna 120 s (okno reconnectu z D14)
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

        ValueReader reader(name, args[++index]);

        // Jedna linia na opcję. Wszystko, co się w niej powtarzało — parsowanie, sprawdzenie
        // zakresu, propagacja błędu — siedzi w `ValueReader` i jest tam napisane raz.
        if (name == "--match-id")
        {
            if (!is_canonical_guid(reader.text()))
            {
                return std::unexpected(std::format(
                    "Opcja --match-id oczekuje GUID-a, a dostała '{}'.",
                    reader.text()));
            }

            options.match_id = to_lower_ascii(reader.text());
        }
        else if (name == "--port")
        {
            reader.number(options.port, 1u, 65535u);
        }
        else if (name == "--map")
        {
            options.map_path = reader.text();
        }
        else if (name == "--seed")
        {
            reader.number(options.seed);
        }
        else if (name == "--max-actors")
        {
            reader.number(options.max_actors, 1u, max_actors_per_match);
        }
        else if (name == "--ticket-key")
        {
            options.ticket_key_path = reader.text();
        }
        else if (name == "--manifest")
        {
            options.manifest_path = reader.text();
        }
        else if (name == "--bots")
        {
            // Bez zakresu, choć pomoc mówi `<0|1>`: każda wartość niezerowa znaczy „tak"
            // i tak było od początku. Zawężenie tutaj byłoby zmianą zachowania przemyconą
            // w refaktorze — na to jest osobna decyzja i osobny test.
            std::uint32_t bots = 0;

            reader.number(bots);

            options.fill_bots = bots != 0;
        }
        else if (name == "--idle-seconds")
        {
            reader.number(options.idle_seconds);
        }
        else if (name == "--max-ticks")
        {
            reader.number(options.max_ticks);
        }

        if (reader.failed())
        {
            return std::unexpected(reader.take_error());
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
