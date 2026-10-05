#pragma once

#include "SongComposer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pulso {

// Publication blocks on objective MIDI integrity and exact AI authorship.
// Harmonic interpretations are preserved as contextual evidence for listening.
struct CoherentProofIssue {
    std::string kind;
    std::string assessment;
    std::string chordId;
    std::string chordFunction;
    std::string registerName;
    double beat{};
    double overlapBeats{};
    double noteDurationBeats{};
    int pitch{};
    int otherPitch{};
    std::uint16_t partId{};
    std::uint16_t otherPartId{};
    bool pitchDeclared{};
    bool otherPitchDeclared{};
    bool resolutionObserved{};
};

struct CoherentProofGateReport {
    bool exactAiNotes{};
    bool technicalReady{};
    bool ready{};
    bool musicalReviewRequired{};
    bool uniformActivity{};
    bool underwrittenRoles{};
    int chordBedPolyphonicStages{};
    int leadOnsetRange{};
    int chordBedOnsetRange{};
    std::size_t metricViolations{};
    std::size_t unsafeDurations{};
    std::size_t orphanEvents{};
    int unsupportedChromaticNotes{};
    int strongNonChordNotes{};
    int invalidSustains{};
    int unintendedHarshOverlaps{};
    std::size_t lowRegisterVerticalClashes{};
    std::vector<CoherentProofIssue> issues;
};

class CoherentProofGate final {
public:
    [[nodiscard]] static CoherentProofGateReport evaluate(
        const SongPlan&, const Pattern&, const CompositionRenderReport&,
        std::size_t authoredNotes);
};

} // namespace pulso
