#pragma once

#include "MusicTypes.h"

#include <optional>
#include <string>
#include <string_view>

namespace pulso {

struct SongPlan;

enum class ArticulationStyle : std::uint8_t {
    Percussive = 0, Staccato, Detached, Natural, Legato, Sustained, Swelling
};

enum class DynamicContour : std::uint8_t {
    Steady = 0, PhraseArc, Crescendo, Decrescendo, Swell, Pulsing
};

enum class VibratoStyle : std::uint8_t {
    None = 0, LateSubtle, LateExpressive, ContinuousSubtle
};

enum class PitchGesture : std::uint8_t {
    Stable = 0, Approach, GentleBends, Portamento
};

struct PerformanceProfile {
    ArticulationStyle articulation{ArticulationStyle::Natural};
    DynamicContour dynamics{DynamicContour::PhraseArc};
    VibratoStyle vibrato{VibratoStyle::None};
    PitchGesture pitchGesture{PitchGesture::Stable};
    double expressionDepth{0.45};
    double brightness{0.50};
    double humanization{0.30};
    bool sustainPedal{};
    bool authored{};
    std::string intent;
};

[[nodiscard]] PerformanceProfile defaultPerformanceProfile(VoiceId) noexcept;
[[nodiscard]] std::string_view articulationStyleKey(ArticulationStyle) noexcept;
[[nodiscard]] std::string_view dynamicContourKey(DynamicContour) noexcept;
[[nodiscard]] std::string_view vibratoStyleKey(VibratoStyle) noexcept;
[[nodiscard]] std::string_view pitchGestureKey(PitchGesture) noexcept;
[[nodiscard]] std::optional<ArticulationStyle> articulationStyleFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<DynamicContour> dynamicContourFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<VibratoStyle> vibratoStyleFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<PitchGesture> pitchGestureFromKey(std::string_view) noexcept;

class PerformanceExpression final {
public:
    // Converts semantic, AI-authored performance intent into portable MIDI expression.
    // Safe channel pitch gestures are limited to monophonic voices with dedicated channels.
    static void apply(Pattern&, const SongPlan&);
    static void applyIdeaDefaults(Pattern&, double bpm, double beatsPerBar = 4.0);
};

} // namespace pulso
