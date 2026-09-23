#include "TestSupport.h"

#include "core/SelectiveRepair.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace pulso;

void runSelectiveRepairTests() {
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
    require(SelectiveRepair::ensureAuthoredProtagonistCoda(
                narrativePlan, oversizedSourceScore, oversizedSource.id),
            "A source cell longer than the resolution must produce a bounded verified authored coda fragment");
    const auto oversizedFindings = SelectiveRepair::performanceDeficits(
        narrativePlan, oversizedSourceScore, {0});
    const auto oversizedDeficit = std::find_if(oversizedFindings.begin(), oversizedFindings.end(),
        [](const auto& finding) { return finding.instrumentId == "protagonist"; });
    require(oversizedDeficit == oversizedFindings.end() ||
                !oversizedDeficit->missingCodaResolution,
            "A repaired oversized source must pass the independent terminal-boundary audit");

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
                literalLeadFindings.front().minimumNarrativePhraseWindows == 7 &&
                literalLeadFindings.front().missingThematicDevelopment &&
                literalLeadFindings.front().literalPlacementRatio > .99 &&
                literalLeadFindings.front().missingMelodicSpeech,
            "A ubiquitous literal leap-cell must not masquerade as a developed AI protagonist");
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
}
