#include "meta/manifest.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

// Manifest to jedyne miejsce, w którym dane graczy wchodzą do procesu meczu. Nicki idą stąd
// wprost do `MatchInit` rozsyłanego wszystkim, a slot decyduje o punkcie startowym — więc
// proces nie ma powodu wierzyć meta na słowo, nawet jeśli to meta ten plik napisała.
namespace
{

constexpr std::uint32_t max_actors = 8;

std::expected<std::vector<gs::ManifestPlayer>, std::string> parse(std::string_view json)
{
    return gs::parse_manifest(json, max_actors);
}

} // namespace

TEST(ManifestTest, ReadsPlayersInTheOrderTheyCame)
{
    const auto players = parse(
        R"({"players":[{"slot":3,"name":"Ala","colorRgb":16711680},)"
        R"({"slot":1,"name":"Bob","colorRgb":255}]})");

    ASSERT_TRUE(players.has_value()) << (players ? "" : players.error());
    ASSERT_EQ(players->size(), 2u);

    EXPECT_EQ((*players)[0].slot, 3);
    EXPECT_EQ((*players)[0].name, "Ala");
    EXPECT_EQ((*players)[0].color_rgb, 16711680u);
    EXPECT_EQ((*players)[1].name, "Bob");
}

// Pusty manifest jest legalnym stanem, a nie awarią: tak wygląda przebieg ręczny i tak
// wyglądałby mecz, z którego wszyscy wyszli przed startem. Boty i tak wypełnią sloty.
TEST(ManifestTest, TreatsAnEmptyInputAsAMatchWithoutHumans)
{
    const auto empty = parse("");
    const auto whitespace = parse("  \n\t ");

    ASSERT_TRUE(empty.has_value());
    ASSERT_TRUE(whitespace.has_value());

    EXPECT_TRUE(empty->empty());
    EXPECT_TRUE(whitespace->empty());
}

TEST(ManifestTest, AcceptsAnEmptyPlayerList)
{
    const auto players = parse(R"({"players":[]})");

    ASSERT_TRUE(players.has_value()) << (players ? "" : players.error());
    EXPECT_TRUE(players->empty());
}

// Dwóch graczy na jednym slocie to dwóch graczy na jednym punkcie startowym i bilet, który
// wpuszcza obu. Nikt dalej po drodze tego nie zauważy.
TEST(ManifestTest, RefusesTheSameSlotTwice)
{
    EXPECT_FALSE(parse(R"({"players":[{"slot":2,"name":"A","colorRgb":1},)"
                       R"({"slot":2,"name":"B","colorRgb":2}]})")
                     .has_value());
}

TEST(ManifestTest, RefusesReservedAndOutOfRangeSlots)
{
    EXPECT_FALSE(parse(R"({"players":[{"slot":0,"name":"A","colorRgb":1}]})").has_value());
    EXPECT_FALSE(parse(R"({"players":[{"slot":9,"name":"A","colorRgb":1}]})").has_value());
    EXPECT_FALSE(parse(R"({"players":[{"slot":255,"name":"A","colorRgb":1}]})").has_value());
}

TEST(ManifestTest, RefusesAnEmptyOrOverlongNick)
{
    EXPECT_FALSE(parse(R"({"players":[{"slot":1,"name":"","colorRgb":1}]})").has_value());

    const std::string long_name(81, 'x');

    EXPECT_FALSE(
        parse(R"({"players":[{"slot":1,"name":")" + long_name + R"(","colorRgb":1}]})")
            .has_value());
}

/// PowerShell dokleja BOM przy każdym przekierowaniu na wejście procesu, a bez zdjęcia go
/// parser wywala się na pierwszym bajcie i mówi „to nie jest poprawny JSON" o treści, która
/// jest bez zarzutu. Kosztowało to raz godzinę szukania nie tam, gdzie trzeba.
TEST(ManifestTest, IgnoresAByteOrderMark)
{
    const auto players =
        parse("\xEF\xBB\xBF" R"({"players":[{"slot":1,"name":"Ala","colorRgb":1}]})");

    ASSERT_TRUE(players.has_value()) << players.error();

    EXPECT_EQ(players->size(), 1u);
    EXPECT_EQ((*players)[0].name, "Ala");
}

/// Sufit nicku liczy bajty, a meta liczy znaki — dwadzieścia znaków z polskimi ogonkami waży
/// czterdzieści bajtów i musi przejść. Przy ciaśniejszym suficie mecz nie wstawałby dla
/// gracza, którego meta uznaje za całkowicie poprawnego.
TEST(ManifestTest, AcceptsTheLongestNickMetaAllows)
{
    // Dwadzieścia znaków po dwa bajty każdy — limit meta w najgorszym przypadku dla alfabetu
    // polskiego. Escapowanie jest tu prawdziwe, nie wygodne: meta wypisuje manifest z każdym
    // znakiem spoza ASCII jako \uXXXX, więc tą drogą nick faktycznie przychodzi.
    std::string nick;

    for (int index = 0; index < 20; ++index)
    {
        // Escape JSON-a, nie znak w źródle: plik zostaje czystym ASCII, a parser dostaje
        // dokładnie te bajty, które wypisuje meta.
        nick += "\\u017C";
    }

    const auto players = parse(R"({"players":[{"slot":1,"name":")" + nick + R"(","colorRgb":1}]})");

    ASSERT_TRUE(players.has_value()) << players.error();

    EXPECT_EQ((*players)[0].name.size(), 40u);
}

TEST(ManifestTest, RefusesAColourOutsideRgb)
{
    EXPECT_FALSE(parse(R"({"players":[{"slot":1,"name":"A","colorRgb":16777216}]})").has_value());
    EXPECT_FALSE(parse(R"({"players":[{"slot":1,"name":"A","colorRgb":-1}]})").has_value());
}

TEST(ManifestTest, RefusesMorePlayersThanTheMatchHolds)
{
    std::string json = R"({"players":[)";

    for (std::uint32_t slot = 1; slot <= max_actors + 1; ++slot)
    {
        json += (slot == 1 ? "" : ",");
        json += R"({"slot":)" + std::to_string(slot) + R"(,"name":"A","colorRgb":1})";
    }

    json += "]}";

    EXPECT_FALSE(parse(json).has_value());
}

TEST(ManifestTest, RefusesSomethingThatIsNotAManifest)
{
    EXPECT_FALSE(parse("{").has_value());
    EXPECT_FALSE(parse(R"({"gracze":[]})").has_value());
    EXPECT_FALSE(parse(R"({"players":{}})").has_value());
    EXPECT_FALSE(parse(R"({"players":[{"slot":"1","name":"A","colorRgb":1}]})").has_value());
    EXPECT_FALSE(parse(R"({"players":[{"slot":1,"colorRgb":1}]})").has_value());
}
