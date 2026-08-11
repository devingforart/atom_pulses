#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

// Continuous musical primitives, authored by GPT, replace closed genre labels.
// They describe how a rhythm behaves without naming a style or prescribing a template.
struct RhythmLanguage {
    std::string description{"Open rhythmic conversation"};
    double pulseStability{0.62};
    double backbeatGravity{0.48};
    double syncopation{0.42};
    double ghostDensity{0.18};
    double velocityContrast{0.52};
    double timingFreedom{0.20};
    double orchestrationMotion{0.40};
    double silenceBias{0.28};
    double callResponse{0.52};
};

enum class RhythmInstrument : std::uint8_t {
    KickDeep = 0, KickAlt, Snare, Sidestick, Clap,
    TomLow, TomMid, TomHigh, ClosedHat, PedalHat, OpenHat,
    Ride, Crash, Shaker, Tambourine, Cowbell, CongaLow, CongaHigh
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
    struct Ornament {
        int step{};
        RhythmInstrument instrument{RhythmInstrument::Shaker};
        int velocity{72};
        double durationSteps{0.5};
    };
    std::vector<Ornament> ornaments;
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

[[nodiscard]] std::string_view rhythmInstrumentKey(RhythmInstrument) noexcept;
[[nodiscard]] std::string_view kickStateKey(KickState) noexcept;
[[nodiscard]] std::string_view kickContinuityKey(KickContinuity) noexcept;
[[nodiscard]] std::string_view rhythmGestureKey(RhythmGestureKind) noexcept;
[[nodiscard]] std::string_view rhythmLaneKey(RhythmLane) noexcept;
[[nodiscard]] std::string_view rhythmMutationKey(RhythmMutationKind) noexcept;
[[nodiscard]] std::optional<RhythmInstrument> rhythmInstrumentFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<KickState> kickStateFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<KickContinuity> kickContinuityFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmGestureKind> rhythmGestureFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmLane> rhythmLaneFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<RhythmMutationKind> rhythmMutationFromKey(std::string_view) noexcept;

} // namespace pulso
