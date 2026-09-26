#include "TestSupport.h"

#include "core/ElectronicRoleContract.h"
#include "core/ElectronicCompositionFabric.h"
#include "core/ArrangementDensityPlanner.h"
#include "core/AttentionDirector.h"
#include "core/SelectiveRepair.h"
#include "core/TrackViability.h"

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

    PerformanceScore fractionalScaleSource;
    auto fractionalCell = promoted;
    fractionalCell.id = "fractional_scale_authored_phrase";
    fractionalCell.lengthBeats = 48.0;
    fractionalCell.notes = {
        {0.0, .5, 61, 84, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {20.0, .5, 63, 88, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
        {40.0, 1.0, 65, 78, VoiceId::Lead, MetricIntent::StrictGrid, protagonist.id},
    };
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
                literalLeadFindings.front().minimumNarrativePhraseWindows == 7 &&
                literalLeadFindings.front().missingThematicDevelopment &&
                literalLeadFindings.front().literalPlacementRatio > .99 &&
                literalLeadFindings.front().missingMelodicSpeech,
            "A ubiquitous literal leap-cell must not masquerade as a developed AI protagonist");

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
    require(testimonialReport.mergedTracks == 1 &&
                std::none_of(testimonialPattern.notes.begin(), testimonialPattern.notes.end(),
                    [](const auto& note) { return note.partId == 2; }) &&
                std::count_if(testimonialPattern.notes.begin(), testimonialPattern.notes.end(),
                    [](const auto& note) {
                        return note.partId == 1 && note.narrativeId == 102;
                    }) == 3,
            "A testimonial lane must disappear before export while its authored gestures survive intact");

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
    require(substantialTokenReport.mergedTracks == 1 &&
                std::none_of(substantialToken.notes.begin(), substantialToken.notes.end(),
                    [](const auto& note) { return note.partId == 2; }),
            "A non-essential independent lane that remains token-sized at publication must relay into a viable owner");
}
