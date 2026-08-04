#include "app/startup.hpp"

#include "app/log.hpp"
#include "app/options.hpp"
#include "meta/manifest.hpp"
#include "tick/match_clock.hpp"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace gs
{
namespace
{

/// Sprawdza opcje, bez których proces nie ma czego robić.
///
/// Nie w `parse_options`, bo tam mieszka **składnia** wiersza poleceń: co jest opcją, co jej
/// wartością i czy liczba mieści się w zakresie. Tutaj jest pytanie innego rodzaju — czy da
/// się z tych opcji zbudować mecz — i odpowiedź na nie idzie tą samą drogą co „nie ma takiego
/// pliku".
std::expected<void, std::string> check_required(const Options& options)
{
    if (options.ticket_key_path.empty())
    {
        return std::unexpected(std::string{
            "Opcja --ticket-key jest wymagana: bez klucza publicznego meta proces nie ma jak "
            "odróżnić biletu od zmyślonego ciągu."});
    }

    if (options.map_path.empty())
    {
        return std::unexpected(std::string{
            "Opcja --map jest wymagana: bez terenu nie ma czego wysłać w keyframie, a mecz bez "
            "mapy nie jest meczem. Plik .tmap robi tmapgen."});
    }

    return {};
}

/// Wczytuje manifest i buduje z niego obsadę meczu.
///
/// Manifest idzie stdinem (decyzja 6.2), więc czytamy go, zanim cokolwiek innego dotknie
/// wejścia. Pusty manifest jest legalny i znaczy „mecz bez ludzi" — same boty.
std::expected<Roster, std::string> build_roster(const Options& options)
{
    const std::expected<std::string, std::string> manifest = read_manifest(options.manifest_path);

    if (!manifest)
    {
        return std::unexpected(manifest.error());
    }

    const std::expected<std::vector<ManifestPlayer>, std::string> players =
        parse_manifest(*manifest, options.max_actors);

    if (!players)
    {
        return std::unexpected(players.error());
    }

    return Roster::build(*players, options.max_actors, options.seed, options.fill_bots);
}

/// Stawia każdego aktora na punkcie startowym o numerze jego slotu (§3.6 planu).
std::expected<void, std::string> place_actors(World& world, const Roster& roster)
{
    for (const Actor& actor : roster.actors())
    {
        if (!world.place_actor(actor.slot))
        {
            return std::unexpected(std::format(
                "Nie udało się postawić aktora na slocie {}: punkt startowy {} jest zajęty "
                "albo pokrywa się z innym.",
                actor.slot,
                actor.slot));
        }
    }

    return {};
}

/// Opis meczu dla `MatchInit` — to, co klient musi wiedzieć, zanim zobaczy pierwszy kafelek.
MatchDescription describe_match(const Options& options, const MapFile& map, const World& world)
{
    MatchDescription description;

    description.map_id = std::string{map.map().id};
    std::ranges::copy(map.sha256(), description.map_sha256.begin());
    description.map_width = world.width();
    description.map_height = world.height();
    description.tick_rate = static_cast<std::uint32_t>(1000 / TickRates{}.sim_period.count());
    description.seed = options.seed;

    return description;
}

} // namespace

std::expected<MatchSetup, std::string> MatchSetup::open(const Options& options)
{
    if (const std::expected<void, std::string> required = check_required(options); !required)
    {
        return std::unexpected(required.error());
    }

    std::expected<MapFile, std::string> map = MapFile::open(options.map_path);

    if (!map)
    {
        return std::unexpected(map.error());
    }

    World world(map->map());

    if (world.spawn_count() < options.max_actors)
    {
        return std::unexpected(std::format(
            "Mapa '{}' ma {} punktów startowych, a mecz przewiduje {} aktorów. Część z nich nie "
            "miałaby gdzie stanąć.",
            map->map().id,
            world.spawn_count(),
            options.max_actors));
    }

    std::expected<Roster, std::string> roster = build_roster(options);

    if (!roster)
    {
        return std::unexpected(roster.error());
    }

    if (const std::expected<void, std::string> placed = place_actors(world, *roster); !placed)
    {
        return std::unexpected(placed.error());
    }

    // Keyframe budowany **po** postawieniu aktorów: to zdjęcie świata, które dostaje każdy
    // wchodzący, więc musi już widzieć terytoria startowe.
    MatchIntro intro(describe_match(options, *map, world), roster->actors(), world.owner());

    std::expected<TicketVerifier, std::string> tickets =
        TicketVerifier::from_pem_file(options.ticket_key_path, options.match_id, options.max_actors);

    if (!tickets)
    {
        return std::unexpected(tickets.error());
    }

    return MatchSetup{
        std::move(*map),
        std::move(world),
        std::move(*roster),
        std::move(intro),
        std::move(*tickets)};
}

void log_match_summary(const Options& options, const MatchSetup& setup)
{
    log::info(
        "Mecz {} — port {}, ziarno {}, aktorów {} ({} ludzi, {} botów).",
        options.match_id,
        options.port,
        options.seed,
        options.max_actors,
        setup.roster.humans(),
        setup.roster.bots());

    if (setup.roster.humans() == 0)
    {
        log::warn("Manifest nie przyniósł ani jednego gracza.");
    }

    if (!options.fill_bots)
    {
        log::info("Boty wyłączone (--bots 0) — grają wyłącznie ludzie z manifestu.");
    }

    const World& world = setup.world;

    log::info(
        "Mapa '{}' {}×{}: {:.1f}% lądu, {} punktów startowych, sha256 {}.",
        setup.map.map().id,
        world.width(),
        world.height(),
        100.0 * static_cast<double>(world.land_tiles()) / static_cast<double>(world.owner().size()),
        world.spawn_count(),
        setup.map.sha256_hex());

    log::info(
        "Keyframe: {} runów, {} B na drucie.",
        setup.intro.run_count(),
        setup.intro.keyframe_bytes());
}

} // namespace gs
