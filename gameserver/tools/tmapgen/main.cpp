#include "convert.hpp"
#include "png.hpp"
#include "synthetic.hpp"

#include "map/tmap.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr std::string_view usage = R"(Konwerter map do formatu .tmap.

Konwersja narysowanej mapy:
  tmapgen --source maps/moon.png --meta maps/moon.json --out maps/moon.tmap

Mapa z ziarna, do czasu, aż powstanie pierwsza narysowana:
  tmapgen --synthetic --out maps/synthetic.tmap --seed 1

  --source <plik.png>   siatka terenu, 1 piksel = 1 kafelek, dokładnie 4 kolory
  --meta <plik.json>    id, nazwa, maxActors i punkty startowe
  --out <plik.tmap>     wynik konwersji
  --synthetic           zamiast czytać obrazek, wygeneruj teren z ziarna
  --id <tekst>          identyfikator mapy syntetycznej (domyślnie 'synthetic')
  --width <liczba>      szerokość mapy syntetycznej (domyślnie 2000)
  --height <liczba>     wysokość mapy syntetycznej (domyślnie 1000)
  --max-actors <1-254>  ilu aktorów mapa ma pomieścić (domyślnie 100)
  --seed <liczba>       ziarno generatora (domyślnie 1)
  --help                ta pomoc
)";

struct Arguments
{
    std::string source;
    std::string meta;
    std::string out;

    bool synthetic = false;
    bool help = false;

    gs::tmapgen::SyntheticRequest request;
};

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

std::expected<Arguments, std::string> parse_arguments(std::span<const std::string_view> args)
{
    Arguments arguments;

    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const std::string_view name = args[index];

        if (name == "--help" || name == "-h")
        {
            arguments.help = true;

            return arguments;
        }

        if (name == "--synthetic")
        {
            arguments.synthetic = true;

            continue;
        }

        if (index + 1 >= args.size())
        {
            return std::unexpected(std::format("Opcja {} wymaga wartości.", name));
        }

        const std::string_view value = args[++index];

        if (name == "--source")
        {
            arguments.source = value;
        }
        else if (name == "--meta")
        {
            arguments.meta = value;
        }
        else if (name == "--out")
        {
            arguments.out = value;
        }
        else if (name == "--id")
        {
            arguments.request.id = value;
        }
        else if (name == "--width" || name == "--height")
        {
            const std::expected<std::uint32_t, std::string> parsed =
                parse_number<std::uint32_t>(name, value);

            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }

            if (*parsed == 0 || *parsed > gs::tmap::max_dimension)
            {
                return std::unexpected(std::format(
                    "Opcja {} oczekuje wartości 1..{}, a dostała {}.",
                    name,
                    gs::tmap::max_dimension,
                    *parsed));
            }

            (name == "--width" ? arguments.request.width : arguments.request.height) =
                static_cast<std::uint16_t>(*parsed);
        }
        else if (name == "--max-actors")
        {
            const std::expected<std::uint32_t, std::string> parsed =
                parse_number<std::uint32_t>(name, value);

            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }

            if (*parsed == 0 || *parsed > 254)
            {
                return std::unexpected(std::format(
                    "Opcja --max-actors oczekuje wartości 1..254, a dostała {}.",
                    *parsed));
            }

            arguments.request.max_actors = *parsed;
        }
        else if (name == "--seed")
        {
            const std::expected<std::uint64_t, std::string> parsed =
                parse_number<std::uint64_t>(name, value);

            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }

            arguments.request.seed = *parsed;
        }
        else
        {
            return std::unexpected(std::format("Nieznana opcja '{}'.", name));
        }
    }

    if (arguments.out.empty())
    {
        return std::unexpected(std::string{"Opcja --out jest wymagana."});
    }

    if (arguments.synthetic)
    {
        if (!arguments.source.empty() || !arguments.meta.empty())
        {
            return std::unexpected(std::string{
                "--synthetic i --source wykluczają się: albo teren przychodzi z obrazka, "
                "albo z ziarna."});
        }
    }
    else if (arguments.source.empty() || arguments.meta.empty())
    {
        return std::unexpected(std::string{
            "Konwersja wymaga --source i --meta. Bez obrazka nie ma siatki, bez opisu nie ma "
            "punktów startowych."});
    }

    return arguments;
}

/// Buduje mapę z pary plików źródłowych: obrazek daje siatkę, JSON całą resztę.
std::expected<gs::tmap::Map, std::string> convert(const Arguments& arguments)
{
    std::expected<gs::tmapgen::Metadata, std::string> metadata =
        gs::tmapgen::read_metadata_file(arguments.meta);

    if (!metadata)
    {
        return std::unexpected(metadata.error());
    }

    std::expected<gs::png::Image, std::string> image = gs::png::read_file(arguments.source);

    if (!image)
    {
        return std::unexpected(image.error());
    }

    std::expected<std::vector<std::uint8_t>, std::string> terrain =
        gs::tmapgen::terrain_from_image(*image);

    if (!terrain)
    {
        return std::unexpected(std::format("Plik '{}': {}", arguments.source, terrain.error()));
    }

    gs::tmap::Map map;
    map.id = std::move(metadata->id);
    map.width = static_cast<std::uint16_t>(image->width);
    map.height = static_cast<std::uint16_t>(image->height);
    map.spawns = std::move(metadata->spawns);
    map.terrain = std::move(*terrain);

    for (std::size_t index = 0; index < map.spawns.size(); ++index)
    {
        if (map.spawns[index].x >= map.width || map.spawns[index].y >= map.height)
        {
            return std::unexpected(std::format(
                "Punkt startowy {} stoi na [{}, {}], a mapa ma {}×{}.",
                index,
                map.spawns[index].x,
                map.spawns[index].y,
                map.width,
                map.height));
        }
    }

    std::expected<void, std::string> valid = gs::tmapgen::validate(map, metadata->max_actors);

    if (!valid)
    {
        return std::unexpected(valid.error());
    }

    return map;
}

void describe(const gs::tmap::Map& map)
{
    std::array<std::size_t, gs::tmap::terrain_type_count> counts{};

    for (const std::uint8_t tile : map.terrain)
    {
        ++counts[tile];
    }

    const double tiles = static_cast<double>(map.terrain.size());

    std::cout << std::format(
        "Mapa '{}' {}×{}, {} kafelków, {} punktów startowych.\n",
        map.id,
        map.width,
        map.height,
        map.terrain.size(),
        map.spawns.size());

    static constexpr std::array<std::string_view, gs::tmap::terrain_type_count> names{
        "woda",
        "niziny",
        "wyżyny",
        "góry"};

    for (std::size_t type = 0; type < counts.size(); ++type)
    {
        std::cout << std::format(
            "  {:<8} {:>10} ({:>5.1f}%)\n",
            names[type],
            counts[type],
            100.0 * static_cast<double>(counts[type]) / tiles);
    }
}

/// Wypisuje komunikat o przerwaniu bez użycia strumieni i formatowania.
///
/// `std::fputs` zamiast `std::cerr`: obsługa wyjątku ma być najprostszym mechanizmem, jaki jest
/// pod ręką. Strumienie i `std::format` same potrafią rzucić, a wyjątek z obsługi wyjątku
/// w `main` wraca dokładnie do tego, przed czym ta obsługa broni. Wynik zapisu jest tu bez
/// znaczenia — jeśli nie da się pisać na `stderr`, to nie ma już komu tego powiedzieć.
void report_fatal(const char* message, const char* detail) noexcept
{
    static_cast<void>(std::fputs(message, stderr));

    if (detail != nullptr)
    {
        static_cast<void>(std::fputs(detail, stderr));
    }

    static_cast<void>(std::fputs("\n", stderr));
}

} // namespace

int main(int argc, char* argv[])
try
{
    const std::vector<std::string_view> args(argv + 1, argv + argc);

    const std::expected<Arguments, std::string> arguments = parse_arguments(args);

    if (!arguments)
    {
        std::cerr << arguments.error() << "\n\n" << usage;

        return EXIT_FAILURE;
    }

    if (arguments->help)
    {
        std::cout << usage;

        return EXIT_SUCCESS;
    }

    std::expected<gs::tmap::Map, std::string> map =
        arguments->synthetic ? gs::tmapgen::generate(arguments->request) : convert(*arguments);

    if (!map)
    {
        std::cerr << map.error() << "\n";

        return EXIT_FAILURE;
    }

    // Mapa syntetyczna przechodzi tę samą walidację co narysowana. Generator, który wypuszcza
    // pliki odrzucane przez własne reguły, jest gorszy niż jego brak.
    if (arguments->synthetic)
    {
        const std::expected<void, std::string> valid =
            gs::tmapgen::validate(*map, arguments->request.max_actors);

        if (!valid)
        {
            std::cerr << std::format("Generator wypuścił mapę, która nie przechodzi walidacji: {}\n",
                                     valid.error());

            return EXIT_FAILURE;
        }
    }

    const std::expected<std::string, std::string> encoded = gs::tmap::encode(*map);

    if (!encoded)
    {
        std::cerr << encoded.error() << "\n";

        return EXIT_FAILURE;
    }

    std::ofstream out(arguments->out, std::ios::binary | std::ios::trunc);

    if (!out || !out.write(encoded->data(), static_cast<std::streamsize>(encoded->size())))
    {
        std::cerr << std::format("Nie udało się zapisać '{}'.\n", arguments->out);

        return EXIT_FAILURE;
    }

    describe(*map);

    std::cout << std::format("Zapisano '{}' — {} B.\n", arguments->out, encoded->size());

    return EXIT_SUCCESS;
}
// Konwerter czyta pliki i alokuje dwumegabajtowe bufory, więc wyjątek jest tu możliwy —
// a wyjątek, który dochodzi do `main`, kończy proces przez `std::terminate`, bez komunikatu.
catch (const std::exception& exception)
{
    report_fatal("tmapgen przerwany: ", exception.what());

    return EXIT_FAILURE;
}
catch (...)
{
    report_fatal("tmapgen przerwany wyjątkiem nieznanego typu.", nullptr);

    return EXIT_FAILURE;
}
