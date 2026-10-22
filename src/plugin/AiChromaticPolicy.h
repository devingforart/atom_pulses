#pragma once

#include "core/SongComposer.h"
#include "core/Scale.h"
#include <juce_core/juce_core.h>
#include <algorithm>

namespace pulso::plugin {
// Explicit per-job policy. An absent value preserves older jobs/other clients.
struct AiChromaticPolicy {
    static bool strict() {
        return juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC", {}) == "0";
    }
    static bool allows(const SongPlan& plan, int pitch) {
        if (!strict()) return true;
        if (plan.scale == ScaleKind::Chromatic) return false;
        const auto pc = ((pitch - plan.rootPitchClass) % 12 + 12) % 12;
        const auto intervals = intervalsFor(plan.scale);
        return std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
    }
    static juce::String instructions() {
        const auto setting = juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC", {});
        if (setting == "0")
            return "\nUSER PITCH CONTRACT (overrides stylistic chromatic suggestions): Chromaticism is OFF. "
                "Choose and keep ONE named non-chromatic key/mode for the entire work. All pitched MIDI notes, "
                "including bass, chords, ornaments, approaches and sustained tones, must belong to that SAME "
                "declared scale. Spell its pitch classes explicitly in the musical plan. No borrowed "
                "chords, altered dominants, raised minor leading tones, modal mixture, modulation, bitonality "
                "or external passing notes. Natural minor means natural minor, not harmonic minor. "
                "A style reference never overrides this choice. Build expression through in-key harmony, "
                "voice leading, rhythm, register, articulation and dynamics. Do not treat this as chord-tones-only "
                "or a ban on all diatonic dissonance. Unpitched drum mappings are exempt. "
                "Review and correct any offending explicit notes yourself; no code retunes or deletes them.\n";
        if (setting == "1")
            return "\nUSER PITCH CONTRACT: Chromaticism is permitted, not required. Retain a clear tonal center "
                "and use external notes only for audible harmonic or melodic purpose, with intentional "
                "preparation, voicing and resolution. A permission is not a quota for dissonance.\n";
        return {};
    }
    static juce::String prepareRequest(const juce::String& body) {
        const auto contract = instructions();
        if (contract.isEmpty()) return body;
        auto request = juce::JSON::parse(body);
        auto* object = request.getDynamicObject();
        if (object == nullptr) return body;
        object->setProperty("instructions", object->getProperty("instructions").toString() + contract);
        return juce::JSON::toString(request, true);
    }
    static void constrain(juce::DynamicObject& pitch, const SongPlan& plan, VoiceId voice,
                          int minimum, int maximum) {
        if (!strict() || voiceDefinition(voice).family == VoiceFamily::Rhythm) return;
        juce::Array<juce::var> values;
        for (int n = std::max(0, minimum); n <= std::min(127, maximum); ++n)
            if (allows(plan, n)) values.add(n);
        pitch.setProperty("enum", values);
    }
};
} // namespace pulso::plugin
