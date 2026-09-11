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
    std::size_t peakActivePartsBefore{};
    std::size_t peakActivePartsAfter{};
    double averageActivePartsBefore{};
    double averageActivePartsAfter{};
    double harmonicFloorCoverageBefore{};
    double harmonicFloorCoverageAfter{};
};

// Edits attention, not composition. GPT remains the owner of pitches, motifs and form;
// this director schedules its existing instrumental lines across time, derives only
// chord-floor sustains from the authored harmonic plan, and creates explicit breath.
class AttentionDirector final {
public:
    [[nodiscard]] static AttentionDirectionReport shape(Pattern&, const SongPlan&);
    [[nodiscard]] static AttentionDirectionReport audit(const Pattern&, const SongPlan&);
};

} // namespace pulso
