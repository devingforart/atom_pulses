#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

enum class HarmonicFunction : std::uint8_t {
    Tonic = 0, Predominant, Dominant, Modal, Chromatic, Pedal, Transitional, Colour
};

enum class VoicingStrategy : std::uint8_t {
    Close = 0, Open, Drop2, Quartal, Cluster, Shell, Mixed
};

// The tonal policy is a safety boundary, not a style preset. Consolidated is the
// default for every ordinary request; wider pitch vocabularies require explicit
// language in the user's direction.
enum class TonalPolicy : std::uint8_t { Consolidated = 0, Expanded, Free };

struct HarmonicLanguage {
    std::string description{"Open harmonic narrative"};
    TonalPolicy tonalPolicy{TonalPolicy::Consolidated};
    double tonalGravity{0.65};
    double modalFluidity{0.25};
    double chromaticism{0.18};
    double extensionRichness{0.48};
    double inversionMotion{0.42};
    double voiceLeadingSmoothness{0.72};
    double harmonicRhythmActivity{0.40};
    double pedalToneAffinity{0.28};
    double ambiguity{0.30};
    double cadenceStrength{0.62};
};

struct HarmonicChord {
    std::string id{"tonic"};
    std::string label{"Tonic"};
    int rootPitchClass{};
    int bassPitchClass{};
    std::vector<int> pitchClasses;
    HarmonicFunction function{HarmonicFunction::Tonic};
    VoicingStrategy voicing{VoicingStrategy::Mixed};
    double tension{0.20};
};

struct HarmonicEvent {
    int barOffset{};
    double beatOffset{};
    std::string chordId{"tonic"};
    double emphasis{0.50};
    std::string purpose;
};

[[nodiscard]] std::string_view harmonicFunctionKey(HarmonicFunction) noexcept;
[[nodiscard]] std::string_view voicingStrategyKey(VoicingStrategy) noexcept;
[[nodiscard]] std::string_view tonalPolicyKey(TonalPolicy) noexcept;
[[nodiscard]] std::optional<HarmonicFunction> harmonicFunctionFromKey(std::string_view) noexcept;
[[nodiscard]] std::optional<VoicingStrategy> voicingStrategyFromKey(std::string_view) noexcept;
[[nodiscard]] TonalPolicy tonalPolicyForDirection(std::string_view);

} // namespace pulso
