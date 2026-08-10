#include "RhythmPlan.h"

#include <array>
#include <utility>

namespace pulso {
namespace {
template <typename Enum, std::size_t Size>
std::optional<Enum> fromKey(std::string_view key,
                            const std::array<std::pair<std::string_view, Enum>, Size>& values) noexcept {
    for (const auto& [name, value] : values)
        if (name == key) return value;
    return std::nullopt;
}
} // namespace

std::string_view grooveFamilyKey(GrooveFamily value) noexcept {
    constexpr std::array names{"deep_progressive_house", "organic_progressive",
                               "driving_house", "hybrid"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 0];
}
std::string_view kickStateKey(KickState value) noexcept {
    constexpr std::array names{"muted", "reduced", "sparse", "four_on_floor"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 3];
}
std::string_view kickContinuityKey(KickContinuity value) noexcept {
    constexpr std::array names{"required", "sectional", "free"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 1];
}
std::string_view rhythmGestureKey(RhythmGestureKind value) noexcept {
    constexpr std::array names{"drop_last_kick", "double_kick", "pickup_fill",
                               "half_bar_mute", "full_bar_mute", "percussion_fill"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 0];
}
std::string_view rhythmLaneKey(RhythmLane value) noexcept {
    constexpr std::array names{"kick", "snare_clap", "closed_hats", "open_hats_shaker",
                               "low_percussion", "high_percussion"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 0];
}
std::string_view rhythmMutationKey(RhythmMutationKind value) noexcept {
    constexpr std::array names{"add", "remove", "shift", "ratchet", "velocity"};
    return names[static_cast<std::size_t>(value) < names.size() ? static_cast<std::size_t>(value) : 0];
}

std::optional<GrooveFamily> grooveFamilyFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, GrooveFamily>{"deep_progressive_house", GrooveFamily::DeepProgressiveHouse},
        std::pair<std::string_view, GrooveFamily>{"organic_progressive", GrooveFamily::OrganicProgressive},
        std::pair<std::string_view, GrooveFamily>{"driving_house", GrooveFamily::DrivingHouse},
        std::pair<std::string_view, GrooveFamily>{"hybrid", GrooveFamily::Hybrid}};
    return fromKey(key, values);
}
std::optional<KickState> kickStateFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, KickState>{"muted", KickState::Muted},
        std::pair<std::string_view, KickState>{"reduced", KickState::Reduced},
        std::pair<std::string_view, KickState>{"sparse", KickState::Sparse},
        std::pair<std::string_view, KickState>{"four_on_floor", KickState::FourOnFloor}};
    return fromKey(key, values);
}
std::optional<KickContinuity> kickContinuityFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, KickContinuity>{"required", KickContinuity::Required},
        std::pair<std::string_view, KickContinuity>{"sectional", KickContinuity::Sectional},
        std::pair<std::string_view, KickContinuity>{"free", KickContinuity::Free}};
    return fromKey(key, values);
}
std::optional<RhythmGestureKind> rhythmGestureFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, RhythmGestureKind>{"drop_last_kick", RhythmGestureKind::DropLastKick},
        std::pair<std::string_view, RhythmGestureKind>{"double_kick", RhythmGestureKind::DoubleKick},
        std::pair<std::string_view, RhythmGestureKind>{"pickup_fill", RhythmGestureKind::PickupFill},
        std::pair<std::string_view, RhythmGestureKind>{"half_bar_mute", RhythmGestureKind::HalfBarMute},
        std::pair<std::string_view, RhythmGestureKind>{"full_bar_mute", RhythmGestureKind::FullBarMute},
        std::pair<std::string_view, RhythmGestureKind>{"percussion_fill", RhythmGestureKind::PercussionFill}};
    return fromKey(key, values);
}
std::optional<RhythmLane> rhythmLaneFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, RhythmLane>{"kick", RhythmLane::Kick},
        std::pair<std::string_view, RhythmLane>{"snare_clap", RhythmLane::SnareClap},
        std::pair<std::string_view, RhythmLane>{"closed_hats", RhythmLane::ClosedHats},
        std::pair<std::string_view, RhythmLane>{"open_hats_shaker", RhythmLane::OpenHatsShaker},
        std::pair<std::string_view, RhythmLane>{"low_percussion", RhythmLane::LowPercussion},
        std::pair<std::string_view, RhythmLane>{"high_percussion", RhythmLane::HighPercussion}};
    return fromKey(key, values);
}
std::optional<RhythmMutationKind> rhythmMutationFromKey(std::string_view key) noexcept {
    constexpr std::array values{
        std::pair<std::string_view, RhythmMutationKind>{"add", RhythmMutationKind::Add},
        std::pair<std::string_view, RhythmMutationKind>{"remove", RhythmMutationKind::Remove},
        std::pair<std::string_view, RhythmMutationKind>{"shift", RhythmMutationKind::Shift},
        std::pair<std::string_view, RhythmMutationKind>{"ratchet", RhythmMutationKind::Ratchet},
        std::pair<std::string_view, RhythmMutationKind>{"velocity", RhythmMutationKind::Velocity}};
    return fromKey(key, values);
}

} // namespace pulso
