#include "LiveDeployer.h"

#include <algorithm>

namespace pulso::plugin {
namespace {

juce::File bridgeDirectory() {
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("PULSO_LIVE_BRIDGE_DIR", {});
    if (overridePath.isNotEmpty()) return juce::File(overridePath);
   #if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty()) return juce::File(local).getChildFile("PULSO").getChildFile("LiveBridge");
   #endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PULSO").getChildFile("LiveBridge");
}

juce::String departmentName(ScoreDepartment department) {
    if (department == ScoreDepartment::Rhythm) return "rhythm";
    if (department == ScoreDepartment::Melody) return "melody";
    return "harmony";
}

void addCandidate(juce::Array<juce::var>& candidates, const juce::String& value) {
    if (value.isNotEmpty() && !candidates.contains(value)) candidates.add(value);
}

bool isSilentContainer(const juce::String& device) {
    return device == "Drum Rack" || device == "Instrument Rack" || device == "Sampler" ||
           device == "Simpler" || device == "Drum Sampler" || device == "Impulse" ||
           device == "External Instrument";
}

void addNativeSoundProperties(juce::DynamicObject& track, const juce::String& device,
                              const juce::String& intent, double prominence,
                              ScoreDepartment department, const juce::String& catalogId = {}) {
    track.setProperty("sound_source", "live_native");
    const auto requestedDevice = device.isNotEmpty() && device != "auto" ? device :
        (department == ScoreDepartment::Rhythm ? "Drum Rack" : "Drift");
    track.setProperty("native_device", requestedDevice);
    track.setProperty("preset_intent", intent);
    juce::Array<juce::var> candidates;
    addCandidate(candidates, intent);
    addCandidate(candidates, catalogId.replaceCharacter('_', ' ') +
                             (department == ScoreDepartment::Rhythm ? " kit" : " orchestral"));
    if (department == ScoreDepartment::Rhythm) {
        const auto identity = catalogId.toLowerCase();
        if (identity.contains("taiko") || identity.contains("timpani") ||
            identity.contains("percussion") || identity.contains("shaker")) {
            addCandidate(candidates, "Percussion Spirit Kit.adg");
            addCandidate(candidates, "Percussion Core Kit.adg");
        }
        addCandidate(candidates, "909 Core Kit.adg");
        addCandidate(candidates, "808 Core Kit.adg");
    } else {
        if (!isSilentContainer(requestedDevice)) addCandidate(candidates, requestedDevice);
        // A raw synthesizer is audible; an empty Instrument/Sampler Rack is not.
        addCandidate(candidates, department == ScoreDepartment::Melody ? "Wavetable" : "Drift");
    }
    track.setProperty("device_candidates", candidates);
    track.setProperty("mixer_gain_db", juce::jlimit(-18.0, 0.0, -12.0 + prominence * 10.0));
}

} // namespace

bool writeLiveDeploymentRequest(const Pattern& pattern, const LiveDeploymentOptions& options,
                                juce::String& statusMessage, const juce::File& directoryOverride) {
    if (pattern.notes.empty() || pattern.parts.empty()) {
        statusMessage = "COMPOSE A SONG BEFORE DEPLOYING";
        return false;
    }
    auto root = new juce::DynamicObject();
    root->setProperty("schema_version", 3);
    root->setProperty("request_id", juce::Uuid().toString());
    root->setProperty("created_utc_ms", juce::Time::getCurrentTime().toMilliseconds());
    root->setProperty("title", options.title.isNotEmpty() ? options.title : "PULSO Song");
    root->setProperty("bpm", options.bpm);
    root->setProperty("time_signature_numerator", options.numerator);
    root->setProperty("time_signature_denominator", options.denominator);
    root->setProperty("length_beats", pattern.lengthBeats);
    root->setProperty("deployment_mode",
        options.aggregateDepartmentStems ? "quick_3_stem" : "full_orchestration");

    root->setProperty("sound_engine", "ableton_live_native");

    juce::Array<juce::var> tracks;
    if (options.aggregateDepartmentStems) {
        for (auto departmentIndex = 0; departmentIndex < 3; ++departmentIndex) {
            const auto department = static_cast<ScoreDepartment>(departmentIndex);
            juce::Array<juce::var> notes;
            for (const auto& note : pattern.notes) {
                const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& item) {
                    return item.id == note.partId && item.department == department;
                });
                if (part == pattern.parts.end()) continue;
                auto item = new juce::DynamicObject();
                item->setProperty("pitch", note.pitch);
                item->setProperty("start", note.startBeat);
                item->setProperty("duration", note.durationBeats);
                item->setProperty("velocity", note.velocity);
                item->setProperty("channel", note.channel);
                notes.add(juce::var(item));
            }
            if (notes.isEmpty()) continue;
            auto track = new juce::DynamicObject();
            const auto label = department == ScoreDepartment::Rhythm ? "RHYTHM" :
                               department == ScoreDepartment::Harmony ? "HARMONY" : "MELODY";
            track->setProperty("name", "PULSO " + juce::String(label));
            track->setProperty("track_key", "department:" + departmentName(department));
            track->setProperty("department", departmentName(department));
            track->setProperty("role", "Native Live Sound Director stem");
            addNativeSoundProperties(*track,
                department == ScoreDepartment::Rhythm ? "Drum Rack" :
                department == ScoreDepartment::Harmony ? "Instrument Rack" : "Wavetable",
                department == ScoreDepartment::Rhythm ? "cohesive production drum kit" :
                department == ScoreDepartment::Harmony ? "warm expressive harmonic ensemble" :
                                                         "expressive foreground voice", 0.72, department,
                department == ScoreDepartment::Rhythm ? "production_drums" :
                department == ScoreDepartment::Harmony ? "harmonic_ensemble" : "foreground_voice");
            track->setProperty("notes", notes);
            tracks.add(juce::var(track));
        }
    } else {
    for (const auto& part : pattern.parts) {
        juce::Array<juce::var> notes;
        for (const auto& note : pattern.notes) {
            if (note.partId != part.id) continue;
            auto item = new juce::DynamicObject();
            item->setProperty("pitch", note.pitch);
            item->setProperty("start", note.startBeat);
            item->setProperty("duration", note.durationBeats);
            item->setProperty("velocity", note.velocity);
            item->setProperty("channel", note.channel);
            notes.add(juce::var(item));
        }
        if (notes.isEmpty()) continue;
        auto track = new juce::DynamicObject();
        track->setProperty("name", "PULSO " + juce::String::fromUTF8(part.name.c_str()));
        track->setProperty("track_key", "part:" + juce::String(static_cast<int>(part.id)));
        track->setProperty("part_id", static_cast<int>(part.id));
        track->setProperty("catalog_id", juce::String::fromUTF8(part.catalogId.c_str()));
        track->setProperty("department", departmentName(part.department));
        track->setProperty("role", juce::String::fromUTF8(part.role.c_str()));
        track->setProperty("orchestral_function", juce::String::fromUTF8(part.orchestralFunction.c_str()));
        track->setProperty("articulation", juce::String::fromUTF8(part.articulation.c_str()));
        track->setProperty("divisi_voices", part.divisiVoices);
        addNativeSoundProperties(*track, juce::String::fromUTF8(part.liveDevice.c_str()),
            juce::String::fromUTF8(part.livePresetIntent.c_str()), part.prominence,
            part.department, juce::String::fromUTF8(part.catalogId.c_str()));
        track->setProperty("notes", notes);
        tracks.add(juce::var(track));
    }
    }
    root->setProperty("tracks", tracks);
    if (tracks.isEmpty()) {
        statusMessage = "NO ORCHESTRAL PARTS TO DEPLOY";
        return false;
    }

    const auto directory = directoryOverride.getFullPathName().isEmpty() ? bridgeDirectory()
                                                                         : directoryOverride;
    if (!directory.createDirectory()) {
        statusMessage = "CANNOT CREATE LIVE BRIDGE DIRECTORY";
        return false;
    }
    const auto pending = directory.getChildFile("request.pending.json");
    const auto request = directory.getChildFile("request.json");
    if (!pending.replaceWithText(juce::JSON::toString(juce::var(root), false), false, false, "\n")) {
        statusMessage = "CANNOT WRITE LIVE DEPLOYMENT REQUEST";
        return false;
    }
    const auto published = request.existsAsFile() ? pending.replaceFileIn(request)
                                                   : pending.moveFileTo(request);
    if (!published) {
        statusMessage = "CANNOT WRITE LIVE DEPLOYMENT REQUEST";
        return false;
    }
    statusMessage = "DEPLOY REQUEST SENT - " + juce::String(tracks.size()) + " TRACKS";
    return true;
}

juce::String readLiveDeploymentStatus() {
    if (!liveBridgeIsAvailable()) return "ENABLE PulsoDeployRemote IN LIVE SETTINGS";
    const auto file = bridgeDirectory().getChildFile("status.json");
    if (!file.existsAsFile()) return {};
    const auto parsed = juce::JSON::parse(file);
    if (auto* object = parsed.getDynamicObject())
        return object->getProperty("message").toString();
    return {};
}

bool liveBridgeIsAvailable() {
    const auto heartbeat = bridgeDirectory().getChildFile("heartbeat.json");
    return heartbeat.existsAsFile() &&
           juce::Time::getCurrentTime().toMilliseconds() - heartbeat.getLastModificationTime().toMilliseconds() < 4000;
}

juce::String readLiveNativeInventorySummary() {
    const auto file = bridgeDirectory().getChildFile("inventory.json");
    if (!file.existsAsFile()) return "NATIVE INVENTORY PENDING";
    const auto parsed = juce::JSON::parse(file);
    if (auto* object = parsed.getDynamicObject()) {
        const auto count = static_cast<int>(object->getProperty("loadable_count"));
        return juce::String(count) + " LIVE SOUNDS INDEXED";
    }
    return "NATIVE INVENTORY PENDING";
}

bool liveNativeInventoryIsReady() {
    if (!liveBridgeIsAvailable()) return false;
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("inventory.json"));
    if (auto* object = parsed.getDynamicObject())
        return static_cast<bool>(object->getProperty("complete")) &&
               static_cast<int>(object->getProperty("loadable_count")) > 0;
    return false;
}

} // namespace pulso::plugin
