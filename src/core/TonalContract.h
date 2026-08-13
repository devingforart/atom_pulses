#pragma once

#include "HarmonyPlan.h"
#include "MusicTypes.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pulso {

struct HarmonicWindow {
    double startBeat{};
    double endBeat{};
    int rootPitchClass{};
    int bassPitchClass{};
    std::vector<int> pitchClasses;
    HarmonicFunction function{HarmonicFunction::Tonic};
    VoicingStrategy voicing{VoicingStrategy::Mixed};
    double tension{};
    std::string chordId;
    std::string label;
};

struct TonalIssue {
    double beat{};
    VoiceId voice{VoiceId::Unspecified};
    VoiceId otherVoice{VoiceId::Unspecified};
    int pitch{};
    int otherPitch{};
    std::string kind;
};

struct TonalAuditReport {
    int pitchedNotes{};
    int unsupportedChromaticNotes{};
    int strongNonChordNotes{};
    int invalidSustains{};
    int unintendedHarshOverlaps{};
    int intentionalClusters{};
    std::vector<TonalIssue> issues;

    [[nodiscard]] bool productionReady() const noexcept {
        return unsupportedChromaticNotes == 0 && strongNonChordNotes == 0 &&
               invalidSustains == 0 && unintendedHarshOverlaps == 0;
    }
};

struct TonalRepairReport {
    int outOfScaleRepaired{};
    int strongBeatRepaired{};
    int unsupportedChromaticRepaired{};
    int verticalCollisionsRepaired{};
    int harmonicOverlapsTrimmed{};
    int intentionalChromaticNotes{};
    int exactBoundaryTrims{};
    int notesRetunedForVoicing{};
    int notesRemoved{};
    int intentionalClusters{};
    TonalAuditReport before;
    TonalAuditReport after;
};

[[nodiscard]] std::string canonicalKeyName(int rootPitchClass, ScaleKind);
[[nodiscard]] std::optional<std::pair<int, ScaleKind>> parseKeyName(std::string_view);
void canonicalizeMotif(std::vector<int>& intervals, ScaleKind);

[[nodiscard]] TonalRepairReport repairTonalContract(
    Pattern&, int rootPitchClass, ScaleKind, double beatsPerBar,
    std::span<const std::vector<int>> harmonyByBar,
    double maximumChromaticRatio = 0.035,
    TonalPolicy policy = TonalPolicy::Consolidated);

[[nodiscard]] TonalAuditReport auditTonalContract(
    const Pattern&, int rootPitchClass, ScaleKind, double beatsPerBar,
    std::span<const HarmonicWindow> harmony,
    TonalPolicy policy = TonalPolicy::Consolidated);

[[nodiscard]] TonalRepairReport repairTonalContract(
    Pattern&, int rootPitchClass, ScaleKind, double beatsPerBar,
    std::span<const HarmonicWindow> harmony,
    double maximumChromaticRatio = 0.035,
    TonalPolicy policy = TonalPolicy::Consolidated);

} // namespace pulso
