#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct AttentionDirectionReport {
    bool active{};
    std::size_t windows{};
    std::size_t structuralBreathBars{};
    std::size_t phraseBreathsCreated{};
    std::size_t notesRemoved{};
    std::size_t notesTrimmed{};
    std::size_t floorNotesCreated{};
    std::size_t overcrowdedBarsBefore{};
    std::size_t overcrowdedBarsAfter{};
    std::size_t underfilledBarsBefore{};
    std::size_t underfilledBarsAfter{};
    std::size_t overloadedBarsBefore{};
    std::size_t overloadedBarsAfter{};
    std::size_t peakActivePartsBefore{};
    std::size_t peakActivePartsAfter{};
    double averageActivePartsBefore{};
    double averageActivePartsAfter{};
    double averagePerceptualLoadBefore{};
    double averagePerceptualLoadAfter{};
    double peakPerceptualLoadBefore{};
    double peakPerceptualLoadAfter{};
    double harmonicFloorCoverageBefore{};
    double harmonicFloorCoverageAfter{};
    std::size_t densityNotesRemoved{};
    std::size_t semanticNotesRemoved{};
    std::size_t authoredNotesPreserved{};
};

// Measures perceptual load instead of treating track count as loudness. GPT remains
// the owner of pitches, motifs, rests and form. The director never removes authored
// notes to satisfy a numeric density ceiling; it may only constrain semantically invalid
// transition beds and derive chord-floor sustains where the authored fabric is underfilled.
class AttentionDirector final {
public:
    [[nodiscard]] static AttentionDirectionReport shape(Pattern&, const SongPlan&);
    [[nodiscard]] static AttentionDirectionReport audit(const Pattern&, const SongPlan&);
};

} // namespace pulso
