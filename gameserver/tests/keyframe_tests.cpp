#include "state/keyframe.hpp"

#include "map/tmap.hpp"

#include <game.pb.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Keyframe RLE. Cała rzecz sprowadza się do jednej reguły, którą łatwo napisać źle:
// pustkowia **nie jadą**, a `start_delta` mierzy przerwę od końca poprzedniego runu.
namespace
{

using Owner = std::vector<std::uint8_t>;

constexpr std::uint8_t water = gs::tmap::water_owner;

game::Snapshot keyframe_of(const Owner& owner)
{
    game::Snapshot snapshot;

    gs::build_keyframe(owner, snapshot);

    return snapshot;
}

} // namespace

TEST(KeyframeTest, MarksItselfAsAKeyframe)
{
    EXPECT_TRUE(keyframe_of(Owner{water}).is_keyframe());
}

TEST(KeyframeTest, SkipsWastelandAndMeasuresTheGapToTheNextRun)
{
    // w w w w w _ _ w w w w w   — pustkowie na dwóch kafelkach w środku
    const Owner owner{water, water, water, water, water, 0, 0, water, water, water, water, water};

    const game::Snapshot snapshot = keyframe_of(owner);

    ASSERT_EQ(snapshot.runs_size(), 2);

    EXPECT_EQ(snapshot.runs(0).start_delta(), 0u);
    EXPECT_EQ(snapshot.runs(0).length(), 5u);
    EXPECT_EQ(snapshot.runs(0).slot(), water);

    // Poprzedni run skończył się na indeksie 5, następny zaczyna się na 7 — stąd dwójka.
    EXPECT_EQ(snapshot.runs(1).start_delta(), 2u);
    EXPECT_EQ(snapshot.runs(1).length(), 5u);
    EXPECT_EQ(snapshot.runs(1).slot(), water);
}

TEST(KeyframeTest, BreaksARunWhereTheOwnerChanges)
{
    const Owner owner{1, 1, 2, 2, 2};

    const game::Snapshot snapshot = keyframe_of(owner);

    ASSERT_EQ(snapshot.runs_size(), 2);

    EXPECT_EQ(snapshot.runs(0).slot(), 1u);
    EXPECT_EQ(snapshot.runs(0).length(), 2u);

    EXPECT_EQ(snapshot.runs(1).start_delta(), 0u);
    EXPECT_EQ(snapshot.runs(1).slot(), 2u);
    EXPECT_EQ(snapshot.runs(1).length(), 3u);
}

// Pierwsza przerwa liczy się od początku tablicy, więc `start_delta` pierwszego runu jest
// po prostu jego indeksem.
TEST(KeyframeTest, CountsTheFirstGapFromTheStartOfTheArray)
{
    const Owner owner{0, 0, 0, 7};

    const game::Snapshot snapshot = keyframe_of(owner);

    ASSERT_EQ(snapshot.runs_size(), 1);

    EXPECT_EQ(snapshot.runs(0).start_delta(), 3u);
    EXPECT_EQ(snapshot.runs(0).length(), 1u);
    EXPECT_EQ(snapshot.runs(0).slot(), 7u);
}

TEST(KeyframeTest, SendsNothingForAMapThatIsAllWasteland)
{
    EXPECT_EQ(keyframe_of(Owner(64, 0)).runs_size(), 0);
}

// Klient odbudowuje `owner[]` zerując tablicę i nakładając runy. Ten test jest tą odbudową
// wykonaną wprost — jeśli kiedykolwiek przestanie przechodzić, to znaczy, że keyframe
// przestał opisywać stan w całości.
TEST(KeyframeTest, DescribesTheWholeArray)
{
    const Owner owner{water, 0, 3, 3, 0, 0, water, 1};

    const game::Snapshot snapshot = keyframe_of(owner);

    Owner rebuilt(owner.size(), 0);

    std::size_t cursor = 0;

    for (const game::OwnershipRun& run : snapshot.runs())
    {
        cursor += run.start_delta();

        for (std::uint32_t offset = 0; offset < run.length(); ++offset)
        {
            rebuilt[cursor + offset] = static_cast<std::uint8_t>(run.slot());
        }

        cursor += run.length();
    }

    EXPECT_EQ(rebuilt, owner);
}
