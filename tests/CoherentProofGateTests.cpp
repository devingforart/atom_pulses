#include "TestSupport.h"

#include "core/CoherentProofGate.h"

#include <algorithm>

void runCoherentProofGateTests() {
    pulso::SongPlan plan;
    plan.totalBars = 16;
    plan.beatsPerBar = 4.0;
    pulso::InstrumentAssignment bed;
    bed.id = "bed";
    bed.role = "primary_chord_bed";
    bed.sourceVoice = pulso::VoiceId::HarmonicFoundation;
    pulso::InstrumentAssignment bass;
    bass.id = "bass";
    bass.sourceVoice = pulso::VoiceId::MovementBass;
    pulso::InstrumentAssignment lead;
    lead.id = "lead";
    lead.sourceVoice = pulso::VoiceId::Lead;
    plan.instruments = {bed, bass, lead};
    plan.narrativeSpine.protagonistInstrumentId = "lead";
    pulso::HarmonicChord chord;
    chord.id = "c_major";
    chord.pitchClasses = {0, 4, 7, 11};
    plan.chordPalette.push_back(chord);
    pulso::SongSection section;
    section.bars = 16;
    section.harmonicEvents.push_back({0, 0.0, "c_major"});
    plan.sections.push_back(section);

    pulso::Pattern score;
    score.lengthBeats = 64.0;
    score.productionAuditPerformed = true;
    score.productionReady = true;
    for (int bar = 0; bar < 16; ++bar) {
        score.notes.push_back({bar * 4.0, 3.0, 60, 80, 3,
            pulso::VoiceId::HarmonicFoundation, 1, true,
            pulso::NoteOrigin::AiAuthored});
        score.notes.push_back({bar * 4.0 + 1.0, 0.75, 72, 88, 2,
            pulso::VoiceId::Lead, 3, true,
            pulso::NoteOrigin::AiAuthored});
    }
    pulso::CompositionRenderReport audit;
    audit.production.ready = true;
    const auto uniform = pulso::CoherentProofGate::evaluate(
        plan, score, audit, score.notes.size());
    require(uniform.ready && uniform.uniformActivity && uniform.underwrittenRoles &&
        uniform.leadOnsetRange == 0 && uniform.chordBedOnsetRange == 0,
        "A safe but flat or incomplete AI score should publish with musical advisories");
    score.notes.push_back({0.0, 3.0, 71, 76, 3,
        pulso::VoiceId::HarmonicFoundation, 1, true,
        pulso::NoteOrigin::AiAuthored});
    require(pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).uniformActivity,
        "Chord voicing size must not be mistaken for an extra rhythmic attack");

    audit.production.ready = false;
    audit.production.unintendedHarshOverlaps = 1;
    pulso::TonalIssue colour;
    colour.beat = 0.0;
    colour.pitch = 71;
    colour.otherPitch = 60;
    colour.kind = "harsh_overlap";
    colour.partId = 1;
    colour.otherPartId = 1;
    audit.finalTonalPass.after.issues.push_back(colour);
    const auto musicalWarning = pulso::CoherentProofGate::evaluate(
        plan, score, audit, score.notes.size());
    require(musicalWarning.ready && musicalWarning.musicalReviewRequired &&
        musicalWarning.issues.size() == 1 &&
        musicalWarning.issues[0].chordId == "c_major" &&
        musicalWarning.issues[0].pitchDeclared &&
        musicalWarning.issues[0].registerName == "mid" &&
        musicalWarning.issues[0].noteDurationBeats == 3.0,
        "Harmonic concerns need chord context but must not masquerade as broken MIDI");
    auto lowConflict = score;
    lowConflict.notes.push_back({4.0, 6.0, 58, 76, 3,
        pulso::VoiceId::HarmonicFoundation, 1, true,
        pulso::NoteOrigin::AiAuthored});
    lowConflict.notes.push_back({7.0, 2.0, 45, 84, 1,
        pulso::VoiceId::MovementBass, 2, true,
        pulso::NoteOrigin::AiAuthored});
    audit.production.lowRegisterVerticalClashes = 1;
    const auto lowWarning = pulso::CoherentProofGate::evaluate(
        plan, lowConflict, audit, lowConflict.notes.size());
    const auto lowIssue = std::find_if(lowWarning.issues.begin(), lowWarning.issues.end(),
        [](const auto& issue) { return issue.kind == "low_register_overlap"; });
    require(lowWarning.ready && lowIssue != lowWarning.issues.end() &&
        lowIssue->beat == 7.0 && lowIssue->overlapBeats == 2.0 &&
        lowIssue->pitch == 58 && lowIssue->otherPitch == 45 &&
        lowIssue->registerName == "low",
        "The saved MIDI low clash must become contextual review evidence, not corruption");
    audit.production.lowRegisterVerticalClashes = 0;
    audit.production.ready = true;
    score.productionReady = false;
    require(pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "A legacy production flag that includes tonal opinion must not veto intact MIDI");
    score.productionReady = true;
    audit.production.metricViolations = 1;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "Off-grid MIDI remains a hard integrity failure");
    audit.production.metricViolations = 0;
    audit.production.unsafeDurations = 1;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "Unsafe note lengths remain a hard integrity failure");
    audit.production.unsafeDurations = 0;
    audit.production.orphanEvents = 1;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "Notes without an instrument owner remain a hard integrity failure");
    audit.production.orphanEvents = 0;
    score.productionAuditPerformed = false;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "An unaudited score must never publish as technically safe");
    score.productionAuditPerformed = true;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size() + 1).ready,
        "Missing AI notes must block publication even when the production audit passes");
    score.notes.back().origin = pulso::NoteOrigin::Procedural;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "Procedural notes must never enter the AI-only proof");
    score.notes.back().origin = pulso::NoteOrigin::AiAuthored;
    const auto previousPitch = score.notes.back().pitch;
    score.notes.back().pitch = 128;
    require(!pulso::CoherentProofGate::evaluate(
                plan, score, audit, score.notes.size()).ready,
        "An out-of-range MIDI pitch must block publication");
    score.notes.back().pitch = previousPitch;
    score.notes.back().startBeat += 0.25;
    score.notes.push_back({60.0, 0.5, 74, 84, 2, pulso::VoiceId::Lead,
        3, true, pulso::NoteOrigin::AiAuthored});
    const auto varied = pulso::CoherentProofGate::evaluate(
        plan, score, audit, score.notes.size());
    require(varied.ready && !varied.uniformActivity && varied.leadOnsetRange == 1,
        "A change in authored phrase activity should clear the uniformity advisory");
}
