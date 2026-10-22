#pragma once

#include "core/OrchestrationScore.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {
// A reversible feature boundary, not a MIDI filter or a rhythmic template.
struct AiInstrumentationPolicy {
    static bool pitchedOnly() {
        return juce::SystemStats::getEnvironmentVariable("PULSO_AI_PITCHED_ONLY", {}) == "1";
    }
    static bool percussionIdentity(const juce::String& key) {
        if (const auto voice = voiceIdFromKey(key.toStdString());
            voice && isVoiceInFamily(*voice, VoiceFamily::Rhythm)) return true;
        if (const auto* instrument = instrumentDefinition(key.toStdString());
            instrument && instrument->department == ScoreDepartment::Rhythm) return true;
        return key == "Drum Rack";
    }
    static juce::String brief() {
        return "FEATURE AVAILABILITY: This composition session supports pitched musical voices only. "
            "Do not plan or write drums, drum kits, percussion tracks, kick, snare, clap, hats, shakers, "
            "cymbals, unpitched hits or Drum Rack devices, even if a style normally includes them. "
            "Compose a complete harmonic-melodic work without depending on absent drum parts. "
            "Bass, stabs, chord pulses, arpeggios, ostinatos, syncopation, rhythmic counterpoint and "
            "pitched textures remain fully available; pitched-only does NOT mean rhythm-free, slow, "
            "sparse or sustained-only. Keep the requested musical character and meaningful independent "
            "voices. Set percussion_free=true, kick_state=muted, percussion_density=0, "
            "kick_continuity=free, rhythm_motif_id=\"\" and percussion motif/mutation/gesture arrays=[] where required. "
            "All actual notes, timing and orchestration are still your own musical decisions. "
            "No local code will delete percussion tracks from an already composed arrangement.";
    }
    // Only the schema is constrained. Never call this on an AI response/score.
    static void constrainSchema(juce::var schema, const juce::String& field = {}) {
        if (!pitchedOnly()) return;
        auto* object = schema.getDynamicObject();
        if (object == nullptr) {
            if (auto* values = schema.getArray())
                for (auto& value : *values) constrainSchema(value);
            return;
        }
        if (field == "rhythm_motifs" || field == "rhythm_mutations" || field == "rhythm_gestures") {
            object->setProperty("minItems", 0);
            object->setProperty("maxItems", 0);
            return; // Keep the unused item schema valid; no item can be authored.
        }
        if (auto* values = object->getProperty("enum").getArray())
            for (int i = values->size(); --i >= 0;)
                if ((*values)[i].isString() && percussionIdentity((*values)[i].toString())) values->remove(i);
        if (field == "percussion_free") object->setProperty("enum", juce::Array<juce::var>{true});
        if (field == "percussion_density") object->setProperty("enum", juce::Array<juce::var>{0});
        if (field == "kick_state") object->setProperty("enum", juce::Array<juce::var>{"muted"});
        if (field == "kick_continuity") object->setProperty("enum", juce::Array<juce::var>{"free"});
        if (field == "rhythm_motif_id") object->setProperty("enum", juce::Array<juce::var>{""});
        for (auto& item : object->getProperties()) {
            if (item.name == juce::Identifier("enum")) continue;
            constrainSchema(item.value, item.name.toString());
        }
    }
    static juce::String prepareRequest(const juce::String& body) {
        if (!pitchedOnly()) return body;
        auto request = juce::JSON::parse(body);
        auto* object = request.getDynamicObject();
        if (object == nullptr) return body;
        auto instructions = object->getProperty("instructions").toString();
        if (instructions.isNotEmpty()) instructions += "\n";
        object->setProperty("instructions", instructions + brief());
        constrainSchema(request.getProperty("text", {}).getProperty("format", {}).getProperty("schema", {}));
        return juce::JSON::toString(request, true);
    }
    static bool validateCast(const juce::String& source, juce::String& error) {
        if (!pitchedOnly()) return true;
        const auto root = juce::JSON::parse(source);
        if (const auto* instruments = root.getProperty("instruments", {}).getArray())
            for (const auto& instrument : *instruments)
                for (const auto* field : {"instrument", "source_voice", "live_device"})
                    if (percussionIdentity(instrument.getProperty(field, {}).toString())) {
                        error = "This session supports pitched voices only: percussion owner " +
                            instrument.getProperty("id", {}).toString() +
                            " is unavailable. Compose a new pitched-only plan; no tracks were removed.";
                        return false;
                    }
        return true;
    }
};
} // namespace pulso::plugin
