#include "AiEnsembleDiscourse.h"
#include "AiCompleteDialogue.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pulso::plugin {
namespace {
bool text(const juce::var& value) {
    return value.isString() && value.toString().trim().isNotEmpty() &&
        value.toString().length() <= 1200;
}
bool time(const juce::var& value, double& result) {
    if (!(value.isInt() || value.isInt64() || value.isDouble())) return false;
    result = static_cast<double>(value);
    return std::isfinite(result) &&
        std::abs(result * 4 - std::round(result * 4)) < .001;
}
bool owner(const SongPlan& plan, const juce::var& value, bool allowEmpty = false) {
    if (!value.isString()) return false;
    const auto id = value.toString();
    return (allowEmpty && id.isEmpty()) || std::any_of(plan.instruments.begin(),
        plan.instruments.end(), [&](const auto& part) {
            return id == juce::String::fromUTF8(part.id.c_str());
        });
}
} // namespace

bool AiEnsembleDiscourse::enabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT", {}) == "1" || prosodyEnabled();
}
bool AiEnsembleDiscourse::prosodyEnabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_PHRASE_PROSODY_EXPERIMENT", {}) == "1";
}

juce::String AiEnsembleDiscourse::extendSchema(const juce::String& schema) {
    if (!enabled()) return schema;
    auto parsed = juce::JSON::parse(schema);
    auto phrase = parsed.getProperty("properties", {}).getProperty("phrases", {})
        .getProperty("items", {});
    const auto spans = juce::JSON::parse(R"json({
      "type":"array","maxItems":16,"items":{
        "type":"object","additionalProperties":false,
        "required":["beat","duration","foreground_ids","pulse_carrier_id",
          "listening_thread","arrival_beat","arrival_goal","parts"],
        "properties":{
          "beat":{"type":"number"},"duration":{"type":"number"},
          "foreground_ids":{"type":"array","maxItems":70,"items":{"type":"string"}},
          "pulse_carrier_id":{"type":"string"},
          "listening_thread":{"type":"string"},
          "arrival_beat":{"type":"number"},"arrival_goal":{"type":"string"},
          "parts":{"type":"array","maxItems":70,"items":{
            "type":"object","additionalProperties":false,
            "required":["instrument_id","action","attack_relation"],
            "properties":{"instrument_id":{"type":"string"},
              "action":{"type":"string"},"attack_relation":{"type":"string"}}}}
        }}})json");
    if (prosodyEnabled()) {
        auto* item = spans.getDynamicObject()->getProperty("items").getDynamicObject();
        auto* properties = item->getProperty("properties").getDynamicObject();
        // One explicit span-relative arrival origin in the new protocol. Keep
        // legacy arrival_beat readable; never guess its origin or rewrite MIDI.
        properties->removeProperty("arrival_beat");
        properties->setProperty("arrival_offset", juce::JSON::parse(R"json({"type":"number","minimum":0,
          "description":"Beat offset from THIS ensemble_span's start. Must be between zero and this span's duration."})json"));
        auto* required = item->getProperty("required").getArray();
        for (int i = 0; i < required->size(); ++i)
            if ((*required)[i].toString() == "arrival_beat") required->set(i, "arrival_offset");
        properties->setProperty("joint_voice_ids", juce::JSON::parse(
            R"({"type":"array","maxItems":70,"items":{"type":"string"}})"));
        properties->setProperty("rhythmic_identity", juce::JSON::parse(R"({"type":"string"})"));
        properties->setProperty("articulation_intent", juce::JSON::parse(R"({"type":"string"})"));
        properties->setProperty("metric_supports", juce::JSON::parse(R"json({
          "type":"array","maxItems":16,"items":{"type":"object","additionalProperties":false,
          "required":["beat","function"],"properties":{"beat":{"type":"number",
          "description":"Zero-based SPAN-LOCAL beat relative to this ensemble_span start, not phrase-local or absolute."},
          "function":{"type":"string"}}}})json"));
        for (const auto* field : {"joint_voice_ids", "rhythmic_identity", "articulation_intent", "metric_supports"})
            item->getProperty("required").getArray()->add(field);
    }
    phrase.getDynamicObject()->getProperty("properties").getDynamicObject()
        ->setProperty("ensemble_spans", spans);
    phrase.getDynamicObject()->getProperty("required").getArray()->add("ensemble_spans");
    return juce::JSON::toString(parsed, true);
}

juce::String AiEnsembleDiscourse::planningBrief() {
    if (!enabled()) return {};
    juce::String out;
    if (prosodyEnabled())
        out << "\nPHRASE PROSODY: Choose joint_voice_ids for each ensemble span: "
            "the pitched voices whose actual rhythmic relationship must be composed "
            "together, including a moving bass, ostinato or inner voice when its "
            "attacks shape the heard phrase. These exact cast IDs determine the "
            "joint writing relationships; do not list only the lead by habit. Do not list "
            "every sustained harmonic part merely to fill the roster. An empty "
            "list is legitimate when no coordinated moving relationship is needed. "
            "In rhythmic_identity describe a recognizable grouping/subdivision, "
            "accent and continuation for THIS musical thought. A technical "
            "sixteenth grid is not an instruction to introduce sixteenth offsets. "
            "Use metric_supports sparingly for meaningful supports, anticipation "
            "or release, with beat SPAN-LOCAL and function explaining why a listener "
            "hears it. No MIDI note is emitted from these supports; the actual "
            "writer may refine them rather than append obligatory attacks. An "
            "empty list is available for free/floating phrasing. Articulation_intent "
            "describes connections within the sentence versus its true breaths. "
            "Do not make accompaniment answer every small gap, or make all voices "
            "follow the same scale a fraction of a beat apart. Retain deliberate "
            "syncopation, counterpoint, virtuosity and rhythmic steadiness where "
            "musically useful; choose the relationships rather than banning them.\n";
    out << "\nENSEMBLE DISCOURSE STUDY: Plan what a listener follows, not just "
        "what each instrument could play. Add ensemble_spans to each phrase: "
        "a few meaningful listening spans, not a grid of events. beat and duration "
        "use PHRASE-LOCAL beats on the existing quarter-beat representation. ";
    if (prosodyEnabled())
        out << "arrival_offset is SPAN-LOCAL: zero is this span's own start, "
            "and the offset cannot exceed this span's duration. Do not put an "
            "absolute or phrase-local time in arrival_offset. ";
    else
        out << "arrival_beat is PHRASE-LOCAL; the arrival belongs inside its "
            "span and may be its end. ";
    out <<
        "foreground_ids identifies the heard speaker or interacting speakers; "
        "an empty list can describe a deliberate ensemble breath. Choose "
        "pulse_carrier_id from the cast, or empty when no one carries a pulse. "
        "listening_thread identifies the specific remembered pitch/rhythm claim "
        "that continues through this span. arrival_goal describes the actual "
        "melodic/harmonic consequence at the stated arrival, not merely 'more tension'. "
        "In parts, give the existing instruments whose coordination matters "
        "their action and attack_relation to the heard line. Keep fields concise. "
        "Do not fill all schema slots or schedule an event for every bar.\n"
        "Independent voices need a shared prosody: decide which accented notes "
        "are the sentence's claims, which motion leads into them, where a "
        "partner answers and how the ensemble recognizes an arrival. A flowing "
        "counterpoint may overlap throughout; it need not become alternating "
        "solos or homorhythm. If several voices move quickly together, explain "
        "the heard relationship (a specific imitation, contrary continuation, "
        "common accent or other intelligible connection), instead of giving "
        "every instrument an unrelated ornamental rhythm. A foreground run "
        "does not obligate the bass, inner voice and colour to add runs too. "
        "They may retain a harmonically rich body while the moving line speaks. "
        "Do not confuse a sustained accompaniment with absent musical content. "
        "Choose natural motion and stillness for this work; there is no maximum "
        "number of moving voices, fixed rhythm or enforced silence. Do not "
        "prescribe fewer instruments or simplify away the requested character.\n";
    return out;
}

juce::String AiEnsembleDiscourse::realizationBrief() {
    if (!enabled()) return {};
    return "\nENSEMBLE LISTENING PRIORITY: Read the AI-authored ensemble spans "
        "as one evolving argument. You still author every note, duration, rest "
        "and accent. Make the foreground's important tones distinguishable from "
        "its approach notes in timing, duration, register and dynamics. A "
        "virtuosic passage needs an impulse and a destination, not a new unrelated "
        "turn after each small cell. Maintain a heard pulse and metrical weight "
        "through syncopation; being on the technical grid alone is not prosody. "
        "Retain recognizable rhythmic hinges when a thought returns, letting "
        "its continuation change rather than replacing every rhythm at once.\n"
        "Companion writing must hear the already accepted foreground attacks "
        "and held tones. Author their entrances, preparation and release around "
        "that actual sentence. Independent does not mean equally foregrounded "
        "or constantly moving: a register change, sustained inner tension or "
        "prepared suspension can be a complete contribution. Related overlaps "
        "and rich contrapuntal writing remain available. Avoid assembling "
        "several individually plausible but unrelated streams of passing notes. "
        "When two voices share a pitch, choose whether a shared onset, intentional "
        "echo or distinct register makes their argument audible; do not "
        "accidentally stutter a held melody through a companion's near reattack. "
        "Judge the combined score as well as the separate timbres, since the "
        "listener may audition the whole MIDI with one piano. No code will "
        "thin the texture, coordinate accents or remove repetitions afterwards.\n";
}

bool AiEnsembleDiscourse::validate(const juce::var& phrase, double beats,
    const SongPlan& plan, juce::String& error) {
    const auto* object = phrase.getDynamicObject();
    if (object == nullptr || !object->hasProperty("ensemble_spans")) return true;
    const auto* spans = object->getProperty("ensemble_spans").getArray();
    if (spans == nullptr || spans->size() > 16) {
        error = "Invalid AI ensemble span list"; return false;
    }
    for (const auto& span : *spans) {
        double start{}, duration{}, arrival{};
        if (!span.isObject() || !time(span.getProperty("beat", {}), start) ||
            !time(span.getProperty("duration", {}), duration) ||
            !time(span.getProperty(span.hasProperty("arrival_offset") ? "arrival_offset" : "arrival_beat", {}), arrival) ||
            start < 0 ||
            duration <= 0 || start + duration > beats + .001 ||
            arrival < (span.hasProperty("arrival_offset") ? 0.0 : start) ||
            arrival > (span.hasProperty("arrival_offset") ? duration : start + duration) + .001 ||
            !text(span.getProperty("listening_thread", {})) ||
            !text(span.getProperty("arrival_goal", {})) ||
            !owner(plan, span.getProperty("pulse_carrier_id", {}), true)) {
            error = "AI ensemble span has invalid phrase-local timing or context"; return false;
        }
        const auto* ids = span.getProperty("foreground_ids", {}).getArray();
        const auto* parts = span.getProperty("parts", {}).getArray();
        if (ids == nullptr || parts == nullptr || ids->size() > 70 || parts->size() > 70) {
            error = "Invalid AI ensemble ownership list"; return false;
        }
        for (const auto& id : *ids)
            if (!owner(plan, id)) {
                error = "AI ensemble foreground has an unknown instrument: " + id.toString();
                return false;
            }
        // Multiple musical instructions for one owner are legitimate context,
        // not duplicated MIDI. Do not reject a whole plan for a repeated label.
        for (const auto& part : *parts)
            if (!part.isObject() || !owner(plan, part.getProperty("instrument_id", {})) ||
                !text(part.getProperty("action", {})) ||
                !text(part.getProperty("attack_relation", {}))) {
                error = "AI ensemble part has invalid identity or musical instruction: " +
                    part.getProperty("instrument_id", {}).toString(); return false;
            }
        if (prosodyEnabled() && span.hasProperty("joint_voice_ids")) {
            const auto* joint = span.getProperty("joint_voice_ids", {}).getArray();
            const auto* supports = span.getProperty("metric_supports", {}).getArray();
            if (joint == nullptr || joint->size() > 70 || supports == nullptr || supports->size() > 16 ||
                !text(span.getProperty("rhythmic_identity", {})) ||
                !text(span.getProperty("articulation_intent", {}))) {
                error = "Invalid AI phrase-prosody representation"; return false;
            }
            for (const auto& id : *joint) if (!owner(plan, id)) {
                error = "Unknown joint phrase owner: " + id.toString(); return false;
            }
            for (const auto& support : *supports) {
                double beat{};
                if (!support.isObject() || !time(support.getProperty("beat", {}), beat) ||
                    beat < 0 || beat > duration || !text(support.getProperty("function", {}))) {
                    error = "Invalid span-local prosodic support"; return false;
                }
            }
        }
    }
    return true;
}

std::set<std::string> AiEnsembleDiscourse::foregroundIds(const SongPlan& plan) {
    std::set<std::string> ids;
    if (!enabled()) return ids;
    const auto parsed = juce::JSON::parse(
        juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (const auto* phrases = parsed.getProperty("phrases", {}).getArray())
        for (const auto& phrase : *phrases)
            if (const auto* spans = phrase.getProperty("ensemble_spans", {}).getArray())
                for (const auto& span : *spans)
                    if (const auto* foreground = span.getProperty("foreground_ids", {}).getArray())
                        for (const auto& id : *foreground)
                            if (owner(plan, id)) ids.insert(id.toString().toStdString());
    return ids;
}

std::set<std::string> AiEnsembleDiscourse::jointVoiceIds(const SongPlan& plan) {
    std::set<std::string> result;
    if (!prosodyEnabled()) return result;
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (const auto* phrases = parsed.getProperty("phrases", {}).getArray())
        for (const auto& phrase : *phrases)
            if (const auto* spans = phrase.getProperty("ensemble_spans", {}).getArray())
                for (const auto& span : *spans)
                    if (const auto* ids = span.getProperty("joint_voice_ids", {}).getArray())
                        for (const auto& id : *ids)
                            if (owner(plan, id)) result.insert(id.toString().toStdString());
    return result;
}

juce::String AiEnsembleDiscourse::actualPhraseMemory(const SongPlan& plan,
    const Pattern& accepted, double windowStart) {
    if ((!enabled() && !AiCompleteDialogue::enabled()) || windowStart <= 0) return {};
    const auto parsed = juce::JSON::parse(
        juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    const auto* phrases = parsed.getProperty("phrases", {}).getArray();
    if (phrases == nullptr) return {};
    juce::String out;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto id = juce::String::fromUTF8(plan.instruments[index].id.c_str());
        auto first = windowStart;
        auto last = 0.0;
        for (const auto& phrase : *phrases) {
            if (phrase.getProperty("speaker_id", {}).toString() != id &&
                phrase.getProperty("response_id", {}).toString() != id) continue;
            const auto section = static_cast<int>(phrase.getProperty("section_index", -1));
            if (section < 0 || section >= static_cast<int>(plan.sections.size())) continue;
            const auto start = (plan.sections[static_cast<std::size_t>(section)].startBar +
                static_cast<int>(phrase.getProperty("local_start_bar", 0))) * plan.beatsPerBar;
            const auto end = start + static_cast<int>(phrase.getProperty("bars", 0)) *
                plan.beatsPerBar;
            if (end <= windowStart + .001 && end > start &&
                (start < first || (std::abs(start - first) < .001 && end < last))) {
                first = start;
                last = end;
            }
        }
        if (last <= first) continue;
        std::vector<const NoteEvent*> notes;
        for (const auto& note : accepted.notes)
            if (note.partId == index + 1 && note.startBeat < last &&
                note.endBeat() > first && note.startBeat < windowStart)
                notes.push_back(&note);
        std::stable_sort(notes.begin(), notes.end(), [](auto* left, auto* right) {
            return left->startBeat < right->startBeat;
        });
        if (notes.empty()) continue;
        if (out.isEmpty())
            out << "\nACTUAL EARLIEST COMPLETED PHRASE MEMORY: These are the "
                "accepted original statements, not new notes or a repetition "
                "template. Read their continuation, pulse, held tones and "
                "destination as well as their first clue. Decide what the "
                "current argument inherits or changes. Absolute beat/pitch/"
                "sounding duration/velocity; notes are not clipped to phrase bounds.\n";
        out << id << " phrase=" << juce::String(first, 2) << ".."
            << juce::String(last, 2) << "; "
            << (notes.size() <= 96 ? "complete actual notes: " :
                "bounded first/last 48-note context from ") ;
        if (notes.size() > 96) out << static_cast<int>(notes.size()) << " notes: ";
        for (std::size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
            if (notes.size() > 96 && noteIndex >= 48 && noteIndex < notes.size() - 48) continue;
            const auto& note = *notes[noteIndex];
            out << juce::String(note.startBeat, 2) << "/" << note.pitch << "/"
                << juce::String(note.durationBeats, 2) << "/" << note.velocity << "; ";
        }
        out << "\n";
    }
    return out;
}

juce::String AiEnsembleDiscourse::phraseBrief(const juce::var& phrase,
    double phraseStart, double windowStart, double windowEnd) {
    if (!enabled()) return {};
    const auto* spans = phrase.getProperty("ensemble_spans", {}).getArray();
    if (spans == nullptr) return {};
    juce::String out;
    for (const auto& span : *spans) {
        const auto start = phraseStart + static_cast<double>(span.getProperty("beat", 0));
        const auto end = start + static_cast<double>(span.getProperty("duration", 0));
        if (start >= windowEnd - .001 || end <= windowStart + .001) continue;
        out << "  AI ENSEMBLE SPAN (WINDOW-LOCAL beats, full intention not clipped): "
            << juce::String(start - windowStart, 2) << ".."
            << juce::String(end - windowStart, 2) << " foreground="
            << juce::JSON::toString(span.getProperty("foreground_ids", {}), true)
            << " pulse_carrier=" << span.getProperty("pulse_carrier_id", {}).toString()
            << " thread=" << span.getProperty("listening_thread", {}).toString()
            << " arrival=" << juce::String(phraseStart +
                (span.hasProperty("arrival_offset") ? static_cast<double>(span.getProperty("beat", 0)) : 0.0) +
                static_cast<double>(span.getProperty(span.hasProperty("arrival_offset") ? "arrival_offset" : "arrival_beat", 0)) - windowStart, 2)
            << " goal=" << span.getProperty("arrival_goal", {}).toString() << "\n";
        if (const auto* parts = span.getProperty("parts", {}).getArray())
            for (const auto& part : *parts)
                out << "    " << part.getProperty("instrument_id", {}).toString()
                    << ": " << part.getProperty("action", {}).toString()
                    << " | attack relation=" << part.getProperty("attack_relation", {}).toString()
                    << "\n";
        if (prosodyEnabled())
            out << "    PHRASE PROSODY: joint=" << juce::JSON::toString(span.getProperty("joint_voice_ids", {}), true)
                << " identity=" << span.getProperty("rhythmic_identity", {}).toString()
                << " articulation=" << span.getProperty("articulation_intent", {}).toString()
                << " span-local supports (intentions, not extra notes)="
                << juce::JSON::toString(span.getProperty("metric_supports", {}), true) << "\n";
    }
    return out;
}

} // namespace pulso::plugin
