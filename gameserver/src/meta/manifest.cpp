#include "meta/manifest.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <sstream>

namespace gs
{
namespace
{

/// Sufit długości nicku **w bajtach**. Meta pilnuje własnych reguł, ale proces nie ma powodu
/// jej wierzyć: nick idzie do `MatchInit` rozsyłanego wszystkim, więc kilobajtowy trafiłby
/// do każdego gracza w meczu.
///
/// Osiemdziesiąt, a nie okrągłe trzydzieści dwa, bo meta liczy **znaki**, nie bajty: jej
/// limit to 20 znaków, a jeden znak zajmuje w UTF-8 do czterech bajtów. Ciaśniejszy sufit
/// odrzucałby nicki, które meta uznaje za poprawne — a odrzucony manifest to mecz, który
/// nie wstaje. Wychodzi to dopiero na pierwszym graczu z polskimi znakami w nicku, czyli
/// w najgorszym możliwym momencie.
constexpr std::size_t max_name_length = 80;

constexpr std::uint32_t max_color = 0xFFFFFF;

} // namespace

std::expected<std::vector<ManifestPlayer>, std::string> parse_manifest(
    std::string_view json,
    std::uint32_t max_actors)
{
    std::vector<ManifestPlayer> players;

    // Znacznik kolejności bajtów zdejmowany, zanim cokolwiek spojrzy na treść. To jedyne
    // miejsce, w którym przyjmujemy coś, czego nie rozumiemy — i ma uzasadnienie: BOM nie
    // niesie ŻADNEJ informacji w strumieniu, o którym i tak wiadomo, że jest UTF-8, a dokleja
    // go domyślnie każde przekierowanie z PowerShella. Odrzucony daje „manifest nie jest
    // poprawnym JSON-em", czyli komunikat wskazujący na treść, która jest bez zarzutu.
    constexpr std::string_view byte_order_mark = "\xEF\xBB\xBF";

    if (json.starts_with(byte_order_mark))
    {
        json.remove_prefix(byte_order_mark.size());
    }

    // Pusty manifest to mecz bez ludzi — same boty. Legalny stan: tak wygląda przebieg
    // ręczny i tak wyglądałby mecz, z którego wszyscy wyszli przed startem.
    if (json.find_first_not_of(" \t\r\n") == std::string_view::npos)
    {
        return players;
    }

    boost::system::error_code error;

    const boost::json::value parsed = boost::json::parse(json, error);

    if (error)
    {
        return std::unexpected(std::format("Manifest nie jest poprawnym JSON-em: {}.", error.message()));
    }

    if (!parsed.is_object() || !parsed.get_object().contains("players"))
    {
        return std::unexpected(std::string{"Manifest musi być obiektem z polem 'players'."});
    }

    const boost::json::value& list = parsed.get_object().at("players");

    if (!list.is_array())
    {
        return std::unexpected(std::string{"Pole 'players' musi być tablicą."});
    }

    const boost::json::array& entries = list.get_array();

    if (entries.size() > max_actors)
    {
        return std::unexpected(std::format(
            "Manifest niesie {} graczy, a mecz mieści {} aktorów.",
            entries.size(),
            max_actors));
    }

    players.reserve(entries.size());

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        if (!entries[index].is_object())
        {
            return std::unexpected(std::format("Gracz {} nie jest obiektem.", index));
        }

        const boost::json::object& entry = entries[index].get_object();

        for (const std::string_view field : {"slot", "name", "colorRgb"})
        {
            if (!entry.contains(field))
            {
                return std::unexpected(
                    std::format("Gracz {}: brakuje pola '{}'.", index, field));
            }
        }

        if (!entry.at("slot").is_int64() || !entry.at("colorRgb").is_int64()
            || !entry.at("name").is_string())
        {
            return std::unexpected(std::format(
                "Gracz {}: 'slot' i 'colorRgb' mają być liczbami, a 'name' tekstem.",
                index));
        }

        const std::int64_t slot = entry.at("slot").get_int64();

        // Slot zero to pustkowie, 255 to woda (D12) — jedno i drugie w rosterze znaczyłoby
        // aktora, którego kafelki są nie do odróżnienia od terenu.
        if (slot < 1 || slot > static_cast<std::int64_t>(max_actors))
        {
            return std::unexpected(std::format(
                "Gracz {}: slot {} jest poza zakresem 1..{}.",
                index,
                slot,
                max_actors));
        }

        const std::int64_t color = entry.at("colorRgb").get_int64();

        if (color < 0 || color > max_color)
        {
            return std::unexpected(std::format(
                "Gracz {}: kolor {} jest poza zakresem 0..{}.",
                index,
                color,
                max_color));
        }

        ManifestPlayer player;
        player.slot = static_cast<std::uint8_t>(slot);
        player.name = entry.at("name").get_string().c_str();
        player.color_rgb = static_cast<std::uint32_t>(color);

        if (player.name.empty() || player.name.size() > max_name_length)
        {
            return std::unexpected(std::format(
                "Gracz {}: nick ma {} znaków, a mieści się w 1..{}.",
                index,
                player.name.size(),
                max_name_length));
        }

        // Dwa razy ten sam slot to dwóch graczy na jednym punkcie startowym i bilet, który
        // wpuszcza obu. Wykrywalne tylko tutaj — dalej po drodze nikt już tego nie widzi.
        if (std::ranges::any_of(
                players,
                [&player](const ManifestPlayer& earlier) { return earlier.slot == player.slot; }))
        {
            return std::unexpected(
                std::format("Slot {} występuje w manifeście dwa razy.", player.slot));
        }

        players.push_back(std::move(player));
    }

    return players;
}

std::expected<std::string, std::string> read_manifest(std::string_view source)
{
    std::ostringstream content;

    if (source == "-")
    {
        content << std::cin.rdbuf();

        // Sam brak danych na wejściu nie jest błędem — pusty manifest to mecz bez ludzi.
        // Błędem jest strumień, który padł w trakcie czytania.
        if (std::cin.bad())
        {
            return std::unexpected(std::string{"Nie udało się przeczytać manifestu ze stdin."});
        }

        return content.str();
    }

    const std::ifstream file{std::string{source}, std::ios::binary};

    if (!file)
    {
        return std::unexpected(std::format("Nie udało się otworzyć manifestu '{}'.", source));
    }

    content << file.rdbuf();

    return content.str();
}

} // namespace gs
