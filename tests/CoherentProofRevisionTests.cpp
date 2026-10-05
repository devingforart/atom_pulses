#include "TestSupport.h"

#include "core/CoherentProofRevision.h"

#include <algorithm>

void runCoherentProofRevisionTests() {
    pulso::CoherentProofIssue sustained;
    sustained.kind = "harsh_overlap";
    sustained.beat = 16.0;
    sustained.overlapBeats = 8.0;
    sustained.pitch = 60;
    sustained.otherPitch = 59;
    sustained.partId = 1;
    sustained.otherPartId = 1;
    sustained.registerName = "mid";
    sustained.chordFunction = "tonic";
    sustained.pitchDeclared = true;
    sustained.otherPitchDeclared = true;
    require(pulso::CoherentProofRevision::issuePriority(sustained) > 1.8,
        "A sustained close-voiced tonic colour should receive contextual review");
    auto fleeting = sustained;
    fleeting.overlapBeats = 0.25;
    require(pulso::CoherentProofRevision::issuePriority(fleeting) == 0.0,
        "A fleeting tension must not trigger a paid AI rewrite");
    auto resolving = sustained;
    resolving.registerName = "high";
    resolving.chordFunction = "dominant";
    resolving.resolutionObserved = true;
    require(pulso::CoherentProofRevision::issuePriority(resolving) <
        pulso::CoherentProofRevision::issuePriority(sustained),
        "Register, function and release should change the review priority");

    pulso::SongPlan plan;
    plan.totalBars = 16;
    plan.beatsPerBar = 4.0;
    plan.instruments.resize(3);
    pulso::Pattern score;
    score.notes.push_back({16.0, 8.0, 60, 80, 3,
        pulso::VoiceId::HarmonicFoundation, 1, true,
        pulso::NoteOrigin::AiAuthored});
    pulso::CoherentProofGateReport before;
    before.ready = before.exactAiNotes = before.technicalReady = true;
    before.chordBedPolyphonicStages = 3;
    before.issues.push_back(sustained);
    const auto windows = pulso::CoherentProofRevision::selectWindows(plan, score, before);
    require(windows.size() == 1 && windows[0].partId == 1 &&
        windows[0].startBeat == 16.0 && windows[0].endBeat == 24.0,
        "Review must target the contextual bar span, not a hard-coded pitch pair");
    auto crossing = score;
    crossing.notes.push_back({15.0, 2.0, 64, 80, 3,
        pulso::VoiceId::HarmonicFoundation, 1, true,
        pulso::NoteOrigin::AiAuthored});
    require(pulso::CoherentProofRevision::selectWindows(plan, crossing, before).empty(),
        "A patch must not cut through an immutable sustaining note");

    auto improved = before;
    improved.issues.clear();
    require(pulso::CoherentProofRevision::accepts(before, improved, 100, 98),
        "A technically safe AI revision with less contextual risk should be accepted");
    require(!pulso::CoherentProofRevision::accepts(before, improved, 100, 50),
        "Removing half the score must not count as musical improvement");
    improved.technicalReady = false;
    require(!pulso::CoherentProofRevision::accepts(before, improved, 100, 98),
        "A musically smoother but technically broken revision must be rejected");
}
