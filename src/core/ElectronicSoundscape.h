#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace pulso {

struct SongPlan;

enum class SoundscapeLayerKind : std::uint8_t { Voice = 0, Environment, Transition, OneShot };
enum class SoundscapeTimeScale : std::uint8_t { Fast = 0, Medium, Slow, Event };

[[nodiscard]] std::string_view soundscapeLayerKindKey(SoundscapeLayerKind) noexcept;
[[nodiscard]] std::string_view soundscapeTimeScaleKey(SoundscapeTimeScale) noexcept;
[[nodiscard]] SoundscapeLayerKind soundscapeLayerKindFromKey(std::string_view) noexcept;
[[nodiscard]] SoundscapeTimeScale soundscapeTimeScaleFromKey(std::string_view) noexcept;

struct SoundscapeLayerPlan {
    // Stable InstrumentAssignment::id. The layer describes a concrete DAW track,
    // never only an abstract execution voice.
    std::string instrumentId;
    SoundscapeLayerKind kind{SoundscapeLayerKind::Voice};
    SoundscapeTimeScale timeScale{SoundscapeTimeScale::Medium};
    std::string narrativeRole;
    std::string relationship;
    std::string evolution;
    int minimumActiveBars{8};
    int minimumPhrases{2};
    int maximumStaticBars{8};
    double foregroundDepth{0.5};
};

struct ElectronicSoundscapePlan {
    bool active{};
    bool authored{};
    bool percussionFree{};
    std::string scene;
    std::string spatialNarrative;
    double targetMedianActiveLayers{7.0};
    std::vector<SoundscapeLayerPlan> layers;
};

struct ElectronicSoundscapeReport {
    bool active{};
    bool percussionFree{};
    std::size_t declaredLayers{};
    std::size_t materializedLayers{};
    std::size_t meaningfulLayers{};
    std::size_t underdevelopedVoices{};
    std::size_t underdevelopedEnvironments{};
    std::size_t missingTransitionEvents{};
    std::size_t undeclaredPopulatedParts{};
    std::size_t staticLayerRuns{};
    double medianActiveLayers{};
    double meaningfulCoverage{1.0};
    std::size_t independentMusicalLines{};
    std::size_t meaningfulMusicalLines{};
    std::size_t protagonistPhraseWindows{};
    std::size_t arpeggioNoteCount{};
    std::size_t dialogueMusicalLines{};
    double harmonicFloorCoverage{};
    double medianHarmonicFloorLayers{};
    double score{1.0};
    bool ready{true};
    std::vector<std::string> issues;
};

// Validates that electronic labels correspond to audible, evolving MIDI. A voice,
// an environment, a transition and a one-shot intentionally have different minimums.
class ElectronicSoundscapeDirector final {
public:
    static void normalize(SongPlan&);
    [[nodiscard]] static ElectronicSoundscapeReport audit(const Pattern&, const SongPlan&);
    static void stamp(Pattern&, const ElectronicSoundscapeReport&);
};

} // namespace pulso
