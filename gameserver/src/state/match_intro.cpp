#include "state/match_intro.hpp"

#include "state/keyframe.hpp"

#include <utility>

namespace gs
{

MatchIntro::MatchIntro(
    MatchDescription description,
    std::span<const Actor> actors,
    std::span<const std::uint8_t> owner)
    : description_(std::move(description))
{
    init_.set_map_id(description_.map_id);
    init_.set_map_sha256(description_.map_sha256.data(), description_.map_sha256.size());
    init_.set_tick_rate(description_.tick_rate);
    init_.set_seed(static_cast<std::uint64_t>(description_.seed));
    init_.set_map_width(description_.map_width);
    init_.set_map_height(description_.map_height);

    for (const Actor& actor : actors)
    {
        game::SlotInfo* slot = init_.add_slots();

        slot->set_slot(actor.slot);
        slot->set_name(actor.name);
        slot->set_color_rgb(actor.color_rgb);
        slot->set_is_bot(actor.is_bot);
    }

    build_keyframe(owner, keyframe_);
}

std::shared_ptr<const std::string> MatchIntro::init_for(std::uint8_t slot) const
{
    game::ServerMsg message;

    // Kopia gotowej wiadomości plus jedno pole. Przy stu aktorach to kilka kilobajtów raz na
    // wchodzącego gracza — tańsze niż trzymanie osobnej wersji dla każdego slotu.
    *message.mutable_init() = init_;
    message.mutable_init()->set_your_slot(slot);

    return std::make_shared<const std::string>(message.SerializeAsString());
}

std::shared_ptr<const std::string> MatchIntro::keyframe_at(std::uint32_t tick) const
{
    game::ServerMsg message;

    *message.mutable_snapshot() = keyframe_;
    message.mutable_snapshot()->set_tick(tick);

    return std::make_shared<const std::string>(message.SerializeAsString());
}

std::size_t MatchIntro::keyframe_bytes() const
{
    game::ServerMsg message;

    *message.mutable_snapshot() = keyframe_;

    return message.ByteSizeLong();
}

} // namespace gs
