#include "AiMelodicDiscourse.h"
#include "core/Orchestration.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pulso::plugin {
namespace {
constexpr const char* textKeys[] = {
    "proposition", "continuation", "arrival", "connection", "dialogue"
};

bool number(const juce::var& value, double& out) {
    if (!(value.isInt() || value.isInt64() || value.isDouble())) return false;
    out = static_cast<double>(value);
    return std::isfinite(out);
}
} // namespace

bool AiMelodicDiscourse::enabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", {}) == "1";
}

juce::String AiMelodicDiscourse::extendSchema(const juce::String& schema) {
    if (!enabled()) return schema;
    auto result = juce::JSON::parse(schema);
    auto phrase = result.getProperty("properties", {}).getProperty("phrases", {})
        .getProperty("items", {});
    const auto delivery = juce::JSON::parse(R"json({
      "type":"object","additionalProperties":false,
      "required":["proposition","continuation","arrival","connection","dialogue","structural_tones"],
      "properties":{
        "proposition":{"type":"string"},"continuation":{"type":"string"},
        "arrival":{"type":"string"},"connection":{"type":"string"},
        "dialogue":{"type":"string"},
        "structural_tones":{"type":"array","maxItems":12,"items":{
          "type":"object","additionalProperties":false,
          "required":["beat","duration","pitch","purpose"],
          "properties":{"beat":{"type":"number"},"duration":{"type":"number"},
            "pitch":{"type":"integer"},"purpose":{"type":"string"}}}}
      }})json");
    phrase.getDynamicObject()->getProperty("properties").getDynamicObject()
        ->setProperty("delivery", delivery);
    phrase.getDynamicObject()->getProperty("required").getArray()->add("delivery");
    return juce::JSON::toString(result, true);
}

juce::String AiMelodicDiscourse::planningBrief(double bpm) {
    if (!enabled()) return {};
    juce::String out;
    out << "\nMELODIC DISCOURSE AND DELIVERY: Plan a sentence a performer can "
        "mean, not a motif label followed by decorative activity. In delivery, "
        "proposition names the concrete musical claim (interval, rhythm and "
        "harmonic implication); continuation explains what remains unfinished "
        "and how the line carries it to arrival. arrival names the musical "
        "destination and what it resolves or deliberately defers. connection "
        "describes sustained bearing tones, linked movement, real breath points "
        "and which occasional short notes belong to a longer impulse. dialogue "
        "identifies what an actual earlier voice said, what the next speaker "
        "retains or contradicts and where their arguments agree. Do not merely "
        "transpose the same cell, exchange isolated signals or alternate solos "
        "whose continuations have no consequence for one another.\n"
        "Sketch only the essential bearing tones in structural_tones for this "
        "phrase's speaker. These are YOUR chosen MIDI pitches, onsets, durations "
        "and purposes, not a stock pattern. beat is relative to the PHRASE "
        "start, not the section; durations are beats, on the existing quarter-beat "
        "grid, and releases stay within this phrase. This is a selective sketch, "
        "not the full ornamented line: do not fill the available twelve slots. "
        "Let the few tones needed for the thought actually carry its weight "
        "and give its destination enough time to be heard. Choose the count "
        "and durations yourself; no minimum note length or density is imposed. "
        "At " << juce::String(bpm, 1) << " BPM, one beat lasts "
        << juce::String(60.0 / std::max(1.0, bpm), 3)
        << " seconds. A quarter-beat ornament lasts only "
        << juce::String(15.0 / std::max(1.0, bpm), 3)
        << " seconds: it cannot bear the same perceptual argument as a sustained "
        "melodic statement. Let speed be an earned local gesture with preparation "
        "and a destination, not the default way to create intensity. Connected "
        "does not mean motionless, permanently legato or uniformly slow.\n";
    out << "Unresolved does not mean unwritten. Even while the GLOBAL harmonic "
        "question remains open, a local melodic sentence needs a meaningful "
        "continuation and provisional arrival. Do not describe a tiny call, "
        "several bars of absence and an unrelated late held pitch as a complete "
        "developed sentence. A protagonist may genuinely yield the floor, but "
        "the partner must continue the specific argument in audible notes. "
        "Long suspense must be composed as a related line or a deliberately "
        "sustained harmonic tension, not explained afterwards with poetic labels. "
        "Sketch the continuation and locally felt destination as well as the "
        "initial clue. Let the next voice inherit a phrase's live implication, "
        "not merely its instrumentation or a repeated three-note signature. "
        "Keep each delivery field a concise musical instruction; do not repeat "
        "the global story in every field. Put concrete bearing pitches and "
        "durations in the sketch rather than substituting more prose.\n";
    return out;
}

int AiMelodicDiscourse::planningOutputTokenBudget(int sections) {
    // A ceiling, not a target: unused output capacity is not generated. Richer
    // phrase representation needs headroom without altering musical content.
    return std::min(20000, 6500 + std::clamp(sections, 0, 64) * (enabled() ? 1200 : 360));
}

juce::String AiMelodicDiscourse::macroBrief() {
    if (!enabled()) return {};
    return " MELODIC DISCOURSE PRIORITY: Design a story that can be heard through "
        "complete interacting melodic sentences. A short motivic clue seeds a "
        "line; it is not the entire line. Let local statements reach meaningful "
        "provisional destinations even while the global harmonic question stays "
        "open. Avoid making the whole narrative depend on withholding one last "
        "note of a tiny fragment until the final scene. Development needs an "
        "actual continuation, a related counterargument and an encounter that "
        "changes their meaning. Do not equate tension or virtuosity "
        "with clipped attacks everywhere, constant angular novelty or unconnected "
        "short signals. Give expressive lines time to be perceived and allow "
        "intensity through harmonic direction, bearing-tone duration, register "
        "and the interaction of voices, not only rhythmic compression. Keep the "
        "requested musical character and freedom; no prescribed note lengths "
        "or formal template are requested. Do not output MIDI in this macro phase.";
}

std::vector<std::vector<std::size_t>> AiMelodicDiscourse::writingGroups(
    const SongPlan& plan, const std::vector<std::size_t>& owners) {
    if (!enabled()) return {owners};
    std::set<std::string> speakers;
    const auto architecture = juce::JSON::parse(
        juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (const auto* phrases = architecture.getProperty("phrases", {}).getArray())
        for (const auto& phrase : *phrases)
            for (const auto* field : {"speaker_id", "response_id"}) {
                const auto id = phrase.getProperty(field, {}).toString().toStdString();
                if (!id.empty()) speakers.insert(id);
            }
    std::set<std::size_t> selected;
    for (const auto owner : owners)
        if (owner < plan.instruments.size() &&
            (speakers.contains(plan.instruments[owner].id) ||
             plan.instruments[owner].id == plan.narrativeSpine.protagonistInstrumentId))
            selected.insert(owner);
    if (selected.empty()) return {owners};
    // Keep a real chordal/bass context with the AI-chosen dialogue. Remaining
    // colours are written against actual accepted notes, not competing drafts.
    for (const auto family : {VoiceFamily::Harmony, VoiceFamily::Bass})
        for (const auto owner : owners)
            if (owner < plan.instruments.size() &&
                (family == VoiceFamily::Harmony
                    ? plan.instruments[owner].sourceVoice == VoiceId::HarmonicFoundation
                    : isVoiceInFamily(plan.instruments[owner].sourceVoice, family))) {
                selected.insert(owner);
                break;
            }
    std::vector<std::size_t> dialogue, companions;
    for (const auto owner : owners)
        (selected.contains(owner) ? dialogue : companions).push_back(owner);
    if (companions.empty()) return {owners};
    return {dialogue, companions};
}

bool AiMelodicDiscourse::validate(const juce::var& phrase, double beats,
    int minimumPitch, int maximumPitch, juce::String& error) {
    const auto* root = phrase.getDynamicObject();
    if (root == nullptr || !root->hasProperty("delivery")) return true; // Legacy plans.
    const auto* delivery = root->getProperty("delivery").getDynamicObject();
    if (delivery == nullptr) { error = "AI delivery sketch is not an object"; return false; }
    for (const auto* key : textKeys) {
        const auto value = delivery->getProperty(key);
        if (!value.isString() || value.toString().trim().isEmpty() ||
            value.toString().length() > 2000) {
            error = "AI delivery sketch has an invalid " + juce::String(key);
            return false;
        }
    }
    const auto* tones = delivery->getProperty("structural_tones").getArray();
    if (tones == nullptr || tones->size() > 12) {
        error = "AI delivery sketch has an invalid bearing-tone list"; return false;
    }
    for (const auto& tone : *tones) {
        double beat{}, duration{}, pitch{};
        const auto* obj = tone.getDynamicObject();
        if (obj == nullptr || !number(obj->getProperty("beat"), beat) ||
            !number(obj->getProperty("duration"), duration) ||
            !number(obj->getProperty("pitch"), pitch) ||
            beat < 0 || duration <= 0 || beat + duration > beats + .001 ||
            std::abs(beat * 4 - std::round(beat * 4)) > .001 ||
            std::abs(duration * 4 - std::round(duration * 4)) > .001 ||
            pitch != std::floor(pitch) || pitch < minimumPitch || pitch > maximumPitch ||
            !obj->getProperty("purpose").isString() ||
            obj->getProperty("purpose").toString().trim().isEmpty() ||
            obj->getProperty("purpose").toString().length() > 1000) {
            error = "AI bearing tone has invalid phrase-local timing, register or purpose";
            return false;
        }
    }
    return true;
}

juce::String AiMelodicDiscourse::phraseBrief(const juce::var& phrase,
    double phraseStart, double windowStart, double windowEnd, double bpm) {
    const auto* delivery = phrase.getProperty("delivery", {}).getDynamicObject();
    if (delivery == nullptr) return {};
    juce::String out;
    out << "  AI DELIVERY: ";
    for (const auto* key : textKeys)
        out << key << "=" << delivery->getProperty(key).toString() << "; ";
    out << "\n  AI-SKETCHED BEARING TONES (WINDOW-LOCAL onset/duration/pitch; "
        "not renderer-generated notes): ";
    if (const auto* tones = delivery->getProperty("structural_tones").getArray())
        for (int index = 0; index < tones->size(); ++index) {
            const auto& tone = (*tones)[index];
            const auto beat = phraseStart + static_cast<double>(tone.getProperty("beat", 0));
            const auto duration = static_cast<double>(tone.getProperty("duration", 0));
            if (beat >= windowEnd - .001 || beat + duration <= windowStart + .001) continue;
            out << juce::String(beat - windowStart, 2) << "/"
                << juce::String(duration, 2) << "/"
                << tone.getProperty("pitch", 0).toString() << " ("
                << juce::String(duration * 60.0 / std::max(1.0, bpm), 2) << " s; "
                << tone.getProperty("purpose", {}).toString();
            if (index > 0)
                out << "; interval from previous sketched tone "
                    << static_cast<int>(tone.getProperty("pitch", 0)) -
                       static_cast<int>((*tones)[index - 1].getProperty("pitch", 0))
                    << " semitones";
            out << "); ";
        }
    out << "\n";
    return out;
}

juce::String AiMelodicDiscourse::realizationBrief(double bpm) {
    if (!enabled()) return {};
    juce::String out;
    out << "\nMELODIC DELIVERY PRIORITY: The user needs audible, connected "
        "musical arguments, not a succession of clipped motifs. Realize the AI "
        "delivery sketch in actual note-on/note-off decisions. Bearing tones "
        "must remain perceptible as the line's subject, continuation and arrival; "
        "decorations belong to that sentence, not between every important note. "
        "Do not mechanically render a sketch and fill its gaps. You may refine "
        "your earlier sketch when the actual ensemble calls for it, but retain "
        "its musical proposition and consequence. No code will lengthen, delete "
        "or join your notes afterwards.\n"
        "Compose the duration of the SOUND as deliberately as the next attack. "
        "For connected motion, let each note carry the impulse toward its "
        "successor instead of routinely ending halfway to it. Put breaths at "
        "musical punctuation rather than after every small cell. Articulated "
        "and rapid gestures are available when they have a clear preparation, "
        "accent hierarchy and destination. Do not solve this by making all notes "
        "long, using one rhythm everywhere, removing individuality or adding "
        "arbitrary rubato. At this tempo a quarter beat is "
        << juce::String(15.0 / std::max(1.0, bpm), 3)
        << " seconds; ask which notes a listener can actually retain.\n"
        "A responding voice must transform a SPECIFIC heard interval, rhythm, "
        "suspension or unanswered destination from its partner, then contribute "
        "its own continuation. Coordinate phrase accents and arrival points "
        "while allowing independent inner rhythm. The foreground cannot always "
        "be a nervous stream against slow unrelated harmony. Intensify by "
        "direction, harmonic pressure, register and meaningful delayed release "
        "as well as by speed. Read the actual voice memory: do not restart the "
        "relationship at each scene or substitute an unrelated closing tag.\n";
    return out;
}

juce::String AiMelodicDiscourse::actualVoiceMemory(const SongPlan& plan,
    const Pattern& accepted, double windowStart) {
    if (!enabled() || windowStart <= 0) return {};
    juce::String out;
    out << "\nACTUAL MELODIC DELIVERY MEMORY (absolute beat/pitch/sounding "
        "duration/velocity; your earlier MIDI, not prose intentions):\n";
    for (std::size_t owner = 0; owner < plan.instruments.size(); ++owner) {
        const auto& part = plan.instruments[owner];
        if (!isVoiceInFamily(part.sourceVoice, VoiceFamily::Melodic) &&
            part.id != plan.narrativeSpine.protagonistInstrumentId) continue;
        std::vector<const NoteEvent*> notes;
        for (const auto& note : accepted.notes)
            if (note.partId == owner + 1 && note.startBeat < windowStart)
                notes.push_back(&note);
        std::stable_sort(notes.begin(), notes.end(), [](auto* a, auto* b) {
            return a->startBeat < b->startBeat;
        });
        out << juce::String::fromUTF8(part.id.c_str()) << ": ";
        std::set<std::size_t> selected;
        for (std::size_t n = 0; n < std::min<std::size_t>(4, notes.size()); ++n) selected.insert(n);
        for (auto n = notes.size() > 8 ? notes.size() - 8 : 0; n < notes.size(); ++n) selected.insert(n);
        for (const auto n : selected) {
            const auto& note = *notes[n];
            out << juce::String(note.startBeat, 2) << "/" << note.pitch << "/"
                << juce::String(note.durationBeats, 2) << "/" << note.velocity << "; ";
        }
        if (notes.empty()) out << "has not spoken yet";
        out << "\n";
    }
    return out;
}

} // namespace pulso::plugin
