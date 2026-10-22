#pragma once

#include "core/SongComposer.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace pulso::plugin {

struct AiNoteAddress {
    std::string cellId;
    std::size_t noteIndex{};
    double absoluteBeat{};
};

// Exact symbolic addressing for pitch-only AI revisions. Recovered checkpoints
// use different cell names from live writing: ownership and placement, not a
// naming convention, identify a note. Shared/transformed cells are not editable
// here because changing them would also change protected musical occurrences.
inline std::optional<AiNoteAddress> findAiNoteAddress(const PerformanceScore& score,
    const SongPlan& plan, std::size_t owner, int pitch, double soundingBeat,
    double editableStart, double editableEnd) {
    if (owner >= plan.instruments.size()) return {};
    std::optional<AiNoteAddress> found;
    for (const auto& placement : score.placements) {
        if (placement.sectionIndex < 0 || placement.sectionIndex >= static_cast<int>(plan.sections.size()) ||
            placement.repeats != 1 || placement.transpose != 0 || placement.retrograde ||
            placement.invertContour || !placement.voiceMap.empty() ||
            std::abs(placement.timeScale - 1.0) > .000001 || placement.fragmentStart != 0) continue;
        if (std::count_if(score.placements.begin(), score.placements.end(),
            [&](const auto& other) { return other.cellId == placement.cellId; }) != 1) continue;
        const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
            [&](const auto& item) { return item.id == placement.cellId; });
        if (cell == score.cells.end() || (placement.fragmentEnd >= 0 && placement.fragmentEnd < cell->lengthBeats)) continue;
        const auto origin = plan.sections[placement.sectionIndex].startBar * plan.beatsPerBar + placement.startBeat;
        for (std::size_t index = 0; index < cell->notes.size(); ++index) {
            const auto& note = cell->notes[index];
            const auto start = origin + note.beat;
            if (note.instrumentId != plan.instruments[owner].id || note.pitch != pitch ||
                start < editableStart - .001 || start >= editableEnd - .001 ||
                start > soundingBeat + .001 || start + note.durationBeats <= soundingBeat + .001) continue;
            if (found) return {}; // Ambiguous unison: never guess which note to edit.
            found = AiNoteAddress{cell->id, index, start};
        }
    }
    return found;
}

} // namespace pulso::plugin
