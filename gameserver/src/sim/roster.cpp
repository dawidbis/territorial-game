#include "sim/roster.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <string_view>

namespace gs
{
namespace
{

/// Szesnaście początków i szesnaście końcówek — 256 nicków, czyli dokładnie tyle, ile jest
/// slotów. Nick bota powstaje z **permutacji** tej przestrzeni, więc dwa boty w jednym meczu
/// nie mogą dostać tego samego: przy losowaniu niezależnym kolizja byłaby regułą, nie
/// wyjątkiem (paradoks dnia urodzin przy 253 botach jest pewnością).
constexpr std::array<std::string_view, 16> name_prefixes{
    "Ka", "Mor", "Vel", "Tan", "Zor", "Bri", "Nol", "Sar",
    "Dre", "Ith", "Qua", "Rho", "Lum", "Fen", "Gar", "Oss"};

constexpr std::array<std::string_view, 16> name_suffixes{
    "dar", "mir", "tha", "ren", "lox", "vin", "ash", "gor",
    "nel", "tis", "kar", "dun", "wyn", "sel", "brim", "oth"};

constexpr std::size_t name_space = name_prefixes.size() * name_suffixes.size();

// Permutacja chodzi po 0..255, więc dopiero równość gwarantuje, że każdy slot dostanie inny
// nick. Zmiana którejkolwiek z tablic bez zmiany drugiej cicho przywróciłaby kolizje.
static_assert(name_space == 256, "Przestrzeń nicków musi pokrywać dokładnie 256 slotów.");

/// Strumienie PCG rozdzielone tak, żeby nicki i kolory nie czerpały z tego samego ciągu —
/// inaczej bot o „ładnym" nicku miałby zawsze ten sam kolor.
constexpr std::uint64_t name_stream = 1;
constexpr std::uint64_t colour_stream = 2;

/// Permutacja 0..255 wyliczona z ziarna. Tasowanie własne, bo `std::shuffle` używa rozkładu
/// ze standardu, a te nie mają zdefiniowanej implementacji — ten sam kod dałby na dwóch
/// bibliotekach dwa różne mecze.
std::array<std::uint8_t, 256> shuffled(std::uint64_t seed, std::uint64_t stream)
{
    std::array<std::uint8_t, 256> order{};

    std::ranges::iota(order, std::uint8_t{0});

    Pcg32 rng(seed, stream);

    for (std::size_t index = order.size() - 1; index > 0; --index)
    {
        const std::uint32_t pick = rng.below(static_cast<std::uint32_t>(index + 1));

        std::swap(order[index], order[pick]);
    }

    return order;
}

} // namespace

std::uint32_t color_from_hue(std::uint32_t hue) noexcept
{
    // Sześć odcinków po 256 kroków: w każdym jeden kanał rośnie albo maleje, a pozostałe
    // stoją na skrajnych wartościach.
    const std::uint32_t position = (hue % 256U) * 6U;
    const std::uint32_t segment = position / 256U;
    const std::uint32_t offset = position % 256U;
    const std::uint32_t falling = 255U - offset;

    std::uint32_t red = 0;
    std::uint32_t green = 0;
    std::uint32_t blue = 0;

    switch (segment)
    {
    case 0:
        red = 255; green = offset; break;
    case 1:
        red = falling; green = 255; break;
    case 2:
        green = 255; blue = offset; break;
    case 3:
        green = falling; blue = 255; break;
    case 4:
        red = offset; blue = 255; break;
    default:
        red = 255; blue = falling; break;
    }

    return (red << 16) | (green << 8) | blue;
}

Roster Roster::build(
    const std::vector<ManifestPlayer>& players,
    std::uint32_t max_actors,
    std::int64_t seed,
    bool fill_bots)
{
    // Ziarno jest w meta liczbą ze znakiem, a generator pracuje na bitach — konwersja jest
    // odwracalna i nie gubi ani jednego z nich.
    const std::uint64_t bits = static_cast<std::uint64_t>(seed);

    const std::array<std::uint8_t, 256> names = shuffled(bits, name_stream);
    const std::array<std::uint8_t, 256> hues = shuffled(bits, colour_stream);

    Roster roster;
    roster.humans_ = players.size();
    roster.actors_.reserve(max_actors);

    std::array<bool, 256> taken{};

    for (const ManifestPlayer& player : players)
    {
        roster.actors_.push_back(Actor{player.slot, player.name, player.color_rgb, false});

        taken[player.slot] = true;
    }

    // Sufit aktorów zostaje sufitem, także bez botów: to on ogranicza zakres slotów
    // w bilecie, więc wyłączenie dopełniania nie ma prawa go ruszyć.
    for (std::uint32_t slot = 1; fill_bots && slot <= max_actors; ++slot)
    {
        if (taken[slot])
        {
            continue;
        }

        const std::size_t name = names[slot];

        Actor bot;
        bot.slot = static_cast<std::uint8_t>(slot);
        bot.name = std::string{name_prefixes[name / name_suffixes.size()]}
            + std::string{name_suffixes[name % name_suffixes.size()]};
        bot.color_rgb = color_from_hue(hues[slot]);
        bot.is_bot = true;

        roster.actors_.push_back(std::move(bot));
    }

    // Po slocie, nie po kolejności wejścia: `MatchInit` idzie do wszystkich tym samym
    // buforem, a lista posortowana po czymkolwiek innym zależałaby od kolejności w manifeście.
    std::ranges::sort(
        roster.actors_,
        [](const Actor& left, const Actor& right) { return left.slot < right.slot; });

    return roster;
}

} // namespace gs
