#pragma once

#include "Orchestration.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

enum class Role : std::uint8_t { Bass = 0, Percussion, Countermelody, Ensemble };
enum class ScaleKind : std::uint8_t { Major = 0, Minor, Dorian, Mixolydian, Chromatic };

struct NoteEvent {
    double startBeat{};
    double durationBeats{0.25};
    int pitch{60};
    int velocity{100};
    int channel{1};
    VoiceId voice{VoiceId::Unspecified};

    [[nodiscard]] double endBeat() const noexcept { return startBeat + durationBeats; }
    friend bool operator==(const NoteEvent&, const NoteEvent&) = default;
};

struct ControlEvent {
    double beat{};
    int controller{11};
    int value{100};
    int channel{1};
    VoiceId voice{VoiceId::Unspecified};

    friend bool operator==(const ControlEvent&, const ControlEvent&) = default;
};

struct MarkerEvent {
    double beat{};
    std::string name;

    friend bool operator==(const MarkerEvent&, const MarkerEvent&) = default;
};

struct SourceNote {
    double beat{};
    int pitch{60};
    int velocity{100};
};

struct GenerationContext {
    Role role{Role::Bass};
    ScaleKind scale{ScaleKind::Minor};
    int rootPitchClass{0};
    double beatsPerBar{4.0};
    double follow{0.65};
    double risk{0.30};
    double space{};
    double repetition{0.75};
    double complexity{0.45};
    double development{0.40};
    double groove{};
    double humanize{0.30};
    double cohesion{0.80};
    double energy{0.55};
    int bars{4};
    std::uint64_t seed{1};
    std::uint64_t variationIndex{};
    std::uint64_t evolutionStep{};
    std::vector<int> chordPitchClasses{0, 3, 7};
    std::vector<std::vector<int>> harmonyByBar;
    std::vector<int> thematicIntervals;
    std::vector<SourceNote> sourceNotes;
};

struct Pattern {
    std::vector<NoteEvent> notes;
    std::vector<ControlEvent> controls;
    std::vector<MarkerEvent> markers;
    double lengthBeats{4.0};
    std::uint64_t seed{1};
};

constexpr std::array<std::string_view, 4> roleNames{"Bass", "Percussion", "Countermelody", "Ensemble"};
constexpr std::array<std::string_view, 5> scaleNames{"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"};

[[nodiscard]] constexpr int positiveModulo(int value, int modulus) noexcept {
    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

} // namespace pulso
