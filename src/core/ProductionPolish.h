#pragma once

#include "MusicTypes.h"
#include "TonalContract.h"

#include <cstddef>

namespace pulso {

struct ExpressionCompactionReport {
    std::size_t controlsBefore{};
    std::size_t controlsAfter{};
    std::size_t expressionsBefore{};
    std::size_t expressionsAfter{};
    std::size_t silentEventsRemoved{};
};

struct ProductionAuditReport {
    std::size_t metricViolations{};
    std::size_t unsafeDurations{};
    std::size_t orphanEvents{};
    std::size_t expressionEvents{};
    std::size_t literalRhythmBars{};
    std::size_t maximumRhythmRun{};
    double rhythmRepeatRatio{};
    double expressionEventsPerNote{};
    int unsupportedChromaticNotes{};
    int strongNonChordNotes{};
    int invalidSustains{};
    int unintendedHarshOverlaps{};
    std::size_t lowRegisterVerticalClashes{};
    std::size_t implicitCastParts{};
    double longestGlobalSilenceBeats{};
    double registerClarity{1.0};
    double familyBalance{1.0};
    double score{};
    bool ready{};
};

class ProductionPolish final {
public:
    // Publication timing is always exact. Human feel is a reversible playback layer,
    // never an irreversible offset embedded in draggable or Live-deployed MIDI.
    static std::size_t enforceMetricContract(Pattern&, int onsetStepsPerBeat = 4,
                                             int releaseStepsPerBeat = 16);
    // Curves exist only while their destination part is sounding and are reduced to
    // perceptually meaningful breakpoints, avoiding thousands of redundant events.
    [[nodiscard]] static ExpressionCompactionReport compactExpression(Pattern&, double phraseGapBeats = 1.0,
                                                                      std::size_t maxPointsPerPhrase = 6);
    [[nodiscard]] static ProductionAuditReport audit(const Pattern&, const TonalAuditReport&,
                                                      double beatsPerBar, double registerClarity,
                                                      double familyBalance);
    static void stamp(Pattern&, const ProductionAuditReport&);
};

} // namespace pulso
