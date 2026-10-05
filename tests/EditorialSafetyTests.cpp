#include "TestSupport.h"

#include "core/EditorialSafety.h"

void runEditorialSafetyTests() {
    pulso::Pattern pattern;
    pattern.lengthBeats = 16.0;
    pattern.productionAuditPerformed = true;
    pattern.parts.resize(1);
    pattern.notes.push_back({0.0, 4.0, 60, 80, 1,
        pulso::VoiceId::HarmonicFoundation, 1, true,
        pulso::NoteOrigin::AiAuthored});
    pulso::CompositionRenderReport report;
    report.production.ready = false;
    report.production.unintendedHarshOverlaps = 5;
    report.production.lowRegisterVerticalClashes = 2;
    require(pulso::EditorialSafety::technicallySafeAiScore(pattern, report),
        "Musical tension alone must not be mistaken for broken MIDI");
    report.production.metricViolations = 1;
    require(!pulso::EditorialSafety::technicallySafeAiScore(pattern, report),
        "Off-grid MIDI must remain a technical rejection");
    report.production.metricViolations = 0;
    pattern.notes[0].origin = pulso::NoteOrigin::Procedural;
    require(!pulso::EditorialSafety::technicallySafeAiScore(pattern, report),
        "AI-sovereign publication must not silently include procedural notes");
    pattern.notes[0].origin = pulso::NoteOrigin::AiAuthored;
    pattern.notes[0].durationBeats = 20.0;
    require(!pulso::EditorialSafety::technicallySafeAiScore(pattern, report),
        "A note escaping the MIDI song boundary is technically unsafe");
}
