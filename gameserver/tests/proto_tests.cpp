#include <game.pb.h>

#include <gtest/gtest.h>

#include <string>

// Testy kontraktu, nie logiki: dowodzą, że codegen z `proto/game.proto` faktycznie
// działa i że schemat da się zserializować w obie strony. W etapie E1 to jedyny sposób,
// żeby zauważyć zepsuty schemat — nie ma jeszcze nikogo, kto by go używał.

TEST(ProtoTest, MatchInitSurvivesRoundTrip)
{
    game::ServerMsg message;

    game::MatchInit* init = message.mutable_init();
    init->set_map_id("moon");
    init->set_tick_rate(10);
    init->set_your_slot(7);
    init->set_seed(1234567890123ULL);
    init->set_map_width(2000);
    init->set_map_height(1000);

    game::SlotInfo* slot = init->add_slots();
    slot->set_slot(7);
    slot->set_name("gracz");
    slot->set_color_rgb(0x33AAFF);
    slot->set_is_bot(false);

    std::string bytes;
    ASSERT_TRUE(message.SerializeToString(&bytes));

    game::ServerMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));

    ASSERT_EQ(parsed.msg_case(), game::ServerMsg::kInit);
    EXPECT_EQ(parsed.init().map_id(), "moon");
    EXPECT_EQ(parsed.init().your_slot(), 7u);
    EXPECT_EQ(parsed.init().seed(), 1234567890123ULL);
    ASSERT_EQ(parsed.init().slots_size(), 1);
    EXPECT_EQ(parsed.init().slots(0).name(), "gracz");
    EXPECT_EQ(parsed.init().slots(0).color_rgb(), 0x33AAFFu);
}

// D5 w jednym asercie: właściciel jest płacony raz na grupę, a sąsiednie kafelki kosztują
// po jednym bajcie varinta. Naiwne pary (indeks, właściciel) dałyby tu 4 B na kafelek.
TEST(ProtoTest, TileDeltaGroupCostsAboutOneBytePerTile)
{
    constexpr int tiles = 200;

    game::ServerMsg message;

    game::Snapshot* snapshot = message.mutable_snapshot();
    snapshot->set_tick(42);

    game::TileDeltaGroup* group = snapshot->add_deltas();
    group->set_slot(3);

    for (int index = 0; index < tiles; ++index)
    {
        group->add_index_deltas(1);
    }

    std::string bytes;
    ASSERT_TRUE(message.SerializeToString(&bytes));

    EXPECT_LT(bytes.size(), static_cast<std::size_t>(tiles) * 3 / 2);

    game::ServerMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));

    ASSERT_EQ(parsed.snapshot().deltas_size(), 1);
    EXPECT_EQ(parsed.snapshot().deltas(0).slot(), 3u);
    EXPECT_EQ(parsed.snapshot().deltas(0).index_deltas_size(), tiles);
}

TEST(ProtoTest, ClientHelloCarriesTicket)
{
    game::ClientMsg message;
    message.mutable_hello()->set_ticket("eyJhbGciOiJFUzI1NiJ9.payload.signature");

    std::string bytes;
    ASSERT_TRUE(message.SerializeToString(&bytes));

    game::ClientMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));

    ASSERT_EQ(parsed.msg_case(), game::ClientMsg::kHello);
    EXPECT_EQ(parsed.hello().ticket(), "eyJhbGciOiJFUzI1NiJ9.payload.signature");
}

// `MyState` jest per gracz, więc musi dać się wysłać osobno od wspólnego snapshotu.
// W §6 dokumentu architektury brakowało jej w `oneof` — bez tego nie miałaby jak wyjść.
TEST(ProtoTest, MyStateIsItsOwnServerMessage)
{
    game::ServerMsg message;
    message.mutable_my_state()->set_population(1234);
    message.mutable_my_state()->set_gold(56);

    std::string bytes;
    ASSERT_TRUE(message.SerializeToString(&bytes));

    game::ServerMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));

    ASSERT_EQ(parsed.msg_case(), game::ServerMsg::kMyState);
    EXPECT_EQ(parsed.my_state().population(), 1234u);

    // Kilkanaście bajtów obok snapshotu — cena, jaką płacimy za stan prywatny gracza.
    EXPECT_LT(bytes.size(), 20u);
}
