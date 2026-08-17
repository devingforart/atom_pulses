#include "LiveDeployer.h"

#include <algorithm>
#include <cmath>

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

juce::String expressionTypeName(ExpressionEventType type) {
    if (type == ExpressionEventType::ChannelPressure) return "channel_pressure";
    if (type == ExpressionEventType::PolyAftertouch) return "poly_aftertouch";
    return "pitch_bend";
}

template <typename Matcher>
void addPerformanceProperties(juce::DynamicObject& track, const Pattern& pattern, Matcher&& matches) {
    const auto audibleOnDestination = [&](VoiceId voice, double beat) {
        return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == voice && matches(note.partId, note.voice) &&
                   beat >= note.startBeat - 1.0 && beat <= note.endBeat() + 0.50;
        });
    };
    juce::Array<juce::var> controls;
    std::vector<const ControlEvent*> selectedControls;
    for (const auto& control : pattern.controls) {
        if (!matches(control.partId, control.voice)) continue;
        const auto setup = (control.controller == 6 || control.controller == 38 ||
                            control.controller == 100 || control.controller == 101) &&
                           control.beat <= 0.001;
        if (!setup && !audibleOnDestination(control.voice, control.beat)) continue;
        const auto duplicate = std::find_if(selectedControls.begin(), selectedControls.end(), [&](const auto* item) {
            return std::abs(item->beat - control.beat) < 0.0001 && item->controller == control.controller &&
                   item->channel == control.channel;
        });
        if (duplicate != selectedControls.end()) {
            if ((*duplicate)->partId == 0 && control.partId != 0) *duplicate = &control;
            continue;
        }
        selectedControls.push_back(&control);
    }
    for (const auto* selected : selectedControls) {
        const auto& control = *selected;
        auto* item = new juce::DynamicObject();
        item->setProperty("beat", control.beat);
        item->setProperty("controller", control.controller);
        item->setProperty("value", control.value);
        item->setProperty("channel", control.channel);
        controls.add(juce::var(item));
    }
    juce::Array<juce::var> expressions;
    std::vector<const ExpressionEvent*> selectedExpressions;
    for (const auto& expression : pattern.expressions) {
        if (!matches(expression.partId, expression.voice)) continue;
        if (!audibleOnDestination(expression.voice, expression.beat)) continue;
        const auto duplicate = std::find_if(selectedExpressions.begin(), selectedExpressions.end(), [&](const auto* item) {
            return std::abs(item->beat - expression.beat) < 0.0001 && item->type == expression.type &&
                   item->note == expression.note && item->channel == expression.channel;
        });
        if (duplicate != selectedExpressions.end()) {
            if ((*duplicate)->partId == 0 && expression.partId != 0) *duplicate = &expression;
            continue;
        }
        selectedExpressions.push_back(&expression);
    }
    for (const auto* selected : selectedExpressions) {
        const auto& expression = *selected;
        auto* item = new juce::DynamicObject();
        item->setProperty("beat", expression.beat);
        item->setProperty("type", expressionTypeName(expression.type));
        item->setProperty("value", expression.value);
        item->setProperty("note", expression.note);
        item->setProperty("channel", expression.channel);
        expressions.add(juce::var(item));
    }
    track.setProperty("controls", controls);
    track.setProperty("expressions", expressions);
    track.setProperty("expression_projection_version", 1);
}

void addNativeSoundProperties(juce::DynamicObject& track, const juce::String& device,
                              const juce::String& intent, double prominence,
                              ScoreDepartment department, const juce::String& catalogId = {}) {
    track.setProperty("sound_source", "live_native");
    const auto requestedDevice = device.isNotEmpty() && device != "auto" ? device :
        (department == ScoreDepartment::Rhythm ? "Drum Rack" : "Drift");
    track.setProperty("native_device", requestedDevice);
    track.setProperty("preset_intent", intent);
    track.setProperty("playback_mode", department == ScoreDepartment::Rhythm
        ? "adaptive_percussion" : "chromatic_instrument");
    track.setProperty("same_pitch_overlap_policy", "trim_previous");
    track.setProperty("articulation_duration_policy", "instrument_bound");
    const auto identity = catalogId.toLowerCase();
    const auto critical = identity == "kick_drum" || identity == "sub_synth" ||
                          identity == "electric_bass" || identity == "lead_synth";
    const auto featured = !critical && (prominence >= 0.68 || department == ScoreDepartment::Melody);
    track.setProperty("timbre_priority", critical ? "critical" : featured ? "featured" : "support");
    track.setProperty("minimum_intent_fidelity", critical ? 0.65 : featured ? 0.50 : 0.35);
    const auto releaseSeconds = identity == "kick_drum" ? 0.14 :
        identity == "snare_clap" ? 0.30 : identity == "hi_hats" ? 0.16 :
        identity == "shakers" ? 0.22 : identity == "latin_percussion" ? 0.28 :
        identity == "orchestral_percussion" ? 0.55 : identity == "cymbals" ? 1.40 :
        identity == "sub_synth" ? 0.16 : identity == "electric_bass" ? 0.24 :
        identity == "poly_synth" ? 0.22 : identity == "piano" ? 0.55 :
        identity == "guitar" ? 0.42 : identity == "lead_synth" ? 0.42 :
        identity == "alto_flute" ? 0.60 : identity == "flute" ? 0.55 :
        identity == "piccolo" ? 0.48 : identity == "analog_pad" ? 0.95 :
        identity == "ambient_texture" ? 1.35 : 0.65;
    track.setProperty("release_max_seconds", releaseSeconds);
    juce::Array<juce::var> candidates;
    addCandidate(candidates, intent);
    addCandidate(candidates, catalogId.replaceCharacter('_', ' ') +
                             (department == ScoreDepartment::Rhythm ? " kit" : " orchestral"));
    if (department == ScoreDepartment::Rhythm) {
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

void addSoundWorldProperties(juce::DynamicObject& track, const Pattern& pattern) {
    track.setProperty("sound_world", juce::String::fromUTF8(pattern.soundWorld.c_str()));
    track.setProperty("sound_warmth", pattern.soundWarmth);
    track.setProperty("sound_brightness", pattern.soundBrightness);
    track.setProperty("acoustic_electronic_balance", pattern.acousticElectronicBalance);
}

} // namespace

bool writeLiveDeploymentRequest(const Pattern& pattern, const LiveDeploymentOptions& options,
                                juce::String& statusMessage, const juce::File& directoryOverride) {
    if (pattern.notes.empty() || pattern.parts.empty()) {
        statusMessage = "COMPOSE A SONG BEFORE DEPLOYING";
        return false;
    }
    if (pattern.productionAuditPerformed && !pattern.productionReady) {
        statusMessage = "PRODUCTION GATE BLOCKED INVALID SCORE";
        return false;
    }
    auto root = new juce::DynamicObject();
    root->setProperty("schema_version", 8);
    root->setProperty("request_id", juce::Uuid().toString());
    root->setProperty("created_utc_ms", juce::Time::getCurrentTime().toMilliseconds());
    auto safeTitle = options.title.isNotEmpty() ? options.title : juce::String("PULSO Song");
    const auto middleDot = juce::String::charToString(static_cast<juce::juce_wchar>(0x00b7));
    const auto mojibakeDot = juce::String::charToString(static_cast<juce::juce_wchar>(0x00c2)) + middleDot;
    safeTitle = safeTitle.replace(mojibakeDot, " - ").replace(middleDot, " - ");
    root->setProperty("title", safeTitle);
    root->setProperty("bpm", options.bpm);
    root->setProperty("time_signature_numerator", options.numerator);
    root->setProperty("time_signature_denominator", options.denominator);
    root->setProperty("length_beats", pattern.lengthBeats);
    root->setProperty("deployment_mode",
        options.aggregateDepartmentStems ? "quick_3_stem" : "full_orchestration");

    root->setProperty("sound_engine", "ableton_live_native");
    root->setProperty("expression_delivery", "native_editable_with_lossless_midi_source");
    root->setProperty("production_score", pattern.productionScore);
    root->setProperty("production_domain", juce::String::fromUTF8(pattern.productionDomain.c_str()));
    root->setProperty("production_mode_source", juce::String::fromUTF8(pattern.productionModeSource.c_str()));
    root->setProperty("electronic_production_audited", pattern.electronicProductionAudited);
    if (pattern.electronicProductionAudited)
        root->setProperty("electronic_production_score", pattern.electronicProductionScore);
    root->setProperty("sound_world", juce::String::fromUTF8(pattern.soundWorld.c_str()));
    root->setProperty("narrative_audited", pattern.narrativeAuditPerformed);
    root->setProperty("narrative_score", pattern.narrativeScore);
    root->setProperty("ai_authored_note_ratio", pattern.aiAuthoredNoteRatio);
    root->setProperty("primary_voice_authorship_coverage", pattern.primaryVoiceAuthorshipCoverage);
    root->setProperty("thematic_recall_ratio", pattern.thematicRecallRatio);
    juce::Array<juce::var> narrativeIssues;
    for (const auto& issue : pattern.narrativeIssues)
        narrativeIssues.add(juce::String::fromUTF8(issue.c_str()));
    root->setProperty("narrative_issues", narrativeIssues);

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
                item->setProperty("origin", juce::String(noteOriginKey(note.origin).data()));
                item->setProperty("narrative_id", static_cast<int>(note.narrativeId));
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
            track->setProperty("catalog_id", department == ScoreDepartment::Rhythm ? "production_drums" :
                department == ScoreDepartment::Harmony ? "harmonic_ensemble" : "foreground_voice");
            addNativeSoundProperties(*track,
                department == ScoreDepartment::Rhythm ? "Drum Rack" :
                department == ScoreDepartment::Harmony ? "Instrument Rack" : "Wavetable",
                department == ScoreDepartment::Rhythm ? "cohesive production drum kit" :
                department == ScoreDepartment::Harmony ? "warm expressive harmonic ensemble" :
                                                         "expressive foreground voice", 0.72, department,
                department == ScoreDepartment::Rhythm ? "production_drums" :
                department == ScoreDepartment::Harmony ? "harmonic_ensemble" : "foreground_voice");
            addSoundWorldProperties(*track, pattern);
            addPerformanceProperties(*track, pattern, [&](std::uint16_t partId, VoiceId voice) {
                if (partId != 0) {
                    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(),
                        [&](const auto& part) { return part.id == partId; });
                    return found != pattern.parts.end() && found->department == department;
                }
                return std::any_of(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
                    return part.department == department && part.sourceVoice == voice;
                });
            });
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
            item->setProperty("origin", juce::String(noteOriginKey(note.origin).data()));
            item->setProperty("narrative_id", static_cast<int>(note.narrativeId));
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
        addSoundWorldProperties(*track, pattern);
        addPerformanceProperties(*track, pattern, [&](std::uint16_t partId, VoiceId voice) {
            return partId == part.id || (partId == 0 && voice == part.sourceVoice);
        });
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

juce::String readLiveNativeCapabilitiesSummary() {
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("inventory.json"));
    const auto* root = parsed.getDynamicObject();
    const auto* capabilities = root != nullptr
        ? root->getProperty("capabilities").getDynamicObject() : nullptr;
    if (capabilities == nullptr) return {};
    const auto join = [capabilities](const char* property) {
        juce::StringArray values;
        if (const auto* array = capabilities->getProperty(property).getArray())
            for (const auto& value : *array) values.add(value.toString());
        return values.joinIntoString(", ");
    };
    return "Ableton playback inventory. Prefer exact installed identities: " + join("exact") +
        ". Family-only substitutions (use intentionally and sparingly): " + join("family_fallback") +
        ". Unavailable identities (do not assign unless structurally essential): " + join("unavailable") + ".";
}

juce::String readLiveDeploymentReport() {
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("status.json"));
    const auto* root = parsed.getDynamicObject();
    const auto* details = root != nullptr ? root->getProperty("details").getDynamicObject() : nullptr;
    if (details == nullptr) return {};
    juce::StringArray lines;
    if (const auto* sounds = details->getProperty("sounds").getArray()) {
        for (const auto& value : *sounds) {
            const auto* sound = value.getDynamicObject();
            if (sound == nullptr) continue;
            auto line = sound->getProperty("track").toString() + " -> " +
                        sound->getProperty("matched").toString() + " [" +
                        sound->getProperty("quality").toString() + "] " +
                        sound->getProperty("state").toString();
            if (static_cast<bool>(sound->getProperty("shared_sound"))) line += " (shared)";
            lines.add(std::move(line));
        }
    }
    return lines.joinIntoString("\n");
}

juce::String readLiveAudibleExecutionFeedback() {
    juce::StringArray issues;
    const auto status = juce::JSON::parse(bridgeDirectory().getChildFile("status.json"));
    const auto* root = status.getDynamicObject();
    const auto* details = root != nullptr ? root->getProperty("details").getDynamicObject() : nullptr;
    if (details != nullptr) {
        if (const auto* contracts = details->getProperty("timbre_contracts").getArray()) {
            for (const auto& value : *contracts) {
                const auto* contract = value.getDynamicObject();
                if (contract == nullptr || static_cast<bool>(contract->getProperty("passed"))) continue;
                issues.add(contract->getProperty("track").toString() + " matched " +
                    contract->getProperty("matched").toString() + " below fidelity floor " +
                    contract->getProperty("minimum_fidelity").toString());
                if (issues.size() >= 6) break;
            }
        }
    }
    const auto audible = juce::JSON::parse(bridgeDirectory().getChildFile("audible_audit.json"));
    if (const auto* object = audible.getDynamicObject()) {
        const auto observations = static_cast<int>(object->getProperty("expected_active_observations"));
        if (observations > 0) {
            const auto presence = static_cast<double>(object->getProperty("audible_presence_ratio"));
            const auto tails = static_cast<int>(object->getProperty("tail_violations"));
            if (presence < 0.92)
                issues.add("rendered meter presence ratio=" + juce::String(presence, 3));
            if (tails > 0)
                issues.add("rendered tail violations=" + juce::String(tails));
        }
    }
    if (issues.isEmpty()) return {};
    return "Previous Ableton audible execution defects: " + issues.joinIntoString("; ") +
        ". Choose installed identities and articulation/envelope intents that remove these causes.";
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
