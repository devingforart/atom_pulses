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

inline juce::String localClubPreferences() {
    juce::String out;
    const auto pace = juce::SystemStats::getEnvironmentVariable("PULSO_LOCAL_CLUB_PACE", "auto");
    if (pace == "8-16" || pace == "16-32" || pace == "32-64")
        out << "\nLOCAL LAYER-PACE OVERRIDE: Prefer meaningful layer evolution over " << pace
            << " BARS. This replaces the default 8/16-bar layering preference, not note timing, "
               "tempo or the short groove cycle. Adapt to the work duration; do not stretch, "
               "repeat or schedule events algorithmically. Changes may mature earlier or later "
               "when the musical argument earns them.\n";
    if (juce::SystemStats::getEnvironmentVariable("PULSO_LOCAL_CLUB_VARIETY", {}) == "1")
        out << "\nLOCAL CLUB RESOURCE OVERRIDE: Choose the central musical resource for THIS work "
               "before selecting the cast. Earlier mentions of continuous arpeggio/sequencer "
               "are examples, NOT required tracks. The regular meter carrier may instead be "
               "a repeated bass tone, a chord pulse or a restrained pitched ostinato. A "
               "recognizable groove does not require continuous changing-pitch eighths. "
               "Choose an appropriate allowed subdivision, including quarters or slower "
               "attacks. Omit the arp entirely when another resource carries the identity, "
               "or introduce it only in selected states. Do not rename an uninterrupted arp "
               "as another instrument. Keep harmonic support and a clear tonal dance pulse. "
               "In the nucleus include the chosen carrier, not a compulsory sequence. "
               "Allow expressive non-core responses to use phrase_flexible timing; reserve "
               "cycle_bound for the truly invariant foundation so independent passages can "
               "add flavor without moving that foundation. Choose freely, not a random preset.\n";
    return out;
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
        "supplies a preset groove, copies a cell into the score, fills a pad or corrects pitches. " + localClubPreferences();
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
        "and its development itself. The host never tiles this sketch into MIDI.\n" + localClubPreferences();
}

inline bool validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
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
        "No code copies, transposes or expands this sketch. Write every score event explicitly.\n" + localClubPreferences();
}

} // namespace pulso::plugin::club::nucleus
