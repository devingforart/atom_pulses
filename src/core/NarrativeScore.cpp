#include "NarrativeScore.h"

#include "PerformanceScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <tuple>

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

struct AudibleThemeWindow {
    std::uint32_t narrativeId{};
    VoiceId voice{VoiceId::Unspecified};
    double start{};
    std::vector<const NoteEvent*> notes;
};

double closeness(double left, double right, double tolerance) noexcept {
    return std::clamp(1.0 - std::abs(left - right) / std::max(0.001, tolerance), 0.0, 1.0);
}

// Transposition-invariant comparison of what a listener actually hears. Rhythm and
// interval contour dominate; register, orchestration and dynamics may change freely.
double audibleSimilarity(const AudibleThemeWindow& left, const AudibleThemeWindow& right,
                         double windowBeats) {
    if (left.notes.size() < 2 || right.notes.size() < 2) return 0.0;
    const auto count = std::min(left.notes.size(), right.notes.size());
    const auto lengthScore = static_cast<double>(count) /
        static_cast<double>(std::max(left.notes.size(), right.notes.size()));
    auto rhythm = 0.0;
    auto duration = 0.0;
    auto contour = 0.0;
    auto invertedContour = 0.0;
    const auto leftOrigin = left.notes.front()->startBeat;
    const auto rightOrigin = right.notes.front()->startBeat;
    for (std::size_t index = 0; index < count; ++index) {
        const auto leftPhase = (left.notes[index]->startBeat - leftOrigin) / windowBeats;
        const auto rightPhase = (right.notes[index]->startBeat - rightOrigin) / windowBeats;
        rhythm += closeness(leftPhase, rightPhase, 0.125);
        duration += closeness(left.notes[index]->durationBeats,
                              right.notes[index]->durationBeats, 1.0);
        if (index == 0) continue;
        const auto leftInterval = left.notes[index]->pitch - left.notes[index - 1]->pitch;
        const auto rightInterval = right.notes[index]->pitch - right.notes[index - 1]->pitch;
        contour += closeness(leftInterval, rightInterval, 7.0);
        invertedContour += closeness(leftInterval, -rightInterval, 7.0);
    }
    rhythm /= static_cast<double>(count);
    duration /= static_cast<double>(count);
    const auto intervalCount = static_cast<double>(std::max<std::size_t>(1, count - 1));
    contour = std::max(contour, invertedContour * 0.90) / intervalCount;
    return std::clamp(rhythm * 0.43 + contour * 0.37 + lengthScore * 0.15 +
                      duration * 0.05, 0.0, 1.0);
}

std::vector<AudibleThemeWindow> audibleThemeWindows(const Pattern& pattern,
                                                     double beatsPerBar) {
    const auto windowBeats = std::max(4.0, beatsPerBar * 2.0);
    using Key = std::tuple<int, std::uint32_t, VoiceId>;
    std::map<Key, std::vector<const NoteEvent*>> grouped;
    for (const auto& note : pattern.notes) {
        if (!aiOrigin(note.origin) || note.narrativeId == 0 ||
            (note.voice != VoiceId::Lead && note.voice != VoiceId::Countermelody)) continue;
        const auto window = static_cast<int>(std::floor(note.startBeat / windowBeats));
        grouped[{window, note.narrativeId, note.voice}].push_back(&note);
    }
    std::vector<AudibleThemeWindow> result;
    for (auto& [key, notes] : grouped) {
        if (notes.size() < 2) continue;
        std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
            return left->pitch < right->pitch;
        });
        // Divisi and orchestration can duplicate one authored attack. Keep the audible
        // melodic skeleton rather than rewarding a thicker unison as thematic memory.
        notes.erase(std::unique(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            return std::abs(left->startBeat - right->startBeat) < 0.01 && left->pitch == right->pitch;
        }), notes.end());
        if (notes.size() >= 2)
            result.push_back({std::get<1>(key), std::get<2>(key),
                              std::get<0>(key) * windowBeats, std::move(notes)});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.start < right.start;
    });
    return result;
}

void auditBassPhrasing(const Pattern& pattern, const SongPlan& plan,
                       NarrativeScoreReport& report) {
    std::vector<const NoteEvent*> notes;
    for (const auto& note : pattern.notes)
        if (note.voice == VoiceId::MovementBass) notes.push_back(&note);
    std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
        return left->startBeat < right->startBeat;
    });
    std::vector<std::vector<const NoteEvent*>> phrases;
    for (const auto* note : notes) {
        const auto newPhrase = phrases.empty() || phrases.back().empty() ||
            note->startBeat - phrases.back().back()->endBeat() >= plan.beatsPerBar * 0.50 ||
            note->startBeat - phrases.back().front()->startBeat >= plan.beatsPerBar * 8.0;
        if (newPhrase) phrases.push_back({});
        phrases.back().push_back(note);
    }
    report.bassPhrases = phrases.size();
    for (const auto& phrase : phrases)
        if (phrase.size() == 1) ++report.singleNoteBassPhrases;
    if (!phrases.empty()) {
        const auto coherent = std::count_if(phrases.begin(), phrases.end(), [&](const auto& phrase) {
            if (phrase.size() < 3) return false;
            const auto span = phrase.back()->endBeat() - phrase.front()->startBeat;
            std::set<int> phases;
            for (const auto* note : phrase)
                phases.insert(static_cast<int>(std::lround(
                    std::fmod(note->startBeat, plan.beatsPerBar) * 4.0)));
            return span >= plan.beatsPerBar && phases.size() >= 2;
        });
        report.bassPhraseContinuity = static_cast<double>(coherent) /
            static_cast<double>(phrases.size());
    }
}

void auditDensity(const Pattern& pattern, const SongPlan& plan,
                  NarrativeScoreReport& report) {
    const auto bars = std::max(1, plan.totalBars);
    std::vector<std::set<VoiceId>> active(static_cast<std::size_t>(bars));
    for (const auto& note : pattern.notes) {
        const auto first = std::clamp(static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar)), 0, bars - 1);
        const auto last = std::clamp(static_cast<int>(std::floor(
            std::max(note.startBeat, note.endBeat() - 0.001) / plan.beatsPerBar)), 0, bars - 1);
        for (auto bar = first; bar <= last; ++bar)
            if (note.voice != VoiceId::Unspecified) active[static_cast<std::size_t>(bar)].insert(note.voice);
    }
    const auto club = plan.productionLanguage.domain == ProductionDomain::ClubElectronic;
    const auto ceiling = club ? std::size_t{9} : std::size_t{11};
    auto soundingBars = std::size_t{};
    for (const auto& voices : active) {
        if (voices.empty()) continue;
        ++soundingBars;
        report.peakActiveVoices = std::max(report.peakActiveVoices, voices.size());
        const auto foreground = static_cast<int>(voices.contains(VoiceId::Lead)) +
            static_cast<int>(voices.contains(VoiceId::Countermelody)) +
            static_cast<int>(voices.contains(VoiceId::HarmonicPulse)) +
            static_cast<int>(voices.contains(VoiceId::HarmonicUpper));
        if (voices.size() > ceiling || foreground > 2) ++report.overcrowdedBars;
    }
    report.densityControl = 1.0 - static_cast<double>(report.overcrowdedBars) /
        static_cast<double>(std::max<std::size_t>(1, soundingBars));
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
    report.declaredThematicRecallRatio = static_cast<double>(report.recurringThematicPlacements) /
        std::max<std::size_t>(1, report.thematicPlacements);

    const auto windows = audibleThemeWindows(pattern, plan.beatsPerBar);
    report.audibleThematicWindows = windows.size();
    auto similarityTotal = 0.0;
    auto comparable = std::size_t{};
    for (std::size_t index = 0; index < windows.size(); ++index) {
        auto best = 0.0;
        auto found = false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (windows[previous].narrativeId != windows[index].narrativeId ||
                windows[index].start - windows[previous].start < plan.beatsPerBar * 2.0) continue;
            found = true;
            best = std::max(best, audibleSimilarity(windows[previous], windows[index],
                                                    std::max(4.0, plan.beatsPerBar * 2.0)));
        }
        if (!found) continue;
        ++comparable;
        similarityTotal += best;
        if (best >= 0.70) ++report.audiblyRecurringThematicWindows;
    }
    report.audibleThematicSimilarity = comparable == 0 ? 0.0 :
        similarityTotal / static_cast<double>(comparable);
    report.thematicRecallRatio = comparable == 0 ? 0.0 :
        static_cast<double>(report.audiblyRecurringThematicWindows) /
        static_cast<double>(comparable);

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
    auditBassPhrasing(pattern, plan, report);
    auditDensity(pattern, plan, report);

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

    report.score = std::clamp(report.primaryVoiceCoverage * 0.22 +
        report.thematicRecallRatio * 0.17 + report.audibleThematicSimilarity * 0.13 +
        report.bassPhraseContinuity * 0.15 + report.densityControl * 0.12 +
        report.harmonicDirection * 0.10 + report.rhythmicDevelopment * 0.07 +
        std::min(1.0, report.aiAuthoredNoteRatio / 0.50) * 0.04, 0.0, 1.0);
    if (report.active && report.primaryVoiceCoverage < 0.60) report.issues.push_back("insufficient_ai_phrase_coverage");
    if (report.active && report.audibleThematicWindows >= 3 && report.thematicRecallRatio < 0.40)
        report.issues.push_back("weak_long_range_theme_memory");
    if (report.active && report.audibleThematicWindows >= 3 && report.audibleThematicSimilarity < 0.66)
        report.issues.push_back("theme_labels_without_audible_lineage");
    if (report.bassWindows >= 3 && report.bassPhraseContinuity < 0.55)
        report.issues.push_back("fragmented_movement_bass");
    if (report.densityControl < 0.82) report.issues.push_back("overcrowded_arrangement");
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
    pattern.audibleThematicSimilarity = report.audibleThematicSimilarity;
    pattern.bassPhraseContinuity = report.bassPhraseContinuity;
    pattern.densityControl = report.densityControl;
    pattern.peakActiveVoices = report.peakActiveVoices;
    pattern.narrativeIssues = report.issues;
    for (const auto& issue : report.issues)
        pattern.productionIssues.push_back("narrative:" + issue);
    if (report.active) {
        const auto blocking = report.primaryVoiceCoverage < 0.35 ||
            (report.audibleThematicWindows >= 3 && report.thematicRecallRatio < 0.20) ||
            (report.bassPhrases >= 4 && report.bassPhraseContinuity < 0.25) ||
            report.densityControl < 0.55;
        if (blocking) pattern.productionReady = false;
    }
}

} // namespace pulso
