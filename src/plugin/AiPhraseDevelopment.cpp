#include "AiPhraseDevelopment.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pulso::plugin {
bool AiPhraseDevelopment::enabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT", {}) == "1";
}
bool AiPhraseDevelopment::groundworkEnabled() {
    return enabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT", {}) == "1";
}
bool AiPhraseDevelopment::consequentialEnabled() {
    return groundworkEnabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT", {}) == "1";
}
juce::String AiPhraseDevelopment::developmentStrategies(const juce::String& blueprint) {
    if (!consequentialEnabled()) return {};
    juce::Array<juce::var> strategies;
    const auto root = juce::JSON::parse(blueprint);
    if (const auto* sections = root.getProperty("sections", {}).getArray())
        for (int index = 0; index < sections->size(); ++index) {
            const auto& section = sections->getReference(index);
            auto entry = new juce::DynamicObject();
            entry->setProperty("section_index", index);
            entry->setProperty("section_name", section.getProperty("name", {}));
            entry->setProperty("decision", section.getProperty("development_strategy", {}));
            strategies.add(juce::var(entry));
        }
    return juce::JSON::toString(strategies);
}
juce::String AiPhraseDevelopment::macroSchema(const juce::String& original) {
    if (!groundworkEnabled()) return original;
    const auto old = juce::JSON::parse(original);
    auto root = new juce::DynamicObject();
    auto fields = new juce::DynamicObject();
    // Emit the concrete musical thought BEFORE deriving a full harmonic timeline.
    // This is AI-written reference music, never automatically copied into MIDI.
    fields->setProperty("composition_groundwork", juce::JSON::parse(R"json({
      "type":"object","additionalProperties":false,
      "required":["key","length_beats","identity","voices","harmony","phrases","formal_consequence"],
      "properties":{
        "key":{"type":"string"},"length_beats":{"type":"number"},
        "identity":{"type":"string"},"formal_consequence":{"type":"string"},
        "voices":{"type":"array","maxItems":6,"items":{"type":"object","additionalProperties":false,
          "required":["role","notes"],"properties":{"role":{"type":"string"},
          "notes":{"type":"array","maxItems":128,"items":{"type":"object","additionalProperties":false,
            "required":["beat","duration","pitch","velocity"],"properties":{
              "beat":{"type":"number"},"duration":{"type":"number"},
              "pitch":{"type":"integer"},"velocity":{"type":"integer"}}}}}}},
        "harmony":{"type":"array","maxItems":32,"items":{"type":"object","additionalProperties":false,
          "required":["beat","duration","bass_pitch","pitch_classes","function","arrival"],"properties":{
            "beat":{"type":"number"},"duration":{"type":"number"},"bass_pitch":{"type":"integer"},
            "pitch_classes":{"type":"array","maxItems":12,"items":{"type":"integer"}},
            "function":{"type":"string"},"arrival":{"type":"string"}}}},
        "phrases":{"type":"array","maxItems":12,"items":{"type":"object","additionalProperties":false,
          "required":["beat","duration","speaker_voice_index","relationship","destination"],"properties":{
            "beat":{"type":"number"},"duration":{"type":"number"},
            "speaker_voice_index":{"type":"integer","enum":[0,1,2,3,4,5],"description":"Zero-based position in composition_groundwork.voices. Choose the principal speaker if several voices participate; describe their interaction in relationship."},
            "relationship":{"type":"string"},"destination":{"type":"string"}}}}
      }})json"));
    juce::Array<juce::var> required; required.add("composition_groundwork");
    const auto* oldFields = old.getProperty("properties", {}).getDynamicObject();
    if (oldFields == nullptr) return original;
    for (const auto& property : oldFields->getProperties())
        fields->setProperty(property.name, property.value);
    if (consequentialEnabled()) {
        const auto strategy = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
          "required":["retained_identity","changed_relationship","ensemble_behavior","listener_consequence"],
          "properties":{
            "retained_identity":{"type":"string","description":"Concrete pitch/rhythm relation from the joint passage that remains recognizable. Repetition is allowed."},
            "changed_relationship":{"type":"string","description":"What develops or intentionally stays stable, and why this follows from earlier music. No novelty quota."},
            "ensemble_behavior":{"type":"string","description":"How bass, harmonic rhythm and inner voices jointly support or redirect this thought, without assuming permanent activity or permanent sustained support."},
            "listener_consequence":{"type":"string","description":"The specific audible change of expectation or arrival. A louder return alone is not development."}}})json");
        auto* item = fields->getProperty("sections").getDynamicObject();
        item = item == nullptr ? nullptr : item->getProperty("items").getDynamicObject();
        if (item != nullptr) {
            item->getProperty("properties").getDynamicObject()->setProperty("development_strategy", strategy);
            item->getProperty("required").getArray()->add("development_strategy");
        }
    }
    if (const auto* oldRequired = old.getProperty("required", {}).getArray())
        for (const auto& name : *oldRequired) required.add(name);
    root->setProperty("type", "object"); root->setProperty("additionalProperties", false);
    root->setProperty("properties", juce::var(fields)); root->setProperty("required", required);
    return juce::JSON::toString(juce::var(root));
}
juce::String AiPhraseDevelopment::groundworkBrief() {
    if (!groundworkEnabled()) return {};
    return "\nCOMPOSE BEFORE ARRANGING: First author composition_groundwork: a small complete, playable "
        "passage containing a characteristic thematic statement and its consequential continuation or answer, "
        "with actual pitches, rhythms, durations and an interacting bass/harmonic path. Choose its phrase "
        "proportions for this style (at most 128 quarter-note beats, and never longer than the requested work). "
        "These are symbolic musical roles, not an instrument roster; a single speaker can make both statements. "
        "Make the phrase recognizable by its whole rhythmic/pitch identity and a specific destination, "
        "not just E-F-G or another generic scalar curl. A long stream of scale fragments is not a complete "
        "thought merely because it lands on a chord tone. Repetition, flow, rests, counterpoint and sustained "
        "writing remain available; do not force constant contrast or mandatory silences. Design the response "
        "and bass WITH the melody: their timing and harmonic direction must change what the first thought means. "
        "Avoid independently busy parts that only happen to share a scale. Use the requested style, not a "
        "universal sentence template. Author exact quarter-beat-grid onsets/durations and legal MIDI values. "
        "Harmony events and phrases use the SAME passage-local beat origin as voices. speaker_voice_index "
        "is the zero-based position of the actual speaker in voices (never a new descriptive role alias). Give each phrase a "
        "real destination. In formal_consequence explain what this particular material can develop, where "
        "the decisive arrival belongs, and what a resolved ending must actually answer. THEN derive the "
        "whole-work harmony, sections and narrative from that music, not the reverse. Make the opening "
        "section's chord timeline agree with the passage; include its actual harmonic colors in chord_palette. "
        "Later development may expand, destabilize, contrast or return; do not loop or time-stretch the "
        "reference across the work. The final phrase must deliver the intended degree of closure in sounding "
        "notes, not merely fade activity. References are not extra tracks: later AI writers author every "
        "actual note, preserving this specific initial musical statement before developing it.\n" +
        (consequentialEnabled() ? juce::String(
        "CONSEQUENTIAL DEVELOPMENT: In each section's development_strategy, distinguish the musical "
        "identity retained from the continuation or ensemble relationship that evolves. Ground the decision "
        "in the actual passage: cite its characteristic rhythm/interval relation, not a generic adjective. "
        "An exact repeat or stable ostinato can be the right decision; explain what its changing context "
        "does, or why stability is needed. Coherence does not require retaining the opening's complete "
        "rhythmic surface throughout the work. A destination can justify an expanded phrase, a compressed "
        "reply, a changed harmonic rhythm, continued flow or an earned rest; these are possibilities, not "
        "an obligatory sequence. Section length and phrase rhythm should arise from those decisions, "
        "whether regular or irregular, rather than distribute a motif into equal scene containers. "
        "Decide ensemble_behavior together with the thought: who carries motion, who sustains or responds "
        "and how their roles develop. Do not permanently assign animation to one part and stillness to "
        "all others. Layered hypnotic repetition remains valid when requested. Plan expressive momentum "
        "within phrases and across the work, not just larger velocities in a named climax. Keep the "
        "initial passage as a specific first presentation, not a rhythmic mold for later sentences.\n") : juce::String{});
}
bool AiPhraseDevelopment::validateGroundwork(const juce::var& root, double workBeats, juce::String& error) {
    if (!groundworkEnabled()) return true;
    const auto score = root.getProperty("composition_groundwork", {});
    const auto fail = [&](const char* reason) { error = reason; return false; };
    const auto number = [](const juce::var& value) {
        return (value.isDouble() || value.isInt() || value.isInt64()) &&
            std::isfinite(static_cast<double>(value));
    };
    const auto lengthValue = score.getProperty("length_beats", {});
    const double length = lengthValue;
    if (!score.isObject() || !number(lengthValue) || length <= 0 || length > std::min(128., workBeats))
        return fail("Groundwork needs a finite passage length within this work");
    const auto span = [&](const juce::var& item) {
        const auto beat = item.getProperty("beat", {}), duration = item.getProperty("duration", {});
        const double b = beat, d = duration;
        return number(beat) && number(duration) && b >= 0 && d > 0 && b + d <= length + .001 &&
            std::abs(b * 4 - std::round(b * 4)) < .001 && std::abs(d * 4 - std::round(d * 4)) < .001;
    };
    for (const auto* field : {"key", "identity", "formal_consequence"})
        if (!score.getProperty(field, {}).isString() || score.getProperty(field, {}).toString().trim().isEmpty())
            return fail("Groundwork omitted its musical identity or consequence");
    const auto* voices = score.getProperty("voices", {}).getArray();
    if (!voices || voices->isEmpty() || voices->size() > 6) return fail("Invalid groundwork voice representation");
    std::set<juce::String> roles;
    for (const auto& voice : *voices) {
        const auto role = voice.getProperty("role", {}).toString();
        if (role.trim().isEmpty() || !roles.insert(role).second) return fail("Groundwork roles must be unique");
        const auto* notes = voice.getProperty("notes", {}).getArray();
        if (!notes || notes->isEmpty() || notes->size() > 128) return fail("Invalid groundwork note representation");
        for (const auto& note : *notes) {
            const auto p = note.getProperty("pitch", {}), v = note.getProperty("velocity", {});
            if (!span(note) || !number(p) || !number(v) || double(p) != std::floor(double(p)) ||
                double(p) < 0 || double(p) > 127 || double(v) != std::floor(double(v)) || double(v) < 1 || double(v) > 127)
                return fail("Groundwork note has invalid MIDI values or local timing");
        }
    }
    const auto* harmony = score.getProperty("harmony", {}).getArray();
    if (!harmony || harmony->isEmpty() || harmony->size() > 32) return fail("Groundwork omitted harmonic support");
    for (const auto& event : *harmony) {
        const auto bass = event.getProperty("bass_pitch", {});
        const auto* pcs = event.getProperty("pitch_classes", {}).getArray();
        if (!span(event) || !number(bass) || double(bass) != std::floor(double(bass)) || double(bass) < 0 ||
            double(bass) > 127 || !pcs || pcs->isEmpty() || pcs->size() > 12)
            return fail("Invalid groundwork harmonic event");
        for (const auto& pc : *pcs)
            if (!number(pc) || double(pc) != std::floor(double(pc)) || double(pc) < 0 || double(pc) > 11)
                return fail("Invalid groundwork pitch class");
    }
    const auto* phrases = score.getProperty("phrases", {}).getArray();
    if (!phrases || phrases->isEmpty() || phrases->size() > 12) return fail("Invalid groundwork phrase representation");
    for (const auto& phrase : *phrases) {
        const auto index = phrase.getProperty("speaker_voice_index", {});
        // Retain previously accepted reference checkpoints verbatim. New requests
        // use explicit indices; never infer an owner from a musical description.
        const auto validOwner = index.isVoid()
            ? roles.contains(phrase.getProperty("speaker_role", {}).toString())
            : number(index) && double(index) == std::floor(double(index)) &&
                double(index) >= 0 && double(index) < voices->size();
        if (!span(phrase) || !validOwner)
            return fail("Groundwork phrase needs an existing voice index and a valid local span");
    }
    if (consequentialEnabled()) {
        const auto* sections = root.getProperty("sections", {}).getArray();
        if (!sections || sections->isEmpty()) return fail("Development decisions need real sections");
        for (const auto& section : *sections)
            for (const auto* field : {"retained_identity", "changed_relationship", "ensemble_behavior", "listener_consequence"}) {
                const auto value = section.getProperty("development_strategy", {}).getProperty(field, {});
                if (!value.isString() || value.toString().trim().isEmpty() || value.toString().length() > 3000)
                    return fail("Development strategy has missing or oversized descriptive fields");
            }
    }
    return true;
}
juce::String AiPhraseDevelopment::groundworkContext(const juce::String& blueprint) {
    if (!groundworkEnabled()) return {};
    const auto value = juce::JSON::parse(blueprint).getProperty("composition_groundwork", {});
    if (!value.isObject()) return {};
    return "\nACCEPTED JOINT THEMATIC-HARMONIC PASSAGE (not extra MIDI):\n" +
        juce::JSON::toString(value) +
        "\nAssign these musical roles to the real cast. Preserve the complete initial statement's "
        "recognizable pitch-and-rhythm identity and its harmonic destination in musical_subject and the "
        "opening realization; do not replace it with a shorter generic seed. Then author development, "
        "responses and the final consequence appropriate to this work. Do not fill whole sections with "
        "continuous scales or default half-bar block-chord attacks. Supporting rhythms should articulate "
        "the actual foreground sentence, with meaningful bass motion and shared arrivals where appropriate.\n" +
        (consequentialEnabled() ? "\nJOINT DEVELOPMENT DECISIONS BY SECTION:\n" + developmentStrategies(blueprint) +
        "\nUse existing material_development fields to enact these decisions within each phrase: inherited_from "
        "identifies the retained musical feature; musical_task describes the changed continuation or purposeful "
        "stability; partner_task describes the supporting voices' joint action; heard_consequence describes "
        "the actual destination or changed expectation. No extra theme, asymmetry, rest or novelty is compulsory. "
        "Do not replace a full sentence with an unbroken stream of the seed's opening rhythm.\n" : juce::String{});
}
juce::String AiPhraseDevelopment::extendSchema(const juce::String& original) {
    if (!enabled()) return original;
    auto root = juce::JSON::parse(original);
    auto* properties = root.getDynamicObject()->getProperty("properties").getDynamicObject();
    // Reuse the subject representation, not its notes or a fixed musical form.
    auto material = juce::JSON::parse(juce::JSON::toString(properties->getProperty("musical_subject")));
    if (material.getDynamicObject() == nullptr) return original;
    auto* fields = material.getDynamicObject()->getProperty("properties").getDynamicObject();
    for (const auto* name : {"id", "relationship_to_primary"}) {
        fields->setProperty(name, juce::JSON::parse(R"({"type":"string"})"));
        material.getDynamicObject()->getProperty("required").getArray()->add(name);
    }
    auto materials = juce::JSON::parse(R"({"type":"array","maxItems":4,"items":{}})");
    materials.getDynamicObject()->setProperty("items", material);
    properties->setProperty("thematic_materials", materials);
    root.getDynamicObject()->getProperty("required").getArray()->add("thematic_materials");
    auto* phrase = properties->getProperty("phrases").getDynamicObject()
        ->getProperty("items").getDynamicObject();
    phrase->getProperty("properties").getDynamicObject()->setProperty("material_development",
        juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
          "required":["material_id","inherited_from","musical_task","partner_task","heard_consequence"],
          "properties":{"material_id":{"type":"string","description":"Exact primary or declared thematic_materials id; empty for independent transition."},
          "inherited_from":{"type":"string","description":"Free musical explanation of what earlier material or gesture is inherited, possibly more than one. Not an ID field."},
          "musical_task":{"type":"string"},"partner_task":{"type":"string"},
          "heard_consequence":{"type":"string"}}})json"));
    phrase->getProperty("required").getArray()->add("material_development");
    return juce::JSON::toString(root, true);
}
juce::String AiPhraseDevelopment::bindCastSchema(const juce::String& original, const SongPlan& plan) {
    if (!enabled()) return original;
    auto root = juce::JSON::parse(original);
    auto* properties = root.getDynamicObject()->getProperty("properties").getDynamicObject();
    juce::Array<juce::var> owners;
    for (const auto& part : plan.instruments) owners.add(juce::String::fromUTF8(part.id.c_str()));
    auto withRest = owners; withRest.add("");
    const auto bind = [&](juce::var field, bool rest = false) {
        if (auto* object = field.getDynamicObject()) object->setProperty("enum", rest ? withRest : owners);
    };
    bind(properties->getProperty("argument_owner_ids").getProperty("items", {}));
    bind(properties->getProperty("musical_subject").getProperty("properties", {}).getProperty("instrument_id", {}));
    bind(properties->getProperty("thematic_materials").getProperty("items", {})
        .getProperty("properties", {}).getProperty("instrument_id", {}));
    const auto fields = properties->getProperty("phrases").getProperty("items", {}).getProperty("properties", {});
    bind(fields.getProperty("speaker_id", {})); bind(fields.getProperty("response_id", {}), true);
    const auto span = fields.getProperty("ensemble_spans", {}).getProperty("items", {}).getProperty("properties", {});
    for (const auto* field : {"foreground_ids", "joint_voice_ids"})
        bind(span.getProperty(field, {}).getProperty("items", {}));
    bind(span.getProperty("pulse_carrier_id", {}), true);
    bind(span.getProperty("parts", {}).getProperty("items", {}).getProperty("properties", {})
        .getProperty("instrument_id", {}));
    return juce::JSON::toString(root, true);
}
bool AiPhraseDevelopment::validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!enabled()) return true;
    const auto fail = [&](const char* message) { error = message; return false; };
    const auto* materials = root.getProperty("thematic_materials", {}).getArray();
    if (!materials || materials->size() > 4) return fail("Invalid thematic-material representation");
    std::set<juce::String> ids{"primary"};
    for (const auto& material : *materials) {
        const auto id = material.getProperty("id", {}).toString();
        if (id.trim().isEmpty() || id.length() > 80 || !ids.insert(id).second)
            return fail("Thematic material needs a unique non-primary id");
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& part) {
            return material.getProperty("instrument_id", {}).toString() == juce::String::fromUTF8(part.id.c_str());
        });
        if (owner == plan.instruments.end()) {
            error = "Thematic material '" + id + "' names unavailable owner '" +
                material.getProperty("instrument_id", {}).toString() + "'. Omit absent material rather than adding a placeholder.";
            return false;
        }
        for (const auto* field : {"identity", "continuation", "arrival", "recognizable_return", "relationship_to_primary"}) {
            const auto value = material.getProperty(field, {});
            if (!value.isString() || value.toString().trim().isEmpty() || value.toString().length() > 2000)
                return fail("Thematic material has invalid semantic fields");
        }
        const auto lengthValue = material.getProperty("length_beats", {});
        const auto length = static_cast<double>(lengthValue);
        if (!(lengthValue.isInt() || lengthValue.isInt64() || lengthValue.isDouble()) ||
            !std::isfinite(length) || length <= 0 || length > plan.totalBars * plan.beatsPerBar)
            return fail("Thematic material has invalid local length");
        const auto* notes = material.getProperty("notes", {}).getArray();
        if (!notes || notes->isEmpty() || notes->size() > 96) return fail("Thematic material needs bounded reference notes");
        for (const auto& note : *notes) {
            for (const auto* field : {"beat", "duration", "pitch", "velocity"}) {
                const auto value = note.getProperty(field, {});
                if (!(value.isInt() || value.isInt64() || value.isDouble()) || !std::isfinite(static_cast<double>(value)))
                    return fail("Thematic material note is not numeric");
            }
            const double beat = note.getProperty("beat", {}), duration = note.getProperty("duration", {}),
                pitch = note.getProperty("pitch", {}), velocity = note.getProperty("velocity", {});
            if (beat < 0 || duration <= 0 || beat + duration > length + .001 ||
                std::abs(beat * 4 - std::round(beat * 4)) > .001 ||
                std::abs(duration * 4 - std::round(duration * 4)) > .001 ||
                pitch != std::floor(pitch) || pitch < std::max(0, owner->minimumPitch - 12) ||
                pitch > std::min(127, owner->maximumPitch + 12) ||
                velocity != std::floor(velocity) || velocity < 1 || velocity > 127)
                return fail("Thematic material note is outside its reference representation");
        }
    }
    const auto* phrases = root.getProperty("phrases", {}).getArray();
    if (!phrases) return fail("Thematic development omitted phrases");
    for (const auto& phrase : *phrases) {
        const auto development = phrase.getProperty("material_development", {});
        if (!development.isObject()) return fail("Phrase omitted its musical development relationship");
        for (const auto* field : {"material_id"}) {
            const auto value = development.getProperty(field, {});
            if (!value.isString() || (value.toString().isNotEmpty() && !ids.contains(value.toString())))
                return fail("Phrase refers to an unknown thematic material");
        }
        const auto inheritance = development.getProperty("inherited_from", {});
        if (!inheritance.isString() || inheritance.toString().length() > 2000)
            return fail("Phrase inheritance needs bounded descriptive text, not an owner reference");
        for (const auto* field : {"musical_task", "partner_task", "heard_consequence"}) {
            const auto value = development.getProperty(field, {});
            if (!value.isString() || value.toString().trim().isEmpty() || value.toString().length() > 2000)
                return fail("Phrase development has invalid relationship text");
        }
    }
    return true;
}
juce::String AiPhraseDevelopment::castingBrief() {
    if (!enabled()) return {};
    return "Declare one protagonist ID for tracking, not an exclusive right to musical invention. "
        "Choose the melodic partnership for this style: a secondary subject, an autonomous answer, "
        "or independent contrapuntal material may have its own complete phrase and later consequence. "
        "There is no maximum of one musical answerer. Do not create more tracks to label each variation, "
        "nor require more themes than this form needs. Supporting voices may develop substantive ideas "
        "without all becoming soloists or abandoning their harmonic responsibility. ";
}
juce::String AiPhraseDevelopment::planningBrief() {
    if (!enabled()) return {};
    return "\nPHRASE DEVELOPMENT EXPERIMENT: musical_subject is the primary complete sentence, not the "
        "entire inventory of what the work may say. In thematic_materials author any genuinely useful "
        "secondary complete sentences with actual reference notes and a distinct id. [] is valid for a "
        "monothematic work. Do not fill unused slots or emit dummy, unused or unowned entries: omit them. "
        "They may belong to another voice or the same speaker; their contrasting rhythm, "
        "continuation, harmonic implication and relationship_to_primary must justify their existence. "
        "References are SUBJECT-LOCAL and are NOT placements or extra MIDI layers. Use primary to refer "
        "to musical_subject; all other ids must be declared in thematic_materials.\n"
        "For each phrase, material_development identifies what material it actually develops, what it "
        "inherits, its musical_task, the partner_task and heard_consequence. material_id is an exact "
        "reference id or empty for independent transition; inherited_from is FREE musical explanation "
        "of a gesture or relationship, not another exact-id field. Decide functions "
        "WITHIN each section: a statement that continues, a meaningful interruption or response, a "
        "transition that changes harmonic direction, a gathering continuation, or a local arrival as "
        "appropriate. These are choices, not a required sequence. A large section is not automatically "
        "one long phrase or a stretched copy of a small cell. Choose the actual phrase lengths needed; "
        "phrases can cross section boundaries and overlap. Retain repetition that establishes memory. "
        "Give it a changing consequence when the form needs progress, not constant novelty.\n"
        "Plan motion at different scales: sustained harmony can support a fluent melodic sentence; an "
        "ostinato can stay while the dialogue changes; a bass can redirect a held melody. Natural clarity "
        "does not mean a bar-long note on every downbeat, nor mandatory fast notes. Choose subdivision, "
        "grouping, articulation and cadence for the requested style. A climax must change the musical "
        "relationship it has prepared, not merely increase velocities. Write reference material that "
        "can sustain its intended development; a charming opening clue is not a complete argument.\n";
}
juce::String AiPhraseDevelopment::realizationBrief() {
    if (!enabled()) return {};
    return "\nDEVELOP THE MATERIAL, NOT ITS LABELS: Read musical_subject, thematic_materials and each "
        "material_development as reference ideas and relationships. Write the complete actual ensemble "
        "argument with beginning, continuation, replies, transitions and earned local arrivals. The "
        "reference notes are not an immutable piano roll to replay or a template to stretch to the "
        "section length. Neither must every line derive from primary. Let secondary material speak "
        "when its relationship changes the argument, then prepare any meaningful return. Preserve "
        "recognition without keeping the entire opening rhythm in every phrase. A long note may be a "
        "destination, suspension or sustained floor; constant long-note stepping is not automatically "
        "expressive. A fluent run or repeated figure can be natural when its grouping, voice-leading "
        "and destination are clear. Do not equate rhythmic clarity with avoiding subdivisions or "
        "counterpoint. Follow the style, not a universal density target. All real notes, rests, "
        "subdivisions and releases are yours to author; no algorithm will copy, invert, stretch, "
        "fill, speed up or thin these ideas.\n" + (groundworkEnabled() ? juce::String(
        "COMPOSITION_GROUNDWORK contains the jointly composed initial passage and harmonic logic. "
        "Realize that specific opening statement, its continuation/answer and bass relation before "
        "developing it; do not substitute another generic melody that only shares its tonal center. "
        "Treat its phrases as music, not a mechanical copy loop. Let supporting parts articulate "
        "its actual rhythmic proportions and arrivals rather than defaulting to repeated block chords.\n") : juce::String{}) +
        (consequentialEnabled() ? juce::String(
        "DEVELOPMENT_STRATEGIES and material_development describe joint musical decisions, not "
        "extra note layers. Realize their retained identity AND their specific developing continuation. "
        "Retaining identity does not mean replaying the entire opening rhythm at different pitches. "
        "Let the shape of the whole phrase determine its attacks, durations, register, local emphasis "
        "and arrival; do not achieve development only by making each section louder. Hear bass, inner "
        "parts and protagonist together: their support may evolve, remain hypnotically stable, take "
        "over motion or answer as appropriate. Do not manufacture variety or simplify to a density quota. "
        "Make the final return a consequence of the actual intervening music, not an appended reminder.\n") : juce::String{});
}
juce::String AiPhraseDevelopment::reviewBrief(const SongPlan& plan, const Pattern& actual,
    const std::vector<std::size_t>& owners) {
    if (!enabled()) return {};
    juce::String out;
    out << "\nDEVELOPMENT REVIEW: Technical correctness and an identifiable seed are not sufficient. "
        "Inspect whether a complete thought actually develops, and whether the listener hears "
        "contrast, changing relationships and a consequence beyond returning to the tonic. "
        "Could sections be exchanged without changing the argument? Does a named climax merely "
        "increase velocity while keeping the same rhetoric? Are repeated sustained steps serving "
        "the requested style, or substituting for a missing continuation? Do not reward complexity "
        "or punish sustained writing by itself. Do not correct purposeful modal/chromatic identity "
        "just to remove a CHECK_FOREIGN label. Keep worthwhile music; revise only a real deficiency "
        "in the actual score. State exact owners and beats as evidence, not section adjectives.\n"
        "READ-ONLY PER-SECTION DELIVERY FACTS (not thresholds or rejection rules):\n";
    for (std::size_t section = 0; section < plan.sections.size(); ++section) {
        const auto& span = plan.sections[section];
        const auto begin = span.startBar * plan.beatsPerBar, end = begin + span.bars * plan.beatsPerBar;
        out << "section=" << static_cast<int>(section) << " absolute=" << begin << ".." << end << "\n";
        for (const auto owner : owners) {
            int count = 0, fractional = 0, lo = 128, hi = -1;
            std::map<double, int> durations;
            for (const auto& note : actual.notes) {
                if (note.partId != owner + 1 || note.startBeat < begin || note.startBeat >= end) continue;
                ++count; fractional += std::abs(note.startBeat - std::round(note.startBeat)) > .001;
                lo = std::min(lo, note.pitch); hi = std::max(hi, note.pitch); ++durations[note.durationBeats];
            }
            out << juce::String::fromUTF8(plan.instruments[owner].id.c_str()) << " attacks=" << count
                << " subdivided_onsets=" << fractional;
            if (count) out << " range=" << lo << ".." << hi;
            out << " duration_histogram=";
            for (const auto& [duration, n] : durations) out << juce::String(duration, 2) << ":" << n << ",";
            out << "\n";
        }
    }
    return out;
}
}
