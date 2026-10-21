#include "AiThematicSubject.h"
#include "AiCompleteDialogue.h"
#include "AiPhraseDevelopment.h"
#include "AiMaterialDialogue.h"
#include "AiGestureScore.h"
#include "core/Scale.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pulso::plugin {
bool AiThematicSubject::enabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", {}) == "1";
}
bool AiThematicSubject::rehearsalEnabled() {
    return enabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT", {}) == "1";
}
juce::String AiThematicSubject::rehearsalSchema(const SongPlan& plan,
    const std::vector<std::size_t>& owners) {
    auto schema = juce::JSON::parse(AiCompleteDialogue::schema(plan, owners));
    auto* properties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
    properties->setProperty("decision", juce::JSON::parse(
        R"json({"type":"string","enum":["keep","revise"]})json"));
    if (juce::SystemStats::getEnvironmentVariable("PULSO_AI_NARRATIVE_FOCUS", {}).trim().isNotEmpty())
        properties->setProperty("decision", juce::JSON::parse(
            R"json({"type":"string","enum":["revise"]})json"));
    properties->setProperty("diagnosis", juce::JSON::parse(R"json({"type":"string"})json"));
    juce::Array<juce::var> ids;
    for (const auto owner : owners) ids.add(juce::String::fromUTF8(plan.instruments[owner].id.c_str()));
    auto selected = juce::JSON::parse(R"json({"type":"array","items":{"type":"string"},
      "description":"Exact owners whose COMPLETE notes you replace. Empty for keep. Unlisted owners are immutable."})json");
    selected.getDynamicObject()->getProperty("items").getDynamicObject()->setProperty("enum", ids);
    selected.getDynamicObject()->setProperty("maxItems", static_cast<int>(owners.size()));
    properties->setProperty("revised_owner_ids", selected);
    schema.getDynamicObject()->getProperty("required").getArray()->add("decision");
    schema.getDynamicObject()->getProperty("required").getArray()->add("diagnosis");
    schema.getDynamicObject()->getProperty("required").getArray()->add("revised_owner_ids");
    if (AiGestureScore::enabled()) {
        auto keep = juce::JSON::parse(juce::JSON::toString(schema));
        auto revise = juce::JSON::parse(juce::JSON::toString(schema));
        auto kp = keep.getProperty("properties", {}), rp = revise.getProperty("properties", {});
        kp.getDynamicObject()->setProperty("decision", juce::JSON::parse(R"({"type":"string","enum":["keep"]})"));
        kp.getProperty("windows", {}).getDynamicObject()->setProperty("maxItems", 0);
        kp.getProperty("revised_owner_ids", {}).getDynamicObject()->setProperty("maxItems", 0);
        rp.getDynamicObject()->setProperty("decision", juce::JSON::parse(R"({"type":"string","enum":["revise"]})"));
        rp.getProperty("revised_owner_ids", {}).getDynamicObject()->setProperty("minItems", 1);
        int count = 0;
        for (const auto& block : AiCompleteDialogue::blocks(plan)) count += static_cast<int>(block.size());
        rp.getProperty("windows", {}).getDynamicObject()->setProperty("minItems", count);
        rp.getProperty("windows", {}).getDynamicObject()->setProperty("maxItems", count);
        juce::Array<juce::var> alternatives;
        if (juce::SystemStats::getEnvironmentVariable("PULSO_AI_NARRATIVE_FOCUS", {}).trim().isEmpty())
            alternatives.add(keep);
        alternatives.add(revise);
        auto wrapper = juce::JSON::parse(R"({"type":"object","additionalProperties":false,
          "required":["review"],"properties":{"review":{"anyOf":[]}}})");
        wrapper.getProperty("properties", {}).getProperty("review", {}).getDynamicObject()
            ->setProperty("anyOf", alternatives);
        return juce::JSON::toString(wrapper, true);
    }
    return juce::JSON::toString(schema, true);
}
juce::String AiThematicSubject::rehearsalPrompt(const juce::String& direction,
    const SongPlan& plan, const std::vector<std::size_t>& owners,
    const Pattern& actual) {
    std::vector<DialogueWindow> windows;
    for (const auto& block : AiCompleteDialogue::blocks(plan))
        windows.insert(windows.end(), block.begin(), block.end());
    auto out = AiCompleteDialogue::prompt(direction, plan, owners, windows, {}, false);
    const auto listenerFeedback = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_NARRATIVE_FOCUS", {}).trim().substring(0, 2000);
    if (listenerFeedback.isNotEmpty())
        out << "\nEXPLICIT LISTENER FEEDBACK FOR THIS EXPERIMENT: " << listenerFeedback
            << "\nThis is the artistic problem to address in the actual notes, "
            "not an invitation to defend the previous draft or explain its "
            "titles. Preserve what works and make a substantive, focused "
            "musical response to the feedback. Do not replace the work with "
            "another style or decorate it everywhere. This is a commissioned "
            "editing task, not a request for a grade: return decision=revise "
            "with the complete edited foreground, preserving successful passages. "
            "The proposed edit is a candidate for comparison, not proof of improvement.\n";
    const auto namedPitch = [](int pitch) {
        static constexpr const char* names[] = {"C", "C#", "D", "Eb", "E", "F",
            "F#", "G", "Ab", "A", "Bb", "B"};
        return juce::String(names[(pitch % 12 + 12) % 12]) + juce::String(pitch / 12 - 1);
    };
    const auto harmonicAt = [&](double beat) -> const HarmonicChord* {
        const HarmonicChord* result = nullptr;
        auto latest = -1.0;
        for (const auto& section : plan.sections)
            for (const auto& event : section.harmonicEvents) {
                const auto start = (section.startBar + event.barOffset) * plan.beatsPerBar +
                    event.beatOffset;
                if (start > beat + .001 || start < latest) continue;
                const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                    [&](const auto& item) { return item.id == event.chordId; });
                if (chord != plan.chordPalette.end()) { latest = start; result = &*chord; }
            }
        return result;
    };
    out << "\nREHEARSAL OF THE ACTUAL WHOLE FOREGROUND BELOW: You are now "
        "rehearsing an already composed draft, not composing another unrelated "
        "work. Judge the notes, not their section titles or the earlier plan's "
        "poetic promises. Does a listener have a complete, phrased thought to "
        "remember? Can the listener recognize its actual return, including "
        "continuation and rhythmic identity rather than only a few pitches? "
        "Do partners take a live implication forward, or merely add their own "
        "unrelated fragments? Does an instrument's sounding articulation carry "
        "each impulse, or make every consecutive note a disconnected syllable? "
        "Does the final utterance actually conclude that lived thought, with "
        "a proportionate landing and release rather than isolated last clues? "
        "Check each chromatic tension against the precise simultaneous harmony "
        "and its real destination. Chromaticism and dissonance are allowed by "
        "the requested style, but labels alone do not make an unexplained clash "
        "or accidental near reattack expressive.\n"
        "In an unsolicited review without listener feedback, choose keep with "
        "windows=[] and revised_owner_ids=[] if there is no substantive correction. For a commissioned "
        "edit with listener feedback, return the requested revision, not a grade. "
        "Use revisions to repair a concrete musical deficiency; explain "
        "it briefly in diagnosis with exact owner IDs and absolute beat spans. "
        "For revise explicitly list revised_owner_ids. Return EVERY listed window; "
        "for each owner in revised_owner_ids return its COMPLETE notes in that window, "
        "including unchanged passages. For ALL OTHER owners return []. These unlisted "
        "arrays mean UNCHANGED, not silence; the application preserves those owners "
        "exactly. An empty array for a LISTED owner is an intentional rest/replacement "
        "of that owner's notes in that window. This is a notes-only edit: existing "
        "controls are retained. Never list a successful owner merely to satisfy the "
        "schema or erase it accidentally. Do not infer owner selection from diagnosis. "
        "Do not change the form, harmony, cast, seed or style. Do not equate "
        "improvement with more ornaments, a new rhythm everywhere, uniformly "
        "longer notes, forced legato, or sparse music. Keep intentional rests, "
        "repetition, contrast and virtuosity that already work. Author only "
        "the corrections that make the existing musical argument more legible. "
        "Nothing in code will implement this diagnosis or alter your MIDI.\n"
        "Each source note below is annotated with its verified scientific pitch "
        "name (C4=60), pitch class and actual harmony at the attack. Where a "
        "pitch is outside BOTH the declared scale and the current chord, it is "
        "explicitly marked CHECK_FOREIGN. This is a fact to inspect, NOT an "
        "instruction to remove chromaticism. A prepared, resolving chromatic "
        "neighbor can be expressive; an unsupported held foreign third over "
        "its conflicting natural third is not made coherent by calling it "
        "Dorian colour. Verify real durations and destinations before deciding "
        "keep. If revising, correct pitches yourself, preserving the effective "
        "phrase. Do not change unrelated music to hide a local conflict.\n"
        "ACTUAL FULL FOREGROUND (absolute beat / MIDI pitch / sounding duration "
        "/ velocity; this is the draft to retain or revise):\n";
    for (const auto owner : owners) {
        out << juce::String::fromUTF8(plan.instruments[owner].id.c_str()) << ": ";
        for (const auto& note : actual.notes)
            if (note.partId == owner + 1) {
                const auto* chord = harmonicAt(note.startBeat);
                const auto inChord = chord && std::find(chord->pitchClasses.begin(),
                    chord->pitchClasses.end(), note.pitch % 12) != chord->pitchClasses.end();
                out << juce::String(note.startBeat, 2) << "/" << note.pitch << "/"
                    << juce::String(note.durationBeats, 2) << "/" << note.velocity
                    << " [" << namedPitch(note.pitch) << " pc=" << note.pitch % 12
                    << " harmony=" << (chord ? juce::String::fromUTF8(chord->id.c_str()) : "unknown")
                    << ((!inChord && !isPitchClassInScale(note.pitch % 12,
                        plan.rootPitchClass, plan.scale)) ? " CHECK_FOREIGN" : "") << "]; ";
            }
        out << "\n";
    }
    out << AiPhraseDevelopment::reviewBrief(plan, actual, owners);
    out << AiMaterialDialogue::reviewBrief();
    out << AiGestureScore::reviewEvidence(plan, actual, owners);
    if (AiGestureScore::enabled())
        out << "\nOUTPUT ENVELOPE: Return a single object with key review containing your decision, "
            "diagnosis, revised_owner_ids and windows. For keep: both arrays are []. For revise: "
            "include every listed window exactly once, even windows unchanged by the edit; each "
            "selected owner's complete notes are required in ALL windows, not only edited ones. "
            "Never return only the problematic windows, since that would omit the rest of those "
            "owners' music. Unselected owners use [] and remain immutable.\n";
    return out;
}
bool AiThematicSubject::applyScopedRehearsal(const juce::var& response, const SongPlan& plan,
    const std::vector<std::size_t>& available, const PerformanceScore& original,
    PerformanceScore& revised, juce::String& error) {
    const auto* ids = response.getProperty("revised_owner_ids", {}).getArray();
    std::set<std::string> replaced;
    std::vector<std::size_t> owners;
    if (!ids || ids->isEmpty() || response.getProperty("decision", {}).toString() != "revise") {
        error = "AI revision omitted its explicit owner scope"; return false;
    }
    for (const auto& id : *ids) {
        const auto owner = std::find_if(available.begin(), available.end(), [&](const auto index) {
            return index < plan.instruments.size() && id.isString() &&
                id.toString() == juce::String::fromUTF8(plan.instruments[index].id.c_str());
        });
        if (owner == available.end() || !replaced.insert(id.toString().toStdString()).second) {
            error = "AI revision selected an unavailable or duplicate owner"; return false;
        }
        owners.push_back(*owner);
    }
    auto filtered = juce::JSON::parse(juce::JSON::toString(response));
    auto* rows = filtered.getDynamicObject()->getProperty("windows").getArray();
    if (!rows) { error = "AI revision omitted its windows"; return false; }
    for (auto& row : *rows) {
        const auto parts = row.getProperty("parts", {});
        juce::var scoped(new juce::DynamicObject());
        for (const auto index : available) {
            const auto id = juce::String::fromUTF8(plan.instruments[index].id.c_str());
            const auto notes = parts.getProperty(juce::Identifier(id), {});
            if (!notes.isArray()) { error = "AI revision omitted an owner array"; return false; }
            if (replaced.contains(id.toStdString())) scoped.getDynamicObject()->setProperty(juce::Identifier(id), notes);
            else if (notes.size() != 0) {
                error = "AI revision supplied notes outside its declared owner scope"; return false;
            }
        }
        row.getDynamicObject()->setProperty("parts", scoped);
    }
    std::vector<DialogueWindow> windows;
    for (const auto& block : AiCompleteDialogue::blocks(plan))
        windows.insert(windows.end(), block.begin(), block.end());
    PerformanceScore replacement;
    if (!AiCompleteDialogue::parse(juce::JSON::toString(filtered), plan, owners,
                                  windows, replacement, error)) return false;
    // Lossless owner-scoped note replacement. No inferred pitches, times or rests.
    // Preserve controls and every note that the AI did not explicitly select.
    PerformanceScore candidate;
    std::set<std::string> retainedIds;
    for (const auto& source : original.cells) {
        auto cell = source;
        for (const auto& note : cell.notes)
            if (note.instrumentId.empty()) {
                error = "Scoped rehearsal requires explicitly owned source notes"; return false;
            }
        const auto before = cell.notes.size();
        std::erase_if(cell.notes, [&](const auto& note) { return replaced.contains(note.instrumentId); });
        if (before != cell.notes.size() && cell.notes.empty() && cell.controls.empty()) continue;
        retainedIds.insert(cell.id); candidate.cells.push_back(std::move(cell));
    }
    for (const auto& placement : original.placements)
        if (retainedIds.contains(placement.cellId)) candidate.placements.push_back(placement);
    std::map<std::string, std::string> mapping;
    for (auto& cell : replacement.cells) {
        auto id = std::string("scoped_rehearsal_") + cell.id;
        while (retainedIds.contains(id)) id = "_" + id;
        retainedIds.insert(id); mapping[cell.id] = id; cell.id = id;
        candidate.cells.push_back(std::move(cell));
    }
    for (auto& placement : replacement.placements) {
        placement.cellId = mapping.at(placement.cellId);
        candidate.placements.push_back(std::move(placement));
    }
    revised = std::move(candidate);
    return true;
}
juce::String AiThematicSubject::extendSchema(const juce::String& schema) {
    if (!enabled()) return schema;
    auto result = juce::JSON::parse(schema);
    auto* root = result.getDynamicObject();
    root->getProperty("properties").getDynamicObject()->setProperty("musical_subject",
        juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
          "required":["instrument_id","length_beats","notes","identity",
          "continuation","arrival","recognizable_return"],"properties":{
          "instrument_id":{"type":"string"},"length_beats":{"type":"number"},
          "notes":{"type":"array","minItems":1,"maxItems":96,"items":{
            "type":"object","additionalProperties":false,
            "required":["beat","duration","pitch","velocity"],"properties":{
              "beat":{"type":"number","minimum":0,"multipleOf":0.25},
              "duration":{"type":"number","minimum":0.25,"multipleOf":0.25},
              "pitch":{"type":"integer","minimum":0,"maximum":127},
              "velocity":{"type":"integer","minimum":1,"maximum":127}}}},
          "identity":{"type":"string"},"continuation":{"type":"string"},
          "arrival":{"type":"string"},"recognizable_return":{"type":"string"}}})json"));
    root->getProperty("required").getArray()->add("musical_subject");
    return AiPhraseDevelopment::extendSchema(juce::JSON::toString(result, true));
}
juce::String AiThematicSubject::planningBrief(const SongPlan& plan) {
    if (!enabled()) return {};
    juce::String out;
    out << "\nTHEMATIC SUBJECT STUDY: Compose a complete, memorable musical "
        "sentence in musical_subject, not just the first interval of a motif. "
        "It needs an audible beginning, a continuation that belongs to that "
        "beginning, and a locally comprehensible arrival. Let a performer "
        "phrase and remember the whole thought. Choose its span, rhythm, "
        "pitch contour and articulation for the user's style; no stock "
        "rhythm or number of bars is imposed. It is not a scale exercise "
        "or a handful of clues separated by unrelated decoration. "
        "instrument_id names an existing melodic speaker. Notes use SUBJECT-LOCAL "
        "beats, not a schedule of extra attacks for the finished score. Every "
        "note has your chosen sounding duration and velocity, on the existing "
        "quarter-beat grid, within length_beats. Fit the first exposition to "
        "the actual opening harmony listed below.\n"
        "Use this actual sentence when planning phrase relationships. In identity, "
        "name what makes the complete sentence recognizable, including rhythm "
        "and its important continuation, not only a three-note fingerprint. "
        "continuation and arrival describe what these actual notes accomplish. "
        "In recognizable_return, choose where listeners will hear a substantial "
        "return of that thought and why it matters after intervening music. "
        "The return may be literal, varied, reharmonized or expanded as appropriate; "
        "it must not become so transformed that only a prose explanation connects it. "
        "Repeating a complete phrase, retaining a pulse, or holding an accompanying "
        "figure can create meaning. Development is not a demand for permanent "
        "novelty. For a longer work, allow established thoughts time to inhabit "
        "their harmony before redirecting them. Do not introduce a new dramatic "
        "problem every few bars merely because more time was requested. "
        "All phrases still get their own AI-authored notes later; the renderer "
        "will not copy, transpose, extend or fill this reference.\n"
        "ACTUAL CHORD PITCH CLASSES (C=0; labels alone are not authority):\n";
    for (const auto& chord : plan.chordPalette) {
        out << juce::String::fromUTF8(chord.id.c_str()) << ": ";
        for (const auto pc : chord.pitchClasses) out << pc << ",";
        out << "\n";
    }
    out << AiPhraseDevelopment::planningBrief();
    return out;
}
juce::String AiThematicSubject::realizationBrief() {
    if (!enabled()) return {};
    if (AiPhraseDevelopment::enabled()) return AiPhraseDevelopment::realizationBrief();
    return "\nTHEMATIC REALIZATION: The musical_subject in the phrase plan is "
        "an actual complete AI-composed reference sentence in its own local "
        "time. Establish its recognizable thought early in the definitive MIDI, "
        "not just a distant intervallic resemblance. Its continuation, arrival "
        "and characteristic rhythm matter as much as its opening clue. "
        "Compose a substantial recognizable return where the form earns it. "
        "Listeners should recognize what has returned BEFORE hearing its "
        "changed consequence. Literal repetition, transposed statements, "
        "rhythmic steadiness and an unchanged accompaniment can be expressive; "
        "do not avoid them merely to demonstrate transformation. Retain enough "
        "of a complete thought that the answer is perceptible without reading "
        "the plan. Choose which voices are listeners or supports while another "
        "speaks. Independent counterpoint must be composed in relation to the "
        "actual phrase, not as simultaneous unrelated soloing. This reference "
        "is NOT an additional MIDI layer: author each final note once. Refine "
        "it where the actual harmony requires, preserving its audible identity. "
        "A final cadence must conclude the lived phrase, not just end a scale "
        "descent. No code will repeat this subject or create its variations.\n";
}
bool AiThematicSubject::validate(const juce::var& root, const SongPlan& plan,
    juce::String& error) {
    if (!enabled()) return true;
    const auto* subject = root.getProperty("musical_subject", {}).getDynamicObject();
    if (!subject) { error = "AI thematic plan omitted its complete reference subject"; return false; }
    const auto id = subject->getProperty("instrument_id").toString();
    const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& part) { return id == juce::String::fromUTF8(part.id.c_str()); });
    const auto value = subject->getProperty("length_beats");
    const auto length = static_cast<double>(value);
    if (owner == plan.instruments.end() || !(value.isInt() || value.isInt64() || value.isDouble()) ||
        !std::isfinite(length) || length <= 0 || length > plan.totalBars * plan.beatsPerBar) {
        error = "AI thematic subject has invalid owner or local duration"; return false;
    }
    for (const auto* key : {"identity", "continuation", "arrival", "recognizable_return"}) {
        const auto text = subject->getProperty(key);
        if (!text.isString() || text.toString().trim().isEmpty() || text.toString().length() > 2000) {
            error = "AI thematic subject has invalid " + juce::String(key); return false;
        }
    }
    const auto* notes = subject->getProperty("notes").getArray();
    if (!notes || notes->isEmpty() || notes->size() > 96) {
        error = "AI thematic subject has no bounded reference-note array"; return false;
    }
    for (const auto& note : *notes) {
        for (const auto* key : {"beat", "duration", "pitch", "velocity"}) {
            const auto x = note.getProperty(key, {});
            if (!(x.isInt() || x.isInt64() || x.isDouble()) || !std::isfinite(static_cast<double>(x))) {
                error = "AI thematic reference note is not numeric"; return false;
            }
        }
        const auto beat = static_cast<double>(note.getProperty("beat", {}));
        const auto duration = static_cast<double>(note.getProperty("duration", {}));
        const auto pitch = static_cast<double>(note.getProperty("pitch", {}));
        const auto velocity = static_cast<double>(note.getProperty("velocity", {}));
        if (beat < 0 || duration <= 0 || beat + duration > length + .001 ||
            std::abs(beat * 4 - std::round(beat * 4)) > .001 ||
            std::abs(duration * 4 - std::round(duration * 4)) > .001 ||
            pitch != std::floor(pitch) || pitch < std::max(0, owner->minimumPitch - 12) ||
            pitch > std::min(127, owner->maximumPitch + 12) ||
            velocity != std::floor(velocity) || velocity < 1 || velocity > 127) {
            error = "AI thematic subject has invalid subject-local note representation"; return false;
        }
    }
    return AiPhraseDevelopment::validate(root, plan, error);
}
}
