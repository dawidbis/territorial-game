#include "sim/enclosure.hpp"

#include "map/tmap.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_map>

namespace gs
{
namespace
{

/// Jedna fala przeszukiwania, puszczana z jednego sąsiada przejętego kafelka.
struct Wave
{
    std::vector<std::uint32_t> tiles;

    /// Dokąd doszła. Indeks w `tiles`, bo lista odwiedzonych jest zarazem kolejką.
    std::size_t cursor = 0;

    /// Grupa, do której fala należy. Spotkanie dwóch fal scala ich grupy — i **to jest cała
    /// detekcja spójności**: fale, które się spotkały, są po tej samej stronie rozcięcia.
    std::uint32_t group = 0;

    bool exhausted() const noexcept
    {
        return cursor >= tiles.size();
    }
};

/// Stan grupy fal: obwód i to, czy fragment ma dokądkolwiek wyjście.
struct Group
{
    std::array<std::uint32_t, 256> border{};

    /// Wyjście z fragmentu: woda albo pustkowie. Jedno i drugie odbiera prawo do aneksji.
    ///
    /// Woda z reguły 2 — morze, jezioro i rzeka znaczą to samo. Pustkowie, bo „okrążony
    /// z każdej strony" ma znaczyć **z każdej**: fragment stykający się z wolną ziemią nie
    /// jest w kotle, tylko na skraju mapy politycznej i ma dokąd rosnąć. Krawędź świata
    /// wyjściem nie jest — tamtędy nikt nie przyjdzie i nikt nie wyjdzie.
    bool has_outlet = false;
};

/// Czy wszystkie fale zeszły się do jednej grupy — czyli czy nic nie zostało rozcięte.
///
/// Wołane wyłącznie przy starcie z więcej niż jedną falą: pojedyncza fala jest jedną grupą
/// z definicji, a jej zadaniem jest odpowiedzieć na inne pytanie („czy całe państwo jest
/// okrążone"), więc kończyć jej nie wolno.
bool single_group(const std::vector<Wave>& waves) noexcept
{
    if (waves.size() < 2)
    {
        return false;
    }

    const std::uint32_t first = waves.front().group;

    return std::ranges::all_of(waves, [first](const Wave& wave) { return wave.group == first; });
}

/// Ile grup ma jeszcze niewyczerpaną falę.
///
/// Jedna taka grupa to główny trzon terytorium — po nim nie ma po co chodzić, bo o niczym
/// nie rozstrzyga, a kosztuje tyle, ile ma pól.
std::size_t running_groups(const std::vector<Wave>& waves)
{
    std::size_t count = 0;

    for (std::uint32_t group = 0; group < waves.size(); ++group)
    {
        for (const Wave& wave : waves)
        {
            if (wave.group == group && !wave.exhausted())
            {
                ++count;

                break;
            }
        }
    }

    return count;
}

/// Kto wchłania kocioł: właściciel większości pól na obwodzie.
///
/// Remis rozstrzyga **niższy slot**, a nie kolejność napotkania — porządek totalny jest tu
/// warunkiem replayu (D10), tak samo jak w kolejce podboju.
std::uint8_t majority_owner(const std::array<std::uint32_t, 256>& border) noexcept
{
    std::uint8_t best = tmap::wasteland_owner;
    std::uint32_t best_count = 0;

    for (std::size_t slot = 1; slot < border.size(); ++slot)
    {
        if (slot == tmap::water_owner)
        {
            continue;
        }

        if (border[slot] > best_count)
        {
            best = static_cast<std::uint8_t>(slot);
            best_count = border[slot];
        }
    }

    return best;
}

} // namespace

bool may_split(const World& world, std::uint32_t captured, std::uint8_t defender) noexcept
{
    std::array<std::uint32_t, 4> neighbors{};

    const std::uint32_t count = world.neighbors4(captured, neighbors);

    std::uint32_t owned = 0;

    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (world.owner_at(neighbors[index]) == defender)
        {
            ++owned;
        }
    }

    // Jeden sąsiad nie rozcina niczego: usunięcie liścia zostawia resztę spójną. Taki kafelek
    // bywa za to **ostatnim połączeniem** odciętego już fragmentu i tamten przypadek łapie
    // osobna, tańsza droga — patrz `find_enclosure` i sufit rozmiaru.
    return owned >= 2;
}

std::vector<Enclosure> find_enclosures(
    const World& world,
    std::uint32_t captured,
    std::uint8_t defender)
{
    // Reguła 3: pustkowia nie wchłania się okrążeniem. Zamknięcie pierścienia wokół pustego
    // obszaru dawałoby go za darmo i przestawiało tempo pierwszych minut meczu.
    if (defender == tmap::wasteland_owner || defender == tmap::water_owner)
    {
        return {};
    }

    std::array<std::uint32_t, 4> neighbors{};

    const std::uint32_t count = world.neighbors4(captured, neighbors);

    // Kafelek → fala, która go zajęła. Spotkanie fal poznajemy po tym, że kafelek jest już
    // czyjś — wspólny zbiór „odwiedzone" bez tej informacji kazałby fali zamilknąć w chwili,
    // gdy jej front zajęła sąsiadka, czyli mylił spójne terytorium z rozciętym.
    std::unordered_map<std::uint32_t, std::size_t> claimed;

    std::vector<Wave> waves;

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const std::uint32_t neighbor = neighbors[index];

        if (world.owner_at(neighbor) != defender || claimed.contains(neighbor))
        {
            continue;
        }

        claimed.emplace(neighbor, waves.size());

        Wave wave;
        wave.tiles.push_back(neighbor);
        wave.group = static_cast<std::uint32_t>(waves.size());

        waves.push_back(std::move(wave));
    }

    if (waves.empty())
    {
        return {};
    }

    // Jedna fala nie może wykryć rozcięcia — rozcięcie zostawia fragmenty przy **różnych**
    // sąsiadach przejętego kafelka. Zostaje pytanie „czy to całe państwo jest okrążone",
    // a na nie odpowiada przejście po całym terytorium. Dlatego wchodzi tu sufit rozmiaru:
    // sprawdzenie kosztuje tyle, ile państwo ma pól, więc dla wielkich państw nie ruszamy go
    // wcale. Kocioł, o którym ktokolwiek pomyśli „kocioł", jest daleko poniżej tej granicy.
    if (waves.size() == 1 && world.tiles_of(defender) > max_surrounded_player_tiles)
    {
        return {};
    }

    std::vector<Group> groups(waves.size());

    std::size_t visited = 0;

    for (;;)
    {
        bool progressed = false;

        for (Wave& wave : waves)
        {
            if (wave.exhausted())
            {
                continue;
            }

            const std::uint32_t tile = wave.tiles[wave.cursor++];

            ++visited;

            const std::uint32_t around = world.neighbors4(tile, neighbors);

            // Krawędź mapy nie jest wodą i nie blokuje aneksji: kafelek przy brzegu świata
            // ma mniej niż czterech sąsiadów, a to znaczy tylko tyle, że tamtędy nikt nie
            // przyjdzie.
            for (std::uint32_t index = 0; index < around; ++index)
            {
                const std::uint32_t neighbor = neighbors[index];
                const std::uint8_t owner = world.owner_at(neighbor);

                if (owner == tmap::water_owner || owner == tmap::wasteland_owner)
                {
                    groups[wave.group].has_outlet = true;

                    continue;
                }

                if (owner != defender)
                {
                    ++groups[wave.group].border[owner];

                    continue;
                }

                const auto [entry, fresh] = claimed.try_emplace(neighbor, wave.group);

                if (fresh)
                {
                    wave.tiles.push_back(neighbor);

                    continue;
                }

                const std::uint32_t other = waves[entry->second].group;

                if (other != wave.group)
                {
                    // Fale się spotkały — po tej stronie nic nie zostało rozcięte. Grupy
                    // scalają się razem z obwodem i kontaktem z wodą, bo od tej chwili
                    // opisują jeden fragment.
                    const std::uint32_t merged = wave.group;

                    groups[merged].has_outlet =
                        groups[merged].has_outlet || groups[other].has_outlet;

                    for (std::size_t slot = 0; slot < groups[merged].border.size(); ++slot)
                    {
                        groups[merged].border[slot] += groups[other].border[slot];
                    }

                    for (Wave& sibling : waves)
                    {
                        if (sibling.group == other)
                        {
                            sibling.group = merged;
                        }
                    }

                    // Wszystkie fale w jednej grupie znaczą, że przejęcie niczego nie
                    // rozcięło. Bez tego wyjścia przeszukiwanie szłoby dalej — po całym
                    // terytorium obrońcy, przy każdym przejęciu na froncie.
                    if (single_group(waves))
                    {
                        return {};
                    }
                }
            }

            progressed = true;
        }

        if (visited > max_enclosure_tiles)
        {
            return {};
        }

        if (!progressed || running_groups(waves) <= 1)
        {
            // Koniec rachunku. Jedna grupa wciąż w ruchu to główny trzon terytorium —
            // dalsze chodzenie po nim kosztuje, a niczego nie rozstrzyga. **Na tym polega
            // marsz krok w krok**: płacimy rozmiarem odciętych kawałków, nie całości.
            break;
        }
    }

    std::vector<Enclosure> found;

    for (std::uint32_t group = 0; group < groups.size(); ++group)
    {
        bool exhausted = false;

        Enclosure enclosure;

        for (const Wave& wave : waves)
        {
            if (wave.group != group)
            {
                continue;
            }

            if (!wave.exhausted())
            {
                exhausted = false;

                break;
            }

            exhausted = true;

            enclosure.tiles.insert(enclosure.tiles.end(), wave.tiles.begin(), wave.tiles.end());
        }

        // Fragment z wyjściem — nad wodą albo na pustkowie — zdobywa się polem po polu,
        // jak każdy inny.
        if (!exhausted || groups[group].has_outlet)
        {
            continue;
        }

        enclosure.annexer = majority_owner(groups[group].border);

        // Obwód bez ani jednego gracza znaczy, że fragmentu nikt nie otacza — nie ma komu
        // go anektować.
        if (enclosure.annexer == tmap::wasteland_owner)
        {
            continue;
        }

        found.push_back(std::move(enclosure));
    }

    return found;
}

} // namespace gs
