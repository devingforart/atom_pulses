#include "EditorialSafety.h"

#include <algorithm>
#include <cmath>

namespace pulso {

bool EditorialSafety::technicallySafeAiScore(
    const Pattern& pattern, const CompositionRenderReport& report) noexcept {
    if (!pattern.productionAuditPerformed || pattern.notes.empty() ||
        !std::isfinite(pattern.lengthBeats) || pattern.lengthBeats <= 0.0 ||
        report.production.metricViolations != 0 ||
        report.production.unsafeDurations != 0 ||
        report.production.orphanEvents != 0) return false;
    return std::all_of(pattern.notes.begin(), pattern.notes.end(),
        [&](const NoteEvent& note) {
            return std::isfinite(note.startBeat) &&
                std::isfinite(note.durationBeats) &&
                note.startBeat >= 0.0 && note.durationBeats > 0.0 &&
                note.endBeat() <= pattern.lengthBeats + 0.0001 &&
                note.pitch >= 0 && note.pitch <= 127 &&
                note.velocity >= 1 && note.velocity <= 127 &&
                note.channel >= 1 && note.channel <= 16 &&
                note.partId > 0 && note.partId <= pattern.parts.size() &&
                (note.origin == NoteOrigin::AiAuthored ||
                 note.origin == NoteOrigin::AiTransformed);
        });
}

} // namespace pulso
