#include "AiMaterialDialogue.h"
#include "AiCompleteDialogue.h"
#include <set>
#include <algorithm>

namespace pulso::plugin {
bool AiMaterialDialogue::enabled() {
    return AiCompleteDialogue::wholeArgumentEnabled() &&
        (expectationLedEnabled() ||
         juce::SystemStats::getEnvironmentVariable("PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT", {}) == "1");
}
bool AiMaterialDialogue::expectationLedEnabled() {
    return AiCompleteDialogue::wholeArgumentEnabled() &&
        juce::SystemStats::getEnvironmentVariable("PULSO_AI_EXPECTATION_LED_EXPERIMENT", {}) == "1";
}
juce::String AiMaterialDialogue::planningSchema(const juce::String& original, const SongPlan& plan) {
    if (!enabled() || expectationLedEnabled()) return original;
    auto root = juce::JSON::parse(original);
    auto design = juce::JSON::parse(R"json({"type":"array","maxItems":5,"items":{
      "type":"object","additionalProperties":false,
      "required":["material_id","recognizable_identity","distinct_contribution","interaction","motion_partners","development_states"],
      "properties":{
        "material_id":{"type":"string","description":"Exact primary or declared thematic_materials id. Not an instrument ID or a prose label."},
        "recognizable_identity":{"type":"string"},"distinct_contribution":{"type":"string"},
        "interaction":{"type":"string"},
        "motion_partners":{"type":"array","maxItems":6,"items":{"type":"object","additionalProperties":false,
          "required":["instrument_id","role_change"],"properties":{"instrument_id":{"type":"string"},"role_change":{"type":"string"}}}},
        "development_states":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,
          "required":["context","retained_identity","changed_continuation","motion_relationship","consequence"],
          "properties":{"context":{"type":"string"},"retained_identity":{"type":"string"},
            "changed_continuation":{"type":"string"},"motion_relationship":{"type":"string"},"consequence":{"type":"string"}}}}
      }}})json");
    juce::Array<juce::var> owners;
    for (const auto& part : plan.instruments) owners.add(juce::String::fromUTF8(part.id.c_str()));
    design.getProperty("items", {}).getProperty("properties", {}).getProperty("motion_partners", {})
        .getProperty("items", {}).getProperty("properties", {}).getProperty("instrument_id", {})
        .getDynamicObject()->setProperty("enum", owners);
    root.getDynamicObject()->getProperty("properties").getDynamicObject()->setProperty("material_design", design);
    root.getDynamicObject()->getProperty("required").getArray()->add("material_design");
    return juce::JSON::toString(root, true);
}
bool AiMaterialDialogue::validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!enabled() || expectationLedEnabled()) return true;
    const auto fail = [&](const char* message) { error = message; return false; };
    const auto text = [](const juce::var& value) {
        return value.isString() && value.toString().trim().isNotEmpty() && value.toString().length() <= 2400;
    };
    std::set<juce::String> materialIds{"primary"}, castIds, selectedIds, used;
    for (const auto& part : plan.instruments) castIds.insert(juce::String::fromUTF8(part.id.c_str()));
    if (const auto* selected = root.getProperty("argument_owner_ids", {}).getArray())
        for (const auto& owner : *selected) selectedIds.insert(owner.toString());
    if (const auto* materials = root.getProperty("thematic_materials", {}).getArray())
        for (const auto& material : *materials) {
            materialIds.insert(material.getProperty("id", {}).toString());
            if (!selectedIds.contains(material.getProperty("instrument_id", {}).toString()))
                return fail("A thematic speaker is outside the selected whole argument");
        }
    const auto* design = root.getProperty("material_design", {}).getArray();
    if (!design || design->size() > 5) return fail("Invalid bounded material design representation");
    for (const auto& material : *design) {
        const auto id = material.getProperty("material_id", {}).toString();
        if (!materialIds.contains(id) || !used.insert(id).second)
            return fail("Material design needs unique, declared material references");
        for (const auto* field : {"recognizable_identity", "distinct_contribution", "interaction"})
            if (!text(material.getProperty(field, {}))) return fail("Invalid descriptive material relationship");
        const auto* partners = material.getProperty("motion_partners", {}).getArray();
        const auto* states = material.getProperty("development_states", {}).getArray();
        if (!partners || partners->size() > 6 || !states || states->size() > 8)
            return fail("Invalid bounded material motion representation");
        std::set<juce::String> seenPartners;
        for (const auto& partner : *partners) {
            const auto owner = partner.getProperty("instrument_id", {}).toString();
            if (!castIds.contains(owner) || !seenPartners.insert(owner).second ||
                !text(partner.getProperty("role_change", {}))) return fail("Invalid material motion partner");
        }
        for (const auto& state : *states)
            for (const auto* field : {"context", "retained_identity", "changed_continuation", "motion_relationship", "consequence"})
                if (!text(state.getProperty(field, {}))) return fail("Invalid descriptive material development state");
    }
    return true;
}
juce::String AiMaterialDialogue::planningBrief() {
    if (!enabled()) return {};
    if (expectationLedEnabled())
        return "\nEXPECTATION-LED STUDY: Choose a complete musical thought worth remembering, and "
            "which selected voices must jointly realize its consequences. Hear what its actual rhythm, "
            "contour and unfinished destination let a listener anticipate. Plan a continuation that "
            "does something with that anticipation, not merely the same figure on the next chord. "
            "A changed consequence must affect what follows. Repetition can establish the expectation; "
            "a stable or hypnotic work need not become theatrical. Choose additional material only "
            "when it contributes a distinct thought. Do not assign a compulsory twist, new theme or "
            "cadence to each section. Keep prose concise. The final whole-score writer has authority "
            "over the actual phrasing; these intentions are not additional scheduled attacks.\n";
    return "\nMATERIAL DIALOGUE EXPERIMENT: Preserve the connected whole argument, but distinguish "
        "musical identity from a permanently identical surface pulse. Decide whether this work needs "
        "related independent material in thematic_materials or a single theme with substantively different "
        "developmental states. There is NO minimum theme count: a monothematic work is valid. Do not add "
        "a new motif each section, unrelated ideas or continual surprises. If a secondary thought is useful, "
        "write its complete reference phrase in the existing thematic_materials, with its own rhythmic "
        "proportions and continuation, and select its speaker in argument_owner_ids. A different instrument "
        "or register alone is not a second thought. Explain in material_design each material's recognizable "
        "identity, distinct contribution and encounter with the others or with its own changed context. "
        "Use primary for musical_subject and exact declared ids for additional materials.\n"
        "In development_states decide what remains recognizable and what changes in the continuation, "
        "expectation or motion relationship. Changing pitch while replaying the complete opening rhythm "
        "can retain identity but is not the only development available. A broader line can grow from a "
        "short gesture; a reply can become the next main statement; a bass or harmonic rhythm can redirect "
        "a held melody; a return can combine implications of earlier material. These are possibilities, "
        "not a required recipe. Repetition or a steady hypnotic pulse can remain appropriate. Decide "
        "motion_partners jointly: which real voices carry or release motion, and how their relationship "
        "changes the perceived momentum. Include a harmonic/motion carrier in argument_owner_ids if its "
        "actual notes are essential to that transformation; do not delegate the musical turning point "
        "to unspecified later orchestration. Supporting voices need not all become soloists.\n"
        "Let phrase rhythm and harmonic motion serve those interactions, rather than permanently assign "
        "fast figures to the lead and static durations to everyone else. Dynamic means changing musical "
        "behavior, not merely velocities, more notes, or tempo changes. Leave space for recognition and "
        "expectation; surprise must become understandable in retrospect. No fixed bar interval, number "
        "of transformations, asymmetry, rests or subdivision is required. Keep descriptions concise "
        "and relate material_development in actual phrases to these decisions. Empty optional arrays "
        "remain valid when the work intentionally does not need extra material or a changing role.\n";
}
juce::String AiMaterialDialogue::realizationBrief() {
    if (!enabled()) return {};
    if (expectationLedEnabled())
        return "EXPECTATION-LED WHOLE SCORE\n"
            "Compose the complete reduced score so a listener can follow a thought, anticipate its "
            "continuation, and hear why the continuation matters. Compose the musical events first, "
            "not an illustration of narrative labels. Establish something memorable with enough "
            "continuity for recognition. Hear the listener's expectation from the notes already "
            "written: destination, rhythmic proportion, register or the relationship of voices. "
            "Choose when to fulfil it and when to redirect it. A redirection must change the ensuing "
            "phrase, not become an isolated surprise followed by business as usual. Its preparation "
            "and consequence should be audible without explanation.\n"
            "Let continuation lengths, melodic motion, harmonic pace and bass behavior respond to "
            "that particular event. A bass can carry an implication while a melody sustains; movement "
            "can accumulate or open into a broader utterance. These are options, not required effects. "
            "Do not maintain one accompaniment motor automatically just because the bass keeps its "
            "role. Do not make every part change whenever one changes. A steady pulse or repetition "
            "can be essential; use recognition to give a meaningful departure its significance.\n"
            "The reference subject is material to develop, not a loop or a note-copy obligation. "
            "Compose substantive continuations, counterpoint and style-appropriate virtuosity when "
            "earned. Do not reduce development to louder, busier or more irregular notes. No quota "
            "of themes, surprises, silences or climax points is imposed. Decide the form of the "
            "actual sentence across the entire work; output-window boundaries are coordinates, "
            "not phrase lengths or required arrivals. The ending should conclude what the opening "
            "set in motion, not append a tonic after an unfinished stream.\n"
            "Hear all selected voices together with one piano. Every pitch, attack, release, rest "
            "and velocity is yours to author. Keep intentional rhythm and articulation intelligible; "
            "arbitrary tiny displacements are not expression. Release a repeated pitch before its "
            "next attack unless distinct overlapping voices are intentional. No renderer supplies "
            "development or rearranges your notes; later accompaniment follows this exact score.\n";
    return "\nREALIZE THE MATERIAL DIALOGUE: Read material_design and the actual subject references, "
        "then compose the whole selected score as their changing musical relationship. Make any "
        "secondary material a real complete utterance with its own continuation, not a brief label "
        "between quotations of primary. In a one-theme work, develop its different states as actual "
        "changes of implication, not just transposed copies of one attack pattern. Let rhythmic "
        "proportions, harmonic/bass motion, articulation, register and phrase destination interact. "
        "Choose when a partner takes over motion beneath a held foreground and when the ensemble "
        "converges. An inherited pulse need not dictate every voice's surface rhythm. Do not manufacture "
        "constant novelty, force irregularity, remove a useful ostinato, increase density everywhere "
        "or turn every reply into a solo. The end should reflect what the encounter actually changed. "
        "All pitches, durations, rests and relationships are yours to write: no code performs the "
        "described transformations or adds reference notes to the realized score.\n";
}
juce::String AiMaterialDialogue::accompanimentBrief() {
    if (!enabled()) return {};
    if (expectationLedEnabled())
        return "\nEXPECTATION-LED ACCOMPANIMENT: Read the actual completed argument, including what "
            "precedes and follows this span. Support its changing implications through real bass, "
            "harmonic and inner-voice behavior. Do not repeat one backing template across the work "
            "out of habit, or add decorative surprises unrelated to the lived phrase. Steadiness, "
            "rich harmony and independent counterpoint remain available. The accepted foreground "
            "is immutable: write its partners, not a competing replacement story.\n";
    return "\nMATERIAL-AWARE ACCOMPANIMENT: Follow the actual selected voices and the material_design "
        "motion partnerships. Supporting parts participate in the changing momentum and harmonic "
        "meaning, rather than maintain one accompaniment template merely because their instrument "
        "role stays the same. A held body or steady pulse can remain; change only where the specific "
        "phrase relationship calls for it. Do not obscure the accepted dialogue or rewrite its notes.\n";
}
juce::String AiMaterialDialogue::accompanimentContext(const juce::var& root) {
    if (!enabled()) return {};
    if (expectationLedEnabled()) return accompanimentBrief();
    return "\nAI MATERIAL DESIGN AND MOTION PARTNERSHIPS (intentions, not proof of success):\n" +
        juce::JSON::toString(root.getProperty("material_design", {}), true) + accompanimentBrief();
}
juce::String AiMaterialDialogue::reviewBrief() {
    if (!enabled()) return {};
    if (expectationLedEnabled())
        return "\nEXPECTATION-LED AUDITION: Read the actual score, not section titles. Identify "
            "whether an established musical expectation has a consequential continuation and an "
            "earned conclusion, or merely compatible activity. A change of pitch, instrument or "
            "velocity alone does not establish development. Neither does a descriptive diagnosis. "
            "Keep intentional stability, repetition and successful music; no twist is compulsory. "
            "Revise only a concrete deficiency you can identify in exact owners and actual beats, "
            "preserving its context. Do not rewrite the work merely to claim a more dramatic story.\n";
    return "\nMATERIAL-DIALOGUE AUDITION: Judge the actual reduced score, not a count of themes or "
        "the material_design promises. Does related material have its own complete contribution, or "
        "does every speaker keep the same small gesture and surface pulse? If there is one theme, "
        "do its continuations change the listener's expectation? Does movement pass meaningfully "
        "between the speaker, bass and harmonic partners, or is every change merely louder activity? "
        "Would the main idea's later return make the same sense without the intervening encounter? "
        "Monothematic development, symmetry and hypnotic repetition are valid. Keep successful music; "
        "only revise a concrete audible deficiency, scoped to exact owners and beats. Never add "
        "a secondary theme, ornaments, rests or a new rhythm everywhere just to meet this brief.\n";
}
}
