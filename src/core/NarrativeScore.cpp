#include "NarrativeScore.h"

#include "PerformanceScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string_view>

namespace pulso {
namespace {

bool aiOrigin(NoteOrigin origin) noexcept {
    return origin == NoteOrigin::AiAuthored || origin == NoteOrigin::AiTransformed;
}

bool primaryVoice(VoiceId voice) noexcept {
    return voice == VoiceId::SubBass || voice == VoiceId::MovementBass ||
           voice == VoiceId::HarmonicFoundation || voice == VoiceId::HarmonicPulse ||
           voice == VoiceId::Lead || voice == VoiceId::Countermelody;
}

bool grooveVoice(VoiceId voice) noexcept {
    return voice == VoiceId::CoreDrums || voice == VoiceId::SnareClap ||
           voice == VoiceId::ClosedHats || voice == VoiceId::OpenHatsShaker ||
           voice == VoiceId::LowPercussion || voice == VoiceId::HighPercussion;
}

const PerformanceCell* findCell(const PerformanceScore& score, const std::string& id) noexcept {
    const auto found = std::find_if(score.cells.begin(), score.cells.end(),
        [&](const auto& cell) { return cell.id == id; });
    return found == score.cells.end() ? nullptr : &*found;
}

VoiceId targetVoice(const PerformancePlacement& placement, VoiceId source) noexcept {
    const auto found = std::find_if(placement.voiceMap.begin(), placement.voiceMap.end(),
        [&](const auto& map) { return map.from == source; });
    return found == placement.voiceMap.end() ? source : found->to;
}

double unionLength(std::vector<std::pair<double, double>> spans) {
    if (spans.empty()) return 0.0;
    std::sort(spans.begin(), spans.end());
    auto start = spans.front().first;
    auto end = spans.front().second;
    auto total = 0.0;
    for (std::size_t index = 1; index < spans.size(); ++index) {
        if (spans[index].first <= end + 0.0001) end = std::max(end, spans[index].second);
        else { total += end - start; start = spans[index].first; end = spans[index].second; }
    }
    return total + end - start;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool narrativePurpose(const PerformanceCell& cell) {
    const auto purpose = lower(cell.narrativeFunction);
    return purpose != "support" && purpose != "continuity" && !purpose.empty();
}

} // namespace

NarrativeScoreReport NarrativeScoreGate::audit(const Pattern& pattern, const SongPlan& plan) {
    NarrativeScoreReport report;
    report.active = plan.productionModeSource == "gpt_plan";
    report.totalNotes = pattern.notes.size();
    report.aiAuthoredNotes = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [](const auto& note) { return aiOrigin(note.origin); });
    report.aiAuthoredNoteRatio = static_cast<double>(report.aiAuthoredNotes) /
        std::max<std::size_t>(1, report.totalNotes);

    using Spans = std::vector<std::pair<double, double>>;
    std::map<std::pair<int, VoiceId>, Spans> authoredSpans;
    std::map<std::string, std::set<int>> themeSections;
    std::map<std::string, std::size_t> themePlacements;
    for (const auto& placement : plan.performanceScore.placements) {
        const auto* cell = findCell(plan.performanceScore, placement.cellId);
        if (cell == nullptr || placement.sectionIndex < 0 ||
            placement.sectionIndex >= static_cast<int>(plan.sections.size())) continue;
        const auto sectionLength = plan.sections[static_cast<std::size_t>(placement.sectionIndex)].bars *
                                   plan.beatsPerBar;
        const auto fragmentStart = std::clamp(placement.fragmentStart, 0.0, cell->lengthBeats);
        const auto fragmentEnd = placement.fragmentEnd < 0.0 ? cell->lengthBeats :
            std::clamp(placement.fragmentEnd, fragmentStart, cell->lengthBeats);
        const auto iteration = cell->lengthBeats * placement.timeScale;
        for (auto repeat = 0; repeat < placement.repeats; ++repeat) {
            const auto origin = placement.startBeat + repeat * iteration;
            const auto start = std::clamp(origin + fragmentStart * placement.timeScale, 0.0, sectionLength);
            const auto end = std::clamp(origin + fragmentEnd * placement.timeScale, 0.0, sectionLength);
            if (end <= start) continue;
            for (const auto owner : cell->ownedVoices)
                authoredSpans[{placement.sectionIndex, targetVoice(placement, owner)}].push_back({start, end});
        }
        if (narrativePurpose(*cell)) {
            ++report.thematicPlacements;
            const auto theme = cell->themeId.empty() ? cell->id : cell->themeId;
            ++themePlacements[theme];
            themeSections[theme].insert(placement.sectionIndex);
        }
    }
    for (const auto& [theme, count] : themePlacements)
        if (count > 1 && themeSections[theme].size() > 1) report.recurringThematicPlacements += count;
    report.thematicRecallRatio = static_cast<double>(report.recurringThematicPlacements) /
        std::max<std::size_t>(1, report.thematicPlacements);

    auto primaryAvailable = 0.0;
    auto primaryAuthored = 0.0;
    auto grooveAvailable = 0.0;
    auto grooveAuthored = 0.0;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto length = section.bars * plan.beatsPerBar;
        for (const auto voice : section.activeVoices) {
            if (primaryVoice(voice)) {
                primaryAvailable += length;
                primaryAuthored += unionLength(authoredSpans[{static_cast<int>(sectionIndex), voice}]);
            }
            if (grooveVoice(voice)) {
                grooveAvailable += length;
                grooveAuthored += unionLength(authoredSpans[{static_cast<int>(sectionIndex), voice}]);
            }
        }
    }
    report.primaryVoiceCoverage = primaryAvailable > 0.0 ? primaryAuthored / primaryAvailable : 1.0;
    report.grooveAuthorshipCoverage = grooveAvailable > 0.0 ? grooveAuthored / grooveAvailable : 1.0;

    const auto window = std::max(4.0, plan.beatsPerBar * 4.0);
    for (auto start = 0.0; start < pattern.lengthBeats; start += window) {
        std::vector<const NoteEvent*> bass;
        for (const auto& note : pattern.notes)
            if (note.voice == VoiceId::MovementBass && note.startBeat >= start &&
                note.startBeat < start + window) bass.push_back(&note);
        if (bass.empty()) continue;
        ++report.bassWindows;
        std::set<int> pitches;
        std::set<int> onsetPhases;
        for (const auto* note : bass) {
            pitches.insert(positiveModulo(note->pitch, 12));
            onsetPhases.insert(static_cast<int>(std::lround(
                std::fmod(note->startBeat - start, plan.beatsPerBar) * 4.0)));
        }
        if (bass.size() >= 3 && (pitches.size() >= 2 || onsetPhases.size() >= 3))
            ++report.developedBassWindows;
    }
    report.bassPhraseContinuity = static_cast<double>(report.developedBassWindows) /
        std::max<std::size_t>(1, report.bassWindows);

    auto directedSections = std::size_t{};
    for (const auto& section : plan.sections) {
        std::set<std::string> chords;
        for (const auto& event : section.harmonicEvents) chords.insert(event.chordId);
        const auto startsAtZero = std::any_of(section.harmonicEvents.begin(), section.harmonicEvents.end(),
            [](const auto& event) { return event.barOffset == 0 && std::abs(event.beatOffset) < 0.001; });
        if (startsAtZero && chords.size() >= 2 && !section.harmonicDirection.empty()) ++directedSections;
    }
    report.harmonicDirection = static_cast<double>(directedSections) /
        std::max<std::size_t>(1, plan.sections.size());

    std::set<std::string> usedMotifs;
    auto developedRhythmSections = std::size_t{};
    for (const auto& section : plan.sections) {
        if (!section.rhythm.motifId.empty()) usedMotifs.insert(section.rhythm.motifId);
        if (!section.rhythm.mutations.empty() || !section.rhythm.gestures.empty())
            ++developedRhythmSections;
    }
    const auto sectionDevelopment = static_cast<double>(developedRhythmSections) /
        std::max<std::size_t>(1, plan.sections.size());
    const auto motifIdentity = std::clamp(static_cast<double>(usedMotifs.size()) / 3.0, 0.0, 1.0);
    report.rhythmicDevelopment = sectionDevelopment * 0.72 + motifIdentity * 0.28;

    report.score = std::clamp(report.primaryVoiceCoverage * 0.25 +
        report.thematicRecallRatio * 0.22 + report.bassPhraseContinuity * 0.16 +
        report.harmonicDirection * 0.17 + report.rhythmicDevelopment * 0.12 +
        std::min(1.0, report.aiAuthoredNoteRatio / 0.45) * 0.08, 0.0, 1.0);
    if (report.active && report.primaryVoiceCoverage < 0.45) report.issues.push_back("insufficient_ai_phrase_coverage");
    if (report.active && report.thematicPlacements >= 3 && report.thematicRecallRatio < 0.35)
        report.issues.push_back("weak_long_range_theme_memory");
    if (report.bassWindows >= 3 && report.bassPhraseContinuity < 0.55)
        report.issues.push_back("fragmented_movement_bass");
    if (report.harmonicDirection < 0.70) report.issues.push_back("weak_harmonic_direction");
    if (report.rhythmicDevelopment < 0.45) report.issues.push_back("undeveloped_rhythm_narrative");
    return report;
}

void NarrativeScoreGate::stamp(Pattern& pattern, const NarrativeScoreReport& report) {
    pattern.narrativeAuditPerformed = true;
    pattern.narrativeScore = report.score;
    pattern.aiAuthoredNoteRatio = report.aiAuthoredNoteRatio;
    pattern.primaryVoiceAuthorshipCoverage = report.primaryVoiceCoverage;
    pattern.thematicRecallRatio = report.thematicRecallRatio;
    pattern.narrativeIssues = report.issues;
    for (const auto& issue : report.issues)
        pattern.productionIssues.push_back("narrative:" + issue);
}

} // namespace pulso
