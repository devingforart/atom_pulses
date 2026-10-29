#pragma once

#include "AiClubPolicy.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace pulso::plugin::club::production {

inline bool foregroundGrooveExperiment() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_CLUB_FOREGROUND_GROOVE_EXPERIMENT", {}) == "1";
}

inline bool jointGrooveNucleusExperiment() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_CLUB_JOINT_GROOVE_EXPERIMENT", {}) == "1";
}

inline bool isPlan(const juce::String& json) {
    return juce::JSON::parse(json).getProperty("kind", {}).toString() == "club_production_v1";
}

inline bool requiresFinaleReview(const SongPlan& plan, bool terminalPhraseReady,
                                  bool ensembleReleaseReady) {
    return plan.compositionBehavior == CompositionBehavior::Club &&
        !plan.continuousProductionSection && !plan.sections.empty() &&
        (!terminalPhraseReady || !ensembleReleaseReady);
}

inline bool coreLayer(const juce::String& json, const std::string& id) {
    const auto root = juce::JSON::parse(json);
    if (const auto* ids = root.getProperty("groove_core_ids", {}).getArray())
        for (const auto& value : *ids) if (value.toString().toStdString() == id) return true;
    return false; // Historical plans remain readable without inventing a core.
}

inline bool cycleBoundLayer(const juce::String& json, const std::string& id) {
    if (coreLayer(json, id)) return true;
    const auto root = juce::JSON::parse(json);
    if (const auto* layers = root.getProperty("layers", {}).getArray())
        for (const auto& layer : *layers)
            if (layer.getProperty("instrument_id", {}).toString().toStdString() == id)
                return layer.getProperty("timing_mode", {}).toString() == "cycle_bound";
    return false; // Legacy non-core layers remain free, not silently retimed.
}

// Membership in an AI-authored cycle, not quantisation or an onset generator.
inline bool coreAttackAllowed(const juce::String& json, const std::string& id, double absoluteBeat) {
    if (!cycleBoundLayer(json, id)) return true;
    const auto root = juce::JSON::parse(json);
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers) {
        if (layer.getProperty("instrument_id", {}).toString().toStdString() != id) continue;
        const auto cycle = static_cast<double>(layer.getProperty("cycle_beats", 0));
        const auto origin = static_cast<double>(layer.getProperty("cycle_origin_beat", 0));
        if (cycle <= 0 || absoluteBeat < origin - .001) return false;
        auto phase = std::fmod(absoluteBeat - origin, cycle);
        if (phase < 0) phase += cycle;
        if (const auto* attacks = layer.getProperty("rhythm_cell", {}).getArray())
            for (const auto& attack : *attacks)
                if (std::abs(phase - static_cast<double>(attack)) < .001) return true;
        return false;
    }
    return false;
}

// Constrain the output BEFORE generation: a technical window cannot receive
// an event which releases outside it. This does not trim or quantise a note.
inline juce::var boundedNoteSchema(double beats) {
    juce::Array<juce::var> choices;
    for (int tick = 0; tick < static_cast<int>(std::round(beats * 4)); ++tick) {
        auto choice = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
          "required":["beat","duration","pitch","velocity"],"properties":{
            "beat":{"type":"number","enum":[]},
            "duration":{"type":"number","minimum":0.25,"maximum":1,"multipleOf":0.25},
            "pitch":{"$ref":"#/$defs/scalePitch"},
            "velocity":{"type":"integer","minimum":1,"maximum":127}}})json");
        auto props = choice.getProperty("properties", {});
        props.getProperty("beat", {}).getDynamicObject()->setProperty("enum", juce::Array<juce::var>{tick / 4.0});
        props.getProperty("duration", {}).getDynamicObject()->setProperty("maximum", beats - tick / 4.0);
        choices.add(choice);
    }
    juce::var result(new juce::DynamicObject());
    result.getDynamicObject()->setProperty("anyOf", choices);
    return result;
}

// Translate an AI-declared arrival to this response's local coordinates.
// A negative result means the ordinary note-array protocol remains unchanged.
inline double requiredArrival(const SongPlan& plan, const std::string& id,
                              std::size_t sectionIndex, int firstBar, int bars) {
    if (!nucleus::closureEnabled() || plan.sections.empty() ||
        sectionIndex + 1 != plan.sections.size()) return -1;
    const auto root = juce::JSON::parse(juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (root.getProperty("format", {}).toString() != "full_arrangement") return -1;
    const auto terminal = root.getProperty("terminal_resolution", {});
    const auto arrival = static_cast<double>(terminal.getProperty("arrival_beat", -1));
    const auto local = arrival - firstBar * plan.beatsPerBar;
    if (arrival < 0 || local < 0 || local >= bars * plan.beatsPerBar) return -1;
    if (const auto* lanes = terminal.getProperty("lanes", {}).getArray())
        for (const auto& lane : *lanes)
            if (lane.getProperty("instrument_id", {}).toString().toStdString() == id &&
                static_cast<bool>(lane.getProperty("joins_arrival",
                    std::abs(static_cast<double>(lane.getProperty("last_attack_beat", -1)) - arrival) < .001)))
                return local;
    return -1;
}

// Schema-level commitment, not note generation: the AI still authors every
// pitch, duration and velocity. Ordinary notes cannot duplicate the arrival.
inline bool windowAttackAllowed(const SongPlan& plan, const std::string& id,
                                std::size_t sectionIndex, double absoluteBeat) {
    if (sectionIndex < plan.sections.size()) {
        const auto& section = plan.sections[sectionIndex];
        const auto arrival = requiredArrival(plan, id, sectionIndex, 0, section.bars);
        if (arrival >= 0 && std::abs(absoluteBeat - section.startBar * plan.beatsPerBar - arrival) < .001)
            return true; // Explicit AI ending gesture, not a new repeating slot.
    }
    return coreAttackAllowed(juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()), id, absoluteBeat);
}

inline juce::var withRequiredArrival(const juce::var& arraySchema,
                                    const juce::var& resolvedItems, double arrival) {
    auto ordinary = juce::JSON::parse(juce::JSON::toString(arraySchema));
    auto items = juce::JSON::parse(juce::JSON::toString(resolvedItems));
    auto* choices = items.getProperty("anyOf", {}).getArray();
    juce::var landing;
    if (choices) for (int i = choices->size(); --i >= 0;) {
        const auto beat = static_cast<double>((*choices)[i].getProperty("properties", {})
            .getProperty("beat", {}).getProperty("enum", {})[0]);
        if (std::abs(beat - arrival) < .001) { landing = (*choices)[i]; choices->remove(i); }
    }
    if (!landing.isObject())
        throw std::runtime_error("AI terminal arrival contradicts permitted writing onsets; reconcile the plan before requesting MIDI");
    ordinary.getDynamicObject()->setProperty("items", choices->isEmpty() ? landing : items);
    if (choices->isEmpty()) ordinary.getDynamicObject()->setProperty("maxItems", 0);
    auto result = juce::JSON::parse(R"({"type":"object","additionalProperties":false,
        "required":["notes","arrival_notes"],"properties":{"notes":{},
        "arrival_notes":{"type":"array","minItems":1,"maxItems":32,"items":{}}}})");
    result.getDynamicObject()->setProperty("description",
        "Write the phrase in notes; write its AI-planned landing in arrival_notes at EXACT local beat " +
        juce::String(arrival, 2) + ". Release a preceding same pitch before retriggering it.");
    auto props = result.getProperty("properties", {});
    props.getDynamicObject()->setProperty("notes", ordinary);
    props.getProperty("arrival_notes", {}).getDynamicObject()->setProperty("items", landing);
    return result;
}

// Flatten response containers only. Never manufacture, retime or trim events.
inline bool readArrivalNotes(const juce::var& value, double arrival,
                             juce::Array<juce::var>& events, juce::String& error) {
    const auto* ordinary = value.getProperty("notes", {}).getArray();
    const auto* landing = value.getProperty("arrival_notes", {}).getArray();
    if (!ordinary || !landing || landing->isEmpty()) {
        error = "Missing required notes/arrival_notes containers for AI-planned landing";
        return false;
    }
    for (const auto& n : *ordinary)
        if (std::abs(static_cast<double>(n.getProperty("beat", -1)) - arrival) < .001) {
            error = "Put arrival attacks only in arrival_notes, not both containers"; return false;
        }
    for (const auto& n : *landing)
        if (std::abs(static_cast<double>(n.getProperty("beat", -1)) - arrival) > .001) {
            error = "arrival_notes contradicts the AI-planned landing time"; return false;
        }
    events.addArray(*ordinary);
    events.addArray(*landing);
    return true;
}

inline bool explicitSection(const juce::String& direction) {
    const auto lower = direction.toLowerCase();
    return lower.contains("seccion plena") || lower.contains(juce::String::fromUTF8("sección plena")) ||
        lower.contains("seccion completa") || lower.contains(juce::String::fromUTF8("sección completa")) ||
        lower.contains("production section");
}

inline juce::String schema(const juce::String& direction = {}, const SongPlan* plan = nullptr) {
    auto result = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["kind","format","identity","focus_owner_id","shared_pulse","groove_core_ids","musical_arcs","energy_trajectory","layers"],
      "properties":{
        "kind":{"type":"string","enum":["club_production_v1"]},
        "format":{"type":"string","enum":["continuous_section","full_arrangement"]},
        "identity":{"type":"string"},"focus_owner_id":{"type":"string"},
        "shared_pulse":{"type":"string"},
        "groove_core_ids":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"string"}},
        "musical_arcs":{"type":"array","minItems":1,"maxItems":12,"items":{
          "type":"object","additionalProperties":false,
          "required":["start_bar","bars","foreground_id","subject","continuation","arrival","support_response"],
          "properties":{"start_bar":{"type":"integer","minimum":0},"bars":{"type":"integer","minimum":1},
            "foreground_id":{"type":"string"},"subject":{"type":"string"},
            "continuation":{"type":"string"},"arrival":{"type":"string"},"support_response":{"type":"string"}}
        }},
        "energy_trajectory":{"type":"array","minItems":1,"maxItems":20,"items":{
          "type":"object","additionalProperties":false,
          "required":["section_index","dramatic_function","energy_intent","density_intent","identity_retained","material_reserved","layer_change","transition_consequence"],
          "properties":{
            "section_index":{"type":"integer","minimum":0},
            "dramatic_function":{"type":"string"},
            "energy_intent":{"type":"number","minimum":0,"maximum":1},
            "density_intent":{"type":"number","minimum":0,"maximum":1},
            "identity_retained":{"type":"string"},
            "material_reserved":{"type":"string"},
            "layer_change":{"type":"string"},
            "transition_consequence":{"type":"string"}
          }}},
        "layers":{"type":"array","minItems":1,"maxItems":64,"items":{
          "type":"object","additionalProperties":false,
          "required":["instrument_id","production_role","cycle_beats","cycle_origin_beat",
            "rhythm_cell","timing_mode","timing_intent","register_low","register_high","harmonic_job","articulation",
            "interaction","polyphonic","states"],
          "properties":{
            "instrument_id":{"type":"string"},"production_role":{"type":"string"},
            "cycle_beats":{"type":"number"},"cycle_origin_beat":{"type":"number"},
            "timing_mode":{"type":"string","enum":["cycle_bound","phrase_flexible"]},
            "timing_intent":{"type":"string","description":"Why these attacks support the shared meter and foreground. Distinguish intentional syncopation from envelope softness; describe phrase landing and support alignment."},
            "rhythm_cell":{"type":"array","description":"Unique ZERO-BASED beat offsets: 0 <= offset < cycle_beats, never cycle_beats itself.","maxItems":128,"items":{"type":"number","minimum":0,"multipleOf":0.25}},
            "register_low":{"type":"integer"},"register_high":{"type":"integer"},
            "harmonic_job":{"type":"string"},"articulation":{"type":"string"},
            "interaction":{"type":"string"},"polyphonic":{"type":"boolean"},
            "states":{"type":"array","minItems":1,"maxItems":64,"items":{
              "type":"object","additionalProperties":false,
              "required":["section_index","mode","minimum_sounding_fraction","entry_beat","release_beat","material","evolution"],
              "properties":{
                "section_index":{"type":"integer"},
                "mode":{"type":"string","enum":["active","reduced","sustain","absent"]},
                "minimum_sounding_fraction":{"type":"number"},
                "entry_beat":{"type":"number","minimum":0,"multipleOf":0.25,"description":"AI-chosen earliest possible attack, in beats from this SECTION start. Not cycle origin or window-local. Use 0 for absent."},
                "release_beat":{"type":"number","minimum":0,"multipleOf":0.25,"description":"AI-chosen latest possible note release, in beats from this SECTION start. At most section bars times beats/bar. Use 0 for absent. Notes need not fill this span."},
                "material":{"type":"string"},"evolution":{"type":"string"}
              }}}
          }}}
      }})json");
    if (jointGrooveNucleusExperiment()) {
        auto* object = result.getDynamicObject();
        object->getProperty("required").getArray()->add("groove_interlock");
        object->getProperty("properties").getDynamicObject()->setProperty("groove_interlock",
            juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
              "required":["bass_owner_id","chord_owner_id","foreground_owner_id",
                "shared_landings","bass_chord_relationship","foreground_sentence","breathing_space"],
              "properties":{
                "bass_owner_id":{"type":"string"},"chord_owner_id":{"type":"string"},
                "foreground_owner_id":{"type":"string"},
                "shared_landings":{"type":"array","minItems":1,"maxItems":8,
                  "items":{"type":"number","minimum":0,"multipleOf":0.25}},
                "bass_chord_relationship":{"type":"string"},
                "foreground_sentence":{"type":"string"},
                "breathing_space":{"type":"string"}
              }})json"));
    }
    if (explicitSection(direction))
        result.getProperty("properties", {}).getProperty("format", {}).getDynamicObject()->setProperty(
            "enum", juce::Array<juce::var>{"continuous_section"});
    if (plan != nullptr && !plan->instruments.empty() && !plan->sections.empty()) {
        const auto properties = result.getProperty("properties", {});
        const auto layerTemplate = properties.getProperty("layers", {}).getProperty("items", {});
        juce::Array<juce::var> layerChoices;
        for (const auto& instrument : plan->instruments) {
            auto layer = juce::JSON::parse(juce::JSON::toString(layerTemplate));
            const auto layerProperties = layer.getProperty("properties", {});
            layerProperties.getProperty("instrument_id", {}).getDynamicObject()->setProperty(
                "enum", juce::Array<juce::var>{juce::String::fromUTF8(instrument.id.c_str())});
            const auto stateTemplate = layerProperties.getProperty("states", {}).getProperty("items", {});
            juce::Array<juce::var> stateChoices;
            for (std::size_t sectionIndex = 0; sectionIndex < plan->sections.size(); ++sectionIndex) {
                const auto permitted = instrument.activeSections.empty() ||
                    std::find(instrument.activeSections.begin(), instrument.activeSections.end(),
                              plan->sections[sectionIndex].name) != instrument.activeSections.end();
                auto absent = juce::JSON::parse(juce::JSON::toString(stateTemplate));
                const auto absentProperties = absent.getProperty("properties", {});
                absentProperties.getProperty("section_index", {}).getDynamicObject()->setProperty(
                    "enum", juce::Array<juce::var>{static_cast<int>(sectionIndex)});
                absentProperties.getProperty("mode", {}).getDynamicObject()->setProperty(
                    "enum", juce::Array<juce::var>{"absent"});
                for (const auto* key : {"minimum_sounding_fraction", "entry_beat", "release_beat"})
                    absentProperties.getProperty(key, {}).getDynamicObject()->setProperty(
                        "enum", juce::Array<juce::var>{0});
                if (!permitted) {
                    stateChoices.add(absent);
                    continue;
                }
                auto sounding = juce::JSON::parse(juce::JSON::toString(stateTemplate));
                const auto soundingProperties = sounding.getProperty("properties", {});
                const auto sectionBeats = plan->sections[sectionIndex].bars * plan->beatsPerBar;
                soundingProperties.getProperty("section_index", {}).getDynamicObject()->setProperty(
                    "enum", juce::Array<juce::var>{static_cast<int>(sectionIndex)});
                soundingProperties.getProperty("mode", {}).getDynamicObject()->setProperty(
                    "enum", juce::Array<juce::var>{"active", "reduced", "sustain"});
                for (const auto* key : {"entry_beat", "release_beat"})
                    soundingProperties.getProperty(key, {}).getDynamicObject()->setProperty(
                        "maximum", sectionBeats);
                juce::var stateChoice(new juce::DynamicObject());
                stateChoice.getDynamicObject()->setProperty("anyOf",
                    juce::Array<juce::var>{absent, sounding});
                stateChoices.add(stateChoice);
            }
            juce::var stateItems(new juce::DynamicObject());
            stateItems.getDynamicObject()->setProperty("anyOf", stateChoices);
            layerProperties.getProperty("states", {}).getDynamicObject()->setProperty(
                "items", stateItems);
            layerChoices.add(layer);
        }
        juce::var layerItems(new juce::DynamicObject());
        layerItems.getDynamicObject()->setProperty("anyOf", layerChoices);
        properties.getProperty("layers", {}).getDynamicObject()->setProperty("items", layerItems);

        const auto trajectoryTemplate = properties.getProperty("energy_trajectory", {}).getProperty("items", {});
        juce::Array<juce::var> trajectoryChoices;
        for (std::size_t sectionIndex = 0; sectionIndex < plan->sections.size(); ++sectionIndex) {
            auto moment = juce::JSON::parse(juce::JSON::toString(trajectoryTemplate));
            moment.getProperty("properties", {}).getProperty("section_index", {})
                .getDynamicObject()->setProperty("enum",
                    juce::Array<juce::var>{static_cast<int>(sectionIndex)});
            trajectoryChoices.add(moment);
        }
        juce::var trajectoryItems(new juce::DynamicObject());
        trajectoryItems.getDynamicObject()->setProperty("anyOf", trajectoryChoices);
        properties.getProperty("energy_trajectory", {}).getDynamicObject()->setProperty(
            "items", trajectoryItems);
    }
    nucleus::extendSchema(result, plan);
    return juce::JSON::toString(result, true);
}

inline juce::String macroSchema(const juce::String& json, CompositionBehavior behavior,
    const juce::String& direction, int bars) {
    if (behavior != CompositionBehavior::Club) return json;
    auto root = juce::JSON::parse(json);
    const auto properties = root.getProperty("properties", {});
    const auto sections = properties.getProperty("sections", {});
    const auto duration = sections.getProperty("items", {}).getProperty("properties", {}).getProperty("bars", {});
    duration.getDynamicObject()->setProperty("minimum", 1);
    const auto acts = properties.getProperty("narrative_spine", {}).getProperty("properties", {}).getProperty("acts", {});
    if (explicitSection(direction)) {
        sections.getDynamicObject()->setProperty("minItems", 1);
        sections.getDynamicObject()->setProperty("maxItems", 1);
        duration.getDynamicObject()->setProperty("enum", juce::Array<juce::var>{bars});
        acts.getDynamicObject()->setProperty("minItems", 1);
        acts.getDynamicObject()->setProperty("maxItems", 1);
    }
    return juce::JSON::toString(root, true);
}

inline juce::String prompt(const SongPlan& plan, const juce::String& direction) {
    // This phase cannot force fictional sections into an explicitly single loop.
    juce::String out = brief();
    out << "\nDesign a CLUB PRODUCTION PLAN, not a dialogue score. No speaker, answerer, "
        "dramatic question or mandatory solo is requested. Every cast owner is a production "
        "layer. Decide its cyclic material, harmonic responsibility, articulation, register "
        "and relationship to the WHOLE texture. The focus may be a bass, chord rhythm, "
        "sequence, texture or melody; focus_owner_id may be empty. Use continuous_section "
        "for an explicitly requested loop/full production section: keep a sustainable full "
        "body and a musical wrap, without squeezing intro/break/drop/outro into it. Use "
        "full_arrangement for an entire song. Do not confuse few notes with elegant economy. "
        "A requested pad/chord bed needs actual sustained polyphonic notes; stabs are a "
        "different rhythmic chord layer and cannot replace it. Several complementary "
        "recurrence layers can coexist without each becoming a solo. Textures need their "
        "own pitched material, not an empty effects label.\n"
        "For a full_arrangement, energy_trajectory must contain exactly one entry for every "
        "section. Design one patient producer-scale journey rather than exposing the finished "
        "body immediately. dramatic_function is freely named musical intent: opening, "
        "establishment, accumulation, arrival, withdrawal, return and release are examples, "
        "not a required fixed order. The opening must reserve some recognizable material for a "
        "later consequential arrival. A withdrawal must retain audible identity, and a return "
        "must change the meaning of something already heard. Energy and density are independent: "
        "tension may rise while notes disappear. Choose micro-layer additions/removals when they "
        "serve the music; 8 or 16 bars are useful possibilities, never an automatic schedule. "
        "Do not give every section effectively the same energy, density and layer occupancy. "
        "For continuous_section, describe one sustainable state and its musical wrap instead.\n"
        "Plan musical_arcs in ABSOLUTE zero-based bars: an actual subject, its continuation, "
        "perceptible arrival and the response of the supporting fabric. These span technical "
        "windows and may overlap; choose a few meaningful spans, not one arc for every track. "
        "Do not restart a new story every eight bars. For electronic "
        "music, the subject may be a developed sequence or melodic figure, not chamber dialogue. "
        "Describe concrete pitch contour, register trajectory, rhythmic identity and harmonic "
        "destination, not only energy adjectives. A repeatable section can still carry a complete "
        "musical thought without a miniature intro/break/outro. Preserve and transform enough "
        "material for the development to be heard; a tiny cue repeated unchanged is not enough. "
        "Choose groove_core_ids: the few owners that jointly make the dependable bass/chord/sequence "
        "backbone. Usually bass and chord pulse are sufficient metric anchors. Do NOT make "
        "a foreground arpeggio/melody a sparse rigid clock merely to qualify as a core owner; "
        "let it develop as a supporting/foreground phrase unless its exact repeating attacks "
        "are genuinely essential. The core is written FIRST, together; the rest hears its actual MIDI. "
        "Each layer chooses timing_mode: cycle_bound for any exact cyclic attack commitment "
        "(including sustained support), phrase_flexible for a genuinely developing phrase. "
        "Core owners are always cycle_bound. rhythm_cell and cycle_origin_beat are binding "
        "possible onsets for cycle_bound owners; omitted attacks are allowed, extra offsets are not. "
        "Choose the actual positions yourself. For phrase_flexible owners the cell describes "
        "identity rather than a compulsory repeated onset list. timing_intent must explain how "
        "this layer supports the meter and phrase arrivals. Soft sound envelopes do NOT require "
        "a late MIDI onset. Avoid assigning every plane a different offbeat to keep them apart: "
        "sustained support may share the harmonic arrival; separation can come from register, "
        "voicing, dynamics and phrasing. Preserve intentional syncopation, but ensure a foreground "
        "phrase can settle on perceptible metric arrivals rather than endlessly evade them. "
        "For these core owners, rhythm_cell is a binding set of possible attacks, not a vague suggestion: "
        "retain that exact absolute phase across ALL sections. You may omit attacks to breathe, "
        "change pitches/voicings/gates and withdraw a layer, but never shift its remaining attacks "
        "by a sixteenth or eighth when a new section/window begins. Choose a simple persuasive "
        "core that works on its own; elaborate decoration is not the metric foundation. "
        "Do not use unrelated cycle lengths in the core simply to sound sophisticated. "
        "For every instrument include exactly one state for EVERY section, absent when "
        "withdrawn. For each state choose entry_beat and release_beat in SECTION-LOCAL "
        "beats, inside its duration. These are binding limits for this layer's activity, "
        "not instructions to fill the interval. Choose 0/0 for absent; otherwise release "
        "must exceed entry. Preserve the absolute rhythmic phase inside the chosen span. "
        "For full_arrangement, explicitly organize discovery in the opening and the "
        "order of completed gestures in the ending using these times and absent states. "
        "Do not default every owner to every beat of every section. Explain the audible "
        "handoff in evolution: what the listener has just heard, what remains to be said, "
        "and who finishes it. Reserve foreground absence/recall where meaningful. Keep "
        "the central full sections rich; an intro/outro is not a request to thin the whole "
        "work. In continuous_section keep the requested repeatable body. "
        "CAST allowed_section_indices are the exact zero-based permissions "
        "already chosen by the AI cast: outside that list the state MUST be absent "
        "with minimum_sounding_fraction=0. Within the list you may still withdraw a "
        "layer. Do not infer extra permissions from a role label. production_role is "
        "free text. Choose cycle_beats and zero-based "
        "cycle_origin_beat in absolute song beats, and rhythm_cell onset offsets within "
        "that cycle. This is YOUR rhythmic grammar, not a supplied pattern. A sustained "
        "body may have only one onset per long cycle. Different periods are allowed with "
        "a clear common metric reference. minimum_sounding_fraction is YOUR conservative "
        "activity estimate for sounding-time coverage, not a hard musical quota; "
        "it is measured for review, never used to force extra notes or longer gates. "
        "Choose zero for intentionally episodic material. For absent use zero. "
        "polyphonic=true promises actual simultaneous pitches where the layer speaks. "
        "Use feasible MIDI registers. No full note arrays yet; the later AI authors every "
        "pitch, duration, velocity and repetition. Do not reserve musical gaps for code "
        "to fill.\nUSER: " << direction << "\nWORK: " << plan.totalBars << " bars; "
        << plan.beatsPerBar << " beats/bar; " << plan.bpm << " BPM; "
        << juce::String::fromUTF8(plan.key.c_str()) << "\nCAST:\n";
    if (foregroundGrooveExperiment() && !plan.continuousProductionSection)
        out << "\nLISTENING-BACKED CLUB A/B: The previous complete MIDI had a bass that "
            "played every slot of its cycle through seventeen repetitions, while "
            "the named focus was an arpeggiated sequence and the lead rarely became "
            "a memorable independent sentence. Plan a NEW interpretation of this "
            "same brief. Keep the durable pulse, but make groove emerge from a "
            "deliberate relationship among bass, chord rhythm, sequence and rests: "
            "the cycle is a vocabulary of possible attacks, not a command to play "
            "every slot each time. The AI should decide where a bass anticipation "
            "drops out, a chord accent answers it, or the sequence yields the floor. "
            "Do not create random variation or abandon the recurring identity. "
            "Give the listener a foreground voice capable of a distinctive, "
            "breathing melodic sentence that can return changed; do not make its "
            "principal idea just another continuous arpeggio. Choose its actual "
            "owner from the cast and make focus_owner_id point to that voice when "
            "the full song promises a tonal subject. The supporting sequence may "
            "stay hypnotic, but should not substitute for the foreground statement. "
            "These are role and phrase relationships, not prescribed MIDI notes, "
            "rhythms, quotas or a fixed song form.\n";
    if (jointGrooveNucleusExperiment())
        out << "\nJOINT GROOVE NUCLEUS EXPERIMENT: Design bass and chord pulse "
            "as ONE interlocking phrase before individual layers. Fill groove_interlock "
            "with their exact owner ids, the foreground melodic owner, and AI-chosen "
            "absolute beat offsets in their first common cycle where bass and chord "
            "both land. Those landings must occur in BOTH declared rhythm_cells. "
            "Explain which bass attacks invite chord answers, where the chord leaves "
            "room for bass, what complete melodic sentence sits above them, and where "
            "that sentence breathes. The foreground is a musical thought with a "
            "beginning and consequence, not a third arpeggio. Every onset and pitch "
            "remains your own choice. Do not maximize note count or cycle diversity. "
            "A common pulse may be spare, but its parts must make sense together.\n";
    if (nucleus::enabled()) {
        // Use a dedicated contract rather than stacking another instruction on
        // the legacy dialogue/omission brief. Shared schema semantics stay exact.
        out = brief() + nucleus::planningBrief() + nucleus::closurePlanningBrief();
        out << "\nOUTPUT CONTRACT: one layer per cast owner, one state per section for each layer, "
            "one energy_trajectory entry per section. Use full_arrangement for a song and "
            "continuous_section for a requested loop. Keep sustained pads and a recurrent "
            "sequencer distinct from short chord stabs and a spacious melodic hook. "
            "groove_core_ids names the repeating metric anchors. Core owners must be "
            "cycle_bound: choose cycle_beats, cycle_origin_beat and unique rhythm_cell "
            "offsets >=0 and <cycle_beats yourself, matching their nucleus. Other genuinely "
            "fixed sequences may also be cycle_bound. Use phrase_flexible for harmonic beds "
            "and free melodic phrases; their rhythm_cell is descriptive, not an onset ban. "
            "For a sounding state choose entry_beat >=0 and release_beat >entry_beat within "
            "THIS SECTION's beat range. Outside allowed_section_indices use absent with "
            "entry_beat=release_beat=minimum_sounding_fraction=0. The fraction is a conservative "
            "sounding-time estimate, not a note quota. Preserve rich full-body spans; assign "
            "absence where the arrangement benefits, not to every layer at every phrase. "
            "polyphonic=true means actual simultaneous pitches. State material/evolution "
            "names what remains from the nucleus and what changes over the long phrase. "
            "musical_arcs use absolute zero-based bars within the work; these describe "
            "long developments, not an obligation to tell a new story every window. "
            "If groove_interlock is requested: bass_owner_id and chord_owner_id must be "
            "distinct core owners; foreground_owner_id must be a third actual owner equal "
            "to focus_owner_id. shared_landings are absolute beat positions permitted "
            "by BOTH core cycles, or [] if their complementary attacks never coincide. "
            "Explain their actual rhythmic relationship; interlock does not require a unison attack. "
            "All IDs must exist. All timings refer to quarter-note beats, NOT seconds. "
            "\nUSER: " << direction << "\nWORK: " << plan.totalBars << " bars; "
            << plan.beatsPerBar << " beats/bar; " << plan.bpm << " BPM; "
            << juce::String::fromUTF8(plan.key.c_str()) << "\nCHORD PALETTE:\n";
        for (const auto& chord : plan.chordPalette) {
            out << juce::String(chord.id) << " pitch_classes=";
            for (const auto pc : chord.pitchClasses) out << pc << " ";
            out << "bass_pc=" << chord.bassPitchClass << "\n";
        }
        out << "CAST:\n";
    }
    for (const auto& part : plan.instruments) {
        juce::Array<juce::var> allowed;
        for (std::size_t index = 0; index < plan.sections.size(); ++index)
            if (part.activeSections.empty() || std::find(part.activeSections.begin(),
                    part.activeSections.end(), plan.sections[index].name) != part.activeSections.end())
                allowed.add(static_cast<int>(index));
        out << juce::String::fromUTF8(part.id.c_str()) << " | "
            << juce::String::fromUTF8(part.role.c_str()) << " | MIDI "
            << part.minimumPitch << ".." << part.maximumPitch
            << " | allowed_section_indices=" << juce::JSON::toString(juce::var(allowed), true) << "\n";
    }
    out << "SECTIONS:\n";
    for (std::size_t s = 0; s < plan.sections.size(); ++s) {
        const auto& section = plan.sections[s];
        out << static_cast<int>(s) << " | " << juce::String::fromUTF8(section.name.c_str())
            << " | absolute bars " << section.startBar << ".." << section.startBar + section.bars
            << " | SECTION-LOCAL entry/release range 0.."
            << juce::String(section.bars * plan.beatsPerBar, 2) << " beats\n";
        for (const auto& event : section.harmonicEvents)
            out << " chord " << juce::String::fromUTF8(event.chordId.c_str())
                << " local bar " << event.barOffset << " beat " << event.beatOffset << "\n";
    }
    return out;
}

inline bool grid(const juce::var& v, double& n) {
    if (!(v.isInt() || v.isInt64() || v.isDouble())) return false;
    n = static_cast<double>(v);
    return std::isfinite(n) && std::abs(n * 4 - std::round(n * 4)) < .001;
}

inline bool validate(const juce::String& json, const SongPlan& plan, juce::String& error) {
    error.clear();
    const auto root = juce::JSON::parse(json);
    const auto* layers = root.getProperty("layers", {}).getArray();
    const auto format = root.getProperty("format", {}).toString();
    if (!isPlan(json) || !layers || layers->size() != static_cast<int>(plan.instruments.size()) ||
        (format != "continuous_section" && format != "full_arrangement") || plan.sections.empty()) {
        error = "Club production plan must cover the exact cast and choose a valid format"; return false;
    }
    std::set<std::string> ids;
    for (const auto& layer : *layers) {
        const auto id = layer.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& p) { return p.id == id; });
        double cycle{}, origin{}, low{}, high{};
        if (owner == plan.instruments.end() || !ids.insert(id).second ||
            !grid(layer.getProperty("cycle_beats", {}), cycle) || cycle <= 0 || cycle > 512 ||
            !grid(layer.getProperty("cycle_origin_beat", {}), origin) || origin < 0 ||
            !grid(layer.getProperty("register_low", {}), low) ||
            !grid(layer.getProperty("register_high", {}), high) || low != std::floor(low) ||
            high != std::floor(high) || low > high || low < std::max(0, owner->minimumPitch - 12) ||
            high > std::min(127, owner->maximumPitch + 12)) {
            error = "Club layer has an unknown/duplicate owner or invalid cycle/register: " + juce::String(id); return false;
        }
        for (const auto* key : {"production_role", "harmonic_job", "articulation", "interaction"})
            if (layer.getProperty(key, {}).toString().trim().isEmpty()) {
                error = "Club layer omits " + juce::String(key) + ": " + juce::String(id); return false;
            }
        const auto* rhythm = layer.getProperty("rhythm_cell", {}).getArray();
        const auto timing = layer.getProperty("timing_mode", {}).toString();
        if (layer.hasProperty("timing_mode") &&
            ((timing != "cycle_bound" && timing != "phrase_flexible") ||
             layer.getProperty("timing_intent", {}).toString().trim().isEmpty() ||
             (coreLayer(json, id) && timing != "cycle_bound"))) {
            error = "Club layer has an invalid timing commitment: " + juce::String(id); return false;
        }
        std::set<double> onsets;
        if (!rhythm || rhythm->size() > 128) { error = "Club rhythm_cell missing"; return false; }
        for (const auto& beat : *rhythm) {
            double value{};
            if (!grid(beat, value) || value < 0 || value >= cycle || !onsets.insert(value).second) {
                error = "Club rhythm offset must be unique, zero-based and inside its cycle: " + juce::String(id); return false;
            }
        }
        if (timing == "cycle_bound" && rhythm->isEmpty()) {
            error = "Club cycle-bound layer needs its AI-authored possible attacks: " + juce::String(id); return false;
        }
        const auto* states = layer.getProperty("states", {}).getArray();
        std::set<int> covered;
        if (!states || states->size() != static_cast<int>(plan.sections.size())) {
            error = "Club layer must declare every section state: " + juce::String(id); return false;
        }
        for (const auto& state : *states) {
            double sectionNumber{};
            const auto fractionValue = state.getProperty("minimum_sounding_fraction", {});
            const auto fraction = static_cast<double>(fractionValue);
            const auto mode = state.getProperty("mode", {}).toString();
            if (!grid(state.getProperty("section_index", {}), sectionNumber) || sectionNumber != std::floor(sectionNumber) ||
                sectionNumber < 0 || sectionNumber >= static_cast<double>(plan.sections.size()) ||
                !covered.insert(static_cast<int>(sectionNumber)).second ||
                !(fractionValue.isInt() || fractionValue.isInt64() || fractionValue.isDouble()) ||
                !std::isfinite(fraction) || fraction < 0 || fraction > 1 ||
                (mode != "active" && mode != "reduced" && mode != "sustain" && mode != "absent") ||
                (mode == "absent" && fraction != 0)) {
                error = "Club layer has invalid or duplicate section state: " + juce::String(id); return false;
            }
            const auto& section = plan.sections[static_cast<std::size_t>(sectionNumber)];
            if (state.hasProperty("entry_beat") || state.hasProperty("release_beat")) {
                double entry{}, release{};
                const auto entryValue = state.getProperty("entry_beat", {});
                const auto releaseValue = state.getProperty("release_beat", {});
                const auto sectionBeats = section.bars * plan.beatsPerBar;
                const auto entryOnGrid = grid(entryValue, entry);
                const auto releaseOnGrid = grid(releaseValue, release);
                if (!entryOnGrid || !releaseOnGrid || entry < 0 || entry > sectionBeats ||
                    release < 0 || release > sectionBeats ||
                    (mode == "absent" ? (entry != 0 || release != 0) : release <= entry)) {
                    error = "Club layer has invalid section-local entry/release: " + juce::String(id) +
                        " | section_index=" + juce::String(static_cast<int>(sectionNumber)) +
                        " | section=" + juce::String::fromUTF8(section.name.c_str()) +
                        " | mode=" + mode + " | entry_beat=" + entryValue.toString() +
                        " | release_beat=" + releaseValue.toString() +
                        " | allowed range=0.." + juce::String(sectionBeats, 2) +
                        " section-local beats, on quarter-beat grid" +
                        (mode == "absent" ? "; absent requires 0/0" :
                            "; sounding requires release_beat > entry_beat");
                    return false;
                }
            }
            if (mode != "absent" && !owner->activeSections.empty() &&
                std::find(owner->activeSections.begin(), owner->activeSections.end(), section.name) == owner->activeSections.end()) {
                error = "Club layer activated outside its cast permission: " + juce::String(id) +
                    " | section_index=" + juce::String(static_cast<int>(sectionNumber)) +
                    " | section=" + juce::String::fromUTF8(section.name.c_str()) +
                    " | use absent with minimum_sounding_fraction=0";
                return false;
            }
        }
    }
    const auto focus = root.getProperty("focus_owner_id", {}).toString().toStdString();
    if (!focus.empty() && !ids.contains(focus)) { error = "Club focus is not a cast owner"; return false; }
    if (root.hasProperty("groove_interlock")) {
        const auto link = root.getProperty("groove_interlock", {});
        const auto bass = link.getProperty("bass_owner_id", {}).toString().toStdString();
        const auto chord = link.getProperty("chord_owner_id", {}).toString().toStdString();
        const auto foreground = link.getProperty("foreground_owner_id", {}).toString().toStdString();
        const auto* landings = link.getProperty("shared_landings", {}).getArray();
        if (!ids.contains(bass) || !ids.contains(chord) || !ids.contains(foreground) ||
            bass == chord || bass == foreground || chord == foreground ||
            !coreLayer(json, bass) || !coreLayer(json, chord) ||
            focus != foreground || !landings || (landings->isEmpty() && !root.hasProperty("tonal_nucleus"))) {
            error = "Club groove interlock must name two distinct core owners and the actual foreground focus";
            return false;
        }
        for (const auto* field : {"bass_chord_relationship", "foreground_sentence", "breathing_space"})
            if (link.getProperty(field, {}).toString().trim().isEmpty()) {
                error = "Club groove interlock omits its musical relationship"; return false;
            }
        for (const auto& landing : *landings) {
            double beat{};
            if (!grid(landing, beat) || beat < 0 ||
                !coreAttackAllowed(json, bass, beat) || !coreAttackAllowed(json, chord, beat)) {
                error = "Club groove interlock names a landing not shared by bass and chord cells";
                return false;
            }
        }
    }
    // New plans make the producer-scale journey explicit. Historical checkpoints
    // remain resumable: they predate this field and are validated by their original
    // layer/arc contract rather than being silently assigned a new trajectory.
    if (root.hasProperty("energy_trajectory")) {
        const auto* trajectory = root.getProperty("energy_trajectory", {}).getArray();
        if (!trajectory || trajectory->size() != static_cast<int>(plan.sections.size())) {
            error = "Club energy trajectory must describe every section exactly once"; return false;
        }
        std::set<int> covered;
        std::set<std::string> functions;
        std::set<int> energyShapes;
        std::set<int> densityShapes;
        for (const auto& moment : *trajectory) {
            double sectionNumber{};
            const auto energyValue = moment.getProperty("energy_intent", {});
            const auto densityValue = moment.getProperty("density_intent", {});
            const auto energy = static_cast<double>(energyValue);
            const auto density = static_cast<double>(densityValue);
            if (!grid(moment.getProperty("section_index", {}), sectionNumber) ||
                sectionNumber != std::floor(sectionNumber) || sectionNumber < 0 ||
                sectionNumber >= static_cast<double>(plan.sections.size()) ||
                !covered.insert(static_cast<int>(sectionNumber)).second ||
                !(energyValue.isInt() || energyValue.isInt64() || energyValue.isDouble()) ||
                !(densityValue.isInt() || densityValue.isInt64() || densityValue.isDouble()) ||
                !std::isfinite(energy) || !std::isfinite(density) ||
                energy < 0 || energy > 1 || density < 0 || density > 1) {
                error = "Club energy trajectory has an invalid section or intent"; return false;
            }
            for (const auto* key : {"dramatic_function", "identity_retained", "material_reserved",
                                    "layer_change", "transition_consequence"})
                if (moment.getProperty(key, {}).toString().trim().isEmpty()) {
                    error = "Club energy trajectory omits " + juce::String(key); return false;
                }
            functions.insert(moment.getProperty("dramatic_function", {}).toString().toLowerCase().toStdString());
            energyShapes.insert(static_cast<int>(std::round(energy * 100)));
            densityShapes.insert(static_cast<int>(std::round(density * 100)));
        }
        if (format == "full_arrangement" && plan.sections.size() >= 3 &&
            (functions.size() < 2 || (energyShapes.size() < 2 && densityShapes.size() < 2))) {
            error = "Club full arrangement needs an AI-authored contrast trajectory, not one static state";
            return false;
        }
    }
    if (root.hasProperty("musical_arcs")) {
        const auto* arcs = root.getProperty("musical_arcs", {}).getArray();
        if (!arcs || arcs->isEmpty() || arcs->size() > 12) { error = "Club musical arcs are missing"; return false; }
        for (const auto& arc : *arcs) {
            double start{}, bars{};
            if (!grid(arc.getProperty("start_bar", {}), start) || start != std::floor(start) || start < 0 ||
                !grid(arc.getProperty("bars", {}), bars) || bars != std::floor(bars) || bars < 1 ||
                start + bars > plan.totalBars ||
                !ids.contains(arc.getProperty("foreground_id", {}).toString().toStdString())) {
                error = "Club musical arc has an invalid span or foreground owner"; return false;
            }
            for (const auto* key : {"subject", "continuation", "arrival", "support_response"})
                if (arc.getProperty(key, {}).toString().trim().isEmpty()) {
                    error = "Club musical arc omits its " + juce::String(key); return false;
                }
            if (!nucleus::validateMelodicStatement(arc, plan, error)) return false;
        }
    }
    if (root.hasProperty("groove_core_ids")) {
        const auto* core = root.getProperty("groove_core_ids", {}).getArray();
        std::set<std::string> unique;
        if (!core || core->isEmpty() || core->size() > 4) {
            error = "Club core must identify one to four existing layers"; return false;
        }
        for (const auto& value : *core) {
            const auto id = value.toString().toStdString();
            if (!ids.contains(id) || !unique.insert(id).second) {
                error = "Club core has an unknown or duplicate owner"; return false;
            }
            for (const auto& layer : *layers) if (layer.getProperty("instrument_id", {}).toString() == value.toString() &&
                layer.getProperty("rhythm_cell", {}).size() == 0) {
                error = "Club core needs an AI-authored rhythm cell"; return false;
            }
        }
    }
    return nucleus::validate(root, plan, error);
}

inline juce::String windowBrief(const juce::String& json, std::size_t sectionIndex) {
    const auto root = juce::JSON::parse(json);
    juce::String out;
    out << "\nAI CLUB PRODUCTION PLAN: format=" << root.getProperty("format", {}).toString()
        << "; identity=" << root.getProperty("identity", {}).toString()
        << "; pulse=" << root.getProperty("shared_pulse", {}).toString()
        << "; focus=" << root.getProperty("focus_owner_id", {}).toString() << "\n";
    if (root.hasProperty("groove_core_ids"))
        out << "GROOVE CORE OWNERS: " << juce::JSON::toString(root.getProperty("groove_core_ids", {}))
            << (root.hasProperty("tonal_nucleus")
                ? ". Preserve their declared attack grammar and absolute phase. A continuous meter carrier keeps its subdivision in continuing active states; omissions belong to explicit musical withdrawals, not arbitrary writing-window changes. Other lanes may breathe according to their own state.\n"
                : ". Their attack grammar/absolute phase is fixed by YOUR plan. Omit attacks when needed, but do not add unplanned offsets. Supporting layers must leave space for this real core.\n");
    if (const auto* trajectory = root.getProperty("energy_trajectory", {}).getArray()) {
        for (int offset = -1; offset <= 1; ++offset) {
            const auto index = static_cast<int>(sectionIndex) + offset;
            if (index < 0 || index >= trajectory->size()) continue;
            out << (offset < 0 ? "PREVIOUS ENERGY STATE: " :
                    offset > 0 ? "NEXT ENERGY STATE: " : "CURRENT ENERGY STATE: ")
                << juce::JSON::toString((*trajectory)[index], true) << "\n";
        }
        out << "Realize this transition through the planned layers and actual MIDI. Preserve the "
            "shared groove identity, but do not pre-reveal material reserved for a later state.\n";
    }
    out << "TIMING REALIZATION: cycle_bound applies to every marked owner, including pads/textures. "
        "Use only its declared absolute cycle attacks; softness is an envelope, not a MIDI delay. "
        "For phrase_flexible voices write connected gestures and intelligible arrivals against "
        "the accepted ensemble. Do not stagger every layer by a sixteenth or make each one "
        "a competing syncopated solo. Sharing an arrival is allowed. Do not blanket-straighten "
        "the music: retain syncopation when it contributes to the shared phrase.\n";
    if (root.hasProperty("groove_interlock"))
        out << "AI-AUTHORED JOINT GROOVE NUCLEUS: "
            << juce::JSON::toString(root.getProperty("groove_interlock", {}), true)
            << "\nHear bass and chord as one phrase with shared landings and answering "
               "space. Their listed rhythm_cells are possibilities, not a demand to "
               "play every slot. Let the foreground sentence speak over that joint "
               "pulse; leave its declared breathing space audible. Do not turn every "
               "voice into a competing arpeggio.\n";
    if (foregroundGrooveExperiment())
        out << "FOREGROUND/GROOVE A/B: Treat the recurring bass and chord cell as "
            "a shared language, not a full grid to exhaust. Author purposeful "
            "omissions, note lengths, accents and harmonic responses in this "
            "section while retaining an audible common pulse. If the sequence "
            "is active, let it support or answer the foreground rather than "
            "doubling a stream of equal notes. The focus voice needs connected "
            "phrases, a perceptible arrival and silence that makes its return "
            "meaningful. Decide every MIDI event yourself.\n";
    if (root.getProperty("format", {}).toString() == "continuous_section")
        out << "This is a FULL PRODUCTION SECTION, not a miniature complete song. Keep the "
            "body and groove through its wrap. Do not impose a breakdown, final solo cadence "
            "or fade merely because the requested excerpt ends.\n";
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers) {
        auto stateForSection = juce::var{};
        if (const auto* states = layer.getProperty("states", {}).getArray())
            for (const auto& state : *states)
                if (static_cast<int>(state.getProperty("section_index", -1)) == static_cast<int>(sectionIndex)) stateForSection = state;
        auto copy = juce::JSON::parse(juce::JSON::toString(layer));
        copy.getDynamicObject()->removeProperty("states");
        copy.getDynamicObject()->setProperty("current_state", stateForSection);
        out << juce::JSON::toString(copy, true) << "\n";
    }
    if (const auto* story = root.getProperty("grounded_story", {}).getDynamicObject()) {
        out << "SCORE-GROUNDED CLUB ARGUMENT: This AI reading of the accepted opening MIDI "
            "takes precedence over a contradictory pre-MIDI arc, without changing the "
            "fixed chord map, cast, layer states or core attack cycles. Opening="
            << story->getProperty("opening_evidence").toString()
            << "; related counteridea=" << story->getProperty("related_counteridea").toString()
            << "; conflict=" << story->getProperty("central_conflict").toString()
            << "; necessary turn=" << story->getProperty("necessary_turn").toString()
            << "; final consequence=" << story->getProperty("final_consequence").toString() << "\n";
    }
    if (const auto* links = root.getProperty("act_links", {}).getArray()) {
        out << "ACTUAL-MIDI GROUNDED SCENES: Let each later section inherit and audibly "
            "transform the established material. Do not restart a new tune at this "
            "technical window. These are AI-authored musical instructions, not note quotas.\n";
        for (int offset = -1; offset <= 1; ++offset) {
            const auto index = static_cast<int>(sectionIndex) + offset;
            if (index < 0 || index >= links->size()) continue;
            const auto& link = (*links)[index];
            out << (offset < 0 ? "previous " : offset > 0 ? "next " : "current ")
                << "section " << index << " carrier=" << link.getProperty("carrier_id", {}).toString()
                << " inherited=" << link.getProperty("inherited_question", {}).toString()
                << " transformation=" << link.getProperty("audible_transformation", {}).toString()
                << " consequence=" << link.getProperty("consequence", {}).toString()
                << " left_open=" << link.getProperty("left_open", {}).toString()
                << " opening_evidence=" << link.getProperty("score_evidence", {}).toString() << "\n";
        }
    }
    out << nucleus::writerBrief(root);
    return out;
}

inline juce::String developmentBrief(const juce::String& json, double begin, double end, double beatsPerBar) {
    const auto root = juce::JSON::parse(json);
    juce::String out;
    if (const auto* arcs = root.getProperty("musical_arcs", {}).getArray()) for (const auto& arc : *arcs) {
        const auto start = static_cast<double>(arc.getProperty("start_bar", 0)) * beatsPerBar;
        const auto stop = start + static_cast<double>(arc.getProperty("bars", 0)) * beatsPerBar;
        if (start < end && stop > begin) {
            out << "\nCONTINUING MUSICAL ARGUMENT (absolute beat " << start << ".." << stop << "): "
                << juce::JSON::toString(arc, true) << "\n";
            if (const auto* notes = arc.getProperty("melodic_statement", {}).getProperty("notes", {}).getArray()) {
                out << "FOREGROUND SENTENCE IN OUTPUT-WINDOW COORDINATES (negative onset means already sounding; "
                    "continue only its remaining sustain when compatible with the harmonic map):\n";
                for (const auto& n : *notes) {
                    const auto attack = start + static_cast<double>(n.getProperty("beat", 0));
                    const auto release = attack + static_cast<double>(n.getProperty("duration", 0));
                    if (attack < end && release > begin)
                        out << "owner=" << arc.getProperty("foreground_id", {}).toString()
                            << " onset=" << (attack - begin) << " release=" << (release - begin)
                            << " pitch=" << static_cast<int>(n.getProperty("pitch", 0))
                            << " velocity=" << static_cast<int>(n.getProperty("velocity", 0)) << "\n";
                }
                out << "These are AI-authored phrase anchors, not a cell to tile. Author the realization; "
                    "do not restart the sentence in each window or invent a new attack for an already-held tone.\n";
            }
            if (const auto* parts = arc.getProperty("melodic_statement", {}).getProperty("ensemble_parts", {}).getArray()) {
                for (const auto& part : *parts) if (const auto* notes = part.getProperty("notes", {}).getArray()) {
                    for (const auto& n : *notes) {
                        const auto attack = start + static_cast<double>(n.getProperty("beat", 0));
                        const auto release = attack + static_cast<double>(n.getProperty("duration", 0));
                        if (attack < end && release > begin)
                            out << "JOINT PHRASE owner=" << part.getProperty("instrument_id", {}).toString()
                                << " function=" << part.getProperty("musical_function", {}).toString()
                                << " onset=" << (attack - begin) << " release=" << (release - begin)
                                << " pitch=" << static_cast<int>(n.getProperty("pitch", 0))
                                << " velocity=" << static_cast<int>(n.getProperty("velocity", 0)) << "\n";
                    }
                }
            }
        }
    }
    return out;
}

inline bool absent(const SongPlan& plan, const std::string& id, std::size_t sectionIndex) {
    const auto root = juce::JSON::parse(juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (root.getProperty("kind", {}).toString() != "club_production_v1") return false;
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers)
        if (layer.getProperty("instrument_id", {}).toString().toStdString() == id)
            if (const auto* states = layer.getProperty("states", {}).getArray()) for (const auto& state : *states)
                if (static_cast<int>(state.getProperty("section_index", -1)) == static_cast<int>(sectionIndex))
                    return state.getProperty("mode", {}).toString() == "absent";
    return false;
}

// Read the AI's arrangement bounds. Legacy plans retain their full section span.
// This never trims notes: schema/validation ask the AI to author within its own plan.
inline std::pair<double, double> activitySpan(const SongPlan& plan, const std::string& id,
                                            std::size_t sectionIndex) {
    if (sectionIndex >= plan.sections.size()) return {0, 0};
    const auto& section = plan.sections[sectionIndex];
    const auto start = section.startBar * plan.beatsPerBar;
    const auto root = juce::JSON::parse(juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers)
        if (layer.getProperty("instrument_id", {}).toString().toStdString() == id)
            if (const auto* states = layer.getProperty("states", {}).getArray()) for (const auto& state : *states)
                if (static_cast<int>(state.getProperty("section_index", -1)) == static_cast<int>(sectionIndex)) {
                    if (state.getProperty("mode", {}).toString() == "absent") return {start, start};
                    return {start + static_cast<double>(state.getProperty("entry_beat", 0)),
                        start + static_cast<double>(state.getProperty("release_beat", section.bars * plan.beatsPerBar))};
                }
    return {start, start + section.bars * plan.beatsPerBar};
}

inline bool silentWindow(const SongPlan& plan, const std::vector<std::size_t>& owners,
                         std::size_t sectionIndex, int firstBar, int bars) {
    if (plan.compositionBehavior != CompositionBehavior::Club ||
        sectionIndex >= plan.sections.size() || owners.empty()) return false;
    const auto start = (plan.sections[sectionIndex].startBar + firstBar) * plan.beatsPerBar;
    const auto end = start + bars * plan.beatsPerBar;
    return std::all_of(owners.begin(), owners.end(), [&](const auto owner) {
        if (owner >= plan.instruments.size()) return false;
        const auto [entry, release] = activitySpan(plan, plan.instruments[owner].id, sectionIndex);
        return release <= start + .001 || entry >= end - .001;
    });
}

// Different release bounds create distinct large note schemas. Keep the groove
// core joint; batch support by its realized window deadline. Every later batch
// still receives the actual accepted ensemble. No musical plan is changed.
inline std::vector<std::vector<std::size_t>> windowGroups(const SongPlan& plan,
    const std::vector<std::vector<std::size_t>>& groups, std::size_t sectionIndex,
    int firstBar, int bars) {
    if (plan.compositionBehavior != CompositionBehavior::Club || sectionIndex >= plan.sections.size()) return groups;
    const auto json = juce::String::fromUTF8(plan.phraseArchitectureJson.c_str());
    const auto start = (plan.sections[sectionIndex].startBar + firstBar) * plan.beatsPerBar;
    const auto end = start + bars * plan.beatsPerBar;
    std::vector<std::vector<std::size_t>> result;
    for (const auto& group : groups) {
        if (std::any_of(group.begin(), group.end(), [&](const auto owner) {
            return owner >= plan.instruments.size() || coreLayer(json, plan.instruments[owner].id);
        })) { result.push_back(group); continue; }
        std::vector<double> releases;
        std::vector<std::vector<std::size_t>> batches;
        for (const auto owner : group) {
            const auto [entry, release] = activitySpan(plan, plan.instruments[owner].id, sectionIndex);
            const auto bound = std::clamp(release, start, end);
            const auto found = std::find(releases.begin(), releases.end(), bound);
            if (found == releases.end()) { releases.push_back(bound); batches.push_back({owner}); }
            else batches[static_cast<std::size_t>(std::distance(releases.begin(), found))].push_back(owner);
        }
        result.insert(result.end(), batches.begin(), batches.end());
    }
    return result;
}

// Read-only evidence against AI-chosen promises. No note is filled or retimed.
inline std::vector<std::string> missingLayers(const juce::String& json, const SongPlan& plan,
    const Pattern& midi, std::size_t sectionIndex, double begin, double end,
    const std::vector<std::size_t>& requested, juce::String& evidence) {
    std::vector<std::string> missing;
    const auto root = juce::JSON::parse(json);
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers) {
        const auto id = layer.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
        if (owner == plan.instruments.end()) continue;
        const auto index = static_cast<std::size_t>(std::distance(plan.instruments.begin(), owner));
        if (std::find(requested.begin(), requested.end(), index) == requested.end()) continue;
        if (const auto* states = layer.getProperty("states", {}).getArray()) for (const auto& state : *states) {
            if (static_cast<int>(state.getProperty("section_index", -1)) != static_cast<int>(sectionIndex)) continue;
            const auto mode = state.getProperty("mode", {}).toString();
            std::vector<std::pair<double, double>> spans;
            for (const auto& note : midi.notes) if (note.partId == index + 1 && note.startBeat < end && note.endBeat() > begin)
                spans.emplace_back(std::max(begin, note.startBeat), std::min(end, note.endBeat()));
            std::sort(spans.begin(), spans.end());
            double covered = 0, lastEnd = begin;
            bool polyphonic = false;
            for (const auto& [a, b] : spans) {
                if (a < lastEnd - .001) polyphonic = true;
                covered += std::max(0.0, b - std::max(a, lastEnd)); lastEnd = std::max(lastEnd, b);
            }
            const auto cycle = static_cast<double>(layer.getProperty("cycle_beats", 0));
            const auto required = static_cast<double>(state.getProperty("minimum_sounding_fraction", 0));
            const auto [entry, release] = activitySpan(plan, id, sectionIndex);
            const auto available = std::max(0.0, std::min(end, release) - std::max(begin, entry));
            const auto completeCycle = available >= cycle - .001;
            // A density estimate is not a musical correctness threshold. Only
            // a completely omitted promised layer is due for automatic recovery;
            // partial coverage goes to read-only evidence and musical listening.
            const auto needsCoverage = mode != "absent" && completeCycle &&
                required > 0 && spans.empty();
            const auto needsPolyphony = mode != "absent" && completeCycle && !spans.empty() &&
                static_cast<bool>(layer.getProperty("polyphonic", false)) && !polyphonic;
            if (needsCoverage || needsPolyphony || (mode == "absent" && !spans.empty())) {
                missing.push_back(id);
                evidence << juce::String(id) << " mode=" << mode << " actual sounding fraction="
                    << juce::String(covered / (end - begin), 3) << " AI-declared minimum=" << required
                    << " polyphonic=" << (polyphonic ? "yes" : "no") << "; ";
            }
        }
    }
    // Check the AI's own declared terminal limits; never truncate or move notes.
    if (sectionIndex + 1 == plan.sections.size()) {
        const auto* terminalLanes = root.getProperty("terminal_resolution", {}).getProperty("lanes", {}).getArray();
        const auto sectionStart = plan.sections[sectionIndex].startBar * plan.beatsPerBar;
        if (terminalLanes) for (const auto& lane : *terminalLanes) {
            const auto id = lane.getProperty("instrument_id", {}).toString().toStdString();
            const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
            if (owner == plan.instruments.end()) continue;
            const auto index = static_cast<std::size_t>(std::distance(plan.instruments.begin(), owner));
            if (std::find(requested.begin(), requested.end(), index) == requested.end()) continue;
            const auto attack = static_cast<double>(lane.getProperty("last_attack_beat", -1));
            // Local closing experiment only. Same-owner/pitch retriggers under an
            // outstanding note-off are ambiguous in MIDI 1.0. Inspect, never trim.
            // Include preceding sustains so collisions at a window boundary count.
            if (nucleus::closureEnabled()) {
                std::vector<std::pair<int, std::pair<double, double>>> sounding;
                for (const auto& n : midi.notes)
                    if (n.partId == index + 1 && n.startBeat < end && n.endBeat() > begin)
                        sounding.push_back({n.pitch, {n.startBeat, n.endBeat()}});
                std::sort(sounding.begin(), sounding.end());
                int pitch = -1;
                double soundingUntil = -1;
                for (const auto& event : sounding) {
                    if (event.first != pitch) { pitch = event.first; soundingUntil = -1; }
                    const auto onset = event.second.first;
                    if (onset >= std::max(begin, static_cast<double>(sectionStart)) &&
                        onset < soundingUntil - .001) {
                        if (std::find(missing.begin(), missing.end(), id) == missing.end()) missing.push_back(id);
                        evidence << juce::String(id) << " same-pitch overlap: pitch " << pitch
                            << " retriggered at absolute beat " << onset << " before release "
                            << soundingUntil << ". AI must choose a clear articulation; preserve the phrase and arrival, no automatic trimming. ";
                        break;
                    }
                    soundingUntil = std::max(soundingUntil, event.second.second);
                }
            }
            const auto release = static_cast<double>(lane.getProperty("release_beat", -1));
            const auto arrival = static_cast<double>(root.getProperty("terminal_resolution", {}).getProperty("arrival_beat", -1));
            const auto joins = static_cast<bool>(lane.getProperty("joins_arrival", std::abs(attack - arrival) < .001));
            const auto arrivalInWindow = sectionStart + arrival >= begin && sectionStart + arrival < end;
            const auto missingArrival = joins && arrivalInWindow && std::none_of(midi.notes.begin(), midi.notes.end(), [&](const auto& n) {
                return n.partId == index + 1 && std::abs(n.startBeat - sectionStart - arrival) < .001;
            });
            const auto violation = std::any_of(midi.notes.begin(), midi.notes.end(), [&](const auto& n) {
                return n.partId == index + 1 && n.startBeat >= std::max(begin, static_cast<double>(sectionStart)) && n.startBeat < end &&
                    (attack < 0 || n.startBeat > sectionStart + attack + .001 || n.endBeat() > sectionStart + release + .001);
            });
            if (violation) {
                if (std::find(missing.begin(), missing.end(), id) == missing.end()) missing.push_back(id);
                evidence << juce::String(id) << " exceeds YOUR terminal plan: last section-local attack <= "
                    << attack << ", release <= " << release << ". Recompose the concluding gesture; do not clip notes. ";
            }
            if (missingArrival) {
                if (std::find(missing.begin(), missing.end(), id) == missing.end()) missing.push_back(id);
                evidence << juce::String(id) << " promised to JOIN arrival at section-local beat "
                    << arrival << " but has no attack there. An anticipation is not the promised landing. ";
            }
        }
        const auto answer = root.getProperty("terminal_resolution", {}).getProperty("melodic_answer", {});
        const auto id = answer.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
        if (owner != plan.instruments.end()) {
            const auto index = static_cast<std::size_t>(std::distance(plan.instruments.begin(), owner));
            if (std::find(requested.begin(), requested.end(), index) != requested.end())
                if (const auto* notes = answer.getProperty("notes", {}).getArray()) for (const auto& promised : *notes) {
                    const auto beat = sectionStart + static_cast<double>(promised.getProperty("beat", -1));
                    if (beat < begin || beat >= end) continue;
                    // A cross-window sustain may be split at the technical boundary.
                    const auto duration = std::min(static_cast<double>(promised.getProperty("duration", 0)), end - beat);
                    const auto pitch = static_cast<int>(promised.getProperty("pitch", -1));
                    if (std::none_of(midi.notes.begin(), midi.notes.end(), [&](const auto& n) {
                        return n.partId == index + 1 && n.pitch == pitch && std::abs(n.startBeat - beat) < .001 &&
                            std::abs(n.durationBeats - duration) < .001;
                    })) {
                        if (std::find(missing.begin(), missing.end(), id) == missing.end()) missing.push_back(id);
                        evidence << juce::String(id) << " did not realize its AI-authored farewell note: absolute beat "
                            << beat << ", pitch " << pitch << ", duration " << duration << ". Rewrite this phrase faithfully; do not substitute a root tag. ";
                        break;
                    }
                }
        }
    }
    return missing;
}

inline juce::String realization(const juce::String& json, const SongPlan& plan, const Pattern& midi) {
    const auto root = juce::JSON::parse(json);
    juce::String out = "\nCLUB LAYER REALIZATION (actual MIDI, not aesthetic certification):\n";
    if (const auto* layers = root.getProperty("layers", {}).getArray()) for (const auto& layer : *layers) {
        const auto id = layer.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
        if (owner == plan.instruments.end()) continue;
        const auto partId = static_cast<std::size_t>(std::distance(plan.instruments.begin(), owner) + 1);
        if (const auto* states = layer.getProperty("states", {}).getArray()) for (const auto& state : *states) {
            const auto s = static_cast<int>(state.getProperty("section_index", -1));
            if (s < 0 || s >= static_cast<int>(plan.sections.size())) continue;
            const auto& section = plan.sections[static_cast<std::size_t>(s)];
            const auto begin = section.startBar * plan.beatsPerBar, end = begin + section.bars * plan.beatsPerBar;
            std::vector<std::pair<double, double>> spans;
            int notes = 0;
            for (const auto& note : midi.notes) if (note.partId == partId && note.startBeat < end && note.endBeat() > begin) {
                ++notes; spans.emplace_back(std::max(begin, note.startBeat), std::min(end, note.endBeat()));
            }
            std::sort(spans.begin(), spans.end());
            double covered = 0, lastEnd = begin;
            for (const auto& [a, b] : spans) { covered += std::max(0.0, b - std::max(a, lastEnd)); lastEnd = std::max(lastEnd, b); }
            out << juce::String(id) << " section=" << s << " mode=" << state.getProperty("mode", {}).toString()
                << " notes=" << notes << " actual_sounding_fraction=" << juce::String(covered / (end - begin), 3)
                << " AI_minimum=" << state.getProperty("minimum_sounding_fraction", {}).toString() << "\n";
        }
    }
    return out;
}

} // namespace pulso::plugin::club::production
