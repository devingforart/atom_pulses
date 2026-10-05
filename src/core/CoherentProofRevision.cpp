#include "CoherentProofRevision.h"

#include <algorithm>
#include <cmath>

namespace pulso {

double CoherentProofRevision::issuePriority(const CoherentProofIssue& issue) noexcept {
    if (issue.kind != "harsh_overlap" && issue.kind != "low_register_overlap") return 0.0;
    const auto distance = std::abs(issue.pitch - issue.otherPitch);
    const auto pitchClass = distance % 12;
    const auto interval = std::min(pitchClass, 12 - pitchClass);
    const auto duration = std::clamp(issue.overlapBeats, 0.0, 16.0);
    if (duration < 0.5) return 0.0;
    // Interval alone is not a verdict. Duration, register, harmonic function,
    // voice ownership and observed release determine whether to ask for review.
    auto weight = interval == 1 ? 1.0 : interval == 6 ? 0.8 : 0.3;
    if (issue.registerName == "low") weight *= 1.45;
    else if (issue.registerName == "high") weight *= 0.55;
    if (issue.partId == issue.otherPartId) weight *= 1.2;
    if (issue.chordFunction == "tonic") weight *= 1.2;
    if (issue.chordFunction == "dominant") weight *= 0.75;
    if (issue.resolutionObserved) weight *= 0.8;
    if (!issue.pitchDeclared || !issue.otherPitchDeclared) weight *= 1.15;
    return weight * std::min(4.0, std::sqrt(duration));
}

double CoherentProofRevision::risk(const CoherentProofGateReport& report) noexcept {
    double total = 0.0;
    for (const auto& issue : report.issues) total += issuePriority(issue);
    return total;
}

std::vector<CoherentRevisionWindow> CoherentProofRevision::selectWindows(
    const SongPlan& plan, const Pattern& rendered,
    const CoherentProofGateReport& report, std::size_t maximum) {
    std::vector<CoherentRevisionWindow> candidates;
    if (plan.beatsPerBar <= 0.0 || maximum == 0) return candidates;
    const auto songEnd = plan.totalBars * plan.beatsPerBar;
    for (const auto& issue : report.issues) {
        const auto priority = issuePriority(issue);
        if (priority < 1.8 || issue.partId == 0 ||
            issue.partId > plan.instruments.size()) continue;
        const auto start = std::floor(issue.beat / plan.beatsPerBar) * plan.beatsPerBar;
        const auto end = std::min(songEnd, std::ceil(
            (issue.beat + issue.overlapBeats - 0.000001) / plan.beatsPerBar) *
            plan.beatsPerBar);
        if (end <= start || end - start > plan.beatsPerBar * 4.0) continue;
        const auto boundaryCrossed = std::any_of(rendered.notes.begin(), rendered.notes.end(),
            [&](const auto& note) {
                if (note.partId != issue.partId) return false;
                return (note.startBeat < start - 0.000001 &&
                        note.endBeat() > start + 0.000001) ||
                       (note.startBeat < end - 0.000001 &&
                        note.endBeat() > end + 0.000001);
            });
        if (boundaryCrossed) continue;
        const auto same = std::find_if(candidates.begin(), candidates.end(),
            [&](const auto& window) {
                return window.partId == issue.partId &&
                    window.startBeat == start && window.endBeat == end;
            });
        if (same == candidates.end()) candidates.push_back({issue.partId, start, end, priority});
        else same->priority = std::max(same->priority, priority);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) { return a.priority > b.priority; });
    std::vector<CoherentRevisionWindow> chosen;
    for (const auto& candidate : candidates) {
        if (chosen.size() >= maximum) break;
        const auto conflicts = std::any_of(chosen.begin(), chosen.end(),
            [&](const auto& previous) {
                return previous.partId == candidate.partId &&
                    candidate.startBeat < previous.endBeat &&
                    previous.startBeat < candidate.endBeat;
            });
        if (!conflicts) chosen.push_back(candidate);
    }
    std::sort(chosen.begin(), chosen.end(), [](const auto& a, const auto& b) {
        return a.startBeat < b.startBeat;
    });
    return chosen;
}

bool CoherentProofRevision::accepts(const CoherentProofGateReport& before,
                                     const CoherentProofGateReport& after,
                                     std::size_t originalNotes,
                                     std::size_t revisedNotes) noexcept {
    if (!after.ready || !after.exactAiNotes || !after.technicalReady ||
        originalNotes == 0 || revisedNotes < originalNotes * 0.8 ||
        revisedNotes > originalNotes * 1.25 + 4) return false;
    if (after.underwrittenRoles && !before.underwrittenRoles) return false;
    if (after.chordBedPolyphonicStages < before.chordBedPolyphonicStages) return false;
    if (after.unsupportedChromaticNotes > before.unsupportedChromaticNotes ||
        after.strongNonChordNotes > before.strongNonChordNotes ||
        after.invalidSustains > before.invalidSustains) return false;
    return risk(after) + 0.1 < risk(before);
}

} // namespace pulso
