#include "meta/ticket.hpp"
#include "ticket_vectors.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

// Testy kontraktu z meta. Bilety poniżej **nie są wymyślone** — wystawił je .NET tymi samymi
// prymitywami, których używa MatchTicketService (ECDsa.SignData, czyli IEEE P-1363).
// To jedyne miejsce w tym repozytorium, gdzie obie strony kontraktu spotykają się w teście;
// wszystko inne sprawdza każdą z nich osobno.
//
// Odtworzenie wektorów: scratchpad/gen-tickets.cs
namespace
{
using namespace vectors;


/// Chwila trzydzieści sekund przed wygaśnięciem ważnego biletu.
constexpr std::chrono::system_clock::time_point in_time{std::chrono::seconds{valid_until - 30}};

gs::TicketVerifier make_verifier(std::uint32_t max_actors = 100)
{
    auto verifier = gs::TicketVerifier::from_pem(public_key_pem, std::string{match_id}, max_actors);

    EXPECT_TRUE(verifier.has_value()) << (verifier ? "" : verifier.error());

    return std::move(*verifier);
}

} // namespace

TEST(TicketTest, AcceptsATicketIssuedByMeta)
{
    auto verifier = make_verifier();

    const auto ticket = verifier.verify(valid_token, in_time);

    ASSERT_TRUE(ticket.has_value());
    EXPECT_EQ(ticket->player_id, player_id);
    EXPECT_EQ(ticket->match_id, match_id);
    EXPECT_EQ(ticket->slot, 7);
    EXPECT_EQ(ticket->nonce, "nonce-one");
    EXPECT_EQ(verifier.admitted(), 1u);
}

// Podpis wystawiony innym kluczem P-256 — czyli ktoś, kto zna format biletu, ale nie ma
// klucza meta. Cała asymetria istnieje wyłącznie po to, żeby ten test przechodził.
TEST(TicketTest, RejectsATicketSignedWithAnotherKey)
{
    auto verifier = make_verifier();

    const auto ticket = verifier.verify(foreign_key_token, in_time);

    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), gs::TicketError::bad_signature);
}

TEST(TicketTest, RejectsATicketWithOneCharacterOfThePayloadChanged)
{
    auto verifier = make_verifier();

    std::string tampered{valid_token};

    // "slot":7 → "slot":8 nie da się zrobić bez ruszania base64, więc podmieniamy jeden
    // znak ładunku. Efekt jest ten sam: podpis przestaje pasować.
    const std::size_t middle = tampered.find('.') + 10;
    tampered[middle] = tampered[middle] == 'A' ? 'B' : 'A';

    const auto ticket = verifier.verify(tampered, in_time);

    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), gs::TicketError::bad_signature);
}

TEST(TicketTest, RejectsAnExpiredTicket)
{
    auto verifier = make_verifier();

    const auto ticket = verifier.verify(expired_token, in_time);

    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), gs::TicketError::expired);
}

/// Zapas na rozjazd zegarów działa w obie strony: bilet świeżo po terminie jeszcze wchodzi,
/// ale sekundę za zapasem już nie.
TEST(TicketTest, AllowsTheConfiguredClockSkewAndNotAMomentMore)
{
    auto verifier = make_verifier();

    const std::chrono::system_clock::time_point expiry{std::chrono::seconds{valid_until}};

    EXPECT_TRUE(verifier.verify(valid_token, expiry + gs::TicketVerifier::clock_skew).has_value());

    const auto late = verifier.verify(
        second_token,
        expiry + gs::TicketVerifier::clock_skew + std::chrono::seconds{1});

    ASSERT_FALSE(late.has_value());
    EXPECT_EQ(late.error(), gs::TicketError::expired);
}

// Proces obsługuje jeden mecz (D7), więc bilet do innego meczu jest tak samo obcy jak
// podrobiony — nawet jeśli podpis się zgadza.
TEST(TicketTest, RejectsATicketForAnotherMatch)
{
    auto verifier = make_verifier();

    const auto ticket = verifier.verify(other_match_token, in_time);

    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), gs::TicketError::wrong_match);
}

// Slot 0 to pustkowie, a 255 woda (D12) — żaden z nich nie jest graczem. Slot powyżej
// sufitu meczu wyszedłby poza tablicę aktorów.
TEST(TicketTest, RejectsSlotsOutsideTheMatch)
{
    auto verifier = make_verifier(100);

    const auto zero = verifier.verify(slot_zero_token, in_time);
    const auto high = verifier.verify(slot_too_high_token, in_time);

    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error(), gs::TicketError::bad_slot);
    ASSERT_FALSE(high.has_value());
    EXPECT_EQ(high.error(), gs::TicketError::bad_slot);
}

TEST(TicketTest, AcceptsASlotThatFitsALargerMatch)
{
    auto verifier = make_verifier(254);

    EXPECT_TRUE(verifier.verify(slot_too_high_token, in_time).has_value());
}

// Jednorazowość biletu bierze się stąd, że proces pamięta zużyte nonce (D7) — meta nie
// musi o tym nic wiedzieć.
TEST(TicketTest, RejectsTheSameTicketUsedTwice)
{
    auto verifier = make_verifier();

    ASSERT_TRUE(verifier.verify(valid_token, in_time).has_value());

    const auto again = verifier.verify(valid_token, in_time);

    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), gs::TicketError::replayed);
}

TEST(TicketTest, AcceptsAFreshTicketAfterAnEarlierOne)
{
    auto verifier = make_verifier();

    ASSERT_TRUE(verifier.verify(valid_token, in_time).has_value());
    ASSERT_TRUE(verifier.verify(second_token, in_time).has_value());
    EXPECT_EQ(verifier.admitted(), 2u);
}

TEST(TicketTest, RejectsAnythingThatIsNotAThreePartToken)
{
    auto verifier = make_verifier();

    for (const std::string_view nonsense : {"", "abc", "a.b", "a.b.c.d", "....", "a..c"})
    {
        const auto ticket = verifier.verify(nonsense, in_time);

        ASSERT_FALSE(ticket.has_value()) << nonsense;
        EXPECT_EQ(ticket.error(), gs::TicketError::malformed) << nonsense;
    }
}

/// Nagłówek bez `alg` to klasyczna próba obejścia podpisu — odrzucana zanim cokolwiek
/// zostanie policzone.
TEST(TicketTest, RejectsATokenWithoutTheExpectedAlgorithm)
{
    auto verifier = make_verifier();

    // "e30" to zakodowane base64url `{}`.
    const auto ticket = verifier.verify("e30.e30.AAAA", in_time);

    ASSERT_FALSE(ticket.has_value());
    EXPECT_EQ(ticket.error(), gs::TicketError::unsupported_algorithm);
}

TEST(TicketTest, RefusesToStartWithAKeyItCannotRead)
{
    const auto verifier =
        gs::TicketVerifier::from_pem("to nie jest klucz", std::string{match_id}, 100);

    ASSERT_FALSE(verifier.has_value());
    EXPECT_FALSE(verifier.error().empty());
}
