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
    require(SelectiveRepair::consolidatableDuplicateTargets(plan, constraints, true).empty(),
            "An explicit cast must never lose a requested identity during clone consolidation");
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
}
