#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace pulso {

enum class Role : std::uint8_t { Bass = 0, Percussion, Countermelody };
enum class ScaleKind : std::uint8_t { Major = 0, Minor, Dorian, Mixolydian, Chromatic };

struct NoteEvent {
    double startBeat{};
    double durationBeats{0.25};
    int pitch{60};
    int velocity{100};
    int channel{1};

    [[nodiscard]] double endBeat() const noexcept { return startBeat + durationBeats; }
    friend bool operator==(const NoteEvent&, const NoteEvent&) = default;
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
    double space{0.35};
    std::uint64_t seed{1};
    std::vector<int> chordPitchClasses{0, 3, 7};
    std::vector<SourceNote> sourceNotes;
};

struct Pattern {
    std::vector<NoteEvent> notes;
    double lengthBeats{4.0};
    std::uint64_t seed{1};
};

constexpr std::array<std::string_view, 3> roleNames{"Bass", "Percussion", "Countermelody"};
constexpr std::array<std::string_view, 5> scaleNames{"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"};

[[nodiscard]] constexpr int positiveModulo(int value, int modulus) noexcept {
    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

} // namespace pulso

