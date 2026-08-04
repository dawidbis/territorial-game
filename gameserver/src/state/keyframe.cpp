#include "state/keyframe.hpp"

#include "map/tmap.hpp"

#include <game.pb.h>

namespace gs
{

void build_keyframe(std::span<const std::uint8_t> owner, game::Snapshot& snapshot)
{
    snapshot.set_is_keyframe(true);

    // Koniec ostatniego wypisanego runu, liczony wykluczająco. Od niego liczy się przerwę
    // do następnego — stąd pierwszy `start_delta` jest po prostu indeksem początku.
    std::size_t previous_end = 0;

    std::size_t index = 0;

    while (index < owner.size())
    {
        const std::uint8_t slot = owner[index];

        std::size_t end = index + 1;

        while (end < owner.size() && owner[end] == slot)
        {
            ++end;
        }

        if (slot != tmap::wasteland_owner)
        {
            game::OwnershipRun* run = snapshot.add_runs();

            run->set_start_delta(static_cast<std::uint32_t>(index - previous_end));
            run->set_length(static_cast<std::uint32_t>(end - index));
            run->set_slot(slot);

            previous_end = end;
        }

        index = end;
    }
}

} // namespace gs
