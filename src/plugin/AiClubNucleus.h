#pragma once

#include "AiChromaticPolicy.h"
#include <set>
#include <cmath>

namespace pulso::plugin::club::nucleus {

// Local, opt-in compositional experiment. This module only describes and
// validates AI-authored reference material. It never writes to a Pattern.
inline bool enabled() {
    return juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_NUCLEUS_V2", {}) == "1";
}

inline bool voicingOnly() {
    return enabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_CLUB_VOICING_ONLY", {}) == "1";
}

inline bool closureEnabled() {
    return enabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_CLUB_CLOSURE_EXPERIMENT", {}) == "1";
}

inline juce::String closurePlanningBrief() {
    if (!closureEnabled()) return {};
    return "\nPLAN THE ENDING BEFORE WRITING NOTES. For full_arrangement, terminal_resolution "
        "must describe a perceptible prepared arrival, not merely a timestamp at the file edge. "
        "All its beats are SECTION-LOCAL to the LAST section. Choose preparation_beat, "
        "arrival_beat and resonance_end_beat with preparation < arrival < resonance_end. "
        "Treat the final section as a developed closing phrase, not ordinary looping followed "
        "by a one-bar warning. In melodic_answer, write actual beat/duration/pitch/velocity "
        "notes for one existing melodic owner: recognizable recall of its nucleus, a connected "
        "continuation that makes the destination expectable, and the arriving note at arrival_beat. "
        "Explain the specific thematic relationship in motif_relationship. Do not leave a long "
        "melodic vacancy and then drop in a held root: develop the approach in actual notes. "
        "Let note lengths and contour announce completion while keeping the club pulse. "
        "Plan enough of the section for the approach and for heard repose after it, in relation "
        "to the phrase lengths already established, rather than squeezing both into the final "
        "seconds. Coordinate the motion layers' last statements with that melodic arrival; "
        "Do not habitually place the arrival in the very last bar: choose its location "
        "from the established phrase pace so the listener hears both the resolution "
        "and its aftermath. A file ending on a technically complete note is not enough "
        "if all voices cease before the conclusion has registered. This is a musical "
        "planning decision, not a mandatory silent tail or fixed bar count. "
        "after it, any remaining attack should acknowledge closure rather than restart the motor. "
        "Explain what earlier hook is being answered and why the listener can anticipate "
        "this arrival. Choose one terminal lane for every cast owner: last_attack_beat and "
        "release_beat plus its concluding gesture. "
        "Declare joins_arrival=true for voices that articulate the shared arrival, false "
        "for voices that withdraw or simply sustain through it. Those joining must attack "
        "at EXACTLY arrival_beat, not the preceding sixteenth; at least the melodic answer "
        "must join. Give the foreground a perceptible rhythmic and registral space: the "
        "sequence should not double the farewell's pitches in the same octave or add a "
        "competing flourish as the sentence resolves. Articulate the melodic approach with "
        "musical grouping and directed note lengths, not a uniform detached scale exercise. "
        "Distinguish the work's climax from its conclusion. In audible_resolution explain "
        "how the final phrase spends the energy already earned rather than announcing "
        "another peak. A high tonic is not automatically a convincing ending: compare its "
        "register, leap and velocity with the preceding phrase before choosing it. "
        "An upward or emphatic ending is allowed when prepared by the motif; do not "
        "combine a surprise registral leap and fresh accent merely to mark the file end. "
        "For each lane's gesture, specify its audible behavior DURING preparation as well "
        "as at arrival: what remains recognizable, what completes its last statement, "
        "and what makes room for the melodic answer. Avoid a binary unchanged-loop/held-chord "
        "switch. Bass and arp should participate in the closing phrase, not simply run "
        "until their last allowed beat. Preserve groove and harmonic support while their "
        "individual sentences conclude; thinning does not mean an empty accompaniment. "
        "Check the ensemble on a single neutral piano: separate the arp's register, "
        "articulation or activity from the answering melody so that the farewell remains "
        "intelligible without relying on different patches. Choose the solution musically, "
        "not a mandatory octave offset or universal density limit. "
        "Use -1 for both terminal lane beats only if the voice is absent "
        "throughout the final section. Coordinate the bass, sequencer and stabs so they "
        "complete their movement instead of proposing another loop over a resolved melody. "
        "Arrival and resonance must be audible in the actual MIDI, not just the prose. "
        "Plan layer states consistently: their release_beat must match the chosen lane "
        "release; no continuous-to-the-end instruction may contradict the ending. "
        "The terminal lane's last attack is an upper bound, not a new required onset outside "
        "its rhythm_cell. Choose that bound from allowed attacks for cycle_bound voices, "
        "except an explicit joins_arrival attack: that one concluding gesture may depart "
        "from the recurring rhythm_cell without changing its other cycles. "
        "A final chord can differ in voicing from the recurring extended pad: choose the "
        "stable color and register that concludes THIS piece. No compulsory cadence, "
        "silence duration or fade is supplied by code. For continuous_section return null.\n";
}

inline juce::String closingBrief(const juce::var& root, const SongPlan& plan,
                                 std::size_t sectionIndex) {
    if (!enabled() || juce::SystemStats::getEnvironmentVariable(
            "PULSO_AI_CLUB_CLOSURE_EXPERIMENT", {}) != "1" ||
        root.getProperty("format", {}).toString() != "full_arrangement" ||
        plan.sections.empty() || sectionIndex + 1 != plan.sections.size()) return {};
    return "\nAI-AUTHORED TERMINAL PLAN (beats are last-section-local): " +
        juce::JSON::toString(root.getProperty("terminal_resolution", {}), true) +
        "\nTERMINAL MUSICAL CONSEQUENCE (actual song ending, not a loop boundary): "
        "The listener must hear the recurring gesture reach its destination, not simply "
        "hear the sequencer run out of allotted time. Compose the last phrase as the "
        "consequence of this work's specific hook and harmony. Choose where its last "
        "meaningful arrival occurs and let the ensemble acknowledge it. A held tonic "
        "in the melody is insufficient if bass, stabs and arp continue proposing the "
        "next cycle. Finish their gestures, then let the arrived harmony be heard. "
        "For this final phrase only, the ordinary recurring attack pattern describes "
        "the vocabulary, not an obligation to retrigger until the file ends. Choose "
        "which existing attacks to retain, which to omit, and how notes release. "
        "Keep the established rhythmic phase; do not replace resolution with slowing "
        "off-grid notes, a generic fade, an unrelated cadenza or a chopped pattern. "
        "The plan's release_beat is an outer bound, not a required final attack; its "
        "continuous-state language yields to the explicit final resolution here. "
        "A new harmonic progression, compulsory empty bars or extra tracks are not "
        "required. Determine the cadence and breathing span from this composition, "
        "not a fixed recipe. Realize the AI-authored melodic_answer in its assigned voice "
        "using the supplied final-section-local coordinates, translated to this window. "
        "It is a developed farewell, not a suggestion to replace it with a single held root. "
        "Keep its thematic approach and arrival audible against the complete ensemble. "
        "joins_arrival=true requires an actual attack at arrival_beat exactly. A sixteenth "
        "anticipation is NOT equivalent. Realize melodic_answer's actual pitches, onsets "
        "and durations; translate coordinates, do not approximate the phrase. Let the "
        "arpeggio yield perceptual space to it rather than doubling its register and pitches. "
        "Realize each lane's concluding gesture THROUGHOUT the preparation, not only its "
        "last timestamp. Keep harmonic support while bass and arp complete recognizable "
        "statements under the foreground. Do not interpret withdrawal as deleting the "
        "whole approach, or resolution as another register/velocity climax. "
        "Within a single owner, release a sounding pitch before retriggering that same "
        "pitch (touching boundaries are fine); a new note-on under its old note-off can "
        "sound cut off in MIDI playback. Different-pitch legato and chords remain allowed. "
        "Choose those durations yourself; the host will report collisions, not trim them. "
        "For joining owners the response schema separates notes from arrival_notes: "
        "write the approach and any post-arrival phrase in notes, and the landing attacks "
        "in arrival_notes at its exact WINDOW-local beat. The schema translates the "
        "section-local plan for you. Do not put the landing in both containers. "
        "Write every resulting MIDI event yourself; no host copies the planned phrase.\n";
}

inline juce::String editorial() {
    return "TONAL CLUB / PRODUCER COMPOSITION v2. Write club music without drums or percussion. "
        "The absence of drums does not turn the arrangement into chamber music: bass, chord stabs "
        "and sequencer must establish a clear shared 4/4 pulse. Separate THREE TIMESCALES. "
        "At the short scale, invent a compelling one-to-four-bar tonal groove whose rhythmic "
        "identity can repeat literally. At the phrase scale, make small purposeful additions or "
        "withdrawals after complete phrases, commonly 8 or 16 bars. At the macro scale, let a "
        "fundamental change mature over roughly 64 bars when duration permits; not every section "
        "is a new groove or a new story. For shorter works establish one convincing state and "
        "its consequence instead of compressing a succession of drops. The AI chooses the form. "
        "Favor phrase-aligned boundaries and harmonic changes; do not invent 10/14-bar phrases "
        "or mid-beat harmonic changes just to demonstrate originality. Preserve the same physical "
        "reference through intro, plateau, tension and release. A hook may breathe for several "
        "bars while the sequencer continues. A requested hypnotic arpeggio is a sustained pattern "
        "of regular eighths or sixteenths across complete cycles, not four offbeat cue notes "
        "followed by half a bar of silence. Choose which layer carries that subdivision; the "
        "others accent or sustain against it instead of each proposing another busy syncopation. "
        "A sustained layer's soft envelope is not a delayed MIDI attack: when it supports a "
        "harmonic arrival, place its attack on that arrival unless a specific musical anticipation "
        "or suspension is intended. Do not delay all sustained voices by a sixteenth at a new "
        "writing window. Judge the ensemble's composite rhythm, including repeated pitches "
        "across lanes, rather than treating each correct grid position as a good rhythm. "
        "Bass should propel and anchor; stabs supply a memorable chord rhythm; pads supply an "
        "audible voiced harmonic floor with connected releases; the melodic hook supplies a "
        "singable contour and spacious return. These are complementary jobs, not independent "
        "solos. Short motifs repeated unchanged are legitimate musical substance. Their meaning "
        "can develop through harmony, register, voicing and arrangement without constantly "
        "recomposing their attacks. Preserve enough unchanged material to hear the transformation. "
        "Keep simultaneous sustained voicings consonant in their actual registers; stacking "
        "unresolved adjacent held tones is not automatically dark atmosphere. In a full song, "
        "reveal the groove, sustain it, earn a contrast or arrival, and complete its last phrase "
        "before the final harmonic release. Do not create silence at technical window boundaries. "
        "Every pitch, attack, duration, accent, repeat and rest is written by the AI. No code "
        "supplies a preset groove, copies a cell into the score, fills a pad or corrects pitches. ";
}

inline void extendSchema(juce::var& root, const SongPlan* plan) {
    if (!enabled()) return;
    auto sketch = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["reference_chord_id","cycle_beats","meter_carrier_id","carrier_subdivision_beats","audible_relationship","lanes"],
      "properties":{
        "reference_chord_id":{"type":"string"},
        "cycle_beats":{"type":"number","enum":[4,8,16]},
        "meter_carrier_id":{"type":"string"},
        "carrier_subdivision_beats":{"type":"number","enum":[0.25,0.5,1,2,4],
          "description":"Exact gap between consecutive attacks of the regular meter carrier in this reference cycle. Quarter-note units: eighths=0.5, sixteenths=0.25. This is onset spacing, not note duration."},
        "audible_relationship":{"type":"string"},
        "lanes":{"type":"array","minItems":1,"maxItems":12,"items":{
          "type":"object","additionalProperties":false,
          "required":["instrument_id","repeat_intent","notes"],
          "properties":{
            "instrument_id":{"type":"string"},"repeat_intent":{"type":"string"},
            "notes":{"type":"array","minItems":1,"maxItems":128,"items":{
              "type":"object","additionalProperties":false,
              "required":["beat","duration","pitch","velocity"],
              "properties":{
                "beat":{"type":"number","minimum":0,"maximum":15.75,"multipleOf":0.25},
                "duration":{"type":"number","minimum":0.25,"maximum":16,"multipleOf":0.25},
                "pitch":{"type":"integer","minimum":0,"maximum":127},
                "velocity":{"type":"integer","minimum":1,"maximum":127}
              }}}
          }}}
      }})json");
    if (plan) {
        juce::Array<juce::var> chordIds, ownerIds;
        for (const auto& chord : plan->chordPalette) chordIds.add(juce::String(chord.id));
        for (const auto& owner : plan->instruments) ownerIds.add(juce::String(owner.id));
        const auto props = sketch.getProperty("properties", {});
        if (!chordIds.isEmpty()) props.getProperty("reference_chord_id", {}).getDynamicObject()->setProperty("enum", chordIds);
        if (!ownerIds.isEmpty()) {
            props.getProperty("meter_carrier_id", {}).getDynamicObject()->setProperty("enum", ownerIds);
            props.getProperty("lanes", {}).getProperty("items", {}).getProperty("properties", {})
                .getProperty("instrument_id", {}).getDynamicObject()->setProperty("enum", ownerIds);
        }
    }
    auto landings = root.getProperty("properties", {}).getProperty("groove_interlock", {})
        .getProperty("properties", {}).getProperty("shared_landings", {});
    if (landings.isObject()) {
        landings.getDynamicObject()->setProperty("minItems", 0);
        landings.getDynamicObject()->setProperty("description",
            "Actual shared core onsets, or [] when complementary rhythms do not coincide. Interlock does not require simultaneous attacks.");
    }
    root.getProperty("required", {}).getArray()->add("tonal_nucleus");
    root.getProperty("properties", {}).getDynamicObject()->setProperty("tonal_nucleus", sketch);
    if (closureEnabled()) {
        // Concrete melodic material alongside the rhythmic nucleus. The host
        // validates/describes it but never copies it into performed MIDI.
        auto arc = root.getProperty("properties", {}).getProperty("musical_arcs", {}).getProperty("items", {});
        if (arc.isObject()) {
            arc.getProperty("required", {}).getArray()->add("melodic_statement");
            arc.getProperty("properties", {}).getDynamicObject()->setProperty("melodic_statement",
                juce::JSON::parse(R"({"type":"object","additionalProperties":false,
                  "required":["development","held_voice","ensemble_relationship","rhythmic_identity","vertical_support","notes"],"properties":{
                    "development":{"type":"string","description":"How this sentence develops the preceding motif and earns its destination; identify intentional instrumental-only arcs."},
                    "held_voice":{"type":"string","description":"Existing owner and actual sustained melodic/harmonic function, register and phrase timing; contrast with the fast carrier."},
                    "ensemble_relationship":{"type":"string","description":"Identify who speaks, who answers at which arc-local beats, and how bass/arp/stabs support rather than compete; allow purposeful counterpoint."},
                    "rhythmic_identity":{"type":"string","description":"Reference phrase and its recognizable onset/accent pattern. Identify literal returns versus deliberate variations with their arc-local location; no unexplained collective sixteenth shift."},
                    "vertical_support":{"type":"string","description":"Concrete support pitches/registers against important foreground holds. Describe intended tensions and their resolutions; avoid duplicating an unprepared close semitone in multiple sustained owners."},
                    "notes":{"type":"array","minItems":0,"maxItems":48,"description":"AI-written foreground sentence. Beats relative to this arc start, not a repeating cell. Empty only for a deliberately instrumental-only arc.",
                      "items":{"type":"object","additionalProperties":false,"required":["beat","duration","pitch","velocity"],"properties":{
                        "beat":{"type":"number","minimum":0,"multipleOf":0.25},
                        "duration":{"type":"number","minimum":0.25,"multipleOf":0.25},
                        "pitch":{"type":"integer","minimum":0,"maximum":127},
                        "velocity":{"type":"integer","minimum":1,"maximum":127}}}}}})"));
            auto statementSchema = arc.getProperty("properties", {}).getProperty("melodic_statement", {});
            auto parts = juce::JSON::parse(R"({"type":"array","minItems":0,"maxItems":12,
              "description":"Jointly composed support voicings and developed replies at important phrase moments. Not an exhaustive arrangement or repeated groove.",
              "items":{"type":"object","additionalProperties":false,
                "required":["instrument_id","musical_function","notes"],"properties":{
                  "instrument_id":{"type":"string"},
                  "musical_function":{"type":"string","description":"Specific connection to the foreground: supported held tone, prepared tension and resolution, or developed reply and destination."}}}})");
            parts.getProperty("items", {}).getProperty("properties", {}).getDynamicObject()->setProperty(
                "notes", juce::JSON::parse(juce::JSON::toString(statementSchema.getProperty("properties", {}).getProperty("notes", {}))));
            parts.getProperty("items", {}).getProperty("properties", {}).getProperty("notes", {}).getDynamicObject()->setProperty(
                "description", "Actual AI-authored notes, ARC-LOCAL beats. Include complete support chords at chosen holds or a connected answering sentence, not all recurring accompaniment.");
            statementSchema.getProperty("properties", {}).getDynamicObject()->setProperty("ensemble_parts", parts);
            statementSchema.getProperty("required", {}).getArray()->add("ensemble_parts");
        }
        auto terminal = juce::JSON::parse(R"json({"anyOf":[{"type":"null"},{
          "type":"object","additionalProperties":false,
          "required":["preparation_beat","arrival_beat","resonance_end_beat","audible_resolution","melodic_answer","lanes"],
          "properties":{
            "preparation_beat":{"type":"number","minimum":0,"multipleOf":0.25},
            "arrival_beat":{"type":"number","minimum":0,"multipleOf":0.25},
            "resonance_end_beat":{"type":"number","minimum":0,"multipleOf":0.25},
            "audible_resolution":{"type":"string"},
            "melodic_answer":{"type":"object","additionalProperties":false,
              "required":["instrument_id","motif_relationship","notes"],
              "properties":{
                "instrument_id":{"type":"string"},
                "motif_relationship":{"type":"string"},
                "notes":{"type":"array","minItems":1,"maxItems":128,"items":{
                  "type":"object","additionalProperties":false,
                  "required":["beat","duration","pitch","velocity"],
                  "properties":{
                    "beat":{"type":"number","minimum":0,"multipleOf":0.25},
                    "duration":{"type":"number","minimum":0.25,"multipleOf":0.25},
                    "pitch":{"type":"integer","minimum":0,"maximum":127},
                    "velocity":{"type":"integer","minimum":1,"maximum":127}
                  }}}
              }},
            "lanes":{"type":"array","minItems":1,"maxItems":70,"items":{
              "type":"object","additionalProperties":false,
              "required":["instrument_id","last_attack_beat","release_beat","joins_arrival","gesture"],
              "properties":{
                "instrument_id":{"type":"string"},
                "last_attack_beat":{"type":"number","minimum":-1,"multipleOf":0.25},
                "release_beat":{"type":"number","minimum":-1,"multipleOf":0.25},
                "joins_arrival":{"type":"boolean"},
                "gesture":{"type":"string"}
              }}}
          } }]})json");
        root.getProperty("required", {}).getArray()->add("terminal_resolution");
        root.getProperty("properties", {}).getDynamicObject()->setProperty("terminal_resolution", terminal);
    }
}

inline bool validateClosure(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!root.hasProperty("terminal_resolution") || root.getProperty("format", {}).toString() != "full_arrangement") return true;
    const auto terminal = root.getProperty("terminal_resolution", {});
    const auto number = [](const juce::var& v) {
        return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite(static_cast<double>(v)) &&
            std::abs(static_cast<double>(v) * 4 - std::round(static_cast<double>(v) * 4)) < .0001;
    };
    const auto a = terminal.getProperty("preparation_beat", {}), b = terminal.getProperty("arrival_beat", {}), c = terminal.getProperty("resonance_end_beat", {});
    const auto* lanes = terminal.getProperty("lanes", {}).getArray();
    if (plan.sections.empty() || !number(a) || !number(b) || !number(c) ||
        static_cast<double>(a) < 0 || static_cast<double>(a) >= static_cast<double>(b) ||
        static_cast<double>(b) >= static_cast<double>(c) ||
        static_cast<double>(c) > plan.sections.back().bars * plan.beatsPerBar || !lanes ||
        static_cast<std::size_t>(lanes->size()) != plan.instruments.size()) {
        error = "Terminal plan needs ordered preparation, arrival and resonance inside the final section, and one lane per owner"; return false;
    }
    std::set<std::string> seen;
    double latestRelease = -1;
    for (const auto& lane : *lanes) {
        const auto id = lane.getProperty("instrument_id", {}).toString().toStdString();
        const auto attack = lane.getProperty("last_attack_beat", {}), release = lane.getProperty("release_beat", {});
        auto absent = static_cast<double>(attack) == -1 && static_cast<double>(release) == -1;
        // Arrangement states use 0/0 for absent voices; accept that equivalent
        // representation only with an explicit final-section absence. Never
        // reinterpret a zero-length active gesture or invent/modify MIDI.
        if (static_cast<double>(attack) == 0 && static_cast<double>(release) == 0 &&
            !static_cast<bool>(lane.getProperty("joins_arrival", false))) {
            if (const auto* layers = root.getProperty("layers", {}).getArray())
                for (const auto& layer : *layers)
                    if (layer.getProperty("instrument_id", {}).toString().toStdString() == id)
                        if (const auto* states = layer.getProperty("states", {}).getArray())
                            for (const auto& state : *states)
                                if (static_cast<int>(state.getProperty("section_index", -1)) == static_cast<int>(plan.sections.size() - 1) &&
                                    state.getProperty("mode", {}).toString() == "absent") absent = true;
        }
        if (!seen.insert(id).second || std::none_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; }) ||
            !number(attack) || !number(release) || (!absent &&
            (static_cast<double>(attack) < 0 || static_cast<double>(attack) >= static_cast<double>(release) || static_cast<double>(release) > static_cast<double>(c)))) {
            error = "Invalid AI terminal lane timing or owner: " + juce::String(id) +
                " last_attack=" + attack.toString() + " release=" + release.toString() +
                ". Active lanes need attack < release; absent lanes use -1/-1 or 0/0 with an explicit absent final state.";
            return false;
        }
        latestRelease = std::max(latestRelease, static_cast<double>(release));
        if (static_cast<bool>(lane.getProperty("joins_arrival", false)) &&
            (absent || static_cast<double>(attack) < static_cast<double>(b) || static_cast<double>(release) <= static_cast<double>(b))) {
            error = "Terminal arrival participant cannot withdraw before its declared arrival: " + juce::String(id); return false;
        }
    }
    if (std::abs(latestRelease - static_cast<double>(c)) > .001) {
        error = "Terminal resonance_end must be realized by at least one declared lane release"; return false;
    }
    // Optional on older saved plans; required by the current experimental schema.
    if (terminal.hasProperty("melodic_answer")) {
        const auto answer = terminal.getProperty("melodic_answer", {});
        const auto id = answer.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
        const auto* notes = answer.getProperty("notes", {}).getArray();
        if (owner == plan.instruments.end() || !notes || notes->isEmpty() || notes->size() > 128) {
            error = "Terminal melodic answer needs an existing owner and explicit notes"; return false;
        }
        double lastAttack = -1, release = -1;
        for (const auto& lane : *lanes) if (lane.getProperty("instrument_id", {}).toString().toStdString() == id) {
            lastAttack = static_cast<double>(lane.getProperty("last_attack_beat", -1));
            release = static_cast<double>(lane.getProperty("release_beat", -1));
        }
        bool arrives = false;
        for (const auto& note : *notes) {
            const auto beat = note.getProperty("beat", {}), duration = note.getProperty("duration", {});
            const auto pitch = note.getProperty("pitch", {}), velocity = note.getProperty("velocity", {});
            if (!number(beat) || !number(duration) || !number(pitch) || !number(velocity) ||
                static_cast<double>(beat) < 0 || static_cast<double>(beat) > lastAttack ||
                static_cast<double>(duration) <= 0 || static_cast<double>(beat) + static_cast<double>(duration) > release + .001 ||
                static_cast<double>(pitch) != std::floor(static_cast<double>(pitch)) ||
                static_cast<int>(pitch) < owner->minimumPitch || static_cast<int>(pitch) > owner->maximumPitch ||
                !AiChromaticPolicy::allows(plan, static_cast<int>(pitch)) ||
                static_cast<double>(velocity) != std::floor(static_cast<double>(velocity)) ||
                static_cast<int>(velocity) < 1 || static_cast<int>(velocity) > 127) {
                error = "Terminal melodic answer violates timing, owner release, register or tonal policy"; return false;
            }
            arrives = arrives || std::abs(static_cast<double>(beat) - static_cast<double>(b)) < .001;
        }
        if (!arrives) { error = "Terminal melodic answer must realize its declared arrival beat"; return false; }
    }
    return true;
}

// Part of the local candidate motor, not a job-specific MIDI edit. Reuses the
// existing state/evolution fields; no fixed variation scheduler or note quota.
inline juce::String sequenceDevelopmentBrief(bool planning) {
    if (!closureEnabled()) return {};
    const juce::String common =
        "\nPURPOSEFUL ARPEGGIO DEVELOPMENT: Keep hypnotic repetition as the identity, "
        "not a defect to correct. Let the listener learn the contour, pulse and accents "
        "before transforming them. A variation should answer a musical event: the hook's "
        "reply, a harmonic opening, a prepared return or a farewell. Choose where literal "
        "repetition is best and where a recognizable change earns its place. Possible "
        "means include a changed phrase ending, a registral opening within the owner's "
        "range, accent/gate shaping, or an in-key suspension that resolves. These are "
        "alternatives, not a checklist. Do not schedule a fill every eight bars or at "
        "every technical window. Keep unchanged returns so the variation has meaning. "
        "Preserve the declared attack phase and carrier subdivision during full-body "
        "states: flavor can change pitches, contour or articulation without adding "
        "busy attacks, shifting the groove or making all voices vary together. A rest "
        "belongs to an intentional arrangement withdrawal, not a randomly broken carrier. "
        "Keep the foreground intelligible on a neutral piano; the supporting sequence "
        "must not compete with the melody. Respect the chromatic policy and harmonic map. ";
    return common + (planning ?
        "In the existing layer states' evolution and material, locate each chosen change "
        "with section-local beats and describe its audible purpose, what stays identical "
        "and where the reference returns. Put invariants in that tonal_nucleus lane's "
        "repeat_intent. Plan the final arp statement as a consequence of its earlier "
        "identity, coordinated with terminal_resolution, not an unrelated last flourish. "
        "No minimum number of changes is required.\n" :
        "Realize the relevant planned state in actual notes, retaining its unchanged "
        "reference where requested. Do not repeat a once-only phrase answer in every "
        "cycle or restart its development at a window boundary. Write the changed notes "
        "yourself; no host embellishes or copies the arpeggio. In the final phrase follow "
        "the terminal plan rather than restarting the repeating pattern.\n");
}

inline bool validateMelodicStatement(const juce::var& arc, const SongPlan& plan, juce::String& error) {
    if (!arc.hasProperty("melodic_statement")) return true; // Saved plans remain readable.
    const auto statement = arc.getProperty("melodic_statement", {});
    const auto* notes = statement.getProperty("notes", {}).getArray();
    const auto id = arc.getProperty("foreground_id", {}).toString().toStdString();
    const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
    if (!notes || notes->size() > 48 || owner == plan.instruments.end()) {
        error = "Melodic statement needs an existing foreground and a note array"; return false;
    }
    for (const auto* field : {"development", "held_voice", "ensemble_relationship"})
        if (statement.getProperty(field, {}).toString().trim().isEmpty()) {
            error = "Melodic statement omits " + juce::String(field); return false;
        }
    for (const auto* field : {"rhythmic_identity", "vertical_support"})
        if (statement.hasProperty(field) && statement.getProperty(field, {}).toString().trim().isEmpty()) {
            error = "Melodic statement has empty " + juce::String(field); return false;
        }
    const auto length = static_cast<double>(arc.getProperty("bars", 0)) * plan.beatsPerBar;
    if (statement.hasProperty("ensemble_parts")) {
        const auto* parts = statement.getProperty("ensemble_parts", {}).getArray();
        if (!parts || parts->size() > 12) { error = "Invalid joint ensemble parts"; return false; }
        for (const auto& part : *parts) {
            if (part.getProperty("musical_function", {}).toString().trim().isEmpty()) {
                error = "Joint ensemble part lacks its musical function"; return false;
            }
            // Reuse only technical note validation. No density, consonance quota,
            // transposition, note insertion, or replacement of the AI's choices.
            auto child = juce::JSON::parse(R"({"melodic_statement":{"development":"joint phrase","held_voice":"joint phrase","ensemble_relationship":"joint phrase"}})");
            child.getDynamicObject()->setProperty("bars", arc.getProperty("bars", 0));
            child.getDynamicObject()->setProperty("foreground_id", part.getProperty("instrument_id", {}));
            child.getProperty("melodic_statement", {}).getDynamicObject()->setProperty("notes", part.getProperty("notes", {}));
            if (!validateMelodicStatement(child, plan, error)) return false;
        }
    }
    for (const auto& n : *notes) {
        for (const auto* field : {"beat", "duration", "pitch", "velocity"}) {
            const auto value = n.getProperty(field, {});
            const auto number = static_cast<double>(value);
            if (!(value.isInt() || value.isInt64() || value.isDouble()) || !std::isfinite(number) ||
                std::abs(number * 4 - std::round(number * 4)) > .001) {
                error = "Melodic statement has invalid numeric/grid data"; return false;
            }
        }
        const auto beat = static_cast<double>(n.getProperty("beat", 0));
        const auto duration = static_cast<double>(n.getProperty("duration", 0));
        const auto pitch = static_cast<double>(n.getProperty("pitch", 0));
        const auto velocity = static_cast<double>(n.getProperty("velocity", 0));
        if (beat < 0 || duration <= 0 || beat + duration > length + .001 ||
            pitch != std::floor(pitch) || pitch < owner->minimumPitch || pitch > owner->maximumPitch ||
            !AiChromaticPolicy::allows(plan, static_cast<int>(pitch)) ||
            velocity != std::floor(velocity) || velocity < 1 || velocity > 127) {
            error = "Melodic statement violates its arc span, register or tonal policy"; return false;
        }
    }
    return true;
}

inline juce::String ensembleSentenceBrief(bool planning) {
    if (!closureEnabled()) return {};
    const juce::String common =
        "\nCONNECTED MELODIC ENSEMBLE: The pulse carrier is accompaniment, not the template "
        "for every voice. Compose a foreground sentence with a recognizable thought, "
        "continuation and destination, rather than a rising scale fragment repeated as "
        "the entire melody. Give selected melodic tones time to be heard over the pulse: "
        "sustained notes can carry expectation, tension and resolution, not only mark the "
        "last chord. Contrast fast motion, a slower singing line and connected harmonic "
        "support. Choose their durations from the harmonic rhythm, not from a universal "
        "note-length rule. Keep useful repeating substance while the sentence unfolds. "
        "Coordinate voices in actual register and time, as if played on one piano. "
        "An answer should continue or complete a thought; it must not restart a competing "
        "solo underneath every lead attack. Avoid near-unison repeated attacks across "
        "voices that obscure phrasing. Deliberate counterpoint is welcome when its "
        "rhythms and voice-leading remain intelligible. Keep the tonal floor alive: "
        "making room for the lead does not mean silencing all supporting layers. "
        "Check VERTICAL VOICING against the actual foreground holds, not merely scale "
        "membership or chord labels. A diatonic adjacent semitone held in the same "
        "register can still obscure the melody, especially when duplicated in two pads. "
        "Choose support inversions, omitted extensions or smooth voice-leading that "
        "serve the melodic destination without deleting the harmonic floor. Intentional "
        "suspensions remain welcome when prepared and resolved audibly; do not ban all "
        "seconds or reduce every chord to a triad. Check the bass and accumulated "
        "sustained voices together before adding another extension. "
        "A literal thematic return preserves its relative attack and accent identity. "
        "A uniform quarter-beat delay is not humanization: do not shift a whole phrase "
        "because a writing window begins. Deliberate rhythmic variation must have a "
        "clear relationship to the established pulse and a recognizable landing. ";
    return common + (planning ?
        "COMPOSE JOINT PHRASE LANDMARKS in melodic_statement.ensemble_parts: choose "
        "important foreground holds and compose the actual simultaneous support voicings "
        "in their existing owners, plus any answering sentence whose development matters. "
        "Use arc-local notes for all owners so their harmony and articulation can be read "
        "together. These are selected moments, not a second full score. Do not solve a "
        "foreground/support conflict by keeping a generic pad extension unchanged and "
        "calling the clash color. Decide the voicing together with the melodic note. "
        "Where a reply returns, decide whether it is a literal hook or an evolved answer; "
        "write the latter's connected continuation and arrival rather than three isolated "
        "ornaments. No obligatory extra voice or compulsory new notes per return. "
        "In each musical_arc write melodic_statement.notes for its foreground_id, "
        "with ARC-LOCAL beats. Compose a developed sentence, including meaningful held "
        "tones where appropriate; do not distribute disconnected cue notes across the "
        "arc to pretend it is developed. Explain its relationship to earlier material in "
        "development. State which existing voice supplies the slower held strand in "
        "held_voice and coordinate responses/support in ensemble_relationship and layer "
        "states. Preserve the club subdivision in the carrier, not in every melody. "
        "In rhythmic_identity locate the reference and describe what survives each "
        "return; in vertical_support name the actual support pitches/registers under "
        "the phrase's important holds, consistent with the layer states. Develop a "
        "continuation that changes the thought, not just the same scale ascent with "
        "longer durations. Keep a memorable subject and let its answer earn the next "
        "arrival. A long note should connect the argument, not leave a vacuum around it. "
        "Use [] for a deliberately instrumental-only arc, not an unfinished melody. "
        "Place releases coherently with the supplied harmonic map. Do not leave the "
        "foreground absent in its layer state where you sketch it. For the last arc "
        "make this material agree with terminal_resolution.melodic_answer. No forced "
        "track count, quota of long notes or compulsory call-and-response alternation.\n" :
        "JOINT PHRASE LANDMARKS in ensemble_parts belong to the same composition as "
        "the foreground, not independent suggestions. Realize their support pitches and "
        "developed replies together with the supplied melody. Where a chosen support "
        "voicing replaces the reference cell, use it instead of stacking both voicings. "
        "Between landmarks preserve the harmonic floor with connected voice-leading; "
        "do not turn sparse planning anchors into sparse accompaniment. Do not reduce "
        "a developed reply back to its first few ornament notes or blindly repeat a "
        "once-only continuation. The host does not render these landmarks into MIDI. "
        "Realize the supplied foreground sentence and its relationships in this window, "
        "not just the short groove sketch. Preserve the planned long-tone functions and "
        "phrase destination instead of shortening all notes to carrier gates. Interpret "
        "the arc as a continuous argument across windows, not a new sentence at every "
        "window. Write sustained voices and complementary answers yourself; the host "
        "does not fill, thin, copy or lengthen notes. The exact terminal plan governs "
        "the final arrival. Preserve the rhythmic_identity plan in actual onsets, and "
        "realize vertical_support against the lead you are writing. Do not independently "
        "stack every available chord extension in pads and texture. Before returning, "
        "check the composite phrase on one neutral instrument: the response must be "
        "legible and a sustained tension must have a meaningful release.\n");
}

inline juce::String planningBrief() {
    return "\nCONCRETE TONAL NUCLEUS: In tonal_nucleus compose an ACTUAL simultaneous short "
        "MIDI sketch before asking a later writer to develop it. Choose a reference_chord_id "
        "from the supplied palette and one common cycle_beats of 4, 8 or 16. This is a neutral "
        "full-body reference, NOT the first bars of the song. Include the groove core, the "
        "continuous sequence and the sustained chord bed; optionally include the hook. "
        "Write exact beat/duration/pitch/velocity for these lanes. All beats are local to "
        "this reference cycle, all releases <= cycle_beats, all pitches in the declared "
        "scale and owner register. Author all repetitions in this sketch explicitly. "
        "Choose a meter_carrier_id whose actual rhythm makes the pulse/subdivision easy "
        "to follow. Declare carrier_subdivision_beats as its exact consecutive ATTACK "
        "spacing: an eighth is 0.5 beats, a sixteenth 0.25, a quarter 1.0. A note with "
        "duration=0.5 but onsets 0,1,2,3 is a detached QUARTER-note pulse, not continuous "
        "eighths. For an eighth sequence across an eight-beat reference write sixteen "
        "attacks spaced by 0.5; choose their pitches, durations and accents yourself. "
        "The meter carrier must realize its declared spacing throughout this reference "
        "cycle including the wrap. Musical withdrawals belong to the arrangement states. "
        "Explain how the other lanes reinforce it. Use repeat_intent to say "
        "which notes/accents should survive unchanged in a full state and how they may "
        "adapt to other chords. Align the sketch's core attacks with its declared rhythm_cell "
        "and use absolute cycle origin 0 for the repeating core (entry time is separate). "
        "A continuous arpeggio must actually continue across the cycle, not become a "
        "fragmented solo because it is a supporting voice. Retain a stable subdivision "
        "in long full-body states, with omissions reserved for deliberate transitions. "
        "Give sustained harmonic beds phrase_flexible timing: they follow the HARMONIC "
        "MAP and connected voice-leading, not a fixed attack slot that leaves an accidental "
        "hole at a chord/section boundary. phrase_flexible does not mean late or syncopated. "
        "Plan the tonal_nucleus and layer states together; neither should contradict the "
        "other. The later AI receives the exact sketch but must author the actual score "
        "and its development itself. The host never tiles this sketch into MIDI.\n" +
        sequenceDevelopmentBrief(true) + ensembleSentenceBrief(true);
}

inline bool validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!validateClosure(root, plan, error)) return false;
    if (!root.hasProperty("tonal_nucleus")) return true; // Older checkpoints unchanged.
    const auto sketch = root.getProperty("tonal_nucleus", {});
    const auto chordId = sketch.getProperty("reference_chord_id", {}).toString().toStdString();
    const auto cycle = static_cast<double>(sketch.getProperty("cycle_beats", 0));
    const auto* lanes = sketch.getProperty("lanes", {}).getArray();
    if ((cycle != 4 && cycle != 8 && cycle != 16) || !lanes || lanes->isEmpty() || lanes->size() > 12 ||
        std::none_of(plan.chordPalette.begin(), plan.chordPalette.end(), [&](const auto& c) { return c.id == chordId; })) {
        error = "Tonal nucleus needs an existing reference chord, bounded cycle and actual lanes"; return false;
    }
    std::set<std::string> owners;
    const auto gridNumber = [](const juce::var& v, double& n) {
        if (!(v.isInt() || v.isInt64() || v.isDouble())) return false;
        n = static_cast<double>(v);
        return std::isfinite(n) && std::abs(n * 4 - std::round(n * 4)) < .0001;
    };
    for (const auto& lane : *lanes) {
        const auto id = lane.getProperty("instrument_id", {}).toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& p) { return p.id == id; });
        const auto* notes = lane.getProperty("notes", {}).getArray();
        if (owner == plan.instruments.end() || !owners.insert(id).second || !notes || notes->isEmpty() || notes->size() > 128) {
            error = "Tonal nucleus has an invalid/duplicate owner or missing notes: " + juce::String(id); return false;
        }
        for (const auto& note : *notes) {
            double beat{}, duration{}, pitch{}, velocity{};
            if (!gridNumber(note.getProperty("beat", {}), beat) || !gridNumber(note.getProperty("duration", {}), duration) ||
                !gridNumber(note.getProperty("pitch", {}), pitch) || !gridNumber(note.getProperty("velocity", {}), velocity) ||
                beat < 0 || beat >= cycle || duration <= 0 || beat + duration > cycle + .0001 ||
                pitch != std::floor(pitch) || pitch < std::max(0, owner->minimumPitch) || pitch > std::min(127, owner->maximumPitch) ||
                velocity != std::floor(velocity) || velocity < 1 || velocity > 127 || !AiChromaticPolicy::allows(plan, static_cast<int>(pitch))) {
                error = "Tonal nucleus has invalid timing/register/scale in " + juce::String(id) +
                    ": " + juce::JSON::toString(note, true) + "; cycle=" + juce::String(cycle) +
                    " register=" + juce::String(owner->minimumPitch) + ".." + juce::String(owner->maximumPitch);
                return false;
            }
        }
    }
    const auto carrier = sketch.getProperty("meter_carrier_id", {}).toString();
    if (!owners.contains(carrier.toStdString())) {
        error = "Tonal nucleus meter carrier must have actual notes"; return false;
    }
    if (sketch.hasProperty("carrier_subdivision_beats")) {
        double unit{};
        if (!gridNumber(sketch.getProperty("carrier_subdivision_beats", {}), unit) ||
            (unit != .25 && unit != .5 && unit != 1 && unit != 2 && unit != 4)) {
            error = "Tonal nucleus has an invalid declared carrier subdivision"; return false;
        }
        std::set<double> attacks;
        for (const auto& lane : *lanes) if (lane.getProperty("instrument_id", {}).toString() == carrier)
            for (const auto& note : *lane.getProperty("notes", {}).getArray())
                attacks.insert(static_cast<double>(note.getProperty("beat", {})));
        auto previous = *attacks.rbegin() - cycle;
        for (const auto beat : attacks) {
            if (std::abs(beat - previous - unit) > .0001) {
                error = "Tonal nucleus carrier " + carrier + " declares attack spacing " + juce::String(unit) +
                    " beats but actual cyclic gap is " + juce::String(beat - previous) +
                    ". Recompose its reference notes to realize the intended subdivision. "
                    "Eighths are attacks every 0.5 beats, not merely notes with duration 0.5. "
                    "No code will insert or retime notes.";
                return false;
            }
            previous = beat;
        }
    }
    if (const auto* core = root.getProperty("groove_core_ids", {}).getArray())
        for (const auto& id : *core) if (!owners.contains(id.toString().toStdString())) {
            error = "Tonal nucleus omitted a declared groove core owner: " + id.toString(); return false;
        }
    return true;
}

// Translate the immutable harmony into the SAME time coordinates as the note
// response. This only describes context: it does not constrain/rewrite pitches.
inline juce::String harmonyWindow(const SongPlan& plan, double begin, double end) {
    std::vector<std::pair<double, std::string>> events;
    for (const auto& section : plan.sections) for (const auto& event : section.harmonicEvents)
        events.emplace_back((section.startBar + event.barOffset) * plan.beatsPerBar + event.beatOffset, event.chordId);
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    juce::String out = "\nWINDOW-LOCAL HARMONY (same beat coordinates as the output notes):\n";
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto from = std::max(begin, events[index].first);
        const auto to = std::min(end, index + 1 < events.size() ? events[index + 1].first : end);
        if (to <= from) continue;
        const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
            [&](const auto& c) { return c.id == events[index].second; });
        if (chord == plan.chordPalette.end()) continue;
        out << "local [" << juce::String(from - begin, 2) << "," << juce::String(to - begin, 2)
            << ") absolute [" << juce::String(from, 2) << "," << juce::String(to, 2)
            << ") chord=" << juce::String(chord->id) << " pitch_classes=";
        for (const auto pc : chord->pitchClasses) out << pc << " ";
        out << " bass_pc=" << chord->bassPitchClass << "\n";
    }
    return out;
}

inline juce::String writerBrief(const juce::var& root) {
    if (!root.hasProperty("tonal_nucleus")) return {};
    return "\nYOUR CONCRETE TONAL NUCLEUS (reference-cycle-local beats, NOT song time): " +
        juce::JSON::toString(root.getProperty("tonal_nucleus", {}), true) +
        "\nTreat this AI-composed groove as the identity to realize, not an optional adjective. "
        "At full-body states retain the recurring attacks, accents and interlock while YOU "
        "adapt actual pitches/voicings to the current harmonic map. During intro/withdrawal "
        "reveal or remove the planned lanes without replacing their surviving rhythm. "
        "Maintain the continuous sequence through complete cycles; do not randomly omit "
        "half its notes to make space. Separate the hook using register and melodic rests. "
        "For the declared meter carrier, a new technical window does not authorize changing "
        "eighths into offbeat quarters. Only an explicit arrangement-state withdrawal changes "
        "that role. Distinguish onset spacing, gate duration, and phrase entry: they are three "
        "different musical decisions. Connected eighth articulation is not automatically a "
        "sixteenth gate followed by a sixteenth rest. Choose gates to realize the stated "
        "articulation and keep it recognizable across windows. "
        "Keep chord beds sounding through intended held states and renew them on harmonic "
        "arrivals. A technical boundary is not a reason for silence or a new rhythmic phase. "
        "At local beat zero, if a sustained harmonic role continues, continue its harmonic "
        "support immediately; do not invent a 0.25-beat collective delay. Musical offbeats "
        "remain welcome when part of the declared groove, not as a workaround for boundaries. "
        "No code copies, transposes or expands this sketch. Write every score event explicitly.\n" +
        sequenceDevelopmentBrief(false) + ensembleSentenceBrief(false);
}

} // namespace pulso::plugin::club::nucleus
