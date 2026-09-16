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
    // Bass, harmonic floor and pulse each have their own continuity contracts. Counting
    // all of them as "the protagonist" multiplied the denominator and encouraged every
    // voice to sound almost continuously. Narrative presence belongs to the foreground
    // speaker and its explicit answer only.
    return voice == VoiceId::Lead || voice == VoiceId::Countermelody;
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
        // Short gates are articulation, not phrase boundaries. A progressive bass can
        // answer once per bar and still form one coherent eight-bar pocket. Only a rest
        // longer than a complete bar plus pickup space, or the eight-bar horizon itself,
        // starts a new musical phrase.
        const auto newPhrase = phrases.empty() || phrases.back().empty() ||
            note->startBeat - phrases.back().back()->endBeat() >= plan.beatsPerBar * 1.25 ||
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
            // Repeating one deliberate phase is a coherent hypnotic bass phrase.
            // Variation is scored by developedBassWindows; continuity only rejects
            // isolated gestures that never become a statement across musical time.
            return span >= plan.beatsPerBar;
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
    auto soundingBars = std::size_t{};
    for (std::size_t bar = 0; bar < active.size(); ++bar) {
        const auto& voices = active[bar];
        if (voices.empty()) continue;
        ++soundingBars;
        report.peakActiveVoices = std::max(report.peakActiveVoices, voices.size());
        auto load = 0.0;
        for (const auto voice : voices) {
            if (isVoiceInFamily(voice, VoiceFamily::Rhythm)) load += .38;
            else if (voice == VoiceId::SubBass || voice == VoiceId::MovementBass) load += 1.05;
            else if (voice == VoiceId::HarmonicFoundation) load += 1.10;
            else if (voice == VoiceId::Lead || voice == VoiceId::Countermelody) load += .88;
            else if (voice == VoiceId::Atmosphere || voice == VoiceId::Transitions) load += .55;
            else load += .72;
        }
        const auto beat = static_cast<double>(bar) * plan.beatsPerBar;
        const SongSection* section = plan.sections.empty() ? nullptr : &plan.sections.front();
        for (const auto& candidate : plan.sections) {
            if (candidate.startBar * plan.beatsPerBar > beat + .001) break;
            section = &candidate;
        }
        const auto energy = section == nullptr ? .5 : section->energy;
        const auto density = section == nullptr ? .5 : section->density;
        const auto ceiling = 8.2 + (energy * .62 + density * .38) * 3.8;
        if (load > ceiling) ++report.overcrowdedBars;
    }
    report.densityControl = 1.0 - static_cast<double>(report.overcrowdedBars) /
        static_cast<double>(std::max<std::size_t>(1, soundingBars));
}

void auditMelodicSpeech(const Pattern& pattern, const SongPlan& plan,
                        NarrativeScoreReport& report) {
    for (const auto voice : {VoiceId::Lead, VoiceId::Countermelody}) {
        std::vector<const NoteEvent*> notes;
        for (const auto& note : pattern.notes)
            if (note.voice == voice) notes.push_back(&note);
        std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
            return left->pitch < right->pitch;
        });
        auto run = std::size_t{};
        for (std::size_t index = 1; index < notes.size(); ++index) {
            const auto gap = notes[index]->startBeat - notes[index - 1]->endBeat();
            if (gap > plan.beatsPerBar * 0.50 ||
                std::abs(notes[index]->startBeat - notes[index - 1]->startBeat) < 0.01) {
                run = 0;
                continue;
            }
            ++report.melodicIntervals;
            const auto interval = std::abs(notes[index]->pitch - notes[index - 1]->pitch);
            if (interval == 1 || interval == 2) {
                ++report.melodicStepwiseIntervals;
                ++run;
                report.maximumMelodicStepRun = std::max(report.maximumMelodicStepRun, run);
            } else {
                run = 0;
            }
        }
    }
    report.melodicStepwiseRatio = static_cast<double>(report.melodicStepwiseIntervals) /
        std::max<std::size_t>(1, report.melodicIntervals);
}

bool declaredFullSilence(const SongSection& section) {
    const auto text = lower(section.name + " " + section.function);
    if (text.find("no full silence") != std::string::npos ||
        text.find("without full silence") != std::string::npos ||
        text.find("sin silencio total") != std::string::npos) return false;
    return text.find("full silence") != std::string::npos ||
           text.find("complete silence") != std::string::npos ||
           text.find("silencio total") != std::string::npos;
}

void auditClubContinuity(const Pattern& pattern, const SongPlan& plan,
                         NarrativeScoreReport& report) {
    if (plan.productionLanguage.domain != ProductionDomain::ClubElectronic ||
        plan.percussionFreeIntent) return;
    auto drumRun = std::size_t{};
    auto lowEndRun = std::size_t{};
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        const auto end = start + plan.beatsPerBar;
        const auto section = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& item) {
            return bar >= item.startBar && bar < item.startBar + item.bars;
        });
        if (section != plan.sections.end() && declaredFullSilence(*section)) {
            drumRun = 0;
            lowEndRun = 0;
            continue;
        }
        const auto kick = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::CoreDrums && (note.pitch == 35 || note.pitch == 36) &&
                   note.startBeat >= start && note.startBeat < end;
        });
        const auto lowEnd = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return (note.voice == VoiceId::SubBass || note.voice == VoiceId::MovementBass) &&
                   note.startBeat < end - 0.001 && note.endBeat() > start + 0.001;
        });
        drumRun = kick ? 0 : drumRun + 1;
        lowEndRun = lowEnd ? 0 : lowEndRun + 1;
        report.maximumClubDrumGapBars = std::max(report.maximumClubDrumGapBars, drumRun);
        report.maximumClubLowEndGapBars = std::max(report.maximumClubLowEndGapBars, lowEndRun);
    }
}

struct ActEvidence {
    NarrativeStage stage{NarrativeStage::Transformation};
    bool audible{};
    double meanPitch{};
    double noteRate{};
    double activeParts{};
    std::vector<const NoteEvent*> melody;
};

ActEvidence evidenceFor(const Pattern& pattern, const SongPlan& plan,
                        const NarrativeAct& act) {
    ActEvidence result;
    result.stage = act.stage;
    const auto section = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& item) {
        return item.name == act.sectionName;
    });
    if (section == plan.sections.end()) return result;
    const auto start = section->startBar * plan.beatsPerBar;
    const auto end = (section->startBar + section->bars) * plan.beatsPerBar;
    std::set<std::uint16_t> parts;
    auto pitchTotal = 0.0;
    for (const auto& note : pattern.notes) {
        if (note.startBeat >= end || note.endBeat() <= start) continue;
        if (note.partId != 0) parts.insert(note.partId);
        if (note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody) {
            result.melody.push_back(&note);
            pitchTotal += note.pitch;
        }
    }
    std::sort(result.melody.begin(), result.melody.end(), [](const auto* left, const auto* right) {
        if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
        return left->pitch < right->pitch;
    });
    result.audible = result.melody.size() >= 3;
    result.meanPitch = result.melody.empty() ? 0.0 : pitchTotal / result.melody.size();
    result.noteRate = static_cast<double>(result.melody.size()) /
        std::max(1.0, static_cast<double>(section->bars));
    result.activeParts = static_cast<double>(parts.size());
    return result;
}

double contourClosure(const std::vector<const NoteEvent*>& premise,
                      const std::vector<const NoteEvent*>& resolution) {
    if (premise.size() < 3 || resolution.size() < 3) return 0.0;
    const auto count = std::min<std::size_t>(5, std::min(premise.size(), resolution.size())) - 1;
    auto matched = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto left = premise[index + 1]->pitch - premise[index]->pitch;
        const auto right = resolution[index + 1]->pitch - resolution[index]->pitch;
        if ((left == 0 && right == 0) || (left > 0 && right > 0) || (left < 0 && right < 0))
            matched += .65;
        matched += .35 * closeness(std::abs(left), std::abs(right), 5.0);
    }
    return matched / std::max<std::size_t>(1, count);
}

void auditNarrativeSpine(const Pattern& pattern, const SongPlan& plan,
                         NarrativeScoreReport& report) {
    if (plan.narrativeSpine.acts.empty()) return;
    std::vector<ActEvidence> evidence;
    evidence.reserve(plan.narrativeSpine.acts.size());
    for (const auto& act : plan.narrativeSpine.acts) {
        evidence.push_back(evidenceFor(pattern, plan, act));
        ++report.declaredNarrativeActs;
        if (evidence.back().audible) ++report.audibleNarrativeActs;
    }
    for (std::size_t index = 1; index < evidence.size(); ++index) {
        const auto& declaration = plan.narrativeSpine.acts[index];
        if (declaration.cause.empty() || declaration.consequence.empty()) continue;
        ++report.causalTransitions;
        if (!evidence[index - 1].audible || !evidence[index].audible) continue;
        const auto changed = std::abs(evidence[index].meanPitch - evidence[index - 1].meanPitch) >= 2.0 ||
            std::abs(evidence[index].activeParts - evidence[index - 1].activeParts) >= 1.0 ||
            std::abs(evidence[index].noteRate - evidence[index - 1].noteRate) >= .30;
        if (changed) ++report.realizedCausalTransitions;
    }
    const auto audibility = static_cast<double>(report.audibleNarrativeActs) /
        std::max<std::size_t>(1, report.declaredNarrativeActs);
    const auto consequence = static_cast<double>(report.realizedCausalTransitions) /
        std::max<std::size_t>(1, report.causalTransitions);
    report.causalNarrative = audibility * .55 + consequence * .45;

    const ActEvidence* premise = nullptr;
    const ActEvidence* climax = nullptr;
    const ActEvidence* resolution = nullptr;
    for (const auto& item : evidence) {
        if (item.stage == NarrativeStage::Premise && premise == nullptr) premise = &item;
        if (item.stage == NarrativeStage::Climax) climax = &item;
        if (item.stage == NarrativeStage::Resolution) resolution = &item;
    }
    std::set<std::uint16_t> protagonistParts;
    const auto protagonist = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& instrument) {
            return instrument.id == plan.narrativeSpine.protagonistInstrumentId;
        });
    if (protagonist != plan.instruments.end()) {
        const auto lane = protagonist->contentLaneId.empty() ? protagonist->id : protagonist->contentLaneId;
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& candidate = plan.instruments[index];
            const auto candidateLane = candidate.contentLaneId.empty()
                ? candidate.id : candidate.contentLaneId;
            if (candidateLane == lane)
                protagonistParts.insert(static_cast<std::uint16_t>(index + 1));
        }
    }
    const auto protagonistNote = [&](const NoteEvent& note) {
        return protagonistParts.empty() ? primaryVoice(note.voice) :
            protagonistParts.contains(note.partId);
    };

    const auto notesInAct = [&](NarrativeStage stage) {
        std::vector<const NoteEvent*> notes;
        const auto act = std::find_if(plan.narrativeSpine.acts.begin(), plan.narrativeSpine.acts.end(),
            [&](const auto& item) { return item.stage == stage; });
        const auto section = act == plan.narrativeSpine.acts.end() ? plan.sections.end() :
            std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& item) {
                return item.name == act->sectionName;
            });
        if (section == plan.sections.end()) return notes;
        const auto start = section->startBar * plan.beatsPerBar;
        const auto end = (section->startBar + section->bars) * plan.beatsPerBar;
        for (const auto& note : pattern.notes)
            if (protagonistNote(note) &&
                note.startBeat >= start && note.startBeat < end)
                notes.push_back(&note);
        std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
            return left->pitch < right->pitch;
        });
        return notes;
    };
    const auto premiseTheme = notesInAct(NarrativeStage::Premise);
    const auto totalBeats = plan.totalBars * plan.beatsPerBar;
    const auto codaStart = std::max(0.0, totalBeats - plan.beatsPerBar * 8.0);
    std::vector<const NoteEvent*> codaTheme;
    for (const auto& note : pattern.notes)
        if (protagonistNote(note) &&
            note.startBeat >= codaStart && note.startBeat < totalBeats)
            codaTheme.push_back(&note);
    std::sort(codaTheme.begin(), codaTheme.end(), [](const auto* left, const auto* right) {
        if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
        return left->pitch < right->pitch;
    });
    const auto protagonistCloses = codaTheme.size() >= 3 &&
        codaTheme.back()->startBeat >= totalBeats - plan.beatsPerBar * 2.0 &&
        positiveModulo(codaTheme.back()->pitch, 12) == plan.rootPitchClass;
    if (!premiseTheme.empty() && !codaTheme.empty())
        report.motifClosure = contourClosure(premiseTheme, codaTheme);
    if (!codaTheme.empty()) {
        const auto* last = codaTheme.back();
        const auto pitchClass = positiveModulo(last->pitch, 12);
        report.tonalClosure = pitchClass == plan.rootPitchClass ? 1.0 :
            pitchClass == positiveModulo(plan.rootPitchClass + 7, 12) ? .55 : 0.0;
        std::vector<double> durations;
        for (const auto* note : codaTheme) durations.push_back(note->durationBeats);
        std::sort(durations.begin(), durations.end());
        const auto median = durations[durations.size() / 2];
        report.tonalClosure = std::clamp(report.tonalClosure * .8 +
            (last->durationBeats >= median * 1.4 ? .2 : 0.0), 0.0, 1.0);
    }
    if (climax != nullptr && resolution != nullptr && climax->audible && resolution->audible) {
        report.registerRelease = std::clamp((climax->meanPitch - resolution->meanPitch + 1.0) / 8.0,
                                            0.0, 1.0);
        report.densityRelease = std::clamp((climax->activeParts - resolution->activeParts + .5) / 4.0,
                                           0.0, 1.0);
    }
    report.resolutionScore = report.motifClosure * .30 + report.tonalClosure * .35 +
        report.registerRelease * .20 + report.densityRelease * .15;
    report.narrativeSpineReady = report.causalNarrative >= .62 && report.resolutionScore >= .58 &&
        premise != nullptr && climax != nullptr && resolution != nullptr &&
        premise->audible && climax->audible && resolution->audible && protagonistCloses;
}

} // namespace

NarrativeScoreReport NarrativeScoreGate::audit(const Pattern& pattern, const SongPlan& plan) {
    NarrativeScoreReport report;
    report.active = plan.productionModeSource == "gpt_plan" ||
        (!plan.performanceScore.empty() && plan.productionModeSource != "local_fallback" &&
         plan.productionModeSource != "local_engine");
    report.totalNotes = pattern.notes.size();
    report.aiAuthoredNotes = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [](const auto& note) { return aiOrigin(note.origin); });
    report.aiAuthoredNoteRatio = static_cast<double>(report.aiAuthoredNotes) /
        std::max<std::size_t>(1, report.totalNotes);
    for (const auto& note : pattern.notes) {
        if (note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody) {
            ++report.foregroundNotes;
            if (aiOrigin(note.origin)) ++report.aiAuthoredForegroundNotes;
        }
        if (note.voice == VoiceId::MovementBass) {
            ++report.movementBassNotes;
            if (aiOrigin(note.origin)) ++report.aiAuthoredMovementBassNotes;
        }
    }
    report.foregroundAiAuthorshipRatio = static_cast<double>(report.aiAuthoredForegroundNotes) /
        std::max<std::size_t>(1, report.foregroundNotes);
    report.movementBassAiAuthorshipRatio = static_cast<double>(report.aiAuthoredMovementBassNotes) /
        std::max<std::size_t>(1, report.movementBassNotes);

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
        // Similarity near one is not automatically excellent. Across a long form it
        // usually means that the cell was copied instead of remembered and developed.
        if (best >= 0.985) ++report.literalThematicReturns;
    }
    report.comparableThematicReturns = comparable;
    report.audibleThematicSimilarity = comparable == 0 ? 0.0 :
        similarityTotal / static_cast<double>(comparable);
    report.thematicRecallRatio = comparable == 0 ? 0.0 :
        static_cast<double>(report.audiblyRecurringThematicWindows) /
        static_cast<double>(comparable);
    report.literalThematicReturnRatio = comparable == 0 ? 0.0 :
        static_cast<double>(report.literalThematicReturns) /
        static_cast<double>(comparable);
    // A human long-form theme needs both memory and consequence. Roughly one third of
    // its returns may be literal anchors; the rest should answer, fragment, displace or
    // cadence. Short ideas are not penalised before they have room to develop.
    report.thematicDevelopment = comparable < 3 ? 1.0 : std::clamp(
        1.0 - std::max(0.0, report.literalThematicReturnRatio - 0.35) / 0.65,
        0.0, 1.0);

    auto primaryAvailable = 0.0;
    auto primaryAuthored = 0.0;
    auto grooveAvailable = 0.0;
    auto grooveAuthored = 0.0;
    auto movementBassAvailable = 0.0;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto length = section.bars * plan.beatsPerBar;
        const auto expectsForeground = std::any_of(section.activeVoices.begin(),
            section.activeVoices.end(), [](const auto voice) { return primaryVoice(voice); });
        // Presence is a phrase-window contract. Three authored attacks inside an
        // eight-bar window count as one narrative appearance; rests and breath inside
        // that phrase are preserved instead of being penalised as missing duration.
        if (expectsForeground) {
            for (auto localBar = 0; localBar < section.bars; localBar += 8) {
                ++primaryAvailable;
                const auto start = (section.startBar + localBar) * plan.beatsPerBar;
                const auto end = std::min((section.startBar + section.bars) * plan.beatsPerBar,
                                          start + plan.beatsPerBar * 8.0);
                const auto notes = std::count_if(pattern.notes.begin(), pattern.notes.end(),
                    [&](const auto& note) {
                        return primaryVoice(note.voice) && aiOrigin(note.origin) &&
                            note.startBeat >= start && note.startBeat < end;
                    });
                if (notes >= 3) ++primaryAuthored;
            }
        }
        for (const auto voice : section.activeVoices) {
            if (grooveVoice(voice)) {
                grooveAvailable += length;
                grooveAuthored += unionLength(authoredSpans[{static_cast<int>(sectionIndex), voice}]);
            }
            if (voice == VoiceId::MovementBass) movementBassAvailable += length;
        }
    }
    report.primaryVoiceCoverage = primaryAvailable > 0.0 ? primaryAuthored / primaryAvailable : 1.0;
    report.grooveAuthorshipCoverage = grooveAvailable > 0.0 ? grooveAuthored / grooveAvailable : 1.0;
    report.foregroundExpected = primaryAvailable >= plan.beatsPerBar * 4.0;
    report.movementBassExpected = movementBassAvailable >= plan.beatsPerBar * 4.0;

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
    auditMelodicSpeech(pattern, plan, report);
    auditClubContinuity(pattern, plan, report);
    auditNarrativeSpine(pattern, plan, report);

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
    if (plan.percussionFreeIntent)
        report.rhythmicDevelopment = 1.0; // Not applicable; motion is graded by soundscape.

    const auto audibleLineage = std::clamp(
        (report.audibleThematicSimilarity - 0.55) / 0.25, 0.0, 1.0);
    const auto melodicSpeech = std::clamp(1.0 - std::max(0.0,
        report.melodicStepwiseRatio - 0.62) / 0.38 -
        std::max(0.0, static_cast<double>(report.maximumMelodicStepRun) - 4.0) * 0.08,
        0.0, 1.0);
    const auto clubContinuity = plan.productionLanguage.domain != ProductionDomain::ClubElectronic ||
        plan.percussionFreeIntent
        ? 1.0 : std::clamp(1.0 -
            std::max(0.0, static_cast<double>(report.maximumClubDrumGapBars) - 12.0) / 20.0 -
            std::max(0.0, static_cast<double>(report.maximumClubLowEndGapBars) - 12.0) / 24.0,
            0.0, 1.0);
    report.score = std::clamp(report.primaryVoiceCoverage * 0.10 +
        report.foregroundAiAuthorshipRatio * 0.18 +
        report.movementBassAiAuthorshipRatio * 0.11 +
        report.grooveAuthorshipCoverage * 0.08 +
        report.thematicRecallRatio * 0.07 + audibleLineage * 0.05 +
        report.thematicDevelopment * 0.05 +
        report.bassPhraseContinuity * 0.07 + report.densityControl * 0.05 +
        report.harmonicDirection * 0.04 + report.rhythmicDevelopment * 0.03 +
        melodicSpeech * 0.04 + clubContinuity * 0.02 +
        report.causalNarrative * 0.07 + report.resolutionScore * 0.04, 0.0, 1.0);
    if (report.active && report.primaryVoiceCoverage < 0.65) report.issues.push_back("insufficient_ai_phrase_coverage");
    if (report.active && report.foregroundExpected && report.foregroundNotes < 8)
        report.issues.push_back("ai_foreground_missing");
    if (report.active && report.foregroundNotes >= 8 && report.foregroundAiAuthorshipRatio < 0.85)
        report.issues.push_back("procedural_foreground_dominates");
    if (report.active && report.movementBassExpected && report.movementBassNotes < 8)
        report.issues.push_back("ai_movement_bass_missing");
    if (report.active && report.movementBassNotes >= 8 && report.movementBassAiAuthorshipRatio < 0.75)
        report.issues.push_back("movement_bass_not_ai_authored");
    if (report.active && plan.productionLanguage.domain == ProductionDomain::ClubElectronic &&
        !plan.percussionFreeIntent &&
        report.grooveAuthorshipCoverage < 0.45)
        report.issues.push_back("groove_structure_not_ai_authored");
    if (report.active && report.audibleThematicWindows >= 3 && report.thematicRecallRatio < 0.40)
        report.issues.push_back("weak_long_range_theme_memory");
    if (report.active && report.audibleThematicWindows >= 3 && report.audibleThematicSimilarity < 0.66)
        report.issues.push_back("theme_labels_without_audible_lineage");
    if (report.active && report.comparableThematicReturns >= 4 &&
        report.literalThematicReturnRatio > 0.70)
        report.issues.push_back("literal_theme_copy_without_development");
    if (report.bassWindows >= 3 && report.bassPhraseContinuity < 0.60)
        report.issues.push_back("fragmented_movement_bass");
    if (report.melodicIntervals >= 8 && (report.melodicStepwiseRatio > 0.78 ||
        report.maximumMelodicStepRun > 5))
        report.issues.push_back("scalar_melody_without_speech");
    if (plan.productionLanguage.domain == ProductionDomain::ClubElectronic && !plan.percussionFreeIntent &&
        report.maximumClubDrumGapBars > 16)
        report.issues.push_back("club_pulse_absent_too_long");
    if (plan.productionLanguage.domain == ProductionDomain::ClubElectronic && !plan.percussionFreeIntent &&
        report.maximumClubLowEndGapBars > 16)
        report.issues.push_back("low_end_narrative_absent_too_long");
    if (report.densityControl < 0.82) report.issues.push_back("overcrowded_arrangement");
    if (report.harmonicDirection < 0.70) report.issues.push_back("weak_harmonic_direction");
    if (!plan.percussionFreeIntent && report.rhythmicDevelopment < 0.45)
        report.issues.push_back("undeveloped_rhythm_narrative");
    if (report.active && !report.narrativeSpineReady) {
        if (report.causalNarrative < .62) report.issues.push_back("narrative_events_lack_audible_consequence");
        if (report.resolutionScore < .58) report.issues.push_back("ending_does_not_repay_harmonic_debt");
    }
    const auto memoryReady = report.audibleThematicWindows < 3 ||
        (report.thematicRecallRatio >= 0.40 && report.audibleThematicSimilarity >= 0.66);
    const auto developmentReady = report.comparableThematicReturns < 4 ||
        (report.literalThematicReturnRatio <= 0.70 && report.thematicDevelopment >= 0.55);
    const auto bassReady = report.bassPhrases < 4 || report.bassPhraseContinuity >= 0.60;
    const auto foregroundReady = !report.foregroundExpected ||
        (report.foregroundNotes >= 8 && report.foregroundAiAuthorshipRatio >= 0.85);
    const auto movementBassReady = !report.movementBassExpected ||
        (report.movementBassNotes >= 8 && report.movementBassAiAuthorshipRatio >= 0.75);
    const auto clubReady = plan.productionLanguage.domain != ProductionDomain::ClubElectronic ||
        plan.percussionFreeIntent ||
        (report.grooveAuthorshipCoverage >= 0.45 && report.maximumClubDrumGapBars <= 16 &&
         report.maximumClubLowEndGapBars <= 16);
    report.creativeReady = !report.active || (report.primaryVoiceCoverage >= 0.65 &&
        foregroundReady && movementBassReady && memoryReady && developmentReady && bassReady &&
        clubReady && report.narrativeSpineReady && report.densityControl >= 0.82 && report.maximumMelodicStepRun <= 5 &&
        report.score >= 0.76);
    return report;
}

void NarrativeScoreGate::stamp(Pattern& pattern, const NarrativeScoreReport& report) {
    pattern.narrativeAuditPerformed = true;
    pattern.narrativeScore = report.score;
    pattern.creativeScore = report.score;
    pattern.creativeReady = report.creativeReady;
    pattern.aiAuthoredNoteRatio = report.aiAuthoredNoteRatio;
    pattern.primaryVoiceAuthorshipCoverage = report.primaryVoiceCoverage;
    pattern.foregroundAiAuthorshipRatio = report.foregroundAiAuthorshipRatio;
    pattern.movementBassAiAuthorshipRatio = report.movementBassAiAuthorshipRatio;
    pattern.grooveAuthorshipCoverage = report.grooveAuthorshipCoverage;
    pattern.thematicRecallRatio = report.thematicRecallRatio;
    pattern.audibleThematicSimilarity = report.audibleThematicSimilarity;
    pattern.literalThematicReturnRatio = report.literalThematicReturnRatio;
    pattern.thematicDevelopment = report.thematicDevelopment;
    pattern.bassPhraseContinuity = report.bassPhraseContinuity;
    pattern.melodicStepwiseRatio = report.melodicStepwiseRatio;
    pattern.maximumMelodicStepRun = report.maximumMelodicStepRun;
    pattern.maximumClubDrumGapBars = report.maximumClubDrumGapBars;
    pattern.maximumClubLowEndGapBars = report.maximumClubLowEndGapBars;
    pattern.densityControl = report.densityControl;
    pattern.peakActiveVoices = report.peakActiveVoices;
    pattern.causalNarrativeScore = report.causalNarrative;
    pattern.narrativeResolutionScore = report.resolutionScore;
    pattern.narrativeSpineReady = report.narrativeSpineReady;
    pattern.narrativeIssues = report.issues;
    if (report.thematicRecallRatio >= 0.40 && report.audibleThematicSimilarity >= 0.66) {
        pattern.productionIssues.erase(std::remove(
            pattern.productionIssues.begin(), pattern.productionIssues.end(),
            "warning:thematic_identity_needs_stronger_recall"),
            pattern.productionIssues.end());
    }
    for (const auto& issue : report.issues)
        pattern.productionIssues.push_back("narrative:" + issue);
    if (!report.creativeReady)
        pattern.productionIssues.push_back("creative:soul_gate_needs_revision");
    // Narrative quality selects and revises candidates, but it must never make a valid
    // composition disappear from the UI. productionReady remains the MIDI-integrity
    // contract; narrative defects are published as explicit critic diagnostics.
}

} // namespace pulso
