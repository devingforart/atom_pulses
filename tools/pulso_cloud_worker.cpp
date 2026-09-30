#include "plugin/AiComposer.h"
#include "plugin/MidiExporter.h"
#include "plugin/SongGenerationPipeline.h"
#include "core/PerformanceScore.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
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

void writeProvisionalMidi(const juce::File& output, const pulso::SongPlan& plan,
                          std::size_t checkpointNumber) {
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
            static_cast<int>(sectionIndex), 0.0, sectionBeats, plan.instruments);
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
    const auto filename = "partial-" +
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
    output.getChildFile("checkpoint.json").replaceWithText(
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
        }, [&output, &checkpointNumber](const pulso::SongPlan& partial,
                                        std::size_t, bool) {
            writeProvisionalMidi(output, partial, ++checkpointNumber);
        });
    if (error.isNotEmpty() || plan.instruments.empty())
        return fail(output, error.isNotEmpty() ? error : "AI returned an empty score");
    pulso::plugin::SongGenerationPipeline::finalizePlan(plan, request);
    if (request.aiSovereign && !plan.instrumentCastAuthored)
        return fail(output, "AI-sovereign mode requires an AI-authored instrument cast");
    if (request.aiSovereign && plan.implicitPerformanceNotesPruned > 0)
        return fail(output, "AI-sovereign plan contains notes without an authored instrument owner; publication stopped");

    // Keep the AI's musical decisions beside the MIDI. A later audit can then
    // distinguish a weak blueprint from a weak performance or post-render edit.
    auto* planAudit = new juce::DynamicObject();
    planAudit->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    planAudit->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    planAudit->setProperty("root_pitch_class", plan.rootPitchClass);
    planAudit->setProperty("total_bars", plan.totalBars);
    planAudit->setProperty("beats_per_bar", plan.beatsPerBar);
    planAudit->setProperty("protagonist_id",
        juce::String::fromUTF8(plan.narrativeSpine.protagonistInstrumentId.c_str()));
    planAudit->setProperty("motif_identity",
        juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str()));
    planAudit->setProperty("harmonic_debt",
        juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str()));
    planAudit->setProperty("resolution_intent",
        juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str()));
    juce::Array<juce::var> sectionsAudit;
    for (const auto& section : plan.sections) {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", juce::String::fromUTF8(section.name.c_str()));
        item->setProperty("function", juce::String::fromUTF8(section.function.c_str()));
        item->setProperty("start_bar", section.startBar);
        item->setProperty("bars", section.bars);
        item->setProperty("energy", section.energy);
        item->setProperty("tension", section.tension);
        item->setProperty("density", section.density);
        juce::Array<juce::var> harmony;
        for (const auto& event : section.harmonicEvents) {
            auto* chord = new juce::DynamicObject();
            chord->setProperty("bar_offset", event.barOffset);
            chord->setProperty("beat_offset", event.beatOffset);
            chord->setProperty("chord_id", juce::String::fromUTF8(event.chordId.c_str()));
            harmony.add(juce::var(chord));
        }
        item->setProperty("harmonic_events", harmony);
        sectionsAudit.add(juce::var(item));
    }
    planAudit->setProperty("sections", sectionsAudit);
    juce::Array<juce::var> castAudit;
    for (const auto& part : plan.instruments) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", juce::String::fromUTF8(part.id.c_str()));
        item->setProperty("name", juce::String::fromUTF8(part.name.c_str()));
        item->setProperty("role", juce::String::fromUTF8(part.role.c_str()));
        item->setProperty("content_lane_id", juce::String::fromUTF8(part.contentLaneId.c_str()));
        item->setProperty("line_relationship", juce::String::fromUTF8(part.lineRelationship.c_str()));
        juce::Array<juce::var> active;
        for (const auto& section : part.activeSections)
            active.add(juce::String::fromUTF8(section.c_str()));
        item->setProperty("active_sections", active);
        castAudit.add(juce::var(item));
    }
    planAudit->setProperty("cast", castAudit);
    planAudit->setProperty("performance_cells",
        static_cast<int>(plan.performanceScore.cells.size()));
    planAudit->setProperty("performance_placements",
        static_cast<int>(plan.performanceScore.placements.size()));
    if (!output.getChildFile("composition-plan.json").replaceWithText(
            juce::JSON::toString(juce::var(planAudit), false), false, false, "\n"))
        return fail(output, "Could not save composition plan audit");

    writeStatus(output, "running", "rendering", 0, static_cast<int>(plan.sections.size()));
    const auto song = pulso::plugin::SongGenerationPipeline::render(plan, request,
        [&output](std::size_t completed, std::size_t total, const pulso::SongSection&) {
            writeStatus(output, "running", "rendering", static_cast<int>(completed),
                        static_cast<int>(total));
        });
    if (song.notes.empty()) return fail(output, "Rendered score has no MIDI notes");
    const auto aiNotes = static_cast<std::size_t>(std::count_if(song.notes.begin(), song.notes.end(),
        [](const auto& note) {
            return note.origin == pulso::NoteOrigin::AiAuthored ||
                note.origin == pulso::NoteOrigin::AiTransformed;
        }));
    if (request.aiSovereign && aiNotes != song.notes.size())
        return fail(output, "AI-sovereign render produced non-AI notes; publication stopped");

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
    manifest->setProperty("tracks", tracks);
    auto* editorial = new juce::DynamicObject();
    editorial->setProperty("technical_ready", song.productionReady);
    editorial->setProperty("creative_ready", song.creativeReady);
    editorial->setProperty("narrative_ready", song.narrativeSpineReady);
    editorial->setProperty("soundscape_ready", song.soundscapeReady);
    editorial->setProperty("track_viability_ready", song.trackViabilityReady);
    editorial->setProperty("resolution_score", song.narrativeResolutionScore);
    editorial->setProperty("dialogue_lines", static_cast<int>(song.dialogueMusicalLines));
    editorial->setProperty("underfilled_bars", static_cast<int>(song.underfilledBarsAfter));
    editorial->setProperty("ai_authored_note_ratio", song.aiAuthoredNoteRatio);
    manifest->setProperty("editorial", juce::var(editorial));
    if (!output.getChildFile("manifest.json").replaceWithText(
            juce::JSON::toString(juce::var(manifest), false), false, false, "\n"))
        return fail(output, "Could not save MIDI manifest");
    writeStatus(output, "completed", "ready", static_cast<int>(tracks.size()),
                static_cast<int>(tracks.size()));
    return 0;
}
