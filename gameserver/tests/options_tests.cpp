#include "app/options.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr std::string_view valid_match_id = "018f3a2b-5c7d-7e91-9a2b-3c4d5e6f7a8b";

std::expected<gs::Options, std::string> parse(std::initializer_list<std::string_view> args)
{
    const std::vector<std::string_view> arguments{args};

    return gs::parse_options(arguments);
}

} // namespace

TEST(OptionsTest, ReadsFullCommandLine)
{
    const auto options = parse({
        "--match-id",
        valid_match_id,
        "--port",
        "5101",
        "--map",
        "maps/moon.tmap",
        "--seed",
        "-42",
        "--max-actors",
        "100",
        "--ticket-key",
        "keys/ticket.pub",
        "--manifest",
        "-",
        "--max-ticks",
        "10",
    });

    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->match_id, valid_match_id);
    EXPECT_EQ(options->port, 5101);
    EXPECT_EQ(options->map_path, "maps/moon.tmap");
    EXPECT_EQ(options->seed, -42);
    EXPECT_EQ(options->max_actors, 100u);
    EXPECT_EQ(options->ticket_key_path, "keys/ticket.pub");
    EXPECT_EQ(options->manifest_path, "-");
    EXPECT_EQ(options->max_ticks, 10u);
}

TEST(OptionsTest, DefaultsManifestToStandardInput)
{
    const auto options = parse({"--match-id", valid_match_id, "--port", "5101"});

    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->manifest_path, "-");
    EXPECT_EQ(options->max_ticks, 0u);
}

// Identyfikator meczu jedzie prosto do porównania ze ścieżką URL-a, a ta może przyjść
// z dowolną wielkością liter.
TEST(OptionsTest, NormalizesMatchIdToLowercase)
{
    const auto options =
        parse({"--match-id", "018F3A2B-5C7D-7E91-9A2B-3C4D5E6F7A8B", "--port", "5101"});

    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->match_id, valid_match_id);
}

TEST(OptionsTest, RejectsMatchIdThatIsNotGuid)
{
    const auto options = parse({"--match-id", "moon", "--port", "5101"});

    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("--match-id"), std::string::npos);
}

TEST(OptionsTest, RequiresMatchId)
{
    const auto options = parse({"--port", "5101"});

    ASSERT_FALSE(options.has_value());
}

TEST(OptionsTest, RequiresPort)
{
    const auto options = parse({"--match-id", valid_match_id});

    ASSERT_FALSE(options.has_value());
}

TEST(OptionsTest, RejectsPortOutsideRange)
{
    EXPECT_FALSE(parse({"--match-id", valid_match_id, "--port", "0"}).has_value());
    EXPECT_FALSE(parse({"--match-id", valid_match_id, "--port", "65536"}).has_value());
}

// Sufit z D12: 254 to ostatni slot aktora, 255 jest zarezerwowane dla wody.
TEST(OptionsTest, AcceptsMaximumActorsAndRejectsOneMore)
{
    EXPECT_TRUE(
        parse({"--match-id", valid_match_id, "--port", "5101", "--max-actors", "254"}).has_value());
    EXPECT_FALSE(
        parse({"--match-id", valid_match_id, "--port", "5101", "--max-actors", "255"}).has_value());
    EXPECT_FALSE(
        parse({"--match-id", valid_match_id, "--port", "5101", "--max-actors", "0"}).has_value());
}

TEST(OptionsTest, RejectsNonNumericValues)
{
    const auto options = parse({"--match-id", valid_match_id, "--port", "5101x"});

    ASSERT_FALSE(options.has_value());
}

// Literówka w wywołaniu z orkiestratora ma zatrzymać proces, a nie uruchomić mecz
// z domyślną wartością, o której nikt nie wie.
TEST(OptionsTest, RejectsUnknownOption)
{
    const auto options = parse({"--match-id", valid_match_id, "--port", "5101", "--turbo"});

    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("--turbo"), std::string::npos);
}

TEST(OptionsTest, RejectsOptionWithoutValue)
{
    const auto options = parse({"--match-id", valid_match_id, "--port"});

    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("--port"), std::string::npos);
}

TEST(OptionsTest, HelpDoesNotRequireOtherOptions)
{
    const auto options = parse({"--help"});

    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_TRUE(options->help);
}
