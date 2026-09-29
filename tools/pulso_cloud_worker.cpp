#include "plugin/AiComposer.h"
#include "plugin/MidiExporter.h"
#include "plugin/SongGenerationPipeline.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stop_token>

namespace {

void writeStatus(const juce::File& output, const juce::String& state,
                 const juce::String& stage, int completed, int total) {
    auto* object = new juce::DynamicObject();
    object->setProperty("state", state);
    object->setProperty("stage", stage);
    object->setProperty("completed", completed);
    object->setProperty("total", total);
    output.getChildFile("progress.json").replaceWithText(
        juce::JSON::toString(juce::var(object), false), false, false, "\n");
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
    writeStatus(output, "running", "blueprint", 0, 1);
    juce::String error;
    auto plan = pulso::plugin::SongGenerationPipeline::plan(request,
        std::stop_token{}, error,
        [&output](const pulso::plugin::AiSongProgressUpdate& update) {
            const auto stage = update.stage == pulso::plugin::AiSongStage::Blueprint ? "blueprint" :
                update.stage == pulso::plugin::AiSongStage::PerformanceBlock ? "writing" :
                update.stage == pulso::plugin::AiSongStage::Recovery ? "recovery" : "validation";
            writeStatus(output, "running", stage, static_cast<int>(update.completed),
                        static_cast<int>(update.total));
        });
    if (error.isNotEmpty() || plan.instruments.empty())
        return fail(output, error.isNotEmpty() ? error : "AI returned an empty score");
    pulso::plugin::SongGenerationPipeline::finalizePlan(plan, request);

    writeStatus(output, "running", "rendering", 0, static_cast<int>(plan.sections.size()));
    const auto song = pulso::plugin::SongGenerationPipeline::render(plan, request,
        [&output](std::size_t completed, std::size_t total, const pulso::SongSection&) {
            writeStatus(output, "running", "rendering", static_cast<int>(completed),
                        static_cast<int>(total));
        });
    if (song.notes.empty()) return fail(output, "Rendered score has no MIDI notes");

    pulso::plugin::MidiExportOptions fullOptions;
    fullOptions.bpm = plan.bpm;
    fullOptions.timeSignatureNumerator = static_cast<int>(std::lround(plan.beatsPerBar));
    fullOptions.clipName = juce::String::fromUTF8(plan.title.c_str());
    fullOptions.includeKeySignature = true;
    fullOptions.rootPitchClass = plan.rootPitchClass;
    fullOptions.scale = plan.scale;
    if (!pulso::plugin::writePatternToMidiFile(song, output.getChildFile("full-song.mid"), fullOptions))
        return fail(output, "Could not write complete MIDI song");

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
    manifest->setProperty("engineVersion", PULSO_VERSION_STRING);
    manifest->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    manifest->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    manifest->setProperty("bpm", plan.bpm);
    manifest->setProperty("bars", plan.totalBars);
    manifest->setProperty("fullFile", "full-song.mid");
    manifest->setProperty("tracks", tracks);
    if (!output.getChildFile("manifest.json").replaceWithText(
            juce::JSON::toString(juce::var(manifest), false), false, false, "\n"))
        return fail(output, "Could not save MIDI manifest");
    writeStatus(output, "completed", "ready", static_cast<int>(tracks.size()),
                static_cast<int>(tracks.size()));
    return 0;
}
