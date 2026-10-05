#include "SovereignScoreRenderer.h"

#include "ArrangementDensityPlanner.h"
#include "ElectronicCompositionFabric.h"
#include "ElectronicProductionDirector.h"
#include "ElectronicSoundscape.h"
#include "MusicalCritic.h"
#include "NarrativeScore.h"
#include "OrchestrationScore.h"
#include "PerformanceScore.h"
#include "ProductionPolish.h"
#include "TonalContract.h"
#include "TrackViability.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pulso {
namespace {

std::vector<HarmonicWindow> authoredHarmony(const SongPlan& plan, double endBeat) {
    std::vector<HarmonicWindow> windows;
    for (const auto& section : plan.sections) {
        for (const auto& event : section.harmonicEvents) {
            const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                [&](const auto& candidate) { return candidate.id == event.chordId; });
            if (chord == plan.chordPalette.end()) continue;
            const auto start = (section.startBar + event.barOffset) * plan.beatsPerBar +
                event.beatOffset;
            if (start < 0.0 || start >= endBeat) continue;
            windows.push_back({start, endBeat, chord->rootPitchClass,
                chord->bassPitchClass, chord->pitchClasses, chord->function,
                chord->voicing, chord->tension, chord->id, chord->label});
        }
    }
    std::stable_sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) {
        return a.startBeat < b.startBeat;
    });
    for (std::size_t index = 0; index + 1 < windows.size(); ++index)
        windows[index].endBeat = std::max(windows[index].startBeat + .01,
            windows[index + 1].startBeat);
    return windows;
}

InstrumentPart authoredPart(const InstrumentAssignment& assignment, std::size_t index) {
    InstrumentPart part;
    part.id = static_cast<std::uint16_t>(index + 1);
    part.catalogId = assignment.instrumentId;
    part.name = assignment.name;
    part.sourceVoice = assignment.sourceVoice;
    part.role = assignment.role;
    part.minimumPitch = assignment.minimumPitch;
    part.maximumPitch = assignment.maximumPitch;
    part.prominence = assignment.prominence;
    part.orchestralFunction = assignment.orchestralFunction;
    part.articulation = assignment.articulation;
    part.divisiVoices = assignment.divisiVoices;
    part.liveDevice = assignment.liveDevice;
    part.livePresetIntent = assignment.livePresetIntent;
    part.timbre = assignment.timbre;
    part.contentLaneId = assignment.contentLaneId;
    part.lineRelationship = assignment.lineRelationship;
    if (const auto* definition = instrumentDefinition(assignment.instrumentId))
        part.department = definition->department;
    else if (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Rhythm))
        part.department = ScoreDepartment::Rhythm;
    else if (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Melodic))
        part.department = ScoreDepartment::Melody;
    else part.department = ScoreDepartment::Harmony;
    return part;
}

} // namespace

Pattern SovereignScoreRenderer::render(const SongPlan& plan,
                                      const SongComposer::ProgressCallback& progress,
                                      CompositionRenderReport* report) {
    Pattern song;
    song.lengthBeats = plan.totalBars * plan.beatsPerBar;
    song.seed = plan.seed;
    song.soundWorld = plan.timbrePalette.description + "; " + plan.timbrePalette.material +
        "; " + plan.timbrePalette.space;
    song.soundWarmth = plan.timbrePalette.warmth;
    song.soundBrightness = plan.timbrePalette.brightness;
    song.acousticElectronicBalance = plan.timbrePalette.acousticElectronicBalance;
    song.productionDomain = plan.productionLanguage.domain == ProductionDomain::ClubElectronic
        ? "club_electronic" : plan.productionLanguage.domain == ProductionDomain::Hybrid
        ? "hybrid" : plan.productionLanguage.domain == ProductionDomain::Orchestral
        ? "orchestral" : "adaptive";
    song.productionModeSource = "ai_sovereign_local";
    song.percussionFreeArrangement = plan.percussionFreeIntent;
    song.soundscapeScene = plan.soundscape.scene;
    song.soundscapeSpatialNarrative = plan.soundscape.spatialNarrative;
    song.parts.reserve(plan.instruments.size());
    for (std::size_t index = 0; index < plan.instruments.size(); ++index)
        song.parts.push_back(authoredPart(plan.instruments[index], index));

    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        song.markers.push_back({section.startBar * plan.beatsPerBar, section.name});
        Pattern chunk;
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        PerformanceScoreEngine::replaceChunk(chunk, plan.performanceScore,
            static_cast<int>(index), 0.0, sectionBeats, plan.instruments, true);
        const auto offset = section.startBar * plan.beatsPerBar;
        for (auto note : chunk.notes) {
            note.startBeat += offset;
            song.notes.push_back(std::move(note));
        }
        for (auto control : chunk.controls) {
            control.beat += offset;
            song.controls.push_back(std::move(control));
        }
        if (progress) progress(index + 1, plan.sections.size(), section);
    }
    std::stable_sort(song.notes.begin(), song.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        if (a.partId != b.partId) return a.partId < b.partId;
        return a.pitch < b.pitch;
    });

    // Audits stamp metadata only. No repair, density filler, silence surgery,
    // voice reassignment, quantization or note-bound expression is applied here.
    const auto harmony = authoredHarmony(plan, song.lengthBeats);
    TonalRepairReport tonal;
    tonal.before = tonal.after = auditTonalContract(song, plan.rootPitchClass,
        plan.scale, plan.beatsPerBar, harmony, plan.harmonicLanguage.tonalPolicy);
    const auto musical = MusicalCritic::review(song, plan);
    const auto production = ProductionPolish::audit(song, tonal.after,
        plan.beatsPerBar, 1.0, 1.0);
    ProductionPolish::stamp(song, production);
    const auto fabric = ElectronicCompositionFabric::audit(song, plan);
    song.independentMusicalLines = fabric.independentLines;
    song.meaningfulMusicalLines = fabric.meaningfulLines;
    song.protagonistPhraseWindows = fabric.protagonistPhraseWindows;
    song.arpeggioNoteCount = fabric.arpeggioNoteCount;
    song.dialogueMusicalLines = fabric.dialogueLines;
    song.harmonicFloorCoverage = fabric.harmonicFloorCoverage;
    song.medianHarmonicFloorLayers = fabric.medianHarmonicFloorLayers;
    const auto density = ArrangementDensityPlanner::auditAndStamp(song, plan);
    const auto electronic = ElectronicProductionDirector::audit(song, plan);
    ElectronicProductionDirector::stamp(song, electronic);
    const auto narrative = NarrativeScoreGate::audit(song, plan);
    NarrativeScoreGate::stamp(song, narrative);
    const auto soundscape = ElectronicSoundscapeDirector::audit(song, plan);
    ElectronicSoundscapeDirector::stamp(song, soundscape);
    const auto viability = TrackViability::audit(song, plan);
    TrackViability::stamp(song, viability);
    song.exactInstrumentCastPublished = viability.retainedTracks == plan.instruments.size();
    song.aiAuthoredNoteRatio = song.notes.empty() ? 0.0 :
        static_cast<double>(std::count_if(song.notes.begin(), song.notes.end(), [](const auto& note) {
            return note.origin == NoteOrigin::AiAuthored ||
                note.origin == NoteOrigin::AiTransformed;
        })) / static_cast<double>(song.notes.size());
    if (report) {
        report->firstTonalPass = tonal;
        report->finalTonalPass = tonal;
        report->musical = musical;
        report->production = production;
        report->electronicFabric = fabric;
        report->arrangementDensity = density;
        report->electronicProduction = electronic;
        report->narrative = narrative;
        report->soundscape = soundscape;
        report->trackViability = viability;
        report->harmonicWindows = harmony.size();
    }
    return song;
}

} // namespace pulso
