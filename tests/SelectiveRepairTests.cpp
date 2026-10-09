#include "TestSupport.h"

#include "core/ElectronicRoleContract.h"
#include "core/ElectronicCompositionFabric.h"
#include "core/EnsembleReference.h"
#include "core/ArrangementDensityPlanner.h"
#include "core/AttentionDirector.h"
#include "core/SelectiveRepair.h"
#include "core/SovereignScoreRenderer.h"
#include "core/TrackViability.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace pulso;

void runSelectiveRepairTests() {
    {
        InstrumentAssignment lead;
        lead.id = "lead";
        lead.sourceVoice = VoiceId::Lead;
        PerformanceScore score;
        PerformanceCell cell;
        cell.id = "written_phrase";
        cell.lengthBeats = 4.0;
        cell.ownedVoices = {VoiceId::Lead};
        cell.notes.push_back({.5, .5, 60, 90, VoiceId::Lead,
                              MetricIntent::StrictGrid, "lead"});
        cell.notes.push_back({2.0, .5, 62, 90, VoiceId::Lead,
                              MetricIntent::StrictGrid, "lead"});
        score.cells.push_back(cell);
        PerformancePlacement placement;
        placement.cellId = cell.id;
        placement.sectionIndex = 0;
        placement.fragmentStart = 3.0;
        placement.fragmentEnd = 4.0;
        score.placements.push_back(placement);
        const std::vector<InstrumentAssignment> instruments{lead};
        const std::vector<double> lengths{32.0};
        const auto clipped = PerformanceScoreEngine::auditRealization(
            score, "lead", instruments, lengths);
        require(clipped.placedSourceNotes == 2 && clipped.realizableNotes == 0 &&
                    clipped.excludedByFragment == 2,
                "Placed source notes clipped out by a fragment must not be reported as audible");
        score.placements.front().fragmentStart = 0.0;
        score.placements.front().voiceMap = {{VoiceId::Lead, VoiceId::Countermelody}};
        const auto misrouted = PerformanceScoreEngine::auditRealization(
            score, "lead", instruments, lengths);
        require(misrouted.realizableNotes == 0 && misrouted.incompatibleVoiceMap == 2,
                "Voice-map output routed away from its named instrument must be diagnosed");
        Pattern misroutedChunk;
        misroutedChunk.lengthBeats = 32.0;
        PerformanceScoreEngine::replaceChunk(misroutedChunk, score, 0,
                                             0.0, 32.0, instruments);
        require(std::none_of(misroutedChunk.notes.begin(), misroutedChunk.notes.end(),
                    [](const auto& note) { return note.partId == 1; }),
                "Realization diagnostics must agree with actual named-part rendering");
        score.placements.front().voiceMap.clear();
        score.placements.front().startBeat = 31.0;
        const auto outside = PerformanceScoreEngine::auditRealization(
            score, "lead", instruments, lengths);
        require(outside.realizableNotes == 1 && outside.excludedBySection == 1,
                "Section-relative placements must count only notes inside the section");
    }
    {
        PerformanceCoverageDeficit before;
        before.narrativePhraseWindows = 2;
        before.minimumNarrativePhraseWindows = 5;
        before.missingNarrativeWindowStartBars = {12, 20, 32};
        before.missingCodaResolution = true;
        const std::vector<int> requested{12, 20};
        require(SelectiveRepair::acceptsFocusedProtagonistCompletion(
                    before, nullptr, requested, false, 83, 95),
                "A focused AI reply that fully resolves all deficits must be accepted");
        auto after = before;
        after.narrativePhraseWindows = 3;
        after.missingNarrativeWindowStartBars = {12, 32};
        require(SelectiveRepair::acceptsFocusedProtagonistCompletion(
                    before, &after, requested, false, 83, 88),
                "A rendered phrase in a requested missing window must be preserved");
        require(!SelectiveRepair::acceptsFocusedProtagonistCompletion(
                    before, &after, std::vector<int>{32}, false, 83, 88),
                "Improvement outside the requested windows must not validate a targeted reply");
        require(!SelectiveRepair::acceptsFocusedProtagonistCompletion(
                    before, nullptr, requested, false, 83, 83),
                "Source cells without new realizable MIDI must not validate a completion");
        after.missingCodaResolution = false;
        require(SelectiveRepair::acceptsFocusedProtagonistCompletion(
                    before, &after, {}, true, 83, 89),
                "An independently rendered coda must be accepted without a phrase-window prerequisite");
    }
    {
        SongPlan plan;
        plan.totalBars = 16;
        plan.beatsPerBar = 4.0;
        SongSection section;
        section.startBar = 0;
        section.bars = 16;
        plan.sections.push_back(section);
        InstrumentAssignment lead;
        lead.id = "lead";
        lead.sourceVoice = VoiceId::Lead;
        plan.instruments.push_back(lead);
        PerformanceScore addition;
        PerformanceCell cell;
        cell.id = "new_phrase";
        cell.lengthBeats = 4.0;
        cell.ownedVoices = {VoiceId::Lead};
        for (auto beat = 0; beat < 4; ++beat)
            cell.notes.push_back({static_cast<double>(beat), .5, 60 + beat,
                                  90, VoiceId::Lead, MetricIntent::StrictGrid, "lead"});
        addition.cells.push_back(cell);
        PerformancePlacement placement;
        placement.cellId = cell.id;
        placement.sectionIndex = 0;
        placement.startBeat = 32.0;
        placement.fragmentStart = 0.0;
        placement.fragmentEnd = 4.0;
        addition.placements.push_back(placement);
        const std::vector<int> target{8};
        require(SelectiveRepair::focusedProtagonistAdditionInScope(
                    plan, addition, 0, target, false),
                "A complete phrase placed in its requested window must be renderable");
        addition.placements.front().startBeat = 0.0;
        require(!SelectiveRepair::focusedProtagonistAdditionInScope(
                    plan, addition, 0, target, false),
                "A phrase placed outside its requested window must be rejected");
        addition.placements.front().startBeat = 32.0;
        addition.placements.front().fragmentEnd = 2.0;
        require(!SelectiveRepair::focusedProtagonistAdditionInScope(
                    plan, addition, 0, target, false),
                "A clipped placement must not masquerade as a complete AI phrase");
        addition.placements.front().fragmentEnd = 4.0;
        addition.cells.push_back(cell);
        addition.cells.back().id = "orphan_phrase";
        require(!SelectiveRepair::focusedProtagonistAdditionInScope(
                    plan, addition, 0, target, false),
                "Unplaced AI notes must not validate a focused completion");
        addition.cells.pop_back();
        addition.placements.front().startBeat = 60.0;
        require(SelectiveRepair::focusedProtagonistAdditionInScope(
                    plan, addition, 0, {}, true),
                "A resolved final-eight-bar phrase must be eligible as a separate coda");
    }
    {
        SongPlan plan;
        plan.totalBars = 16;
        plan.beatsPerBar = 4.0;
        SongSection section;
        section.startBar = 0;
        section.bars = 16;
        plan.sections.push_back(section);
        InstrumentAssignment bed;
        bed.id = "central_bed";
        bed.sourceVoice = VoiceId::HarmonicFoundation;
        bed.role = "primary_chord_bed";
        plan.instruments.push_back(bed);
        require(SelectiveRepair::centralChordBedOwner(plan) == 0,
                "The primary chord bed must be selected as the form owner");
        PerformanceScore score;
        PerformanceCell chord;
        chord.id = "evolving_chord";
        chord.lengthBeats = 4.0;
        chord.ownedVoices = {VoiceId::HarmonicFoundation};
        for (const auto pitch : {60, 64, 67})
            chord.notes.push_back({0.0, 3.0, pitch, 80,
                VoiceId::HarmonicFoundation, MetricIntent::StrictGrid, bed.id});
        score.cells.push_back(chord);
        for (const auto beat : {0.0, 24.0, 48.0}) {
            PerformancePlacement placement;
            placement.cellId = chord.id;
            placement.sectionIndex = 0;
            placement.startBeat = beat;
            placement.fragmentStart = 0.0;
            placement.fragmentEnd = 4.0;
            score.placements.push_back(placement);
        }
        require(SelectiveRepair::chordBedFormCoverage(plan, score, 0).ready(),
                "A real chord attack in each structural third must satisfy the harmonic floor");
        score.placements.pop_back();
        const auto sparse = SelectiveRepair::chordBedFormCoverage(plan, score, 0);
        require(sparse.opening && sparse.development && !sparse.closing,
                "An introductory bed without a closing return must be detected");

        SongPlan sectionalPlan = plan;
        sectionalPlan.totalBars = 24;
        sectionalPlan.sections.clear();
        for (auto index = 0; index < 3; ++index) {
            SongSection scene;
            scene.name = "Scene " + std::to_string(index + 1);
            scene.startBar = index * 8;
            scene.bars = 8;
            sectionalPlan.sections.push_back(scene);
        }
        PerformanceScore sectionalScore;
        sectionalScore.cells = {chord};
        for (const auto [sectionIndex, beat] :
             std::array<std::pair<int, double>, 3>{{{0, 0.0}, {1, 0.0}, {1, 28.0}}}) {
            PerformancePlacement placement;
            placement.cellId = chord.id;
            placement.sectionIndex = sectionIndex;
            placement.startBeat = beat;
            placement.fragmentEnd = 4.0;
            sectionalScore.placements.push_back(placement);
        }
        const auto climaxOnlyClosing = SelectiveRepair::chordBedFormCoverage(
            sectionalPlan, sectionalScore, 0);
        require(climaxOnlyClosing.opening && climaxOnlyClosing.development &&
                    !climaxOnlyClosing.closing,
                "A late-climax chord must not masquerade as an audible chord-bed coda");
        sectionalScore.placements.back().sectionIndex = 2;
        sectionalScore.placements.back().startBeat = 0.0;
        require(SelectiveRepair::chordBedFormCoverage(
                    sectionalPlan, sectionalScore, 0).ready(),
                "A multi-section chord bed must sound in the actual final scene");
        CompositionRenderReport report;
        require(SelectiveRepair::measuredTonalDebt(report) == 0,
                "A clean complete-score checkpoint must have no tonal debt");
        report.production.unintendedHarshOverlaps = 1;
        require(SelectiveRepair::measuredTonalDebt(report) == 1,
                "Even one unresolved harsh overlap must not be silently accumulated");
    }
    {
        SongPlan plan;
        plan.beatsPerBar = 4.0;
        InstrumentAssignment pad;
        pad.id = "pad";
        InstrumentAssignment lead;
        lead.id = "lead";
        plan.instruments = {pad, lead};
        Pattern pattern;
        NoteEvent chord;
        chord.startBeat = 8.0;
        chord.durationBeats = 2.0;
        chord.pitch = 60;
        chord.voice = VoiceId::HarmonicFoundation;
        chord.partId = 1;
        NoteEvent melody = chord;
        melody.startBeat = 8.5;
        melody.durationBeats = 1.0;
        melody.pitch = 61;
        melody.voice = VoiceId::Lead;
        melody.partId = 2;
        pattern.notes = {chord, melody};
        HarmonicWindow harmony;
        harmony.startBeat = 0.0;
        harmony.endBeat = 12.0;
        harmony.pitchClasses = {0, 4, 7};
        const auto audit = auditTonalContract(pattern, 0, ScaleKind::Major, 4.0,
                                              std::span<const HarmonicWindow>(&harmony, 1));
        const auto groups = SelectiveRepair::tonalConflictGroups(plan, audit);
        require(audit.unintendedHarshOverlaps == 1 && groups.size() == 1 &&
                    groups.front().firstInstrument == 0 &&
                    groups.front().secondInstrument == 1 &&
                    groups.front().bar == 2 && groups.front().events == 1 &&
                    groups.front().exampleFirstPitch == 60 &&
                    groups.front().exampleSecondPitch == 61 &&
                    std::abs(groups.front().overlapBeats - 1.0) < .001,
                "Tonal conflict evidence must identify exact instrument pair, bar and overlap duration");
        TonalAuditReport internalChordConflict;
        TonalIssue internalIssue;
        internalIssue.kind = "harsh_overlap";
        internalIssue.beat = 8.0;
        internalIssue.partId = internalIssue.otherPartId = 1;
        internalIssue.pitch = 67;
        internalIssue.otherPitch = 56;
        internalIssue.overlapBeats = 2.0;
        internalChordConflict.issues.push_back(internalIssue);
        const auto internalGroups = SelectiveRepair::tonalConflictGroups(
            plan, internalChordConflict);
        require(internalGroups.size() == 1 &&
                    internalGroups.front().exampleFirstPitch == 56 &&
                    internalGroups.front().exampleSecondPitch == 67,
                "An internal chord collision must report both real pitches, not the same pitch twice");
        Pattern repeated;
        for (int bar = 0; bar < 24; ++bar) {
            auto padNote = chord;
            padNote.startBeat = bar * 4.0;
            auto leadNote = melody;
            leadNote.startBeat = bar * 4.0 + .5;
            repeated.notes.push_back(padNote);
            repeated.notes.push_back(leadNote);
        }
        harmony.endBeat = 100.0;
        const auto longAudit = auditTonalContract(repeated, 0, ScaleKind::Major, 4.0,
                                                  std::span<const HarmonicWindow>(&harmony, 1));
        require(longAudit.unintendedHarshOverlaps == 24 &&
                    SelectiveRepair::tonalConflictGroups(plan, longAudit).size() == 24,
                "A long song must retain conflict evidence beyond the old first-16-event cap");
        plan.instruments.front().sourceVoice = VoiceId::HarmonicFoundation;
        TonalAuditReport lowVoicing;
        TonalIssue heldSecond;
        heldSecond.kind = "harsh_overlap";
        heldSecond.partId = heldSecond.otherPartId = 1;
        heldSecond.pitch = 46;
        heldSecond.otherPitch = 45;
        heldSecond.overlapBeats = 16.0;
        lowVoicing.issues.push_back(heldSecond);
        require(SelectiveRepair::sustainedLowChordBedSeconds(plan, lowVoicing).size() == 1,
                "A four-bar bass-register semitone within one chord bed must be caught before later blocks");
        lowVoicing.issues.front().overlapBeats = .5;
        require(SelectiveRepair::sustainedLowChordBedSeconds(plan, lowVoicing).empty(),
                "Brief passing tensions must not trigger the sustained-voicing gate");
        lowVoicing.issues.front().overlapBeats = 16.0;
        lowVoicing.issues.front().otherPartId = 2;
        require(SelectiveRepair::sustainedLowChordBedSeconds(plan, lowVoicing).empty(),
                "A cross-instrument collision must not be misdiagnosed as an internal chord voicing");
        lowVoicing.issues.front().otherPartId = 1;
        lowVoicing.issues.front().pitch = 64;
        lowVoicing.issues.front().otherPitch = 63;
        lowVoicing.issues.front().overlapBeats = 16.0;
        require(SelectiveRepair::sustainedLowChordBedSeconds(plan, lowVoicing).size() == 1,
                "A sustained upper-register semitone inside the primary chord bed must be repaired");
    }
    {
        CompositionRenderReport rejected;
        rejected.production.unintendedHarshOverlaps = 169;
        rejected.production.invalidSustains = 2;
        rejected.production.lowRegisterVerticalClashes = 2;
        rejected.narrative.score = .770;
        auto partialRewrite = rejected;
        partialRewrite.production.unintendedHarshOverlaps = 164;
        partialRewrite.narrative.score = .767;
        require(SelectiveRepair::improvedTonalCheckpoint(
                    rejected, partialRewrite, 1.0),
                "The September 30 AI repair must survive as an improved checkpoint, not publication");
        partialRewrite.production.metricViolations = 1;
        require(!SelectiveRepair::improvedTonalCheckpoint(
                    rejected, partialRewrite, 1.0),
                "A tonal improvement must not introduce invalid MIDI timing");
        partialRewrite.production.metricViolations = 0;
        require(!SelectiveRepair::improvedTonalCheckpoint(
                    rejected, partialRewrite, .95),
                "A local editorial checkpoint must keep full AI note authorship");
        partialRewrite.production.invalidSustains = 3;
        require(!SelectiveRepair::improvedTonalCheckpoint(
                    rejected, partialRewrite, 1.0),
                "A reduction in overlaps must not introduce a new harmonic sustain fault");
        partialRewrite.production.invalidSustains = 2;
        partialRewrite.narrative.resolutionScore = .46;
        rejected.narrative.resolutionScore = .57;
        require(!SelectiveRepair::improvedTonalCheckpoint(
                    rejected, partialRewrite, 1.0),
                "A tonal rewrite must not discard the resolution of the original score");
        require(!SelectiveRepair::preservesNarrative(rejected, partialRewrite),
                "Whole-score editorial acceptance must protect the same musical ending");
    }
    {
        SongPlan floorPlan;
        floorPlan.totalBars = 32;
        floorPlan.beatsPerBar = 4.0;
        InstrumentAssignment bed;
        bed.id = "bed";
        bed.role = "primary_chord_bed";
        bed.sourceVoice = VoiceId::HarmonicFoundation;
        InstrumentAssignment memory;
        memory.id = "memory";
        memory.sourceVoice = VoiceId::Atmosphere;
        floorPlan.instruments = {bed, memory};
        Pattern floorPattern;
        floorPattern.lengthBeats = 128.0;
        CompositionRenderReport floorReport;
        floorReport.soundscape.active = true;
        floorReport.soundscape.harmonicFloorCoverage = .31;
        const auto diagnosis = SelectiveRepair::diagnose(
            floorPlan, floorPattern, floorReport, 2);
        require(diagnosis.needed && diagnosis.instrumentIndices.size() == 2 &&
                    diagnosis.instrumentIndices.front() == 0 &&
                    diagnosis.instrumentIndices.back() == 1,
                "An unwritten electronic floor must establish its central bed and harmonic companion");
        PerformanceCell populatedBed;
        populatedBed.id = "populated_bed";
        populatedBed.lengthBeats = 32.0;
        populatedBed.ownedVoices = {VoiceId::HarmonicFoundation};
        for (int attack = 0; attack < 8; ++attack)
            for (const auto pitch : {60, 64, 67})
                populatedBed.notes.push_back({static_cast<double>(attack * 4), 3.5,
                    pitch, 80, VoiceId::HarmonicFoundation,
                    MetricIntent::StrictGrid, bed.id});
        floorPlan.performanceScore.cells.push_back(populatedBed);
        const auto populatedDiagnosis = SelectiveRepair::diagnose(
            floorPlan, floorPattern, floorReport, 2);
        require(populatedDiagnosis.instrumentIndices.size() == 2 &&
                    populatedDiagnosis.instrumentIndices.front() == 1,
                "A strong central bed with a missing companion must revise the companion before replacing the bed");
        auto layeredFloorPlan = floorPlan;
        InstrumentAssignment sub;
        sub.id = "sub_tonal_anchor";
        sub.sourceVoice = VoiceId::HarmonicFoundation;
        InstrumentAssignment arp;
        arp.id = "hypnotic_arp";
        arp.sourceVoice = VoiceId::HarmonicPulse;
        layeredFloorPlan.instruments = {bed, sub, arp, memory};
        const auto layeredDiagnosis = SelectiveRepair::diagnose(
            layeredFloorPlan, floorPattern, floorReport, 4);
        require(!layeredDiagnosis.instrumentIndices.empty() &&
                    layeredDiagnosis.instrumentIndices.front() == 3,
                "A sub-bass and an arpeggio must not masquerade as the sustained companion pad");
    }
    {
        SongPlan tonalPriority;
        tonalPriority.totalBars = 8;
        tonalPriority.beatsPerBar = 4.0;
        InstrumentAssignment lead;
        lead.id = "lead";
        lead.sourceVoice = VoiceId::Lead;
        lead.prominence = .9;
        InstrumentAssignment texture;
        texture.id = "texture";
        texture.sourceVoice = VoiceId::HarmonicUpper;
        texture.prominence = .4;
        tonalPriority.instruments = {lead, texture};
        Pattern pattern;
        pattern.lengthBeats = 32.0;
        CompositionRenderReport report;
        report.production.unsupportedChromaticNotes = 2;
        for (auto beat : {2.0, 6.0}) {
            TonalIssue issue;
            issue.beat = beat;
            issue.pitch = 66;
            issue.kind = "unsupported_chromatic";
            issue.partId = 1;
            report.finalTonalPass.after.issues.push_back(issue);
        }
        for (auto index = 0; index < 24; ++index) {
            TonalIssue issue;
            issue.beat = static_cast<double>(index);
            issue.pitch = 96;
            issue.otherPitch = 49;
            issue.kind = "harsh_overlap";
            issue.partId = 2;
            issue.otherPartId = 1;
            issue.overlapBeats = 2.0;
            report.finalTonalPass.after.issues.push_back(issue);
        }
        const auto diagnosis = SelectiveRepair::diagnose(
            tonalPriority, pattern, report, 1);
        require(diagnosis.instrumentIndices.size() == 1 &&
                    diagnosis.instrumentIndices.front() == 0,
                "Unsupported chromatic owners must be repaired before noisy overlap counts");
    }
    {
        SongPlan collisionPlan;
        collisionPlan.totalBars = 16;
        collisionPlan.beatsPerBar = 4.0;
        InstrumentAssignment bed;
        bed.id = "bed";
        bed.sourceVoice = VoiceId::HarmonicFoundation;
        bed.prominence = .4;
        InstrumentAssignment lead;
        lead.id = "lead";
        lead.sourceVoice = VoiceId::Lead;
        lead.prominence = .9;
        InstrumentAssignment hat;
        hat.id = "hat";
        hat.sourceVoice = VoiceId::CoreDrums;
        collisionPlan.instruments = {bed, lead, hat};
        Pattern pattern;
        pattern.lengthBeats = 64.0;
        CompositionRenderReport report;
        report.production.unintendedHarshOverlaps = 51;
        for (auto index = 0; index < 51; ++index) {
            TonalIssue issue;
            issue.kind = "harsh_overlap";
            issue.partId = 1;
            issue.otherPartId = 2;
            issue.overlapBeats = 1.0;
            report.finalTonalPass.after.issues.push_back(issue);
        }
        const auto diagnosis = SelectiveRepair::diagnose(
            collisionPlan, pattern, report, 2);
        require(diagnosis.instrumentIndices.size() == 2 &&
                    diagnosis.instrumentIndices.front() == 0 &&
                    diagnosis.instrumentIndices.back() == 1,
                "Repeated harmonic collisions must prioritize both audible owners before unrelated rhythm lanes");
    }
    {
        SongPlan authored;
        authored.totalBars = 8;
        authored.beatsPerBar = 4.0;
        authored.instrumentCastAuthored = true;
        authored.aiSovereign = true;
        SongSection section;
        section.name = "A";
        section.bars = 8;
        authored.sections.push_back(section);
        InstrumentAssignment lead;
        lead.id = "lead";
        lead.name = "Authored lead";
        lead.instrumentId = "lead_synth";
        lead.sourceVoice = VoiceId::Lead;
        lead.minimumPitch = 48;
        lead.maximumPitch = 84;
        InstrumentAssignment emptyPad;
        emptyPad.id = "pad";
        emptyPad.name = "Silent pad";
        emptyPad.instrumentId = "analog_pad";
        emptyPad.sourceVoice = VoiceId::HarmonicFoundation;
        authored.instruments = {lead, emptyPad};
        PerformanceCell cell;
        cell.id = "one_phrase";
        cell.lengthBeats = 4.0;
        cell.ownedVoices = {VoiceId::Lead};
        cell.notes.push_back({1.0, .5, 67, 80, VoiceId::Lead,
                              MetricIntent::StrictGrid, "lead"});
        authored.performanceScore.cells.push_back(cell);
        PerformancePlacement placement;
        placement.cellId = cell.id;
        placement.sectionIndex = 0;
        placement.startBeat = 0.0;
        placement.fragmentEnd = 4.0;
        authored.performanceScore.placements.push_back(placement);
        CompositionRenderReport observation;
        const auto rendered = SovereignScoreRenderer::render(authored, {}, &observation);
        require(rendered.notes.size() == 1 && rendered.parts.size() == 2 &&
                    rendered.notes.front().startBeat == 1.0 &&
                    rendered.notes.front().pitch == 67 &&
                    rendered.notes.front().origin == NoteOrigin::AiAuthored &&
                    rendered.notes.front().partId == 1,
                "Sovereign rendering must preserve one AI note and its silence without filler");
        require(rendered.aiAuthoredNoteRatio == 1.0 &&
                    observation.trackViability.notesCreated == 0,
                "Sovereign audit must not add notes to a sparse AI score");
        GenerationContext context;
        context.role = Role::Ensemble;
        const auto throughPipeline = SongComposer{}.render(authored, context);
        require(throughPipeline.notes.size() == 1 &&
                    throughPipeline.notes.front().pitch == 67 &&
                    throughPipeline.notes.front().startBeat == 1.0,
                "The shared SongComposer entry point must honor AI-sovereign mode");
    }
    {
        SongPlan referencePlan;
        referencePlan.totalBars = 16;
        referencePlan.beatsPerBar = 4.0;
        referencePlan.narrativeSpine.protagonistInstrumentId = "speaker";
        for (auto index = 0; index < 2; ++index) {
            SongSection section;
            section.name = "Act " + std::to_string(index + 1);
            section.startBar = index * 8;
            section.bars = 8;
            referencePlan.sections.push_back(section);
        }
        InstrumentAssignment speaker;
        speaker.id = "speaker";
        speaker.instrumentId = "lead_synth";
        speaker.sourceVoice = VoiceId::Lead;
        InstrumentAssignment bed;
        bed.id = "bed";
        bed.instrumentId = "analog_pad";
        bed.sourceVoice = VoiceId::HarmonicFoundation;
        bed.role = "primary_chord_bed";
        referencePlan.instruments = {speaker, bed};
        PerformanceScore accepted;
        PerformanceCell speech;
        speech.id = "speech";
        speech.lengthBeats = 4.0;
        speech.ownedVoices = {VoiceId::Lead};
        speech.notes.push_back({0.0, 1.0, 69, 78, VoiceId::Lead,
                                MetricIntent::StrictGrid, "speaker"});
        PerformanceCell chord;
        chord.id = "chord";
        chord.lengthBeats = 4.0;
        chord.ownedVoices = {VoiceId::HarmonicFoundation};
        chord.notes.push_back({0.0, 3.0, 62, 65, VoiceId::HarmonicFoundation,
                               MetricIntent::StrictGrid, "bed"});
        chord.notes.push_back({0.0, 3.0, 63, 65, VoiceId::HarmonicFoundation,
                               MetricIntent::StrictGrid, "bed"});
        chord.controls.push_back({1.0, 11, 88, VoiceId::HarmonicFoundation, "bed"});
        accepted.cells = {speech, chord};
        for (auto section = 0; section < 2; ++section) {
            for (const auto* id : {"speech", "chord"}) {
                PerformancePlacement placement;
                placement.cellId = id;
                placement.sectionIndex = section;
                placement.repeats = 8;
                placement.fragmentEnd = 4.0;
                accepted.placements.push_back(placement);
            }
        }
        const auto reference = EnsembleReference::summarize(
            referencePlan, accepted, {"bed"}, 6);
        require(reference.find("speaker role=") != std::string::npos &&
                    reference.find("bed role=") == std::string::npos &&
                    reference.find("[0.00,69,1.00]") != std::string::npos &&
                    reference.find("[32.00,69,1.00]") != std::string::npos &&
                    reference.find("SECTION 1") != std::string::npos,
                "The next AI block must see bounded real MIDI from earlier sections, excluding its target");
        require(EnsembleReference::summarize(referencePlan, {}, {}).empty(),
                "An empty accepted score must not invent an ensemble reference");
        const auto ledger = EnsembleReference::harmonicLedger(
            referencePlan, accepted, {"speaker"});
        require(ledger.find("bed [0.00-3.00:62,63]") != std::string::npos &&
                    ledger.find("bed [32.00-35.00:62,63]") != std::string::npos &&
                    ledger.find("speaker") == std::string::npos,
                "The harmonic ledger must preserve every simultaneous chord pitch in each section");
        const auto boundedLedger = EnsembleReference::harmonicLedger(
            referencePlan, accepted, {"speaker"}, 2);
        require(boundedLedger.find("SECTION 0") != std::string::npos &&
                    boundedLedger.find("SECTION 1") != std::string::npos &&
                    boundedLedger.find("bed [32.00-35.00:62,63]") != std::string::npos,
                "A bounded harmonic reference must include the ending rather than truncate after the opening");
        require(EnsembleReference::harmonicLedger(referencePlan, {}, {}).empty(),
                "No accepted MIDI must yield no invented harmonic context");
        const auto vertical = EnsembleReference::verticalSnapshots(
            referencePlan, accepted, {"speaker"}, 2);
        require(vertical.find("SECTION 0") != std::string::npos &&
                    vertical.find("SECTION 1") != std::string::npos &&
                    vertical.find("beat=0.00 bed=62,63") != std::string::npos &&
                    vertical.find("beat=32.00 bed=62,63") != std::string::npos &&
                    vertical.find("speaker") == std::string::npos,
                "A bounded ensemble snapshot must show complete simultaneous pitches across the form");
        Pattern before;
        before.lengthBeats = 64.0;
        PerformanceScoreEngine::replaceChunk(before, accepted, 0, 0.0, 32.0,
                                              referencePlan.instruments);
        TonalAuditReport conflicts;
        TonalIssue issue;
        issue.kind = "harsh_overlap";
        issue.beat = 0.0;
        issue.partId = 2;
        issue.otherPartId = 1;
        issue.pitch = 63;
        issue.otherPitch = 69;
        issue.overlapBeats = 1.0;
        conflicts.issues.push_back(issue);
        const auto targets = SelectiveRepair::chordVoicingTargets(
            referencePlan, before, conflicts, 1);
        require(targets.size() == 1 && targets.front().pitches == std::vector<int>({62, 63}) &&
                    targets.front().sectionIndex == 0 && targets.front().sectionBeat == 0.0,
                "Chord repair must target a complete rendered attack, not one sampled note");
        auto repeatedConflicts = conflicts;
        for (const auto beat : {4.0, 8.0, 12.0}) {
            auto repeated = issue;
            repeated.beat = beat;
            repeatedConflicts.issues.push_back(repeated);
        }
        const auto sourceTargets = SelectiveRepair::repeatedSourceVoicingTargets(
            referencePlan, accepted, repeatedConflicts, 1);
        require(sourceTargets.size() == 1 &&
                    sourceTargets.front().sourceCellId == "chord" &&
                    sourceTargets.front().sourceOccurrences == 16 &&
                    sourceTargets.front().occurrences.size() == 16 &&
                    sourceTargets.front().occurrences.front().sectionIndex == 0 &&
                    sourceTargets.front().occurrences.front().sectionBeat == 0.0 &&
                    sourceTargets.front().conflictEvents == 4,
            "Repeated collisions must expose every affected context to one source attack");
        PerformanceScore sourcePatched;
        std::string sourcePatchError;
        require(SelectiveRepair::applySourceVoicingPatches(referencePlan, accepted, 1,
                    sourceTargets, {{0, 0.0, 3.0, {62, 65}}},
                    sourcePatched, sourcePatchError),
            "One AI voicing decision must repair a reused source chord cell");
        Pattern sourceAfter;
        sourceAfter.lengthBeats = 64.0;
        PerformanceScoreEngine::replaceChunk(sourceAfter, sourcePatched, 0, 0.0,
            32.0, referencePlan.instruments);
        for (const auto beat : {0.0, 4.0, 8.0, 12.0}) {
            std::vector<int> pitches;
            for (const auto& note : sourceAfter.notes)
                if (note.partId == 2 && std::abs(note.startBeat - beat) < .001)
                    pitches.push_back(note.pitch);
            std::sort(pitches.begin(), pitches.end());
            require(pitches == std::vector<int>({62, 65}),
                "A source-cell voicing must reach every repeated occurrence");
        }
        require(sourcePatched.cells.size() == accepted.cells.size() &&
                    sourcePatched.placements.size() == accepted.placements.size() &&
                    std::count_if(sourceAfter.notes.begin(), sourceAfter.notes.end(),
                        [](const auto& note) { return note.partId == 1; }) ==
                    std::count_if(before.notes.begin(), before.notes.end(),
                        [](const auto& note) { return note.partId == 1; }),
            "A source-cell repair must preserve the cast, placements and other MIDI owners");
        auto explicitScore = accepted;
        auto& explicitChord = explicitScore.cells[1];
        explicitChord.lengthBeats = 16.0;
        const auto originalChordNotes = explicitChord.notes;
        for (const auto beat : {4.0, 8.0, 12.0})
            for (auto note : originalChordNotes) {
                note.beat += beat;
                explicitChord.notes.push_back(std::move(note));
            }
        for (auto& placement : explicitScore.placements)
            if (placement.cellId == "chord") {
                placement.repeats = 2;
                placement.fragmentEnd = 16.0;
            }
        const auto explicitTargets = SelectiveRepair::repeatedSourceVoicingTargets(
            referencePlan, explicitScore, repeatedConflicts, 1);
        require(explicitTargets.size() == 1 &&
                    explicitTargets.front().sourceAttacks.size() == 4 &&
                    explicitTargets.front().sourceOccurrences == 16 &&
                    explicitTargets.front().occurrences.size() == 16,
            "Identical explicit chord attacks must form one harmonic source cohort");
        PerformanceScore explicitPatched;
        require(SelectiveRepair::applySourceVoicingPatches(referencePlan, explicitScore, 1,
                    explicitTargets, {{0, 0.0, 3.0, {62, 65}}},
                    explicitPatched, sourcePatchError),
            "One AI choice must repair every equivalent explicit source attack");
        Pattern explicitAfter;
        PerformanceScoreEngine::replaceChunk(explicitAfter, explicitPatched, 0, 0.0,
            32.0, referencePlan.instruments);
        for (const auto beat : {0.0, 4.0, 8.0, 12.0}) {
            std::vector<int> pitches;
            for (const auto& note : explicitAfter.notes)
                if (note.partId == 2 && std::abs(note.startBeat - beat) < .001)
                    pitches.push_back(note.pitch);
            std::sort(pitches.begin(), pitches.end());
            require(pitches == std::vector<int>({62, 65}),
                "An explicit source motif repair must reach each written chord attack");
        }
        auto pulsePlan = referencePlan;
        pulsePlan.instruments[1].sourceVoice = VoiceId::HarmonicPulse;
        pulsePlan.instruments[1].role = "independent harmonic pulse";
        auto pulseScore = accepted;
        auto& pulseCell = pulseScore.cells[1];
        pulseCell.notes.erase(pulseCell.notes.begin());
        pulseCell.notes.front().voice = VoiceId::HarmonicPulse;
        pulseCell.ownedVoices = {VoiceId::HarmonicPulse};
        const auto pulseTargets = SelectiveRepair::repeatedSourceVoicingTargets(
            pulsePlan, pulseScore, repeatedConflicts, 1);
        require(pulseTargets.size() == 1 &&
                    pulseTargets.front().pitches == std::vector<int>({63}) &&
                    pulseTargets.front().sourceOccurrences == 16,
                "A repeated single-note harmonic pulse must be a tonal source target");
        PerformanceScore pulsePatched;
        require(SelectiveRepair::applySourceVoicingPatches(pulsePlan, pulseScore, 1,
                    pulseTargets, {{0, 0.0, 3.0, {65}}}, pulsePatched,
                    sourcePatchError),
                "A one-note harmonic pulse must be revoiced without inventing a chord");
        Pattern pulseAfter;
        PerformanceScoreEngine::replaceChunk(pulseAfter, pulsePatched, 0, 0.0,
            32.0, pulsePlan.instruments);
        require(std::count_if(pulseAfter.notes.begin(), pulseAfter.notes.end(),
                    [](const auto& note) {
                        return note.partId == 2 && note.pitch == 65;
                    }) == 8,
                "The single AI pitch choice must reach every repetition of the pulse");
        PerformanceScore patched;
        std::string patchError;
        require(SelectiveRepair::applyChordVoicingPatches(referencePlan, accepted, 1,
                    {{0, 0.0, 3.0, {62, 65}}}, patched, patchError),
                "A compact AI chord reply must splice into the accepted score");
        const auto normalizedPatch = PerformanceScoreEngine::normalize(
            patched, 2, {32.0, 32.0});
        require(normalizedPatch.cellsRejected == 0 &&
                    normalizedPatch.placementsRejected == 0,
                "A compact voicing splice must survive score normalization intact");
        Pattern after;
        after.lengthBeats = 64.0;
        PerformanceScoreEngine::replaceChunk(after, patched, 0, 0.0, 32.0,
                                              referencePlan.instruments);
        const auto pitchesAt = [](const Pattern& song, std::uint16_t part, double beat) {
            std::vector<int> pitches;
            for (const auto& note : song.notes)
                if (note.partId == part && std::abs(note.startBeat - beat) < .001)
                    pitches.push_back(note.pitch);
            std::sort(pitches.begin(), pitches.end());
            return pitches;
        };
        const auto controlSignature = [](const Pattern& song) {
            std::vector<std::tuple<double, std::uint16_t, int, int>> values;
            for (const auto& control : song.controls)
                values.emplace_back(control.beat, control.partId,
                    control.controller, control.value);
            std::sort(values.begin(), values.end());
            return values;
        };
        require(pitchesAt(after, 2, 0.0) == std::vector<int>({62, 65}) &&
                    pitchesAt(after, 2, 4.0) == pitchesAt(before, 2, 4.0) &&
                    pitchesAt(after, 1, 0.0) == pitchesAt(before, 1, 0.0) &&
                    after.notes.size() == before.notes.size() &&
                    controlSignature(after) == controlSignature(before),
                "Only the selected chord attack may change; later chords and the lead stay identical");
        require(SelectiveRepair::preservesUntouchedMidi(referencePlan, before, after,
                    1, {{0, 0.0, 3.0, {62, 65}}}),
                "The transaction must verify every untargeted MIDI note and controller");
        auto damaged = after;
        for (auto& note : damaged.notes)
            if (note.partId == 1) { ++note.pitch; break; }
        require(!SelectiveRepair::preservesUntouchedMidi(referencePlan, before, damaged,
                    1, {{0, 0.0, 3.0, {62, 65}}}),
                "A repair that alters another instrument must never be accepted");
        require(!SelectiveRepair::applyChordVoicingPatches(referencePlan, accepted, 1,
                    {{0, 1.0, 3.0, {62, 65}}}, patched, patchError),
                "An AI reply must not invent a new edit location");
        auto upperPlan = referencePlan;
        upperPlan.instruments[1].sourceVoice = VoiceId::HarmonicUpper;
        auto upperAccepted = accepted;
        upperAccepted.cells[1].ownedVoices = {VoiceId::HarmonicUpper};
        for (auto& note : upperAccepted.cells[1].notes)
            note.voice = VoiceId::HarmonicUpper;
        for (auto& control : upperAccepted.cells[1].controls)
            control.voice = VoiceId::HarmonicUpper;
        require(SelectiveRepair::applyChordVoicingPatches(upperPlan, upperAccepted, 1,
                    {{0, 0.0, 3.0, {62, 65}}}, patched, patchError),
                "A sustained upper chord body must allow the same localized voicing repair");
        Pattern upperAfter;
        PerformanceScoreEngine::replaceChunk(upperAfter, patched, 0, 0.0, 32.0,
                                              upperPlan.instruments);
        require(pitchesAt(upperAfter, 2, 0.0) == std::vector<int>({62, 65}) &&
                    pitchesAt(upperAfter, 1, 0.0) == pitchesAt(before, 1, 0.0),
                "Upper-body repair must leave the other instrument untouched");
        auto bassPlan = referencePlan;
        bassPlan.instruments[1].sourceVoice = VoiceId::MovementBass;
        auto bassAccepted = accepted;
        bassAccepted.cells[1].ownedVoices = {VoiceId::MovementBass};
        bassAccepted.cells[1].notes.resize(1);
        bassAccepted.cells[1].notes.front().voice = VoiceId::MovementBass;
        for (auto& control : bassAccepted.cells[1].controls)
            control.voice = VoiceId::MovementBass;
        Pattern bassBefore;
        PerformanceScoreEngine::replaceChunk(bassBefore, bassAccepted, 0, 0.0,
                                              32.0, bassPlan.instruments);
        auto bassConflicts = conflicts;
        bassConflicts.issues.front().pitch = 62;
        require(SelectiveRepair::chordVoicingTargets(bassPlan, bassBefore,
                    bassConflicts, 1).size() == 1,
                "A measured single-note bass collision must be a repair target");
        require(SelectiveRepair::applyChordVoicingPatches(bassPlan, bassAccepted, 1,
                    {{0, 0.0, 3.0, {60}}}, patched, patchError),
                "The AI must be able to change one bass pitch without rewriting its lane");
        require(patched.cells.size() <= 3,
                "Localized pitch repair must stay compact across repeated placements");
        Pattern bassAfter;
        PerformanceScoreEngine::replaceChunk(bassAfter, patched, 0, 0.0,
                                              32.0, bassPlan.instruments);
        require(pitchesAt(bassAfter, 2, 0.0) == std::vector<int>({60}) &&
                    pitchesAt(bassAfter, 2, 4.0) == pitchesAt(bassBefore, 2, 4.0) &&
                    pitchesAt(bassAfter, 1, 0.0) == pitchesAt(bassBefore, 1, 0.0) &&
                    SelectiveRepair::preservesUntouchedMidi(bassPlan, bassBefore,
                        bassAfter, 1, {{0, 0.0, 3.0, {60}}}),
                "One AI bass edit must preserve every unrelated MIDI event");
        auto leadPlan = referencePlan;
        leadPlan.instruments[1].sourceVoice = VoiceId::Lead;
        auto leadAccepted = bassAccepted;
        leadAccepted.cells[1].ownedVoices = {VoiceId::Lead};
        leadAccepted.cells[1].notes.front().voice = VoiceId::Lead;
        for (auto& control : leadAccepted.cells[1].controls)
            control.voice = VoiceId::Lead;
        Pattern leadBefore;
        PerformanceScoreEngine::replaceChunk(leadBefore, leadAccepted, 0, 0.0,
                                              32.0, leadPlan.instruments);
        require(SelectiveRepair::chordVoicingTargets(leadPlan, leadBefore,
                    bassConflicts, 1).size() == 1,
                "A measured protagonist collision must identify one note attack");
        require(SelectiveRepair::applyChordVoicingPatches(leadPlan, leadAccepted, 1,
                    {{0, 0.0, 3.0, {60}}}, patched, patchError),
                "The AI must be able to correct a protagonist pitch without rewriting its phrase");
        Pattern leadAfter;
        PerformanceScoreEngine::replaceChunk(leadAfter, patched, 0, 0.0,
                                              32.0, leadPlan.instruments);
        require(pitchesAt(leadAfter, 2, 0.0) == std::vector<int>({60}) &&
                    SelectiveRepair::preservesUntouchedMidi(leadPlan, leadBefore,
                        leadAfter, 1, {{0, 0.0, 3.0, {60}}}),
                "A protagonist pitch edit must retain all other notes and controls");
    }

    SongPlan plan;
    plan.totalBars = 128;
    plan.beatsPerBar = 4.0;
    plan.instrumentCastAuthored = true;
    for (auto section = 0; section < 4; ++section) {
        SongSection item;
        item.name = "Act " + std::to_string(section + 1);
        item.startBar = section * 32;
        item.bars = 32;
        plan.sections.push_back(std::move(item));
    }

    InstrumentAssignment owner;
    owner.id = "motion_owner";
    owner.instrumentId = "poly_synth";
    owner.sourceVoice = VoiceId::HarmonicPulse;
    owner.role = "primary_motion_owner";
    owner.orchestralFunction = "pulse";
    owner.contentLaneId = owner.id;
    owner.lineRelationship = "independent";
    owner.prominence = .9;
    InstrumentAssignment clone = owner;
    clone.id = "supposed_counterpoint";
    clone.role = "independent suspension counterpoint";
    clone.orchestralFunction = "counterpoint";
    clone.contentLaneId = clone.id;
    clone.prominence = .4;
    for (const auto& section : plan.sections) {
        owner.activeSections.push_back(section.name);
        clone.activeSections.push_back(section.name);
    }
    plan.instruments = {owner, clone};

    PerformanceScore score;
    for (const auto& instrument : plan.instruments) {
        PerformanceCell cell;
        cell.id = instrument.id + "_literal_loop";
        cell.lengthBeats = 4.0;
        cell.ownedVoices = {instrument.sourceVoice};
        for (auto beat = 0; beat < 4; ++beat)
            cell.notes.push_back({static_cast<double>(beat), .5, 62 + beat % 2, 76,
                                  instrument.sourceVoice, MetricIntent::StrictGrid,
                                  instrument.id});
        score.cells.push_back(std::move(cell));
        for (auto section = 0; section < 4; ++section) {
            PerformancePlacement placement;
            placement.cellId = instrument.id + "_literal_loop";
            placement.sectionIndex = section;
            placement.repeats = 32;
            placement.fragmentEnd = 4.0;
            score.placements.push_back(std::move(placement));
        }
    }

    const auto findings = SelectiveRepair::performanceDeficits(plan, score, {0, 1});
    const auto cloned = std::find_if(findings.begin(), findings.end(), [](const auto& finding) {
        return finding.instrumentId == "supposed_counterpoint";
    });
    const auto cloneEvidence = cloned == findings.end() ? std::string{"missing finding"} :
        " duplicate=" + std::to_string(cloned->duplicatedIndependentLine) +
        " against=" + cloned->duplicatedWithInstrumentId +
        " overlap=" + std::to_string(cloned->duplicateEventOverlap) +
        " evolution=" + std::to_string(cloned->missingSectionalEvolution) +
        " states=" + std::to_string(cloned->sectionalStates) + "/" +
        std::to_string(cloned->minimumSectionalStates) +
        " notes=" + std::to_string(cloned->notes) +
        " bars=" + std::to_string(cloned->activeBars) +
        " sections=" + std::to_string(cloned->sections);
    require(cloned != findings.end() && cloned->duplicatedIndependentLine &&
                cloned->duplicatedWithInstrumentId == "motion_owner" &&
                cloned->duplicateEventOverlap > .99 &&
                cloned->missingSectionalEvolution && cloned->sectionalStates == 1 &&
                cloned->minimumSectionalStates == 4 &&
                SelectiveRepair::requiresReplacement(*cloned),
            "Cloned persistent counterpoint must require transactional replacement: " +
                cloneEvidence);

    const auto constraints = SelectiveRepair::classifyPerformanceDeficits(
        plan, {*cloned}, false);
    require(constraints.size() == 1 && constraints.front().blocksPublication &&
                std::find(constraints.front().operations.begin(),
                          constraints.front().operations.end(),
                          PerformanceRepairOperation::SeparateIndependentLine) !=
                    constraints.front().operations.end() &&
                std::find(constraints.front().operations.begin(),
                          constraints.front().operations.end(),
                          PerformanceRepairOperation::DevelopSectionalEvolution) !=
                    constraints.front().operations.end(),
            "Measured clone and stasis findings must route to focused AI operations");
    require(SelectiveRepair::consolidatableDuplicateTargets(plan, constraints, false) ==
                std::vector<std::size_t>{1},
            "An unresolved unrequested clone must be consolidatable after bounded recovery");
    auto explicitClonePlan = plan;
    explicitClonePlan.instruments[1].explicitPromptIdentity = true;
    require(SelectiveRepair::consolidatableDuplicateTargets(
                explicitClonePlan, constraints, true).empty(),
            "A user-named identity must never be lost during clone consolidation");
    auto protagonistClonePlan = plan;
    protagonistClonePlan.narrativeSpine.protagonistInstrumentId = "supposed_counterpoint";
    require(SelectiveRepair::consolidatableDuplicateTargets(
                protagonistClonePlan, constraints, false).empty(),
            "The declared protagonist must never be retired as a redundant lane");
    SongPlan soleBassPlan;
    InstrumentAssignment soleBass;
    soleBass.id = "only_bass";
    soleBass.sourceVoice = VoiceId::MovementBass;
    InstrumentAssignment harmonicCounterpart;
    harmonicCounterpart.id = "harmonic_counterpart";
    harmonicCounterpart.sourceVoice = VoiceId::HarmonicFoundation;
    soleBassPlan.instruments = {soleBass, harmonicCounterpart};
    auto soleBassConstraint = constraints.front();
    soleBassConstraint.evidence.instrumentIndex = 0;
    soleBassConstraint.evidence.instrumentId = soleBass.id;
    soleBassConstraint.evidence.duplicatedWithInstrumentId = harmonicCounterpart.id;
    require(SelectiveRepair::consolidatableDuplicateTargets(
                soleBassPlan, {soleBassConstraint}, false).empty(),
            "The only authored bass owner must survive clone consolidation");

    auto differentlyArticulatedClone = score;
    for (auto& note : differentlyArticulatedClone.cells[1].notes)
        note.durationBeats = note.durationBeats == .5 ? 1.5 : .5;
    const auto articulatedFindings = SelectiveRepair::performanceDeficits(
        plan, differentlyArticulatedClone, {1});
    const auto articulatedClone = std::find_if(articulatedFindings.begin(),
        articulatedFindings.end(), [](const auto& finding) {
            return finding.instrumentId == "supposed_counterpoint";
        });
    require(articulatedClone != articulatedFindings.end() &&
                articulatedClone->duplicatedIndependentLine &&
                articulatedClone->duplicateEventOverlap > .99,
            "Changing articulation must not disguise an otherwise literal melodic clone");

    auto complementaryHarmony = score;
    for (auto& note : complementaryHarmony.cells[1].notes) note.pitch += 12;
    const auto complementaryFindings = SelectiveRepair::performanceDeficits(
        plan, complementaryHarmony, {1});
    const auto complementary = std::find_if(complementaryFindings.begin(),
        complementaryFindings.end(), [](const auto& finding) {
            return finding.instrumentId == "supposed_counterpoint";
        });
    require(complementary != complementaryFindings.end() &&
                !complementary->duplicatedIndependentLine &&
                complementary->missingSectionalEvolution,
            "Shared harmonic timing at another register must not be classified as cloned MIDI");
    const auto complementaryConstraints = SelectiveRepair::classifyPerformanceDeficits(
        plan, {*complementary}, false);
    require(complementaryConstraints.size() == 1 &&
                !complementaryConstraints.front().blocksPublication &&
                SelectiveRepair::editorialTargets(complementaryConstraints) ==
                    std::vector<std::size_t>{1},
            "Insufficient sectional evolution must remain repairable editorial evidence");

    SongPlan narrativePlan;
    InstrumentAssignment protagonist;
    protagonist.id = "protagonist";
    protagonist.instrumentId = "lead_synth";
    protagonist.name = "Protagonist";
    protagonist.sourceVoice = VoiceId::Lead;
    narrativePlan.instruments = {protagonist};
    narrativePlan.narrativeSpine.protagonistInstrumentId = protagonist.id;
    PerformanceCoverageDeficit unresolved;
    unresolved.instrumentIndex = 0;
    unresolved.instrumentId = protagonist.id;
    unresolved.notes = 48;
    unresolved.minimumNotes = 12;
    unresolved.activeBars = 24;
    unresolved.minimumActiveBars = 12;
    unresolved.phrases = 4;
    unresolved.minimumPhrases = 3;
    unresolved.missingCodaResolution = true;
    const auto narrativeConstraints = SelectiveRepair::classifyPerformanceDeficits(
        narrativePlan, {unresolved}, false);
    require(narrativeConstraints.size() == 1 &&
                narrativeConstraints.front().blocksPublication &&
                SelectiveRepair::blockingTargets(narrativeConstraints) ==
                    std::vector<std::size_t>{0},
            "A populated protagonist may not publish without audible coda resolution");

    narrativePlan.totalBars = 16;
    narrativePlan.beatsPerBar = 4.0;
    narrativePlan.rootPitchClass = 3;
    narrativePlan.instrumentCastAuthored = true;
    SongSection resolution;
    resolution.name = "Resolution";
    resolution.startBar = 0;
    resolution.bars = 16;
    narrativePlan.sections = {resolution};
    NarrativeAct resolutionAct;
    resolutionAct.stage = NarrativeStage::Resolution;
    resolutionAct.sectionName = resolution.name;
    narrativePlan.narrativeSpine.acts = {resolutionAct};

    PerformanceScore promotedScore;
    PerformanceCell promoted;
    promoted.id = "promoted_ai_phrase";
    promoted.lengthBeats = 8.0;
    promoted.ownedVoices = {VoiceId::Lead};
    promoted.notes = {
        {0.0, .5, 61, 84, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {2.0, .5, 63, 88, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {4.0, .5, 66, 82, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {6.0, 1.0, 65, 78, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
    };
    promotedScore.cells.push_back(promoted);
    PerformancePlacement premise;
    premise.cellId = promoted.id;
    premise.sectionIndex = 0;
    premise.startBeat = 0.0;
    premise.repeats = 1;
    premise.fragmentEnd = promoted.lengthBeats;
    promotedScore.placements.push_back(premise);

    require(SelectiveRepair::ensureAuthoredProtagonistCoda(
                narrativePlan, promotedScore, promoted.id),
            "An authored promoted protagonist must receive a reusable coda placement");
    const auto promotedFindings = SelectiveRepair::performanceDeficits(
        narrativePlan, promotedScore, {0});
    const auto promotedDeficit = std::find_if(promotedFindings.begin(), promotedFindings.end(),
        [](const auto& finding) { return finding.instrumentId == "protagonist"; });
    require(promotedDeficit == promotedFindings.end() ||
                !promotedDeficit->missingCodaResolution,
            "The promoted authored phrase must resolve at the audible final boundary");
    const auto resolvedPlacementCount = promotedScore.placements.size();
    require(!SelectiveRepair::ensureAuthoredProtagonistCoda(
                narrativePlan, promotedScore, promoted.id) &&
                promotedScore.placements.size() == resolvedPlacementCount,
            "Coda convergence must be idempotent and never duplicate an already resolved protagonist");

    auto aftermathPlan = narrativePlan;
    aftermathPlan.totalBars = 24;
    aftermathPlan.sections.front().bars = 16;
    SongSection aftermath;
    aftermath.name = "Aftermath";
    aftermath.startBar = 16;
    aftermath.bars = 8;
    aftermathPlan.sections.push_back(aftermath);
    PerformanceScore aftermathScore;
    aftermathScore.cells = {promoted};
    aftermathScore.placements = {premise};
    require(SelectiveRepair::ensureAuthoredProtagonistCoda(
                aftermathPlan, aftermathScore, promoted.id),
            "A declared resolution followed by aftermath must close at the absolute song boundary");
    require(std::any_of(aftermathScore.placements.begin(), aftermathScore.placements.end(),
                [](const auto& placement) {
                    return placement.sectionIndex == 1 &&
                        placement.purpose == "authored protagonist coda";
                }),
            "The protagonist coda must be placed in the actual final section, not the earlier Resolution act");
    const auto aftermathFindings = SelectiveRepair::performanceDeficits(
        aftermathPlan, aftermathScore, {0});
    const auto aftermathDeficit = std::find_if(
        aftermathFindings.begin(), aftermathFindings.end(),
        [](const auto& finding) { return finding.instrumentId == "protagonist"; });
    require(aftermathDeficit == aftermathFindings.end() ||
                !aftermathDeficit->missingCodaResolution,
            "Absolute-boundary verification must accept the connected aftermath coda");

    PerformanceScore oversizedSourceScore;
    auto oversizedSource = promoted;
    oversizedSource.id = "oversized_authored_phrase";
    oversizedSource.lengthBeats = 128.0;
    oversizedSource.notes = {
        {80.0, .5, 61, 84, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {96.0, .5, 63, 88, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {112.0, 1.0, 65, 78, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
    };
    oversizedSourceScore.cells.push_back(oversizedSource);
    auto oversizedPlacement = premise;
    oversizedPlacement.cellId = oversizedSource.id;
    oversizedPlacement.fragmentEnd = oversizedSource.lengthBeats;
    oversizedSourceScore.placements.push_back(oversizedPlacement);
    require(!SelectiveRepair::ensureAuthoredProtagonistCoda(
                narrativePlan, oversizedSourceScore, oversizedSource.id),
            "Isolated marker notes must never be compacted into a synthetic protagonist coda");
    const auto oversizedFindings = SelectiveRepair::performanceDeficits(
        narrativePlan, oversizedSourceScore, {0});
    const auto oversizedDeficit = std::find_if(oversizedFindings.begin(), oversizedFindings.end(),
        [](const auto& finding) { return finding.instrumentId == "protagonist"; });
    require(oversizedDeficit != oversizedFindings.end() &&
                oversizedDeficit->missingCodaResolution,
            "Sparse markers must remain visibly unresolved for focused AI authorship");

    PerformanceScore fractionalScaleSource;
    auto fractionalCell = promoted;
    fractionalCell.id = "fractional_scale_authored_phrase";
    fractionalCell.lengthBeats = 48.0;
    fractionalCell.notes.clear();
    for (auto index = 0; index < 12; ++index)
        fractionalCell.notes.push_back({index * 3.0, index == 11 ? 1.0 : .5,
            61 + (index % 5), 78 + (index % 3) * 4, VoiceId::Lead,
            MetricIntent::StrictGrid, protagonist.id});
    fractionalScaleSource.cells.push_back(fractionalCell);
    auto fractionalPlacement = premise;
    fractionalPlacement.cellId = fractionalCell.id;
    fractionalPlacement.fragmentEnd = fractionalCell.lengthBeats;
    fractionalScaleSource.placements.push_back(fractionalPlacement);
    require(SelectiveRepair::ensureAuthoredProtagonistCoda(
                narrativePlan, fractionalScaleSource, fractionalCell.id),
            "A long authored phrase must receive a coda using a legal normalized time scale");
    narrativePlan.performanceScore = fractionalScaleSource;
    SongComposer::normalizePlan(narrativePlan);
    const auto normalizedCodaFindings = SelectiveRepair::performanceDeficits(
        narrativePlan, narrativePlan.performanceScore, {0});
    const auto normalizedCodaDeficit = std::find_if(
        normalizedCodaFindings.begin(), normalizedCodaFindings.end(),
        [](const auto& finding) { return finding.instrumentId == "protagonist"; });
    require(normalizedCodaDeficit == normalizedCodaFindings.end() ||
                !normalizedCodaDeficit->missingCodaResolution,
            "Plan normalization must not move a verified protagonist coda away from the audible boundary");

    SongPlan longNarrative;
    longNarrative.totalBars = 128;
    longNarrative.beatsPerBar = 4.0;
    longNarrative.rootPitchClass = 0;
    longNarrative.instrumentCastAuthored = true;
    InstrumentAssignment longLead;
    longLead.id = "long_form_protagonist";
    longLead.sourceVoice = VoiceId::Lead;
    longLead.minimumPitch = 48;
    longLead.maximumPitch = 84;
    longLead.contentLaneId = longLead.id;
    longLead.lineRelationship = "independent";
    for (auto sectionIndex = 0; sectionIndex < 4; ++sectionIndex) {
        SongSection section;
        section.name = "Story " + std::to_string(sectionIndex + 1);
        section.startBar = sectionIndex * 32;
        section.bars = 32;
        longLead.activeSections.push_back(section.name);
        longNarrative.sections.push_back(section);
    }
    longNarrative.instruments = {longLead};
    longNarrative.narrativeSpine.protagonistInstrumentId = longLead.id;
    PerformanceScore literalLead;
    PerformanceCell literalCell;
    literalCell.id = "literal_lead";
    literalCell.themeId = "story_theme";
    literalCell.lengthBeats = 8.0;
    literalCell.ownedVoices = {VoiceId::Lead};
    literalCell.notes = {
        {0.0, .5, 60, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {1.0, .5, 67, 82, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {2.0, .5, 62, 78, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {3.0, 1.0, 69, 76, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
    };
    literalLead.cells.push_back(literalCell);
    for (auto sectionIndex = 0; sectionIndex < 4; ++sectionIndex) {
        for (auto phrase = 0; phrase < 2; ++phrase) {
            PerformancePlacement placement;
            placement.cellId = literalCell.id;
            placement.sectionIndex = sectionIndex;
            placement.startBeat = phrase * 32.0;
            placement.repeats = 1;
            placement.fragmentEnd = literalCell.lengthBeats;
            literalLead.placements.push_back(placement);
        }
    }
    const auto literalLeadFindings = SelectiveRepair::performanceDeficits(
        longNarrative, literalLead, {0});
    require(literalLeadFindings.size() == 1 &&
                !literalLeadFindings.front().missingNarrativePresence &&
                literalLeadFindings.front().narrativePhraseWindows == 8 &&
                literalLeadFindings.front().minimumNarrativePhraseWindows == 4 &&
                literalLeadFindings.front().missingThematicDevelopment &&
                literalLeadFindings.front().literalPlacementRatio > .99 &&
                literalLeadFindings.front().missingMelodicSpeech,
            "A ubiquitous literal leap-cell must not masquerade as a developed AI protagonist");

    auto patientLead = literalLead;
    patientLead.placements.erase(
        std::remove_if(patientLead.placements.begin(), patientLead.placements.end(),
            [](const auto& placement) { return placement.startBeat > 0.0; }),
        patientLead.placements.end());
    const auto narrativePatient = SelectiveRepair::performanceDeficits(
        longNarrative, patientLead, {0});
    auto hypnoticPlan = longNarrative;
    hypnoticPlan.compositionBehavior = CompositionBehavior::Hypnotic;
    const auto hypnoticPatient = SelectiveRepair::performanceDeficits(
        hypnoticPlan, patientLead, {0});
    require(narrativePatient.size() == 1 && hypnoticPatient.size() == 1 &&
                !narrativePatient.front().missingNarrativePresence &&
                narrativePatient.front().narrativePhraseWindows == 4 &&
                narrativePatient.front().minimumNarrativePhraseWindows == 4 &&
                !hypnoticPatient.front().missingNarrativePresence &&
                hypnoticPatient.front().narrativePhraseWindows == 4 &&
                hypnoticPatient.front().minimumNarrativePhraseWindows == 4 &&
                hypnoticPatient.front().missingCodaResolution &&
                hypnoticPatient.front().missingThematicDevelopment,
            "One connected statement per authored act can suffice; coda and development remain independent obligations");

    auto lateEntranceLead = literalLead;
    std::erase_if(lateEntranceLead.placements,
        [](const auto& placement) { return placement.sectionIndex == 0; });
    const auto lateEntranceFindings = SelectiveRepair::performanceDeficits(
        longNarrative, lateEntranceLead, {0});
    require(lateEntranceFindings.size() == 1 &&
                lateEntranceFindings.front().narrativePhraseWindows >=
                    lateEntranceFindings.front().minimumNarrativePhraseWindows &&
                lateEntranceFindings.front().missingNarrativePresence &&
                std::find(lateEntranceFindings.front().missingNarrativeWindowStartBars.begin(),
                          lateEntranceFindings.front().missingNarrativeWindowStartBars.end(), 0) !=
                    lateEntranceFindings.front().missingNarrativeWindowStartBars.end(),
            "A protagonist cannot satisfy narrative presence statistically while skipping the premise");
    const auto missingPremiseTargets =
        SelectiveRepair::focusedProtagonistWindowTargets(
            longNarrative, lateEntranceFindings.front());
    require(!missingPremiseTargets.empty() && missingPremiseTargets.front() == 0,
            "An omitted active premise must receive a focused repair target");

    auto deliberateEntrancePlan = longNarrative;
    deliberateEntrancePlan.instruments.front().activeSections.erase(
        deliberateEntrancePlan.instruments.front().activeSections.begin());
    const auto deliberateEntranceFindings = SelectiveRepair::performanceDeficits(
        deliberateEntrancePlan, lateEntranceLead, {0});
    require(deliberateEntranceFindings.size() == 1 &&
                !deliberateEntranceFindings.front().missingNarrativePresence &&
                std::find(deliberateEntranceFindings.front().missingNarrativeWindowStartBars.begin(),
                          deliberateEntranceFindings.front().missingNarrativeWindowStartBars.end(), 0) ==
                    deliberateEntranceFindings.front().missingNarrativeWindowStartBars.end(),
            "An explicitly delayed protagonist entrance must not require MIDI in a withdrawn opening section");

    auto climaxTargetPlan = deliberateEntrancePlan;
    NarrativeAct climaxAct;
    climaxAct.sectionName = climaxTargetPlan.sections[2].name;
    climaxAct.stage = NarrativeStage::Climax;
    climaxTargetPlan.narrativeSpine.acts.push_back(climaxAct);
    PerformanceCoverageDeficit climaxGap;
    climaxGap.instrumentIndex = 0;
    climaxGap.instrumentId = longLead.id;
    climaxGap.missingNarrativePresence = true;
    climaxGap.narrativePhraseWindows = 13;
    climaxGap.minimumNarrativePhraseWindows = 7;
    climaxGap.missingNarrativeWindowStartBars = {0, 64, 72, 80, 88};
    const auto climaxTargets = SelectiveRepair::focusedProtagonistWindowTargets(
        climaxTargetPlan, climaxGap);
    require(climaxTargets.size() == 1 && climaxTargets.front() == 64,
            "A missing active climax needs focused AI repair even when total phrase count exceeds its floor");

    PerformanceCoverageDeficit marginalSpeech;
    marginalSpeech.instrumentId = longLead.id;
    marginalSpeech.notes = marginalSpeech.authoredNotes = 112;
    marginalSpeech.minimumNotes = marginalSpeech.minimumAuthoredNotes = 18;
    marginalSpeech.activeBars = 56;
    marginalSpeech.minimumActiveBars = 12;
    marginalSpeech.phrases = 7;
    marginalSpeech.minimumPhrases = 4;
    marginalSpeech.sections = 7;
    marginalSpeech.minimumSections = 2;
    marginalSpeech.sectionalStates = 7;
    marginalSpeech.minimumSectionalStates = 5;
    marginalSpeech.narrativePhraseWindows = 7;
    marginalSpeech.minimumNarrativePhraseWindows = 3;
    marginalSpeech.melodicIntervals = 111;
    marginalSpeech.melodicStepRatio = 27.0 / 111.0;
    marginalSpeech.missingMelodicSpeech = true;
    require(SelectiveRepair::deferableMarginalMelodicSpeech(longNarrative, marginalSpeech),
            "One missing melodic step in a complete protagonist should wait for full-score audition");
    marginalSpeech.melodicStepRatio = 26.0 / 111.0;
    require(!SelectiveRepair::deferableMarginalMelodicSpeech(longNarrative, marginalSpeech),
            "Two missing melodic steps must not be treated as a marginal observation");
    marginalSpeech.melodicStepRatio = 27.0 / 111.0;
    marginalSpeech.missingCodaResolution = true;
    require(!SelectiveRepair::deferableMarginalMelodicSpeech(longNarrative, marginalSpeech),
            "A weak coda cannot be hidden by the melodic-step exception");

    // The local AI-only profile must not discard a complete authored lead solely
    // because the fixed narrative-window or step ratio targets disagree with it.
    auto localLead = marginalSpeech;
    localLead.missingCodaResolution = false;
    localLead.narrativePhraseWindows = 4;
    localLead.minimumNarrativePhraseWindows = 6;
    localLead.missingNarrativePresence = true;
    localLead.melodicStepRatio = 3.0 / 45.0;
    localLead.melodicIntervals = 45;
    require(SelectiveRepair::deferableLocalProtagonistEditorial(longNarrative, localLead),
            "A complete AI-authored lead must reach whole-score audition despite editorial phrase and step metrics");
    localLead.narrativePhraseWindows = 0;
    require(!SelectiveRepair::deferableLocalProtagonistEditorial(longNarrative, localLead),
            "An entirely absent narrative voice is not a marginal editorial observation");
    localLead.narrativePhraseWindows = 4;
    localLead.missingCodaResolution = true;
    require(!SelectiveRepair::deferableLocalProtagonistEditorial(longNarrative, localLead),
            "The local editorial exception must not hide a missing protagonist coda");
    localLead.missingCodaResolution = false;
    localLead.notes = 0;
    require(!SelectiveRepair::deferableLocalProtagonistEditorial(longNarrative, localLead),
            "An empty protagonist cannot be accepted as an editorial observation");
    localLead.notes = 82;
    localLead.missingAuthoredDevelopment = true;
    require(!SelectiveRepair::deferableLocalProtagonistEditorial(longNarrative, localLead),
            "The local exception must not hide underwritten source material");

    auto weaklyConnectedLead = literalLead;
    weaklyConnectedLead.cells.front().id = "weakly_connected_lead";
    weaklyConnectedLead.cells.front().notes = {
        {0.0, .3, 60, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {.5, .3, 62, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {1.0, .3, 67, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {1.5, .3, 72, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {2.0, .3, 74, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {2.5, .3, 67, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {3.0, .3, 60, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {3.5, .3, 65, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {4.0, .3, 72, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {4.5, .8, 67, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
    };
    for (auto& placement : weaklyConnectedLead.placements)
        placement.cellId = weaklyConnectedLead.cells.front().id;
    const auto weakConnectionFindings = SelectiveRepair::performanceDeficits(
        longNarrative, weaklyConnectedLead, {0});
    require(!weakConnectionFindings.empty() &&
                weakConnectionFindings.front().melodicStepRatio >= .15 &&
                weakConnectionFindings.front().melodicStepRatio < .25 &&
                weakConnectionFindings.front().missingMelodicSpeech,
            "A merely tonal line below 25 percent conjunct motion must be rewritten as melodic speech");

    auto markerLead = literalLead;
    markerLead.cells.front().id = "isolated_markers";
    markerLead.cells.front().lengthBeats = 24.0;
    markerLead.cells.front().notes = {
        {0.0, .5, 60, 80, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {8.0, .5, 62, 78, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
        {16.0, .5, 67, 76, VoiceId::Lead, MetricIntent::StrictGrid, longLead.id},
    };
    for (auto& placement : markerLead.placements)
        placement.cellId = markerLead.cells.front().id;
    const auto markerFindings = SelectiveRepair::performanceDeficits(
        longNarrative, markerLead, {0});
    require(markerFindings.size() == 1 &&
                markerFindings.front().narrativePhraseWindows == 0 &&
                markerFindings.front().missingNarrativePresence,
            "Three isolated marker notes in an eight-bar window must never count as a protagonist phrase");
    auto editorialLeadFinding = literalLeadFindings.front();
    editorialLeadFinding.missingCodaResolution = false; // Resolution is tested independently above.
    const auto literalLeadConstraints = SelectiveRepair::classifyPerformanceDeficits(
        longNarrative, {editorialLeadFinding}, false);
    require(literalLeadConstraints.size() == 1 &&
                !literalLeadConstraints.front().blocksPublication &&
                std::find(literalLeadConstraints.front().operations.begin(),
                          literalLeadConstraints.front().operations.end(),
                          PerformanceRepairOperation::TransformThematicReturns) !=
                    literalLeadConstraints.front().operations.end() &&
                std::find(literalLeadConstraints.front().operations.begin(),
                          literalLeadConstraints.front().operations.end(),
                          PerformanceRepairOperation::ShapeMelodicSpeech) !=
                    literalLeadConstraints.front().operations.end(),
            "A populated protagonist must route literal repetition and disconnected leaps to focused editorial rewriting without erasing the song");

    auto nearlyCompleteLead = literalLeadFindings.front();
    nearlyCompleteLead.missingNarrativePresence = true;
    nearlyCompleteLead.missingThematicDevelopment = false;
    nearlyCompleteLead.missingMelodicSpeech = false;
    nearlyCompleteLead.missingCodaResolution = false;
    nearlyCompleteLead.duplicatedIndependentLine = false;
    const auto nearlyCompleteConstraints = SelectiveRepair::classifyPerformanceDeficits(
        longNarrative, {nearlyCompleteLead}, false);
    require(nearlyCompleteConstraints.size() == 1 &&
                !nearlyCompleteConstraints.front().blocksPublication &&
                std::find(nearlyCompleteConstraints.front().operations.begin(),
                          nearlyCompleteConstraints.front().operations.end(),
                          PerformanceRepairOperation::DevelopNarrativePresence) !=
                    nearlyCompleteConstraints.front().operations.end(),
            "A populated resolved protagonist just below its narrative target must remain an editorial objective");

    SongPlan continuityPlan;
    continuityPlan.totalBars = 16;
    continuityPlan.beatsPerBar = 4.0;
    continuityPlan.percussionFreeIntent = true;
    continuityPlan.productionLanguage.domain = ProductionDomain::Hybrid;
    continuityPlan.productionLanguage.electronicIntent = .9;
    for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        SongSection section;
        section.name = "Continuity " + std::to_string(sectionIndex + 1);
        section.startBar = sectionIndex * 8;
        section.bars = 8;
        section.density = .58;
        continuityPlan.sections.push_back(section);
    }
    InstrumentAssignment floorA;
    floorA.id = "floor_a";
    floorA.sourceVoice = VoiceId::HarmonicFoundation;
    InstrumentAssignment floorB;
    floorB.id = "floor_b";
    floorB.sourceVoice = VoiceId::HarmonicUpper;
    InstrumentAssignment breathingLead;
    breathingLead.id = "breathing_lead";
    breathingLead.sourceVoice = VoiceId::Lead;
    continuityPlan.instruments = {floorA, floorB, breathingLead};
    PerformanceScore continuousEnsemble;
    for (const auto instrumentIndex : {0, 1}) {
        PerformanceCell floorCell;
        floorCell.id = "floor_cell_" + std::to_string(instrumentIndex);
        floorCell.lengthBeats = 16.0;
        floorCell.ownedVoices = {continuityPlan.instruments[instrumentIndex].sourceVoice};
        floorCell.notes.push_back({0.0, 16.0, 48 + instrumentIndex * 7, 62,
            continuityPlan.instruments[instrumentIndex].sourceVoice,
            MetricIntent::StrictGrid, continuityPlan.instruments[instrumentIndex].id});
        continuousEnsemble.cells.push_back(floorCell);
        for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
            PerformancePlacement placement;
            placement.cellId = floorCell.id;
            placement.sectionIndex = sectionIndex;
            placement.repeats = 2;
            placement.fragmentEnd = floorCell.lengthBeats;
            continuousEnsemble.placements.push_back(placement);
        }
    }
    PerformanceCell leadCell;
    leadCell.id = "breathing_phrase";
    leadCell.lengthBeats = 4.0;
    leadCell.ownedVoices = {VoiceId::Lead};
    leadCell.notes = {{0.0, .75, 67, 78, VoiceId::Lead,
                       MetricIntent::StrictGrid, breathingLead.id}};
    continuousEnsemble.cells.push_back(leadCell);
    for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        PerformancePlacement placement;
        placement.cellId = leadCell.id;
        placement.sectionIndex = sectionIndex;
        placement.startBeat = 12.0;
        placement.fragmentEnd = leadCell.lengthBeats;
        continuousEnsemble.placements.push_back(placement);
    }
    const auto continuity = SelectiveRepair::ensembleContinuity(
        continuityPlan, continuousEnsemble);
    require(continuity.ready && continuity.silentWindows == 0 &&
                continuity.audibleCoverage >= .99 &&
                continuity.harmonicFloorCoverage >= .99,
            "A breathing protagonist must be publishable when two authored harmonic layers sustain the ensemble");
    auto hollowEnsemble = continuousEnsemble;
    hollowEnsemble.placements.erase(std::remove_if(
        hollowEnsemble.placements.begin(), hollowEnsemble.placements.end(),
        [](const auto& placement) { return placement.cellId == "floor_cell_1"; }),
        hollowEnsemble.placements.end());
    const auto hollowContinuity = SelectiveRepair::ensembleContinuity(
        continuityPlan, hollowEnsemble);
    require(!hollowContinuity.ready && hollowContinuity.harmonicFloorCoverage < .01,
            "Sparse accompaniment must fail the ensemble gate even when a protagonist still speaks");
    auto intentionalBreathPlan = continuityPlan;
    intentionalBreathPlan.sections[1].function = "complete silence";
    auto intentionalBreathScore = continuousEnsemble;
    intentionalBreathScore.placements.erase(std::remove_if(
        intentionalBreathScore.placements.begin(), intentionalBreathScore.placements.end(),
        [](const auto& placement) { return placement.sectionIndex == 1; }),
        intentionalBreathScore.placements.end());
    const auto intentionalBreath = SelectiveRepair::ensembleContinuity(
        intentionalBreathPlan, intentionalBreathScore);
    require(intentionalBreath.ready && intentionalBreath.intentionalBreathWindows == 2 &&
                intentionalBreath.longestGlobalSilenceBeats < .01,
            "A section explicitly authored as complete silence must not be misclassified as an accidental hole");

    auto longBreathPlan = continuityPlan;
    longBreathPlan.totalBars = 24;
    auto thirdSection = longBreathPlan.sections.back();
    thirdSection.name = "Continuity 3";
    thirdSection.startBar = 16;
    longBreathPlan.sections.push_back(thirdSection);
    PerformanceScore isolatedBreathScore;
    isolatedBreathScore.cells = {continuousEnsemble.cells[0], continuousEnsemble.cells[1]};
    for (auto instrumentIndex = 0; instrumentIndex < 2; ++instrumentIndex) {
        for (auto sectionIndex = 0; sectionIndex < 3; ++sectionIndex) {
            for (auto half = 0; half < 2; ++half) {
                if (sectionIndex == 1 && half == 0) continue;
                PerformancePlacement placement;
                placement.cellId = "floor_cell_" + std::to_string(instrumentIndex);
                placement.sectionIndex = sectionIndex;
                placement.startBeat = half * 16.0;
                placement.fragmentEnd = 16.0;
                isolatedBreathScore.placements.push_back(placement);
            }
        }
    }
    const auto isolatedBreath = SelectiveRepair::ensembleContinuity(
        longBreathPlan, isolatedBreathScore);
    require(isolatedBreath.ready && isolatedBreath.silentWindows == 1 &&
                isolatedBreath.maximumConsecutiveSilentWindows == 1,
            "One isolated four-bar breath in a long dense form must remain publishable");
    auto consecutiveSilenceScore = isolatedBreathScore;
    consecutiveSilenceScore.placements.erase(std::remove_if(
        consecutiveSilenceScore.placements.begin(), consecutiveSilenceScore.placements.end(),
        [](const auto& placement) { return placement.sectionIndex == 1; }),
        consecutiveSilenceScore.placements.end());
    const auto consecutiveSilence = SelectiveRepair::ensembleContinuity(
        longBreathPlan, consecutiveSilenceScore);
    require(!consecutiveSilence.ready &&
                consecutiveSilence.maximumConsecutiveSilentWindows == 2,
            "Adjacent four-bar holes must still fail the ensemble continuity gate");

    SongPlan chordBedPlan;
    chordBedPlan.totalBars = 64;
    chordBedPlan.beatsPerBar = 4.0;
    chordBedPlan.instrumentCastAuthored = true;
    for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        SongSection section;
        section.name = "Chord scene " + std::to_string(sectionIndex + 1);
        section.startBar = sectionIndex * 32;
        section.bars = 32;
        chordBedPlan.sections.push_back(section);
    }
    InstrumentAssignment chordBed;
    chordBed.id = "central_chord_bed";
    chordBed.instrumentId = "analog_pad";
    chordBed.sourceVoice = VoiceId::HarmonicFoundation;
    chordBed.role = "warm harmonic body | primary_chord_bed";
    chordBed.minimumPitch = 42;
    chordBed.maximumPitch = 84;
    chordBed.activeSections = {"Chord scene 1", "Chord scene 2"};
    chordBedPlan.instruments = {chordBed};

    PerformanceCell monophonicBed;
    monophonicBed.id = "mono_bed";
    monophonicBed.lengthBeats = 4.0;
    monophonicBed.ownedVoices = {VoiceId::HarmonicFoundation};
    monophonicBed.notes = {{0.0, 3.5, 54, 62, VoiceId::HarmonicFoundation,
                            MetricIntent::StrictGrid, chordBed.id}};
    PerformanceScore monophonicScore;
    monophonicScore.cells = {monophonicBed};
    for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        PerformancePlacement placement;
        placement.cellId = monophonicBed.id;
        placement.sectionIndex = sectionIndex;
        placement.repeats = 32;
        placement.fragmentEnd = monophonicBed.lengthBeats;
        monophonicScore.placements.push_back(placement);
    }
    const auto monophonicDeficits = SelectiveRepair::performanceDeficits(
        chordBedPlan, monophonicScore, {0});
    require(monophonicDeficits.size() == 1 &&
                monophonicDeficits.front().missingCentralChordBed &&
                monophonicDeficits.front().missingChordBedBreath &&
                monophonicDeficits.front().polyphonicChordAttacks == 0,
            "A continuous monophonic foundation must not masquerade as the exported central chord bed");
    const auto chordOperations = SelectiveRepair::classifyPerformanceDeficits(
        chordBedPlan, monophonicDeficits, false);
    require(chordOperations.size() == 1 && !chordOperations.front().blocksPublication &&
                std::find(chordOperations.front().operations.begin(),
                          chordOperations.front().operations.end(),
                          PerformanceRepairOperation::AuthorCentralChordBed) !=
                    chordOperations.front().operations.end() &&
                std::find(chordOperations.front().operations.begin(),
                          chordOperations.front().operations.end(),
                          PerformanceRepairOperation::ShapeHarmonicBreath) !=
                    chordOperations.front().operations.end(),
            "Chord-bed polyphony and breath must route to a bounded non-destructive AI rewrite");

    auto polyphonicBed = monophonicBed;
    polyphonicBed.id = "polyphonic_bed";
    polyphonicBed.notes = {
        {0.0, 3.5, 54, 62, VoiceId::HarmonicFoundation,
         MetricIntent::StrictGrid, chordBed.id},
        {0.0, 3.5, 57, 58, VoiceId::HarmonicFoundation,
         MetricIntent::StrictGrid, chordBed.id},
        {0.0, 3.5, 61, 56, VoiceId::HarmonicFoundation,
         MetricIntent::StrictGrid, chordBed.id},
    };
    PerformanceScore breathingChordScore;
    breathingChordScore.cells = {polyphonicBed};
    for (auto sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        PerformancePlacement placement;
        placement.cellId = polyphonicBed.id;
        placement.sectionIndex = sectionIndex;
        placement.repeats = 24;
        placement.fragmentEnd = polyphonicBed.lengthBeats;
        breathingChordScore.placements.push_back(placement);
    }
    const auto breathingChordDeficits = SelectiveRepair::performanceDeficits(
        chordBedPlan, breathingChordScore, {0});
    const auto chordFinding = std::find_if(
        breathingChordDeficits.begin(), breathingChordDeficits.end(),
        [](const auto& finding) { return finding.instrumentId == "central_chord_bed"; });
    require(chordFinding == breathingChordDeficits.end() ||
                (!chordFinding->missingCentralChordBed &&
                 !chordFinding->missingChordBedBreath &&
                 chordFinding->polyphonicChordAttacks >=
                    chordFinding->minimumPolyphonicChordAttacks &&
                 chordFinding->longestChordBedBreathBars >= 2),
            "A self-contained polyphonic bed with sectional withdrawal must satisfy the new chord contract");

    auto chordArcPlan = chordBedPlan;
    chordArcPlan.sections.clear();
    for (auto sectionIndex = 0; sectionIndex < 4; ++sectionIndex) {
        SongSection section;
        section.name = "Arc scene " + std::to_string(sectionIndex + 1);
        section.startBar = sectionIndex * 16;
        section.bars = 16;
        chordArcPlan.sections.push_back(section);
    }
    chordArcPlan.instruments.front().activeSections = {
        "Arc scene 1", "Arc scene 2", "Arc scene 3", "Arc scene 4"};
    PerformanceScore incompleteChordArc;
    incompleteChordArc.cells = {polyphonicBed};
    for (auto sectionIndex : {1, 2}) {
        PerformancePlacement placement;
        placement.cellId = polyphonicBed.id;
        placement.sectionIndex = sectionIndex;
        placement.repeats = 16;
        placement.fragmentEnd = polyphonicBed.lengthBeats;
        incompleteChordArc.placements.push_back(placement);
    }
    for (auto sectionIndex : {0, 3}) {
        PerformancePlacement silentPlacement;
        silentPlacement.cellId = polyphonicBed.id;
        silentPlacement.sectionIndex = sectionIndex;
        silentPlacement.repeats = 1;
        silentPlacement.fragmentStart = 1.0;
        silentPlacement.fragmentEnd = 2.0;
        incompleteChordArc.placements.push_back(silentPlacement);
    }
    const auto chordArcDeficits = SelectiveRepair::performanceDeficits(
        chordArcPlan, incompleteChordArc, {0});
    require(chordArcDeficits.size() == 1 &&
                chordArcDeficits.front().missingChordBedNarrativeArc &&
                chordArcDeficits.front().chordBedNarrativeStages <
                    chordArcDeficits.front().minimumChordBedNarrativeStages,
            "Silent or clipped placements must not make a middle-only chord bed pass the audible narrative arc");

    auto unresolvedBedPlan = chordBedPlan;
    unresolvedBedPlan.rootPitchClass = 6; // F-sharp
    unresolvedBedPlan.chordPalette = {
        {"home", "F-sharp minor", 6, 6, {6, 9, 1}, HarmonicFunction::Tonic},
        {"loop_end", "A minor colour", 9, 9, {9, 0, 4}, HarmonicFunction::Colour,
         VoicingStrategy::Open, .62},
    };
    unresolvedBedPlan.sections.back().harmonicEvents = {
        {0, 0.0, "home", .6, "memory"},
        {30, 0.0, "loop_end", .8, "unresolved loop"},
    };
    const auto unresolvedBed = SelectiveRepair::performanceDeficits(
        unresolvedBedPlan, breathingChordScore, {0});
    require(unresolvedBed.size() == 1 && unresolvedBed.front().missingCodaResolution,
            "A primary chord bed ending on an arbitrary non-tonic loop chord must request harmonic closure");
    const auto unresolvedBedConstraints = SelectiveRepair::classifyPerformanceDeficits(
        unresolvedBedPlan, unresolvedBed, false);
    require(unresolvedBedConstraints.size() == 1 &&
                !unresolvedBedConstraints.front().blocksPublication,
            "A populated chord bed with local terminal debt must route to transactional repair instead of rejecting the whole song");

    auto transactionalPlan = unresolvedBedPlan;
    InstrumentAssignment untouchedUpper = chordBed;
    untouchedUpper.id = "untouched_upper";
    untouchedUpper.instrumentId = "poly_synth";
    untouchedUpper.name = "Untouched Upper";
    untouchedUpper.sourceVoice = VoiceId::HarmonicUpper;
    untouchedUpper.role = "independent upper memory";
    transactionalPlan.instruments.push_back(untouchedUpper);
    transactionalPlan.performanceScore = breathingChordScore;
    transactionalPlan.performanceScore.cells.front().ownedVoices.push_back(
        VoiceId::HarmonicUpper);
    transactionalPlan.performanceScore.cells.front().notes.push_back(
        {2.0, 1.0, 73, 51, VoiceId::HarmonicUpper,
         MetricIntent::StrictGrid, untouchedUpper.id});
    Pattern beforeClosure;
    beforeClosure.lengthBeats = transactionalPlan.sections.back().bars *
        transactionalPlan.beatsPerBar;
    PerformanceScoreEngine::replaceChunk(beforeClosure,
        transactionalPlan.performanceScore, 1, 0.0, beforeClosure.lengthBeats,
        transactionalPlan.instruments);
    std::vector<std::tuple<double, double, int>> untouchedBefore;
    std::vector<std::tuple<double, double, int>> bedPrefixBefore;
    for (const auto& note : beforeClosure.notes)
        if (note.partId == 2)
            untouchedBefore.emplace_back(note.startBeat, note.durationBeats, note.pitch);
        else if (note.partId == 1 && note.startBeat < beforeClosure.lengthBeats - 16.0)
            bedPrefixBefore.emplace_back(note.startBeat, note.durationBeats, note.pitch);
    require(SelectiveRepair::ensurePrimaryChordBedClosure(transactionalPlan),
            "A non-tonic chord-bed ending must receive a bounded local tonic closure");
    require(!SelectiveRepair::ensurePrimaryChordBedClosure(transactionalPlan),
            "A verified chord-bed closure must be idempotent");
    const auto repairedBed = SelectiveRepair::performanceDeficits(
        transactionalPlan, transactionalPlan.performanceScore, {0});
    require(repairedBed.empty() || !repairedBed.front().missingCodaResolution,
            "The independent publication audit must observe the repaired harmonic closure");
    require(!transactionalPlan.sections.back().harmonicEvents.empty() &&
                transactionalPlan.sections.back().harmonicEvents.back().chordId == "home" &&
                transactionalPlan.sections.back().harmonicEvents.back().barOffset == 28,
            "Only the final four bars must be rebound to the AI-authored tonic chord");
    Pattern afterClosure;
    afterClosure.lengthBeats = beforeClosure.lengthBeats;
    PerformanceScoreEngine::replaceChunk(afterClosure,
        transactionalPlan.performanceScore, 1, 0.0, afterClosure.lengthBeats,
        transactionalPlan.instruments);
    std::vector<std::tuple<double, double, int>> untouchedAfter;
    std::vector<std::tuple<double, double, int>> bedPrefixAfter;
    for (const auto& note : afterClosure.notes)
        if (note.partId == 2)
            untouchedAfter.emplace_back(note.startBeat, note.durationBeats, note.pitch);
        else if (note.partId == 1 && note.startBeat < afterClosure.lengthBeats - 16.0)
            bedPrefixAfter.emplace_back(note.startBeat, note.durationBeats, note.pitch);
    require(untouchedAfter == untouchedBefore,
            "Transactional chord closure must preserve every unrelated instrument event byte-for-byte");
    require(bedPrefixAfter == bedPrefixBefore,
            "Transactional chord closure must preserve the chord bed before its final four-bar window");
    const auto closureStart = afterClosure.lengthBeats - 16.0;
    require(std::any_of(afterClosure.notes.begin(), afterClosure.notes.end(), [&](const auto& note) {
                return note.partId == 1 && note.startBeat >= closureStart &&
                    positiveModulo(note.pitch, 12) == transactionalPlan.rootPitchClass;
            }),
            "The rendered final window must contain the tonic on the primary chord-bed lane");

    auto declaredOpenBedPlan = unresolvedBedPlan;
    declaredOpenBedPlan.narrativeSpine.resolution = "intentional open modal settlement";
    declaredOpenBedPlan.chordPalette.back().function = HarmonicFunction::Modal;
    declaredOpenBedPlan.chordPalette.back().tension = .28;
    const auto declaredOpenBed = SelectiveRepair::performanceDeficits(
        declaredOpenBedPlan, breathingChordScore, {0});
    require(declaredOpenBed.empty() || !declaredOpenBed.front().missingCodaResolution,
            "An explicitly declared stable modal ending must remain a valid artistic resolution");

    SongPlan authoredMotionReconciliation;
    authoredMotionReconciliation.productionLanguage.domain = ProductionDomain::ClubElectronic;
    authoredMotionReconciliation.productionLanguage.electronicIntent = .95;
    authoredMotionReconciliation.percussionFreeIntent = true;
    InstrumentAssignment emptyVibraphone;
    emptyVibraphone.id = "vibraphone_glass_pulse";
    emptyVibraphone.instrumentId = "vibraphone";
    emptyVibraphone.sourceVoice = VoiceId::HarmonicPulse;
    emptyVibraphone.role = "glass pulse primary_motion_owner";
    InstrumentAssignment authoredBassOrbit;
    authoredBassOrbit.id = "bass_orbital_motion";
    authoredBassOrbit.instrumentId = "sub_synth";
    authoredBassOrbit.sourceVoice = VoiceId::MovementBass;
    authoredBassOrbit.role = "orbital recurrence supporting_motion";
    authoredMotionReconciliation.instruments = {emptyVibraphone, authoredBassOrbit};
    PerformanceCell authoredOrbitCell;
    authoredOrbitCell.id = "authored_bass_orbit";
    authoredOrbitCell.lengthBeats = 4.0;
    authoredOrbitCell.notes = {
        {0.0, 1.0, 42, 94, VoiceId::MovementBass,
         MetricIntent::StrictGrid, authoredBassOrbit.id},
        {2.0, 1.0, 45, 88, VoiceId::MovementBass,
         MetricIntent::StrictGrid, authoredBassOrbit.id},
    };
    PerformancePlacement authoredOrbitPlacement;
    authoredOrbitPlacement.cellId = authoredOrbitCell.id;
    authoredOrbitPlacement.repeats = 8;
    authoredMotionReconciliation.performanceScore.cells = {authoredOrbitCell};
    authoredMotionReconciliation.performanceScore.placements = {authoredOrbitPlacement};
    const auto reconciledOwner =
        ElectronicRoleContract::reconcileAuthoredPrimaryMotionOwner(
            authoredMotionReconciliation);
    require(reconciledOwner.changed &&
                reconciledOwner.previousOwnerId == emptyVibraphone.id &&
                reconciledOwner.electedOwnerId == authoredBassOrbit.id &&
                reconciledOwner.electedAuthoredNotes == 16 &&
                !ElectronicRoleContract::motionOwner(
                    authoredMotionReconciliation.instruments[0]) &&
                ElectronicRoleContract::motionOwner(
                    authoredMotionReconciliation.instruments[1]),
            "An empty provisional motion owner must yield its marker to the compatible lane that GPT actually authored");
    const auto& reconciledNotes =
        authoredMotionReconciliation.performanceScore.cells.front().notes;
    require(reconciledNotes.size() == 2 && reconciledNotes[0].pitch == 42 &&
                reconciledNotes[1].pitch == 45 &&
                reconciledNotes[0].instrumentId == authoredBassOrbit.id &&
                reconciledNotes[1].instrumentId == authoredBassOrbit.id,
            "Motion-owner reconciliation must never copy, synthesize or alter MIDI notes");

    auto explicitMotionOwner = authoredMotionReconciliation;
    explicitMotionOwner.instruments[0] = emptyVibraphone;
    explicitMotionOwner.instruments[0].explicitPromptIdentity = true;
    explicitMotionOwner.instruments[1].role = "orbital recurrence supporting_motion";
    const auto explicitGuard =
        ElectronicRoleContract::reconcileAuthoredPrimaryMotionOwner(explicitMotionOwner);
    require(!explicitGuard.changed &&
                ElectronicRoleContract::motionOwner(explicitMotionOwner.instruments[0]),
            "A concrete motion instrument explicitly named by the user must remain a hard commitment when empty");

    SongPlan closedCast;
    closedCast.totalBars = 192;
    closedCast.instrumentCastAuthored = true;
    closedCast.productionLanguage.domain = ProductionDomain::Hybrid;
    closedCast.productionLanguage.electronicIntent = .95;
    closedCast.instruments = {chordBed, authoredBassOrbit};
    const auto closedCastSize = closedCast.instruments.size();
    ArrangementDensityPlanner::apply(closedCast);
    require(closedCast.instruments.size() == closedCastSize &&
                std::none_of(closedCast.instruments.begin(), closedCast.instruments.end(),
                    [](const auto& item) { return item.id.starts_with("density_"); }),
            "An authoritative AI cast must never receive generic long-form density tracks");

    SongPlan breathProtection;
    breathProtection.totalBars = 4;
    breathProtection.beatsPerBar = 4.0;
    breathProtection.instrumentCastAuthored = true;
    breathProtection.productionLanguage.domain = ProductionDomain::Hybrid;
    breathProtection.productionLanguage.electronicIntent = .95;
    breathProtection.sections = {{"Scene", "development", "hold", "breathe", 0, 4,
                                   .55, .45, .65, 0}};
    auto supportA = chordBed;
    supportA.id = "support_a";
    supportA.role = "supporting pad";
    supportA.sourceVoice = VoiceId::HarmonicUpper;
    auto supportB = supportA;
    supportB.id = "support_b";
    supportB.sourceVoice = VoiceId::Atmosphere;
    breathProtection.instruments = {chordBed, supportA, supportB};
    breathProtection.performanceScore.cells.push_back({"authority_marker", 4.0});
    breathProtection.performanceScore.placements.push_back({"authority_marker", 0});
    Pattern breathPattern;
    breathPattern.lengthBeats = 16.0;
    for (std::size_t index = 0; index < breathProtection.instruments.size(); ++index) {
        const auto& assignment = breathProtection.instruments[index];
        InstrumentPart part;
        part.id = static_cast<std::uint16_t>(index + 1);
        part.catalogId = assignment.instrumentId;
        part.name = assignment.name;
        part.sourceVoice = assignment.sourceVoice;
        part.department = ScoreDepartment::Harmony;
        part.role = assignment.role;
        part.minimumPitch = 42;
        part.maximumPitch = 84;
        part.orchestralFunction = "foundation";
        breathPattern.parts.push_back(part);
    }
    breathPattern.notes = {
        {0.0, 3.5, 54, 62, 3, VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored, 91},
        {0.0, 16.0, 61, 56, 4, VoiceId::HarmonicUpper, 2, true, NoteOrigin::AiAuthored, 92},
        {0.0, 16.0, 66, 48, 5, VoiceId::Atmosphere, 3, true, NoteOrigin::AiAuthored, 93},
    };
    const auto breathReport = AttentionDirector::shape(breathPattern, breathProtection);
    (void) breathReport;
    require(std::none_of(breathPattern.notes.begin(), breathPattern.notes.end(),
                [](const auto& note) {
                    return note.partId == 1 && note.origin == NoteOrigin::PlanDerived;
                }),
            "The local attention director must preserve AI-authored chord-bed breaths");

    auto fabricProtection = breathProtection;
    fabricProtection.totalBars = 4;
    fabricProtection.chordPalette = {
        {"home", "F-sharp minor", 6, 6, {6, 9, 1}, HarmonicFunction::Tonic},
    };
    fabricProtection.sections.front().harmonicEvents = {{0, 0.0, "home", .2, "home"}};
    auto fabricPattern = breathPattern;
    std::vector<std::tuple<double, double, int, NoteOrigin>> bedBeforeFabric;
    for (const auto& note : fabricPattern.notes)
        if (note.partId == 1)
            bedBeforeFabric.emplace_back(note.startBeat, note.durationBeats, note.pitch, note.origin);
    (void) ElectronicCompositionFabric::materialize(fabricPattern, fabricProtection);
    (void) ElectronicCompositionFabric::convergePublication(fabricPattern, fabricProtection);
    std::vector<std::tuple<double, double, int, NoteOrigin>> bedAfterFabric;
    for (const auto& note : fabricPattern.notes)
        if (note.partId == 1)
            bedAfterFabric.emplace_back(note.startBeat, note.durationBeats, note.pitch, note.origin);
    require(bedAfterFabric == bedBeforeFabric,
            "Every local fabric and publication-closure pass must preserve the primary chord bed byte-for-byte");

    SongPlan testimonialPlan;
    testimonialPlan.totalBars = 32;
    testimonialPlan.beatsPerBar = 4.0;
    testimonialPlan.instrumentCastAuthored = true;
    testimonialPlan.instruments = {chordBed, supportA};
    testimonialPlan.performanceScore.cells.push_back({"authored_marker", 4.0});
    testimonialPlan.performanceScore.placements.push_back({"authored_marker", 0});
    Pattern testimonialPattern;
    testimonialPattern.lengthBeats = 128.0;
    for (std::size_t index = 0; index < testimonialPlan.instruments.size(); ++index) {
        const auto& assignment = testimonialPlan.instruments[index];
        InstrumentPart part;
        part.id = static_cast<std::uint16_t>(index + 1);
        part.catalogId = assignment.instrumentId;
        part.name = assignment.name;
        part.sourceVoice = assignment.sourceVoice;
        part.department = ScoreDepartment::Harmony;
        part.role = assignment.role;
        part.orchestralFunction = index == 0 ? "foundation" : "extension";
        part.minimumPitch = 42;
        part.maximumPitch = 84;
        part.contentLaneId = assignment.id;
        part.lineRelationship = "independent";
        testimonialPattern.parts.push_back(part);
    }
    for (auto bar = 0; bar < 20; ++bar)
        testimonialPattern.notes.push_back({bar * 4.0, 3.0, 54 + bar % 3, 62, 3,
            VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored, 101});
    testimonialPattern.notes.push_back({40.0, .5, 67, 58, 4, VoiceId::HarmonicUpper,
        2, true, NoteOrigin::AiAuthored, 102});
    testimonialPattern.notes.push_back({72.0, .5, 69, 60, 4, VoiceId::HarmonicUpper,
        2, true, NoteOrigin::AiAuthored, 102});
    testimonialPattern.notes.push_back({104.0, .5, 72, 62, 4, VoiceId::HarmonicUpper,
        2, true, NoteOrigin::AiAuthored, 102});
    const auto testimonialReport = TrackViability::compactIncomplete(
        testimonialPattern, testimonialPlan);
    require(testimonialReport.mergedTracks == 0 && testimonialReport.tokenTracks == 1 &&
                std::count_if(testimonialPattern.notes.begin(), testimonialPattern.notes.end(),
                    [](const auto& note) {
                        return note.partId == 2 && note.narrativeId == 102;
                    }) == 3,
            "A sparse AI lane must remain on its own track and be reported for AI revision");

    auto substantialTokenPlan = testimonialPlan;
    substantialTokenPlan.instruments[1].lineRelationship = "independent";
    Pattern substantialToken = testimonialPattern;
    substantialToken.notes.erase(std::remove_if(substantialToken.notes.begin(),
        substantialToken.notes.end(), [](const auto& note) { return note.partId == 2; }),
        substantialToken.notes.end());
    for (auto index = 0; index < 10; ++index) {
        const auto start = index < 5 ? index * 4.0 : 80.0 + (index - 5) * 4.0;
        substantialToken.notes.push_back({start, .5, 67 + index % 3, 58, 4,
            VoiceId::HarmonicUpper, 2, true, NoteOrigin::AiAuthored, 103});
    }
    const auto substantialTokenReport = TrackViability::compactIncomplete(
        substantialToken, substantialTokenPlan);
    require(substantialTokenReport.mergedTracks == 0 &&
                std::count_if(substantialToken.notes.begin(), substantialToken.notes.end(),
                    [](const auto& note) { return note.partId == 2; }) == 10,
            "An underdeveloped AI lane must not be silently merged into another owner");

    // The final contract reads the MIDI, not the cast list or the AI's description.
    SongPlan finalPlan;
    finalPlan.totalBars = 24;
    finalPlan.beatsPerBar = 4.0;
    finalPlan.requestedCastCount = 3;
    finalPlan.sections = {
        SongSection{.name = "opening", .function = "premise", .startBar = 0,
                    .bars = 8, .energy = .6, .density = .6},
        SongSection{.name = "preparation", .function = "development", .startBar = 8,
                    .bars = 8, .energy = .7, .density = .7},
        SongSection{.name = "culmination", .function = "climax", .startBar = 16,
                    .bars = 8, .energy = .9, .density = .9}
    };
    InstrumentAssignment bed;
    bed.id = "bed";
    bed.sourceVoice = VoiceId::HarmonicFoundation;
    bed.orchestralFunction = "foundation";
    InstrumentAssignment upper = bed;
    upper.id = "upper";
    upper.sourceVoice = VoiceId::HarmonicUpper;
    upper.orchestralFunction = "body";
    InstrumentAssignment speaker;
    speaker.id = "speaker";
    speaker.sourceVoice = VoiceId::Lead;
    finalPlan.instruments = {bed, upper, speaker};
    finalPlan.narrativeSpine.protagonistInstrumentId = "speaker";
    Pattern finalMidi;
    finalMidi.lengthBeats = 96.0;
    for (auto bar = 0; bar < 24; ++bar) {
        const auto beat = bar * 4.0;
        finalMidi.notes.push_back({beat, 3.75, 54, 75, 1,
            VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored});
        finalMidi.notes.push_back({beat, 3.75, 66, 70, 2,
            VoiceId::HarmonicUpper, 2, true, NoteOrigin::AiAuthored});
        finalMidi.notes.push_back({beat + 1.0, .5, 73 + (bar / 8) * 2, 85, 3,
            VoiceId::Lead, 3, true, NoteOrigin::AiAuthored});
    }
    const auto completeReview = SelectiveRepair::reviewFinalScore(finalPlan, finalMidi);
    require(completeReview.ready && completeReview.developedVoices == 3 &&
                completeReview.harmonicContinuity > .95,
            "Developed independent voices and a sectionally sustained harmonic floor should pass");

    auto transformedReprise = finalMidi;
    for (auto& note : transformedReprise.notes)
        if (note.partId == 3 && note.startBeat >= 16.0)
            note.aiReusedCell = true;
    require(SelectiveRepair::reviewFinalScore(finalPlan, transformedReprise).ready,
            "A transformed melodic reprise remains developed even when most notes reuse an authored cell");

    auto literalReprise = finalMidi;
    for (auto& note : literalReprise.notes)
        if (note.partId == 3) note.pitch = 73;
    require(SelectiveRepair::reviewFinalScore(finalPlan, literalReprise).developedVoices == 2,
            "A literal melodic copy in every section is not independent long-form development");
    auto staticBassPlan = finalPlan;
    staticBassPlan.instruments[2].sourceVoice = VoiceId::SubBass;
    require(SelectiveRepair::reviewFinalScore(staticBassPlan, literalReprise).developedVoices == 3,
            "A stable rhythmic bass cell may be intentional when it has enough authored material and section coverage");

    auto hollowMidi = finalMidi;
    std::erase_if(hollowMidi.notes, [](const NoteEvent& note) {
        return note.startBeat >= 64.0 && note.partId != 1;
    });
    const auto hollowReview = SelectiveRepair::reviewFinalScore(finalPlan, hollowMidi);
    require(!hollowReview.ready && hollowReview.harmonicContinuity < .78 &&
                hollowReview.climaxContrast < .85 &&
                !hollowReview.instrumentIndices.empty(),
            "A thin climax with a missing second harmonic body must trigger targeted musical review");
    finalPlan.sections.back().energy = .5;
    require(SelectiveRepair::reviewFinalScore(finalPlan, hollowMidi).climaxContrast < .85,
            "A named climax cannot escape review merely because its AI blueprint assigns low energy");
    finalPlan.sections.back().energy = .9;

    auto tokenMidi = finalMidi;
    std::erase_if(tokenMidi.notes, [](const NoteEvent& note) { return note.partId == 3; });
    tokenMidi.notes.push_back({1.0, .5, 73, 85, 3,
        VoiceId::Lead, 3, true, NoteOrigin::AiAuthored});
    const auto tokenReview = SelectiveRepair::reviewFinalScore(finalPlan, tokenMidi);
    require(!tokenReview.ready && tokenReview.developedVoices == 2 &&
                std::find(tokenReview.instrumentIndices.begin(),
                          tokenReview.instrumentIndices.end(), 2) !=
                    tokenReview.instrumentIndices.end(),
            "A listed lead with one isolated note is not a developed voice");

    auto clonedMidi = finalMidi;
    for (auto& note : clonedMidi.notes)
        if (note.partId == 3 && note.startBeat >= 4.0)
            note.aiReusedCell = true;
    const auto clonedReview = SelectiveRepair::reviewFinalScore(finalPlan, clonedMidi);
    require(!clonedReview.ready && clonedReview.developedVoices == 2,
            "A line copied across the arrangement without enough new source material is not developed");

    SoundscapeLayerPlan pianoPreview;
    pianoPreview.instrumentId = "speaker";
    pianoPreview.kind = SoundscapeLayerKind::OneShot;
    SoundscapeLayerPlan shimmerPreview;
    shimmerPreview.instrumentId = "upper";
    shimmerPreview.kind = SoundscapeLayerKind::Transition;
    finalPlan.soundscape.layers = {pianoPreview, shimmerPreview};
    require(SelectiveRepair::eligibleDevelopedVoiceOwners(finalPlan) == 3 &&
                SelectiveRepair::reviewFinalScore(finalPlan, finalMidi).ready,
            "A preview one-shot or transition timbre must not erase an independently developed MIDI owner");

    finalPlan.requestedCastCount = 4;
    require(!SelectiveRepair::reviewFinalScore(finalPlan, finalMidi).ready,
            "An explicit four-voice brief cannot pass with only three developed MIDI lines");
    InstrumentAssignment transition;
    transition.id = "riser";
    transition.sourceVoice = VoiceId::Transitions;
    finalPlan.instruments.push_back(transition);
    require(SelectiveRepair::eligibleDevelopedVoiceOwners(finalPlan) == 3,
            "A transition track cannot satisfy an explicit quota of developed musical voices");
    finalPlan.instruments[1].contentLaneId = "shared_harmony";
    finalPlan.instruments[0].contentLaneId = "shared_harmony";
    require(SelectiveRepair::eligibleDevelopedVoiceOwners(finalPlan) == 2,
            "Two timbral tracks sharing a content lane are one musical owner, not two independent voices");
    const auto feasibility = SelectiveRepair::reviewFinalScore(finalPlan, finalMidi);
    require(!SelectiveRepair::canReachDevelopedVoiceTarget(feasibility, 0, 1) &&
                SelectiveRepair::canReachDevelopedVoiceTarget(feasibility, 1, 1),
            "Stop an impossible cast before later paid blocks, but preserve a score still within the bounded revision budget");
}
