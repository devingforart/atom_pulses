#include "plugin/AiComposer.h"
#include "plugin/MidiExporter.h"
#include "plugin/SongGenerationPipeline.h"
#include "core/CoherentProofGate.h"
#include "core/CoherentProofRevision.h"
#include "core/EditorialSafety.h"
#include "core/PerformanceScore.h"
#include "core/SelectiveRepair.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <numeric>
#include <optional>
#include <stop_token>

namespace {

void writeStatus(const juce::File& output, const juce::String& state,
                 const juce::String& stage, int completed, int total,
                 int attempt = 0, const juce::String& detail = {}) {
    auto* object = new juce::DynamicObject();
    object->setProperty("at", juce::Time::getCurrentTime().toISO8601(true));
    object->setProperty("state", state);
    object->setProperty("stage", stage);
    object->setProperty("completed", completed);
    object->setProperty("total", total);
    object->setProperty("attempt", attempt);
    object->setProperty("detail", detail);
    const auto serialized = juce::JSON::toString(juce::var(object), true);
    output.getChildFile("progress.json").replaceWithText(
        serialized, false, false, "\n");
    if (std::getenv("PULSO_TRACE_PATH") != nullptr) {
        const auto trace = output.getChildFile("progress-events.jsonl");
        juce::FileOutputStream stream(trace);
        if (stream.openedOk()) {
            stream.setPosition(trace.getSize());
            stream.writeText(serialized + "\n", false, false, "\n");
            stream.flush();
        }
    }
}

int fail(const juce::File& output, const juce::String& message) {
    auto* object = new juce::DynamicObject();
    object->setProperty("error", message);
    output.getChildFile("error.json").replaceWithText(
        juce::JSON::toString(juce::var(object), false), false, false, "\n");
    writeStatus(output, "failed", "generation", 0, 0);
    std::cerr << message.toStdString() << '\n';
    return 1;
}

pulso::CompositionBehavior behaviorFrom(const juce::String& value) {
    if (value == "hypnotic") return pulso::CompositionBehavior::Hypnotic;
    if (value == "narrative") return pulso::CompositionBehavior::Narrative;
    return pulso::CompositionBehavior::Adaptive;
}

juce::var planAuditFor(const pulso::SongPlan& plan) {
    auto* root = new juce::DynamicObject();
    root->setProperty("schema_version", 2);
    root->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    root->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    root->setProperty("root_pitch_class", plan.rootPitchClass);
    root->setProperty("tonal_policy", juce::String(pulso::tonalPolicyKey(
        plan.harmonicLanguage.tonalPolicy).data()));
    root->setProperty("total_bars", plan.totalBars);
    root->setProperty("requested_cast_count", static_cast<int>(plan.requestedCastCount));
    root->setProperty("beats_per_bar", plan.beatsPerBar);
    root->setProperty("protagonist_id",
        juce::String::fromUTF8(plan.narrativeSpine.protagonistInstrumentId.c_str()));
    root->setProperty("motif_identity",
        juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str()));
    root->setProperty("harmonic_debt",
        juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str()));
    root->setProperty("resolution_intent",
        juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str()));
    juce::Array<juce::var> narrativeActs;
    for (const auto& act : plan.narrativeSpine.acts) {
        auto* item = new juce::DynamicObject();
        item->setProperty("section", juce::String::fromUTF8(act.sectionName.c_str()));
        item->setProperty("stage", juce::String(pulso::narrativeStageKey(act.stage).data()));
        item->setProperty("cause", juce::String::fromUTF8(act.cause.c_str()));
        item->setProperty("consequence", juce::String::fromUTF8(act.consequence.c_str()));
        item->setProperty("unresolved", juce::String::fromUTF8(act.unresolvedElement.c_str()));
        item->setProperty("resolution_target",
            juce::String::fromUTF8(act.resolutionTarget.c_str()));
        narrativeActs.add(juce::var(item));
    }
    root->setProperty("narrative_acts", narrativeActs);
    juce::Array<juce::var> palette;
    for (const auto& chord : plan.chordPalette) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", juce::String::fromUTF8(chord.id.c_str()));
        item->setProperty("label", juce::String::fromUTF8(chord.label.c_str()));
        item->setProperty("root_pitch_class", chord.rootPitchClass);
        item->setProperty("bass_pitch_class", chord.bassPitchClass);
        item->setProperty("function", juce::String(pulso::harmonicFunctionKey(chord.function).data()));
        item->setProperty("voicing", juce::String(pulso::voicingStrategyKey(chord.voicing).data()));
        item->setProperty("tension", chord.tension);
        juce::Array<juce::var> pitches;
        for (const auto pitch : chord.pitchClasses) pitches.add(pitch);
        item->setProperty("pitch_classes", pitches);
        palette.add(juce::var(item));
    }
    root->setProperty("chord_palette", palette);
    juce::Array<juce::var> sections;
    for (const auto& section : plan.sections) {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", juce::String::fromUTF8(section.name.c_str()));
        item->setProperty("function", juce::String::fromUTF8(section.function.c_str()));
        item->setProperty("harmonic_direction",
            juce::String::fromUTF8(section.harmonicDirection.c_str()));
        item->setProperty("motif_treatment",
            juce::String::fromUTF8(section.motifTreatment.c_str()));
        item->setProperty("start_bar", section.startBar);
        item->setProperty("bars", section.bars);
        item->setProperty("energy", section.energy);
        item->setProperty("tension", section.tension);
        item->setProperty("density", section.density);
        juce::Array<juce::var> harmony;
        for (const auto& event : section.harmonicEvents) {
            auto* point = new juce::DynamicObject();
            point->setProperty("bar_offset", event.barOffset);
            point->setProperty("beat_offset", event.beatOffset);
            point->setProperty("absolute_beat", (section.startBar + event.barOffset) *
                plan.beatsPerBar + event.beatOffset);
            point->setProperty("chord_id", juce::String::fromUTF8(event.chordId.c_str()));
            point->setProperty("purpose", juce::String::fromUTF8(event.purpose.c_str()));
            harmony.add(juce::var(point));
        }
        item->setProperty("harmonic_events", harmony);
        sections.add(juce::var(item));
    }
    root->setProperty("sections", sections);
    juce::Array<juce::var> cast;
    for (const auto& part : plan.instruments) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", juce::String::fromUTF8(part.id.c_str()));
        item->setProperty("name", juce::String::fromUTF8(part.name.c_str()));
        item->setProperty("role", juce::String::fromUTF8(part.role.c_str()));
        item->setProperty("minimum_pitch", part.minimumPitch);
        item->setProperty("maximum_pitch", part.maximumPitch);
        item->setProperty("content_lane_id", juce::String::fromUTF8(part.contentLaneId.c_str()));
        item->setProperty("line_relationship", juce::String::fromUTF8(part.lineRelationship.c_str()));
        juce::Array<juce::var> active;
        for (const auto& section : part.activeSections)
            active.add(juce::String::fromUTF8(section.c_str()));
        item->setProperty("active_sections", active);
        cast.add(juce::var(item));
    }
    root->setProperty("cast", cast);
    root->setProperty("performance_cells", static_cast<int>(plan.performanceScore.cells.size()));
    root->setProperty("performance_placements",
        static_cast<int>(plan.performanceScore.placements.size()));
    return juce::var(root);
}

juce::var proofAuditFor(const pulso::CoherentProofGateReport& report) {
    auto* root = new juce::DynamicObject();
    root->setProperty("schema_version", 1);
    root->setProperty("exact_ai_notes", report.exactAiNotes);
    root->setProperty("technical_ready", report.technicalReady);
    root->setProperty("publishable", report.ready);
    root->setProperty("musical_review_required", report.musicalReviewRequired);
    root->setProperty("contextual_risk", pulso::CoherentProofRevision::risk(report));
    root->setProperty("uniform_activity_advisory", report.uniformActivity);
    root->setProperty("underwritten_roles_advisory", report.underwrittenRoles);
    root->setProperty("chord_bed_polyphonic_stages", report.chordBedPolyphonicStages);
    auto* integrity = new juce::DynamicObject();
    integrity->setProperty("metric_violations", static_cast<int>(report.metricViolations));
    integrity->setProperty("unsafe_durations", static_cast<int>(report.unsafeDurations));
    integrity->setProperty("orphan_events", static_cast<int>(report.orphanEvents));
    root->setProperty("integrity", juce::var(integrity));
    auto* musical = new juce::DynamicObject();
    musical->setProperty("unsupported_chromatic", report.unsupportedChromaticNotes);
    musical->setProperty("strong_non_chord", report.strongNonChordNotes);
    musical->setProperty("invalid_sustains", report.invalidSustains);
    musical->setProperty("harsh_overlaps", report.unintendedHarshOverlaps);
    musical->setProperty("low_register_clashes",
        static_cast<int>(report.lowRegisterVerticalClashes));
    root->setProperty("musical_observations", juce::var(musical));
    juce::Array<juce::var> evidence;
    for (const auto& issue : report.issues) {
        auto* item = new juce::DynamicObject();
        item->setProperty("kind", juce::String::fromUTF8(issue.kind.c_str()));
        item->setProperty("assessment", juce::String::fromUTF8(issue.assessment.c_str()));
        item->setProperty("register", juce::String::fromUTF8(issue.registerName.c_str()));
        item->setProperty("beat", issue.beat);
        item->setProperty("overlap_beats", issue.overlapBeats);
        item->setProperty("note_duration_beats", issue.noteDurationBeats);
        item->setProperty("pitch", issue.pitch);
        item->setProperty("other_pitch", issue.otherPitch);
        item->setProperty("part_id", static_cast<int>(issue.partId));
        item->setProperty("other_part_id", static_cast<int>(issue.otherPartId));
        item->setProperty("chord_id", juce::String::fromUTF8(issue.chordId.c_str()));
        item->setProperty("chord_function", juce::String::fromUTF8(issue.chordFunction.c_str()));
        item->setProperty("pitch_declared", issue.pitchDeclared);
        item->setProperty("other_pitch_declared", issue.otherPitchDeclared);
        item->setProperty("possible_resolution", issue.resolutionObserved);
        item->setProperty("review_priority", pulso::CoherentProofRevision::issuePriority(issue));
        evidence.add(juce::var(item));
    }
    root->setProperty("issues", evidence);
    return juce::var(root);
}

juce::var musicalAuditFor(const pulso::Pattern& song,
                          const pulso::CompositionRenderReport& audit) {
    auto* result = new juce::DynamicObject();
    result->setProperty("schema_version", 1);
    result->setProperty("technical_ready",
        pulso::EditorialSafety::technicallySafeAiScore(song, audit));
    result->setProperty("narrative_ready", audit.narrative.narrativeSpineReady);
    result->setProperty("motif_closure", audit.narrative.motifClosure);
    result->setProperty("tonal_closure", audit.narrative.tonalClosure);
    result->setProperty("register_release", audit.narrative.registerRelease);
    result->setProperty("density_release", audit.narrative.densityRelease);
    result->setProperty("resolution_score", audit.narrative.resolutionScore);
    result->setProperty("causal_narrative", audit.narrative.causalNarrative);
    result->setProperty("soundscape_ready", audit.soundscape.ready);
    result->setProperty("declared_layers", static_cast<int>(audit.soundscape.declaredLayers));
    result->setProperty("meaningful_layers", static_cast<int>(audit.soundscape.meaningfulLayers));
    result->setProperty("harmonic_floor_coverage", audit.soundscape.harmonicFloorCoverage);
    result->setProperty("harmonic_conflicts", audit.production.unintendedHarshOverlaps);
    result->setProperty("low_register_conflicts",
        static_cast<int>(audit.production.lowRegisterVerticalClashes));
    result->setProperty("unsupported_chromatic", audit.production.unsupportedChromaticNotes);
    result->setProperty("invalid_sustains", audit.production.invalidSustains);
    juce::Array<juce::var> narrativeIssues;
    for (const auto& issue : audit.narrative.issues)
        narrativeIssues.add(juce::String::fromUTF8(issue.c_str()));
    result->setProperty("narrative_issues", narrativeIssues);
    juce::Array<juce::var> soundscapeIssues;
    for (const auto& issue : audit.soundscape.issues)
        soundscapeIssues.add(juce::String::fromUTF8(issue.c_str()));
    result->setProperty("soundscape_issues", soundscapeIssues);
    juce::Array<juce::var> tonalIssues;
    for (const auto& issue : audit.finalTonalPass.after.issues) {
        if (tonalIssues.size() >= 64) break;
        auto* item = new juce::DynamicObject();
        item->setProperty("kind", juce::String::fromUTF8(issue.kind.c_str()));
        item->setProperty("beat", issue.beat);
        item->setProperty("overlap_beats", issue.overlapBeats);
        item->setProperty("pitch", issue.pitch);
        item->setProperty("other_pitch", issue.otherPitch);
        item->setProperty("part_id", static_cast<int>(issue.partId));
        item->setProperty("other_part_id", static_cast<int>(issue.otherPartId));
        tonalIssues.add(juce::var(item));
    }
    result->setProperty("tonal_issue_examples", tonalIssues);
    return juce::var(result);
}

void writeProvisionalMidi(const juce::File& output, const pulso::SongPlan& plan,
                          std::size_t checkpointNumber, bool diagnostic, bool proofMode) {
    if (plan.performanceScore.empty() || plan.sections.empty()) return;
    pulso::Pattern song;
    song.lengthBeats = plan.totalBars * plan.beatsPerBar;
    song.seed = plan.seed;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& source = plan.instruments[index];
        pulso::InstrumentPart part;
        part.id = static_cast<std::uint16_t>(index + 1);
        part.name = source.name;
        part.sourceVoice = source.sourceVoice;
        part.role = source.role;
        part.minimumPitch = source.minimumPitch;
        part.maximumPitch = source.maximumPitch;
        part.prominence = source.prominence;
        song.parts.push_back(std::move(part));
    }
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        pulso::Pattern chunk;
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        pulso::PerformanceScoreEngine::replaceChunk(chunk, plan.performanceScore,
            static_cast<int>(sectionIndex), 0.0, sectionBeats, plan.instruments,
            plan.aiSovereign);
        const auto offset = section.startBar * plan.beatsPerBar;
        for (auto note : chunk.notes) {
            note.startBeat += offset;
            song.notes.push_back(std::move(note));
        }
        for (auto control : chunk.controls) {
            control.beat += offset;
            song.controls.push_back(std::move(control));
        }
    }
    if (song.notes.empty()) return;
    const auto filename = juce::String(diagnostic ? "diagnostic-" : "partial-") +
        juce::String(static_cast<int>(checkpointNumber)).paddedLeft('0', 2) + ".mid";
    pulso::plugin::MidiExportOptions options;
    options.bpm = plan.bpm;
    options.timeSignatureNumerator = static_cast<int>(std::lround(plan.beatsPerBar));
    options.clipName = "PULSO - provisional / not a finished composition";
    options.includeKeySignature = true;
    options.rootPitchClass = plan.rootPitchClass;
    options.scale = plan.scale;
    if (!pulso::plugin::writePatternToMidiFile(song, output.getChildFile(filename), options))
        return;
    auto* checkpoint = new juce::DynamicObject();
    checkpoint->setProperty("file", filename);
    checkpoint->setProperty("provisional", true);
    checkpoint->setProperty("notes", static_cast<int>(song.notes.size()));
    checkpoint->setProperty("parts", static_cast<int>(std::count_if(
        song.parts.begin(), song.parts.end(), [&](const auto& part) {
            return std::any_of(song.notes.begin(), song.notes.end(), [&](const auto& note) {
                return note.partId == part.id;
            });
        })));
    const auto stem = filename.upToLastOccurrenceOf(".mid", false, false);
    const auto planFile = stem + "-plan.json";
    if (output.getChildFile(planFile).replaceWithText(
            juce::JSON::toString(planAuditFor(plan), false), false, false, "\n"))
        checkpoint->setProperty("planFile", planFile);
    pulso::GenerationContext foundation;
    foundation.role = pulso::Role::Ensemble;
    foundation.rootPitchClass = plan.rootPitchClass;
    foundation.scale = plan.scale;
    foundation.beatsPerBar = plan.beatsPerBar;
    foundation.seed = plan.seed;
    foundation.humanize = 0.0;
    pulso::CompositionRenderReport audit;
    const auto rendered = pulso::SongComposer{}.render(plan, foundation, {}, &audit);
    const auto musicalFile = stem + "-musical-audit.json";
    if (output.getChildFile(musicalFile).replaceWithText(
            juce::JSON::toString(musicalAuditFor(rendered, audit), false),
            false, false, "\n"))
        checkpoint->setProperty("musicalAuditFile", musicalFile);
    if (diagnostic && proofMode) {
        const auto authoredNotes = std::accumulate(plan.performanceScore.cells.begin(),
            plan.performanceScore.cells.end(), std::size_t{},
            [](std::size_t count, const auto& cell) { return count + cell.notes.size(); });
        const auto gate = pulso::CoherentProofGate::evaluate(
            plan, rendered, audit, authoredNotes);
        const auto auditFile = stem + "-audit.json";
        if (output.getChildFile(auditFile).replaceWithText(
                juce::JSON::toString(proofAuditFor(gate), false), false, false, "\n"))
            checkpoint->setProperty("auditFile", auditFile);
    }
    output.getChildFile(diagnostic ? "diagnostic.json" : "checkpoint.json").replaceWithText(
        juce::JSON::toString(juce::var(checkpoint), false), false, false, "\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: pulso_cloud_worker JOB_JSON OUTPUT_DIRECTORY\n";
        return 2;
    }
    const juce::File input(juce::String::fromUTF8(argv[1]));
    const juce::File output(juce::String::fromUTF8(argv[2]));
    if (!input.existsAsFile() || !output.createDirectory()) return 2;
    const auto job = juce::JSON::parse(input);
    const auto* object = job.getDynamicObject();
    if (object == nullptr) return fail(output, "Invalid job input");
    const auto prompt = object->getProperty("prompt").toString().trim();
    const auto duration = static_cast<int>(object->getProperty("duration_seconds"));
    const auto bpm = static_cast<double>(object->getProperty("bpm"));
    const auto seedText = object->getProperty("seed").toString();
    const auto seed = static_cast<std::uint64_t>(seedText.getLargeIntValue());
    if (prompt.isEmpty() || prompt.length() > 600 || duration < 30 || duration > 900 ||
        !std::isfinite(bpm) || bpm < 60.0 || bpm > 180.0 || seed == 0)
        return fail(output, "Invalid composition parameters");
    if (!pulso::plugin::AiComposer::hasApiKey())
        return fail(output, "Server OpenAI key is not configured");
    pulso::plugin::SongGenerationRequest request;
    request.direction = prompt;
    const auto proofMode = object->getProperty("proof_mode") == juce::var(true);
    if (proofMode) {
        if (duration > 60) return fail(output, "Proof mode requires at most 60 seconds");
        const auto explicitCount = pulso::plugin::AiComposer::requestedInstrumentCount(prompt);
        if (explicitCount != 0 && explicitCount != 3)
            return fail(output, "Proof mode requires exactly 3 instruments; disable it for a different cast");
        request.direction +=
            "\nLOCAL MUSICAL COHERENCE PROOF: Compose exactly 3 instrument tracks: "
            "one polyphonic central chord bed, one bass that carries grounded motion, "
            "and one melodic protagonist with a clear phrase and resolution. "
            "Coordinate their pitches and rests against the same harmony. "
            "Do not add drums or decorative extra tracks unless the user explicitly "
            "requests them; if they do, ask for proof mode to be disabled. "
            "The short work must have an opening, development and closing. ";
    }
    request.targetSeconds = duration;
    request.bpm = bpm;
    request.seed = seed;
    request.behavior = behaviorFrom(object->getProperty("behavior").toString());
    request.aiSovereign = object->getProperty("ai_sovereign") == juce::var(true);
    writeStatus(output, "running", "blueprint", 0, 1);
    juce::String error;
    std::size_t checkpointNumber = 0;
    auto plan = pulso::plugin::SongGenerationPipeline::plan(request,
        std::stop_token{}, error,
        [&output](const pulso::plugin::AiSongProgressUpdate& update) {
            const auto stage = update.stage == pulso::plugin::AiSongStage::Blueprint ? "blueprint" :
                update.stage == pulso::plugin::AiSongStage::PerformanceBlock ? "writing" :
                update.stage == pulso::plugin::AiSongStage::Recovery ? "recovery" : "validation";
            writeStatus(output, "running", stage, static_cast<int>(update.completed),
                        static_cast<int>(update.total), update.attempt, update.detail);
        }, [&output, &checkpointNumber, proofMode](const pulso::SongPlan& partial,
                                        std::size_t, bool, bool diagnostic) {
            writeProvisionalMidi(output, partial, ++checkpointNumber, diagnostic, proofMode);
        });
    if (error.isNotEmpty() || plan.instruments.empty())
        return fail(output, error.isNotEmpty() ? error : "AI returned an empty score");
    pulso::plugin::SongGenerationPipeline::finalizePlan(plan, request);
    if (request.aiSovereign && !plan.instrumentCastAuthored)
        return fail(output, "AI-sovereign mode requires an AI-authored instrument cast");
    if (request.aiSovereign && plan.implicitPerformanceNotesPruned > 0)
        return fail(output, "AI-sovereign plan contains notes without an authored instrument owner; publication stopped");

    // The same versioned musical context is saved for completed and rejected works.
    if (!output.getChildFile("composition-plan.json").replaceWithText(
            juce::JSON::toString(planAuditFor(plan), false), false, false, "\n"))
        return fail(output, "Could not save composition plan audit");

    writeStatus(output, "running", "rendering", 0, static_cast<int>(plan.sections.size()));
    pulso::CompositionRenderReport finalAudit;
    const auto song = pulso::plugin::SongGenerationPipeline::render(plan, request,
        [&output](std::size_t completed, std::size_t total, const pulso::SongSection&) {
            writeStatus(output, "running", "rendering", static_cast<int>(completed),
                        static_cast<int>(total));
        }, &finalAudit);
    if (song.notes.empty()) return fail(output, "Rendered score has no MIDI notes");
    const auto aiNotes = static_cast<std::size_t>(std::count_if(song.notes.begin(), song.notes.end(),
        [](const auto& note) {
            return note.origin == pulso::NoteOrigin::AiAuthored ||
                note.origin == pulso::NoteOrigin::AiTransformed;
        }));
    if (request.aiSovereign && aiNotes != song.notes.size())
        return fail(output, "AI-sovereign render produced non-AI notes; publication stopped");
    if (request.aiSovereign) {
        const auto review = pulso::SelectiveRepair::reviewFinalScore(plan, song);
        auto* evidence = new juce::DynamicObject();
        evidence->setProperty("ready", review.ready);
        evidence->setProperty("developed_voices", static_cast<int>(review.developedVoices));
        evidence->setProperty("expected_voices", static_cast<int>(review.expectedVoices));
        evidence->setProperty("harmonic_continuity", review.harmonicContinuity);
        evidence->setProperty("climax_contrast", review.climaxContrast);
        juce::Array<juce::var> findings;
        for (const auto& issue : review.issues)
            findings.add(juce::String::fromUTF8(issue.c_str()));
        evidence->setProperty("issues", findings);
        if (!output.getChildFile("final-score-review.json").replaceWithText(
                juce::JSON::toString(juce::var(evidence), false), false, false, "\n"))
            return fail(output, "Could not save final MIDI review");
        if (!review.ready) {
            const auto firstFinding = review.issues.empty() ? juce::String("musical brief incomplete") :
                juce::String::fromUTF8(review.issues.front().c_str());
            return fail(output, "Final MIDI review requires a focused musical revision: " + firstFinding);
        }
    }
    const auto countNotes = [&](auto predicate) {
        return static_cast<int>(std::count_if(song.notes.begin(), song.notes.end(), predicate));
    };
    const auto directAiNotes = countNotes([](const auto& note) {
        return note.origin == pulso::NoteOrigin::AiAuthored && !note.aiReusedCell;
    });
    const auto repeatedAiNotes = countNotes([](const auto& note) {
        return note.origin == pulso::NoteOrigin::AiAuthored && note.aiReusedCell;
    });
    const auto transformedAiNotes = countNotes([](const auto& note) {
        return note.origin == pulso::NoteOrigin::AiTransformed && !note.aiReusedCell;
    });
    const auto repeatedTransformedAiNotes = countNotes([](const auto& note) {
        return note.origin == pulso::NoteOrigin::AiTransformed && note.aiReusedCell;
    });
    if (!output.getChildFile("musical-audit.json").replaceWithText(
            juce::JSON::toString(musicalAuditFor(song, finalAudit), false),
            false, false, "\n"))
        return fail(output, "Could not save musical audit");
    std::optional<pulso::CoherentProofGateReport> proofGate;
    if (proofMode) {
        const auto authoredNotes = std::accumulate(plan.performanceScore.cells.begin(),
            plan.performanceScore.cells.end(), std::size_t{},
            [](std::size_t count, const auto& cell) { return count + cell.notes.size(); });
        proofGate = pulso::CoherentProofGate::evaluate(plan, song, finalAudit, authoredNotes);
        if (!output.getChildFile("coherence-audit.json").replaceWithText(
                juce::JSON::toString(proofAuditFor(*proofGate), false), false, false, "\n"))
            return fail(output, "Could not save coherent proof audit");
        if (!proofGate->ready) {
            return fail(output, "Coherent proof failed final MIDI audit: authored=" +
                juce::String(static_cast<int>(authoredNotes)) + " rendered=" +
                juce::String(static_cast<int>(song.notes.size())) + " exact_ai=" +
                juce::String(proofGate->exactAiNotes ? 1 : 0) + " technical_ready=" +
                juce::String(proofGate->technicalReady ? 1 : 0) +
                " [metric=" + juce::String(static_cast<int>(proofGate->metricViolations)) +
                " duration=" + juce::String(static_cast<int>(proofGate->unsafeDurations)) +
                " orphan=" + juce::String(static_cast<int>(proofGate->orphanEvents)) + "]");
        }
    }

    pulso::plugin::MidiExportOptions fullOptions;
    fullOptions.bpm = plan.bpm;
    fullOptions.timeSignatureNumerator = static_cast<int>(std::lround(plan.beatsPerBar));
    fullOptions.clipName = juce::String::fromUTF8(plan.title.c_str());
    fullOptions.includeKeySignature = true;
    fullOptions.rootPitchClass = plan.rootPitchClass;
    fullOptions.scale = plan.scale;
    if (!pulso::plugin::writePatternToMidiFile(song, output.getChildFile("full-song.mid"), fullOptions))
        return fail(output, "Could not write complete MIDI song");

    juce::String comparisonFile;
    int comparisonNotes = 0;
    if (request.aiSovereign) {
        try {
            auto referencePlan = plan;
            referencePlan.aiSovereign = false;
            auto referenceRequest = request;
            referenceRequest.aiSovereign = false;
            const auto standard = pulso::plugin::SongGenerationPipeline::render(
                referencePlan, referenceRequest);
            if (!standard.notes.empty()) {
                pulso::plugin::MidiExportOptions referenceOptions = fullOptions;
                referenceOptions.clipName = fullOptions.clipName + " - standard renderer A/B";
                comparisonFile = "reference-standard.mid";
                if (!pulso::plugin::writePatternToMidiFile(standard,
                        output.getChildFile(comparisonFile), referenceOptions))
                    comparisonFile.clear();
                else comparisonNotes = static_cast<int>(standard.notes.size());
            }
        } catch (const std::exception&) {
            // A comparison artifact must never turn the already-exported sovereign
            // composition into a failed job.
            comparisonFile.clear();
        }
    }

    juce::Array<juce::var> tracks;
    for (std::size_t index = 0; index < song.parts.size(); ++index) {
        const auto& part = song.parts[index];
        pulso::Pattern partPattern = song;
        std::erase_if(partPattern.notes, [&](const auto& note) { return note.partId != part.id; });
        std::erase_if(partPattern.controls, [&](const auto& event) { return event.partId != part.id; });
        std::erase_if(partPattern.expressions, [&](const auto& event) { return event.partId != part.id; });
        if (partPattern.notes.empty()) continue;
        const auto filename = "track-" + juce::String(static_cast<int>(index + 1)).paddedLeft('0', 2) + ".mid";
        pulso::plugin::MidiExportOptions options;
        options.bpm = plan.bpm;
        options.timeSignatureNumerator = static_cast<int>(std::lround(plan.beatsPerBar));
        options.clipName = juce::String::fromUTF8(part.name.c_str());
        options.includeKeySignature = true;
        options.rootPitchClass = plan.rootPitchClass;
        options.scale = plan.scale;
        if (!pulso::plugin::writePatternToMidiFile(partPattern,
                output.getChildFile(filename), options))
            return fail(output, "Could not write MIDI track " + juce::String(static_cast<int>(index + 1)));
        auto* track = new juce::DynamicObject();
        track->setProperty("filename", filename);
        track->setProperty("name", options.clipName);
        track->setProperty("notes", static_cast<int>(partPattern.notes.size()));
        tracks.add(juce::var(track));
    }
    if (tracks.isEmpty()) return fail(output, "No nonempty MIDI tracks were produced");
    auto* manifest = new juce::DynamicObject();
    manifest->setProperty("schema_version", 1);
    manifest->setProperty("render_mode", request.aiSovereign ? "ai_sovereign" : "standard");
    manifest->setProperty("ai_authored_or_declared_transform_notes", static_cast<int>(aiNotes));
    manifest->setProperty("all_midi_notes", static_cast<int>(song.notes.size()));
    auto* noteProvenance = new juce::DynamicObject();
    noteProvenance->setProperty("direct_ai", directAiNotes);
    noteProvenance->setProperty("reused_ai", repeatedAiNotes);
    noteProvenance->setProperty("transformed_ai", transformedAiNotes);
    noteProvenance->setProperty("reused_transformed_ai", repeatedTransformedAiNotes);
    noteProvenance->setProperty("other", static_cast<int>(song.notes.size()) -
        directAiNotes - repeatedAiNotes - transformedAiNotes - repeatedTransformedAiNotes);
    manifest->setProperty("note_provenance", juce::var(noteProvenance));
    if (comparisonFile.isNotEmpty()) {
        manifest->setProperty("comparisonFile", comparisonFile);
        manifest->setProperty("comparisonNotes", comparisonNotes);
    }
    manifest->setProperty("engineVersion", PULSO_VERSION_STRING);
    manifest->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    manifest->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    manifest->setProperty("bpm", plan.bpm);
    manifest->setProperty("bars", plan.totalBars);
    manifest->setProperty("fullFile", "full-song.mid");
    manifest->setProperty("planFile", "composition-plan.json");
    manifest->setProperty("musicalAuditFile", "musical-audit.json");
    if (proofGate) manifest->setProperty("auditFile", "coherence-audit.json");
    manifest->setProperty("tracks", tracks);
    auto* editorial = new juce::DynamicObject();
    editorial->setProperty("technical_ready", request.aiSovereign ?
        pulso::EditorialSafety::technicallySafeAiScore(song, finalAudit) : song.productionReady);
    if (request.aiSovereign) editorial->setProperty("legacy_production_ready", song.productionReady);
    editorial->setProperty("creative_ready", song.creativeReady);
    editorial->setProperty("narrative_ready", song.narrativeSpineReady);
    editorial->setProperty("soundscape_ready", song.soundscapeReady);
    editorial->setProperty("track_viability_ready", song.trackViabilityReady);
    editorial->setProperty("resolution_score", song.narrativeResolutionScore);
    editorial->setProperty("dialogue_lines", static_cast<int>(song.dialogueMusicalLines));
    editorial->setProperty("underfilled_bars", static_cast<int>(song.underfilledBarsAfter));
    editorial->setProperty("ai_authored_note_ratio", song.aiAuthoredNoteRatio);
    if (request.aiSovereign && !proofGate) {
        editorial->setProperty("musical_review_required", !song.productionReady ||
            !song.creativeReady || !song.narrativeSpineReady ||
            !song.soundscapeReady || !song.trackViabilityReady);
        editorial->setProperty("harmonic_conflicts", finalAudit.production.unintendedHarshOverlaps);
        editorial->setProperty("low_register_conflicts",
            static_cast<int>(finalAudit.production.lowRegisterVerticalClashes));
    }
    if (proofGate) {
        editorial->setProperty("review_attempted", plan.coherentReview.attempted);
        editorial->setProperty("review_accepted", plan.coherentReview.accepted);
        editorial->setProperty("review_windows", plan.coherentReview.targetedWindows);
        editorial->setProperty("contextual_risk_before", plan.coherentReview.riskBefore);
        editorial->setProperty("contextual_risk_after", plan.coherentReview.riskAfter);
        editorial->setProperty("uniform_activity_advisory", proofGate->uniformActivity);
        editorial->setProperty("musical_review_required", proofGate->musicalReviewRequired);
        editorial->setProperty("underwritten_roles_advisory", proofGate->underwrittenRoles);
        editorial->setProperty("chord_bed_polyphonic_stages",
            proofGate->chordBedPolyphonicStages);
        editorial->setProperty("harmonic_conflicts", proofGate->unintendedHarshOverlaps);
        editorial->setProperty("low_register_conflicts",
            static_cast<int>(proofGate->lowRegisterVerticalClashes));
    }
    manifest->setProperty("editorial", juce::var(editorial));
    if (!output.getChildFile("manifest.json").replaceWithText(
            juce::JSON::toString(juce::var(manifest), false), false, false, "\n"))
        return fail(output, "Could not save MIDI manifest");
    writeStatus(output, "completed", "ready", static_cast<int>(tracks.size()),
                static_cast<int>(tracks.size()));
    return 0;
}
