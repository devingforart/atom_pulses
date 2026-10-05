#include "CoherentProofGate.h"

#include "SelectiveRepair.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace pulso {
namespace {

struct HarmonicPoint {
    double beat{};
    const HarmonicChord* chord{};
};

std::vector<HarmonicPoint> harmonicTimeline(const SongPlan& plan) {
    std::vector<HarmonicPoint> points;
    for (const auto& section : plan.sections)
        for (const auto& event : section.harmonicEvents) {
            const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                [&](const auto& candidate) { return candidate.id == event.chordId; });
            if (chord != plan.chordPalette.end())
                points.push_back({(section.startBar + event.barOffset) * plan.beatsPerBar +
                    event.beatOffset, &*chord});
        }
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.beat < b.beat;
    });
    return points;
}

const HarmonicChord* chordAt(const std::vector<HarmonicPoint>& points, double beat) {
    const auto after = std::upper_bound(points.begin(), points.end(), beat + 0.000001,
        [](double position, const HarmonicPoint& point) { return position < point.beat; });
    return after == points.begin() ? nullptr : std::prev(after)->chord;
}

bool declared(const HarmonicChord* chord, int pitch) {
    return chord != nullptr && std::any_of(chord->pitchClasses.begin(),
        chord->pitchClasses.end(), [pitch](int candidate) {
            return ((candidate - pitch) % 12 + 12) % 12 == 0;
        });
}

bool hasResolution(const Pattern& rendered, const std::vector<HarmonicPoint>& timeline,
                   const CoherentProofIssue& issue) {
    if (issue.partId == 0) return false;
    const auto release = issue.beat + issue.overlapBeats;
    return std::any_of(rendered.notes.begin(), rendered.notes.end(), [&](const auto& note) {
        return note.partId == issue.partId && note.startBeat >= release - 0.000001 &&
            note.startBeat <= release + 4.0 && note.pitch != issue.pitch &&
            std::abs(note.pitch - issue.pitch) <= 2 &&
            declared(chordAt(timeline, note.startBeat), note.pitch);
    });
}

void addContext(CoherentProofIssue& issue, const Pattern& rendered,
                const std::vector<HarmonicPoint>& timeline) {
    const auto lowerPitch = issue.otherPartId > 0 ?
        std::min(issue.pitch, issue.otherPitch) : issue.pitch;
    issue.registerName = lowerPitch < 55 ? "low" : lowerPitch < 72 ? "mid" : "high";
    const auto source = std::find_if(rendered.notes.begin(), rendered.notes.end(),
        [&](const auto& note) {
            return note.partId == issue.partId && note.pitch == issue.pitch &&
                note.startBeat <= issue.beat + 0.000001 &&
                note.endBeat() >= issue.beat - 0.000001;
        });
    if (source != rendered.notes.end()) issue.noteDurationBeats = source->durationBeats;
    const auto* chord = chordAt(timeline, issue.beat);
    if (chord != nullptr) {
        issue.chordId = chord->id;
        issue.chordFunction = std::string(harmonicFunctionKey(chord->function));
        issue.pitchDeclared = declared(chord, issue.pitch);
        issue.otherPitchDeclared = issue.otherPartId > 0 && declared(chord, issue.otherPitch);
    }
    issue.resolutionObserved = hasResolution(rendered, timeline, issue);
    if (issue.kind == "low_register_overlap")
        issue.assessment = "low_register_mud_to_review";
    else if (!issue.pitchDeclared || (issue.otherPartId > 0 && !issue.otherPitchDeclared))
        issue.assessment = "outside_declared_harmony";
    else if (issue.resolutionObserved)
        issue.assessment = "declared_tension_with_possible_resolution";
    else
        issue.assessment = "declared_tension_needs_listening";
}

void appendLowRegisterEvidence(const Pattern& rendered,
                               const std::vector<HarmonicPoint>& timeline,
                               CoherentProofGateReport& result) {
    for (const auto& bass : rendered.notes) {
        if (bass.voice != VoiceId::MovementBass && bass.voice != VoiceId::SubBass) continue;
        for (const auto& support : rendered.notes) {
            if (bass.partId == support.partId ||
                (support.voice != VoiceId::HarmonicFoundation &&
                 support.voice != VoiceId::HarmonicPulse &&
                 support.voice != VoiceId::HarmonicUpper)) continue;
            const auto distance = std::abs(bass.pitch - support.pitch);
            if (std::min(bass.pitch, support.pitch) >= 55 || distance > 19) continue;
            const auto interval = std::min(distance % 12, 12 - distance % 12);
            if (interval != 1 && interval != 6) continue;
            const auto overlap = std::min(bass.endBeat(), support.endBeat()) -
                std::max(bass.startBeat, support.startBeat);
            if (overlap < 1.0 / 16.0 - 0.000001) continue;
            CoherentProofIssue issue;
            issue.kind = "low_register_overlap";
            issue.beat = std::max(bass.startBeat, support.startBeat);
            issue.overlapBeats = overlap;
            issue.pitch = support.pitch;
            issue.otherPitch = bass.pitch;
            issue.partId = support.partId;
            issue.otherPartId = bass.partId;
            addContext(issue, rendered, timeline);
            result.issues.push_back(std::move(issue));
        }
    }
}

void auditActivity(const SongPlan& plan, const Pattern& rendered,
                   CoherentProofGateReport& result) {
    if (plan.totalBars < 8 || plan.beatsPerBar <= 0.0) return;
    const auto bed = SelectiveRepair::centralChordBedOwner(plan);
    const auto lead = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& item) {
            return item.id == plan.narrativeSpine.protagonistInstrumentId;
        });
    if (!bed || lead == plan.instruments.end()) return;
    const auto leadPartId = static_cast<std::uint16_t>(
        std::distance(plan.instruments.begin(), lead) + 1);
    const auto bedPartId = static_cast<std::uint16_t>(*bed + 1);
    std::vector<std::vector<std::int64_t>> leadAttacks(static_cast<std::size_t>(plan.totalBars));
    std::vector<std::vector<std::int64_t>> bedAttacks(static_cast<std::size_t>(plan.totalBars));
    for (const auto& note : rendered.notes) {
        const auto bar = static_cast<int>(note.startBeat / plan.beatsPerBar);
        if (bar < 0 || bar >= plan.totalBars) continue;
        const auto attack = static_cast<std::int64_t>(std::llround(note.startBeat * 16.0));
        if (note.partId == leadPartId)
            leadAttacks[static_cast<std::size_t>(bar)].push_back(attack);
        if (note.partId == bedPartId)
            bedAttacks[static_cast<std::size_t>(bar)].push_back(attack);
    }
    const auto uniqueAttackCounts = [](auto& attacks) {
        std::vector<int> counts;
        counts.reserve(attacks.size());
        for (auto& bar : attacks) {
            std::sort(bar.begin(), bar.end());
            counts.push_back(static_cast<int>(std::unique(bar.begin(), bar.end()) - bar.begin()));
        }
        return counts;
    };
    const auto leadOnsets = uniqueAttackCounts(leadAttacks);
    const auto bedOnsets = uniqueAttackCounts(bedAttacks);
    const auto [leastLead, mostLead] = std::minmax_element(
        leadOnsets.begin(), leadOnsets.end());
    const auto [leastBed, mostBed] = std::minmax_element(
        bedOnsets.begin(), bedOnsets.end());
    result.leadOnsetRange = *mostLead - *leastLead;
    result.chordBedOnsetRange = *mostBed - *leastBed;
    result.uniformActivity = *mostLead > 0 && *mostBed > 0 &&
        result.leadOnsetRange == 0 && result.chordBedOnsetRange == 0;
}

void auditRoleCoverage(const SongPlan& plan, const Pattern& rendered,
                       CoherentProofGateReport& result) {
    if (plan.instruments.size() != 3 || plan.totalBars <= 0 || plan.beatsPerBar <= 0.0)
        return;
    const auto bed = SelectiveRepair::centralChordBedOwner(plan);
    const auto bass = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& item) {
            return item.sourceVoice == VoiceId::MovementBass ||
                item.sourceVoice == VoiceId::SubBass;
        });
    const auto lead = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& item) {
            return item.id == plan.narrativeSpine.protagonistInstrumentId;
        });
    if (!bed || bass == plan.instruments.end() || lead == plan.instruments.end()) {
        result.underwrittenRoles = true;
        return;
    }
    const auto bassId = static_cast<std::uint16_t>(
        std::distance(plan.instruments.begin(), bass) + 1);
    const auto leadId = static_cast<std::uint16_t>(
        std::distance(plan.instruments.begin(), lead) + 1);
    const auto bedId = static_cast<std::uint16_t>(*bed + 1);
    std::array<int, 4> noteCounts{};
    std::array<std::set<int>, 4> activeBars;
    std::map<std::pair<int, std::int64_t>, std::set<int>> bedAttacks;
    const auto songBeats = plan.totalBars * plan.beatsPerBar;
    for (const auto& note : rendered.notes) {
        if (note.partId == 0 || note.partId > 3) continue;
        ++noteCounts[note.partId];
        activeBars[note.partId].insert(static_cast<int>(note.startBeat / plan.beatsPerBar));
        if (note.partId != bedId) continue;
        const auto stage = std::clamp(static_cast<int>(note.startBeat * 3.0 / songBeats), 0, 2);
        bedAttacks[{stage, static_cast<std::int64_t>(std::llround(note.startBeat * 16.0))}]
            .insert(note.pitch);
    }
    std::array<bool, 3> polyphonicStages{};
    for (const auto& [key, pitches] : bedAttacks)
        if (pitches.size() >= 3) polyphonicStages[key.first] = true;
    result.chordBedPolyphonicStages = static_cast<int>(std::count(
        polyphonicStages.begin(), polyphonicStages.end(), true));
    result.underwrittenRoles = noteCounts[bedId] < 9 || noteCounts[bassId] < 4 ||
        noteCounts[leadId] < 6 || activeBars[bedId].size() < 3 ||
        activeBars[bassId].size() < 3 || activeBars[leadId].size() < 3 ||
        result.chordBedPolyphonicStages < 3;
}

} // namespace

CoherentProofGateReport CoherentProofGate::evaluate(
    const SongPlan& plan, const Pattern& rendered,
    const CompositionRenderReport& audit, std::size_t authoredNotes) {
    CoherentProofGateReport result;
    result.exactAiNotes = !rendered.notes.empty() &&
        rendered.notes.size() == authoredNotes &&
        std::all_of(rendered.notes.begin(), rendered.notes.end(), [](const auto& note) {
            return note.origin == NoteOrigin::AiAuthored;
        });
    result.metricViolations = audit.production.metricViolations;
    result.unsafeDurations = audit.production.unsafeDurations;
    result.orphanEvents = audit.production.orphanEvents;
    result.unsupportedChromaticNotes = audit.production.unsupportedChromaticNotes;
    result.strongNonChordNotes = audit.production.strongNonChordNotes;
    result.invalidSustains = audit.production.invalidSustains;
    result.unintendedHarshOverlaps = audit.production.unintendedHarshOverlaps;
    result.lowRegisterVerticalClashes = audit.production.lowRegisterVerticalClashes;
    const auto validNotes = std::all_of(rendered.notes.begin(), rendered.notes.end(),
        [](const auto& note) {
            return std::isfinite(note.startBeat) && std::isfinite(note.durationBeats) &&
                note.startBeat >= 0.0 && note.durationBeats > 0.0 && note.pitch >= 0 &&
                note.pitch <= 127 && note.velocity >= 1 && note.velocity <= 127 &&
                note.channel >= 1 && note.channel <= 16 && note.partId > 0;
        });
    result.technicalReady = rendered.productionAuditPerformed && validNotes &&
        result.metricViolations == 0 && result.unsafeDurations == 0 &&
        result.orphanEvents == 0;
    result.ready = result.exactAiNotes && result.technicalReady;
    result.musicalReviewRequired = result.unsupportedChromaticNotes > 0 ||
        result.strongNonChordNotes > 0 || result.invalidSustains > 0 ||
        result.unintendedHarshOverlaps > 0 || result.lowRegisterVerticalClashes > 0;

    const auto timeline = harmonicTimeline(plan);
    result.issues.reserve(audit.finalTonalPass.after.issues.size() +
        result.lowRegisterVerticalClashes);
    for (const auto& tonal : audit.finalTonalPass.after.issues) {
        CoherentProofIssue issue;
        issue.kind = tonal.kind;
        issue.beat = tonal.beat;
        issue.overlapBeats = tonal.overlapBeats;
        issue.pitch = tonal.pitch;
        issue.otherPitch = tonal.otherPitch;
        issue.partId = tonal.partId;
        issue.otherPartId = tonal.otherPartId;
        addContext(issue, rendered, timeline);
        result.issues.push_back(std::move(issue));
    }
    appendLowRegisterEvidence(rendered, timeline, result);
    auditActivity(plan, rendered, result);
    auditRoleCoverage(plan, rendered, result);
    result.musicalReviewRequired = result.musicalReviewRequired || !result.issues.empty() ||
        result.underwrittenRoles || result.uniformActivity;
    return result;
}

} // namespace pulso
