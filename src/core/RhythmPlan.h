#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

enum class GrooveFamily : std::uint8_t {
    DeepProgressiveHouse = 0, OrganicProgressive, DrivingHouse, Hybrid
};

enum class KickState : std::uint8_t { Muted = 0, Reduced, Sparse, FourOnFloor };
enum class KickContinuity : std::uint8_t { Required = 0, Sectional, Free };
enum class RhythmGestureKind : std::uint8_t {
    DropLastKick = 0, DoubleKick, PickupFill, HalfBarMute, FullBarMute, PercussionFill
};
enum class RhythmLane : std::uint8_t {
    Kick = 0, SnareClap, ClosedHats, OpenHatsShaker, LowPercussion, HighPercussion
};
enum class RhythmMutationKind : std::uint8_t { Add = 0, Remove, Shift, Ratchet, Velocity };

struct RhythmMotif {
    std::string id{"A"};
    int bars{2};
    int stepsPerBar{16};
    std::string kick;
    std::string snareClap;
    std::string closedHats;
    std::string openHatsShaker;
    std::string lowPercussion;
    std::string highPercussion;
};

struct RhythmMutation {
    int barOffset{};
    RhythmLane lane{RhythmLane::Kick};
    RhythmMutationKind kind{RhythmMutationKind::Add};
    int step{};
    int amount{1};
    int velocity{84};
    std::string purpose;
};

struct RhythmGesture {
    int barOffset{};
    RhythmGestureKind kind{RhythmGestureKind::DropLastKick};
    double beat{3.0};
    double intensity{0.65};
};

struct SectionRhythmPlan {
    KickState kickState{KickState::FourOnFloor};
    KickContinuity continuity{KickContinuity::Sectional};
    double percussionDensity{0.55};
    double syncopation{0.38};
    double swing{0.08};
    bool authored{};
    std::string motifId;
    std::vector<RhythmGesture> gestures;
    std::vector<RhythmMutation> mutations;
};

[[nodiscard]] std::string_view grooveFamilyKey(GrooveFamily) noexcept;
[[nodiscard]] std::string_view kickStateKey(KickState) noexcept;
[[nodiscard]] std::string_view kickContinuityKey(KickContinuity) noexcept;
[[nodiscard]] std::string_view rhythmGestureKey(RhythmGestureKind) noexcept;
[[nodiscard]] std::string_view rhythmLaneKey(RhythmLane) noexcept;
[[nodiscard]] std::string_view rhythmMutationKey(RhythmMutationKind) noexcept;
[[nodiscard]] std::optional<GrooveFamily> grooveFamilyFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<KickState> kickStateFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<KickContinuity> kickContinuityFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmGestureKind> rhythmGestureFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmLane> rhythmLaneFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmMutationKind> rhythmMutationFromKey(std::string_view) noexcept;

} // namespace pulso
