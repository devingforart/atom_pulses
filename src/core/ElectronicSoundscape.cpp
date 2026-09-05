#include "ElectronicSoundscape.h"

#include "OrchestrationScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace pulso {
namespace {

bool electronic(const SongPlan& plan) noexcept {
    return plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
}

SoundscapeLayerKind inferredKind(const InstrumentAssignment& part) noexcept {
    if (part.sourceVoice == VoiceId::Transitions || part.orchestralFunction == "transition")
        return SoundscapeLayerKind::Transition;
    if (part.sourceVoice == VoiceId::Atmosphere || part.instrumentId == "ambient_texture" ||
        part.instrumentId == "granular_pad" || part.instrumentId == "spectral_drone" ||
        part.instrumentId == "shimmer_tail")
        return SoundscapeLayerKind::Environment;
    return SoundscapeLayerKind::Voice;
}

SoundscapeTimeScale inferredTimeScale(const InstrumentAssignment& part,
                                      SoundscapeLayerKind kind) noexcept {
    if (kind == SoundscapeLayerKind::Transition || kind == SoundscapeLayerKind::OneShot)
        return SoundscapeTimeScale::Event;
    if (kind == SoundscapeLayerKind::Environment || part.articulation == "sustained" ||
        part.articulation == "swelling") return SoundscapeTimeScale::Slow;
    if (part.articulation == "staccato" || part.articulation == "ostinato")
        return SoundscapeTimeScale::Fast;
    return SoundscapeTimeScale::Medium;
}

std::size_t activeBars(const std::vector<const NoteEvent*>& notes, double beatsPerBar,
                       int totalBars) {
    std::set<int> bars;
    for (const auto* note : notes) {
        const auto first = std::clamp(static_cast<int>(std::floor(note->startBeat / beatsPerBar)),
                                      0, std::max(0, totalBars - 1));
        const auto lastBeat = std::max(note->startBeat, note->endBeat() - 0.0001);
        const auto last = std::clamp(static_cast<int>(std::floor(lastBeat / beatsPerBar)),
                                     first, std::max(first, totalBars - 1));
        for (auto bar = first; bar <= last; ++bar) bars.insert(bar);
    }
    return bars.size();
}

std::size_t phraseCount(std::vector<const NoteEvent*> notes, double beatsPerBar) {
    if (notes.empty()) return 0;
    std::sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) {
        return a->startBeat < b->startBeat;
    });
    auto phrases = std::size_t{1};
    auto soundingUntil = notes.front()->endBeat();
    for (std::size_t i = 1; i < notes.size(); ++i) {
        if (notes[i]->startBeat - soundingUntil >= beatsPerBar * 0.75) ++phrases;
        soundingUntil = std::max(soundingUntil, notes[i]->endBeat());
    }
    return phrases;
}

std::size_t longestStaticRun(const std::vector<const NoteEvent*>& notes,
                             double beatsPerBar, int totalBars) {
    std::vector<std::vector<const NoteEvent*>> byBar(static_cast<std::size_t>(std::max(0, totalBars)));
    for (const auto* note : notes) {
        const auto bar = static_cast<int>(std::floor(note->startBeat / beatsPerBar));
        if (bar >= 0 && bar < totalBars) byBar[static_cast<std::size_t>(bar)].push_back(note);
    }
    auto signature = [&](const std::vector<const NoteEvent*>& barNotes, int bar) {
        std::string result;
        for (const auto* note : barNotes) {
            const auto onset = static_cast<int>(std::lround((note->startBeat - bar * beatsPerBar) * 16.0));
            const auto duration = static_cast<int>(std::lround(note->durationBeats * 16.0));
            result += std::to_string(onset) + ":" + std::to_string(duration) + ":" +
                      std::to_string(positiveModulo(note->pitch, 12)) + ";";
        }
        return result;
    };
    std::string previous;
    auto run = std::size_t{};
    auto longest = std::size_t{};
    for (auto bar = 0; bar < totalBars; ++bar) {
        auto current = signature(byBar[static_cast<std::size_t>(bar)], bar);
        if (current.empty()) { previous.clear(); run = 0; continue; }
        if (current == previous) ++run;
        else { previous = std::move(current); run = 1; }
        longest = std::max(longest, run);
    }
    return longest;
}

} // namespace

std::string_view soundscapeLayerKindKey(SoundscapeLayerKind kind) noexcept {
    switch (kind) {
        case SoundscapeLayerKind::Environment: return "environment";
        case SoundscapeLayerKind::Transition: return "transition";
        case SoundscapeLayerKind::OneShot: return "one_shot";
        case SoundscapeLayerKind::Voice: return "voice";
    }
    return "voice";
}

std::string_view soundscapeTimeScaleKey(SoundscapeTimeScale scale) noexcept {
    switch (scale) {
        case SoundscapeTimeScale::Fast: return "fast";
        case SoundscapeTimeScale::Slow: return "slow";
        case SoundscapeTimeScale::Event: return "event";
        case SoundscapeTimeScale::Medium: return "medium";
    }
    return "medium";
}

SoundscapeLayerKind soundscapeLayerKindFromKey(std::string_view key) noexcept {
    if (key == "environment") return SoundscapeLayerKind::Environment;
    if (key == "transition") return SoundscapeLayerKind::Transition;
    if (key == "one_shot") return SoundscapeLayerKind::OneShot;
    return SoundscapeLayerKind::Voice;
}

SoundscapeTimeScale soundscapeTimeScaleFromKey(std::string_view key) noexcept {
    if (key == "fast") return SoundscapeTimeScale::Fast;
    if (key == "slow") return SoundscapeTimeScale::Slow;
    if (key == "event") return SoundscapeTimeScale::Event;
    return SoundscapeTimeScale::Medium;
}

void ElectronicSoundscapeDirector::normalize(SongPlan& plan) {
    plan.soundscape.active = plan.soundscape.active || electronic(plan);
    plan.percussionFreeIntent = plan.percussionFreeIntent || plan.soundscape.percussionFree;
    plan.soundscape.percussionFree = plan.percussionFreeIntent;
    plan.soundscape.targetMedianActiveLayers = std::clamp(
        plan.soundscape.targetMedianActiveLayers, plan.percussionFreeIntent ? 2.5 : 2.0, 6.0);
    if (!plan.soundscape.active) return;

    std::set<std::string> accepted;
    plan.soundscape.layers.erase(std::remove_if(plan.soundscape.layers.begin(),
        plan.soundscape.layers.end(), [&](auto& layer) {
            const auto assignment = std::find_if(plan.instruments.begin(), plan.instruments.end(),
                [&](const auto& item) { return item.id == layer.instrumentId; });
            if (assignment == plan.instruments.end() ||
                isVoiceInFamily(assignment->sourceVoice, VoiceFamily::Rhythm) ||
                !accepted.insert(layer.instrumentId).second)
                return true;
            layer.minimumActiveBars = std::clamp(layer.minimumActiveBars, 1, plan.totalBars);
            layer.minimumPhrases = std::clamp(layer.minimumPhrases, 1, 16);
            layer.maximumStaticBars = std::clamp(layer.maximumStaticBars, 1, 32);
            layer.foregroundDepth = std::clamp(layer.foregroundDepth, 0.0, 1.0);
            switch (layer.kind) {
                case SoundscapeLayerKind::Voice:
                    layer.minimumActiveBars = std::max(layer.minimumActiveBars,
                        std::min(8, plan.totalBars));
                    layer.minimumPhrases = std::max(layer.minimumPhrases, 2);
                    layer.maximumStaticBars = std::min(layer.maximumStaticBars, 8);
                    break;
                case SoundscapeLayerKind::Environment:
                    layer.minimumActiveBars = std::max(layer.minimumActiveBars,
                        std::min(6, plan.totalBars));
                    layer.maximumStaticBars = std::min(layer.maximumStaticBars, 12);
                    break;
                case SoundscapeLayerKind::Transition:
                    layer.minimumPhrases = std::max(layer.minimumPhrases, 2);
                    layer.maximumStaticBars = std::min(layer.maximumStaticBars, 8);
                    break;
                case SoundscapeLayerKind::OneShot:
                    layer.minimumActiveBars = 1;
                    layer.minimumPhrases = 1;
                    layer.maximumStaticBars = 1;
                    break;
            }
            return false;
        }), plan.soundscape.layers.end());

    // Authored layers remain authoritative, but structural instruments added to satisfy
    // the score contract must also receive an auditable layer contract. This does not
    // invent notes or replace any authored description.
    for (const auto& assignment : plan.instruments) {
        if (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Rhythm) ||
            accepted.contains(assignment.id)) continue;
        const auto kind = inferredKind(assignment);
        SoundscapeLayerPlan layer;
        layer.instrumentId = assignment.id;
        layer.kind = kind;
        layer.timeScale = inferredTimeScale(assignment, kind);
        layer.narrativeRole = assignment.role;
        layer.relationship = "Supports the shared harmony and sectional narrative";
        layer.evolution = "Changes register, density, articulation or spectral motion across returns";
        layer.minimumActiveBars = kind == SoundscapeLayerKind::Voice ? std::min(8, plan.totalBars) :
            kind == SoundscapeLayerKind::Environment ? std::min(6, plan.totalBars) : 1;
        layer.minimumPhrases = kind == SoundscapeLayerKind::Voice ? 2 : 1;
        layer.maximumStaticBars = kind == SoundscapeLayerKind::Environment ? 12 : 8;
        layer.foregroundDepth = assignment.prominence;
        plan.soundscape.layers.push_back(std::move(layer));
    }
}

ElectronicSoundscapeReport ElectronicSoundscapeDirector::audit(const Pattern& pattern,
                                                                 const SongPlan& plan) {
    ElectronicSoundscapeReport report;
    report.active = plan.soundscape.active && electronic(plan);
    report.percussionFree = plan.percussionFreeIntent;
    if (!report.active) return report;
    report.declaredLayers = plan.soundscape.layers.size();

    std::map<std::string, std::uint16_t> partByAssignment;
    for (std::size_t i = 0; i < plan.instruments.size() && i < pattern.parts.size(); ++i)
        partByAssignment.emplace(plan.instruments[i].id, pattern.parts[i].id);
    std::map<std::uint16_t, std::vector<const NoteEvent*>> notesByPart;
    for (const auto& note : pattern.notes)
        if (note.partId != 0) notesByPart[note.partId].push_back(&note);

    std::set<std::uint16_t> declaredParts;
    for (const auto& layer : plan.soundscape.layers) {
        const auto partFound = partByAssignment.find(layer.instrumentId);
        if (partFound == partByAssignment.end()) continue;
        declaredParts.insert(partFound->second);
        const auto notesFound = notesByPart.find(partFound->second);
        if (notesFound == notesByPart.end() || notesFound->second.empty()) {
            if (layer.kind == SoundscapeLayerKind::Voice) ++report.underdevelopedVoices;
            else if (layer.kind == SoundscapeLayerKind::Environment) ++report.underdevelopedEnvironments;
            else ++report.missingTransitionEvents;
            continue;
        }
        ++report.materializedLayers;
        const auto& notes = notesFound->second;
        const auto bars = activeBars(notes, plan.beatsPerBar, plan.totalBars);
        const auto phrases = phraseCount(notes, plan.beatsPerBar);
        const auto staticRun = longestStaticRun(notes, plan.beatsPerBar, plan.totalBars);
        const auto staticEnough = staticRun <= static_cast<std::size_t>(layer.maximumStaticBars);
        bool meaningful{};
        switch (layer.kind) {
            case SoundscapeLayerKind::Voice:
                meaningful = notes.size() >= 6 && bars >= static_cast<std::size_t>(layer.minimumActiveBars) &&
                    phrases >= static_cast<std::size_t>(layer.minimumPhrases) && staticEnough;
                if (!meaningful) ++report.underdevelopedVoices;
                break;
            case SoundscapeLayerKind::Environment:
                meaningful = bars >= static_cast<std::size_t>(layer.minimumActiveBars) &&
                    phrases >= static_cast<std::size_t>(layer.minimumPhrases) && staticEnough;
                if (!meaningful) ++report.underdevelopedEnvironments;
                break;
            case SoundscapeLayerKind::Transition:
                meaningful = notes.size() >= static_cast<std::size_t>(layer.minimumPhrases);
                if (!meaningful) ++report.missingTransitionEvents;
                break;
            case SoundscapeLayerKind::OneShot:
                meaningful = !notes.empty();
                break;
        }
        if (!staticEnough) ++report.staticLayerRuns;
        if (meaningful) ++report.meaningfulLayers;
    }

    for (const auto& [partId, notes] : notesByPart) {
        const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(),
            [&](const auto& item) { return item.id == partId; });
        if (!notes.empty() && !declaredParts.contains(partId) &&
            part != pattern.parts.end() && part->department != ScoreDepartment::Rhythm)
            ++report.undeclaredPopulatedParts;
    }

    std::vector<std::size_t> activeByBar;
    activeByBar.reserve(static_cast<std::size_t>(plan.totalBars));
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        std::set<std::uint16_t> active;
        const auto start = bar * plan.beatsPerBar;
        const auto end = start + plan.beatsPerBar;
        for (const auto& note : pattern.notes)
            if (note.partId != 0 && note.startBeat < end && note.endBeat() > start)
                active.insert(note.partId);
        activeByBar.push_back(active.size());
    }
    if (!activeByBar.empty()) {
        const auto middle = activeByBar.begin() + static_cast<std::ptrdiff_t>(activeByBar.size() / 2);
        std::nth_element(activeByBar.begin(), middle, activeByBar.end());
        report.medianActiveLayers = static_cast<double>(*middle);
    }
    report.meaningfulCoverage = static_cast<double>(report.meaningfulLayers) /
        std::max<std::size_t>(1, report.declaredLayers);
    report.independentMusicalLines = pattern.independentMusicalLines;
    report.meaningfulMusicalLines = pattern.meaningfulMusicalLines;
    report.protagonistPhraseWindows = pattern.protagonistPhraseWindows;
    report.arpeggioNoteCount = pattern.arpeggioNoteCount;
    report.dialogueMusicalLines = pattern.dialogueMusicalLines;
    report.harmonicFloorCoverage = pattern.harmonicFloorCoverage;
    report.medianHarmonicFloorLayers = pattern.medianHarmonicFloorLayers;
    const auto densityFit = std::clamp(1.0 - std::abs(report.medianActiveLayers -
        plan.soundscape.targetMedianActiveLayers) / 4.0, 0.0, 1.0);
    const auto declarationFit = 1.0 - std::min(1.0,
        static_cast<double>(report.undeclaredPopulatedParts) /
        std::max<std::size_t>(1, notesByPart.size()));
    const auto lineFit = report.independentMusicalLines == 0 ? 0.0 : std::clamp(
        static_cast<double>(report.meaningfulMusicalLines) /
        static_cast<double>(report.independentMusicalLines), 0.0, 1.0);
    const auto floorFit = std::clamp(report.harmonicFloorCoverage, 0.0, 1.0);
    const auto narrativeFit = (report.protagonistPhraseWindows >= std::max<std::size_t>(3, plan.totalBars / 24) &&
                               report.arpeggioNoteCount >= 32 && report.dialogueMusicalLines >= 1) ? 1.0 : .35;
    report.score = std::clamp(report.meaningfulCoverage * .42 + densityFit * .12 +
                              declarationFit * .08 + lineFit * .16 + floorFit * .14 +
                              narrativeFit * .08, 0.0, 1.0);
    report.ready = report.declaredLayers >= (report.percussionFree ? 10U : 6U) &&
        report.meaningfulCoverage >= .78 && report.underdevelopedVoices == 0 &&
        report.underdevelopedEnvironments == 0 && report.missingTransitionEvents == 0 &&
        report.staticLayerRuns == 0 && report.undeclaredPopulatedParts == 0 && densityFit >= .75 &&
        lineFit >= .75 && report.harmonicFloorCoverage >= .80 &&
        report.medianHarmonicFloorLayers >= 2.0 &&
        report.protagonistPhraseWindows >= std::max<std::size_t>(3, plan.totalBars / 24) &&
        report.arpeggioNoteCount >= 32 && report.dialogueMusicalLines >= 1;
    if (report.declaredLayers < (report.percussionFree ? 10U : 6U))
        report.issues.push_back("electronic_scene_has_too_few_declared_layers");
    if (report.underdevelopedVoices > 0)
        report.issues.push_back("soundscape_voices_are_only_token_tracks");
    if (report.underdevelopedEnvironments > 0)
        report.issues.push_back("environments_lack_sectional_evolution");
    if (report.missingTransitionEvents > 0)
        report.issues.push_back("transition_narrative_is_not_materialized");
    if (report.staticLayerRuns > 0)
        report.issues.push_back("electronic_layers_repeat_without_evolution");
    if (report.undeclaredPopulatedParts > 0)
        report.issues.push_back("populated_electronic_parts_lack_soundscape_contracts");
    if (densityFit < .75)
        report.issues.push_back("sectional_fabric_misses_declared_layer_density");
    if (lineFit < .75)
        report.issues.push_back("instrument_count_exceeds_independent_musical_content");
    if (report.harmonicFloorCoverage < .80 || report.medianHarmonicFloorLayers < 2.0)
        report.issues.push_back("hypnotic_harmonic_floor_is_not_continuous");
    if (report.protagonistPhraseWindows < std::max<std::size_t>(3, plan.totalBars / 24))
        report.issues.push_back("primary_speaker_has_no_complete_narrative");
    if (report.arpeggioNoteCount < 32)
        report.issues.push_back("electronic_arpeggio_is_not_materialized");
    if (report.dialogueMusicalLines < 1)
        report.issues.push_back("melodic_dialogue_is_not_materialized");
    return report;
}

void ElectronicSoundscapeDirector::stamp(Pattern& pattern,
                                           const ElectronicSoundscapeReport& report) {
    pattern.soundscapeAuditPerformed = report.active;
    pattern.soundscapeReady = report.ready;
    pattern.soundscapeScore = report.score;
    pattern.declaredSoundscapeLayers = report.declaredLayers;
    pattern.meaningfulSoundscapeLayers = report.meaningfulLayers;
    pattern.underdevelopedSoundscapeLayers = report.declaredLayers - report.meaningfulLayers;
    pattern.medianActiveSoundscapeLayers = report.medianActiveLayers;
    pattern.independentMusicalLines = report.independentMusicalLines;
    pattern.meaningfulMusicalLines = report.meaningfulMusicalLines;
    pattern.protagonistPhraseWindows = report.protagonistPhraseWindows;
    pattern.arpeggioNoteCount = report.arpeggioNoteCount;
    pattern.dialogueMusicalLines = report.dialogueMusicalLines;
    pattern.harmonicFloorCoverage = report.harmonicFloorCoverage;
    pattern.medianHarmonicFloorLayers = report.medianHarmonicFloorLayers;
    if (!report.active) return;
    for (const auto& issue : report.issues)
        pattern.productionIssues.push_back("soundscape:" + issue);
    pattern.creativeReady = pattern.creativeReady && report.ready;
    pattern.creativeScore = std::clamp(pattern.creativeScore * .72 + report.score * .28, 0.0, 1.0);
    if (!report.ready)
        pattern.productionIssues.push_back("creative:electronic_soundscape_needs_revision");
}

} // namespace pulso
