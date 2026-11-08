#pragma once
#include <set>

namespace pulso::plugin::club::scenes {
inline bool requested() {
    return juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_ENSEMBLE_SCENES", {}) == "1";
}
inline bool active(const SongPlan& plan) {
    return plan.compositionBehavior == CompositionBehavior::Club &&
        juce::JSON::parse(juce::String(plan.phraseArchitectureJson)).hasProperty("ensemble_scenes");
}
inline void extend(juce::var& root, const SongPlan* plan) {
    if (!requested() || !plan || plan->instruments.empty() || plan->sections.empty()) return;
    auto entry = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["section_index","inherited_material","foreground_id","voices","transition","consequence","musical_decision"],
      "properties":{"section_index":{"type":"integer"},"inherited_material":{"type":"string"},
        "foreground_id":{"type":"string"},"transition":{"type":"string"},"consequence":{"type":"string"},
        "musical_decision":{"type":"object","additionalProperties":false,
          "required":["retained_identity","development","relationship","audible_payoff"],"properties":{
            "retained_identity":{"type":"string"},"development":{"type":"string"},
            "relationship":{"type":"string"},"audible_payoff":{"type":"string"}}},
        "voices":{"type":"array","minItems":1,"maxItems":12,"items":{"type":"object","additionalProperties":false,
          "required":["owner_id","material","relationship","speaking_span","statement","continuation","arrival"],"properties":{
            "owner_id":{"type":"string"},"material":{"type":"string"},
            "relationship":{"type":"string"},"speaking_span":{"type":"string"},
            "statement":{"type":"string"},"continuation":{"type":"string"},
            "arrival":{"type":"string"}}}}}})json");
    juce::Array<juce::var> ids, indices;
    for (const auto& owner : plan->instruments) ids.add(juce::String(owner.id));
    for (int s=0; s<static_cast<int>(plan->sections.size()); ++s) indices.add(s);
    auto props=entry.getProperty("properties", {});
    props.getProperty("section_index", {}).getDynamicObject()->setProperty("enum", indices);
    props.getProperty("foreground_id", {}).getDynamicObject()->setProperty("enum", ids);
    props.getProperty("voices", {}).getProperty("items", {}).getProperty("properties", {})
        .getProperty("owner_id", {}).getDynamicObject()->setProperty("enum", ids);
    auto list=juce::JSON::parse(R"({"type":"array"})");
    list.getDynamicObject()->setProperty("minItems", static_cast<int>(plan->sections.size()));
    list.getDynamicObject()->setProperty("maxItems", static_cast<int>(plan->sections.size()));
    list.getDynamicObject()->setProperty("items", entry);
    root.getProperty("required", {}).getArray()->add("ensemble_scenes");
    root.getProperty("properties", {}).getDynamicObject()->setProperty("ensemble_scenes", list);
}
inline juce::String foregroundContinuity() {
    return "\nFOREGROUND AND SUPPORT RESPONSIBILITY: Make each scene's relationship and "
        "speaking_span explain who carries the listener's attention while the main hook rests. "
        "That may be a developed secondary thought, an evolving inner voice, a meaningful "
        "held tension, or intentionally unadorned groove. A lead absence is not a command "
        "to fill every bar; give a long absence a perceptible consequence and prepare its return. "
        "A chord pulse can retain its timing identity without dominating every scene: choose "
        "where its voicing, register, velocity, articulation, or selected omissions yield to "
        "the active speaker. Preserve a legible shared pulse in the ensemble; do not turn "
        "every support layer into another melody. Let repetition establish memory before "
        "a change has a reason. No compulsory mute, fade, fill, or fixed bar-change schedule. "
        "Connect the last melodic preparation to its destination through an audible handoff: "
        "if the speaker pauses, say what sustains or carries the unresolved intention and how "
        "the arrival answers it. A silent or sustained span can be intentional; a disconnected "
        "final tonic appended after the argument has ended is not preparation. Author these "
        "relationships in the existing scene fields and realize them in the MIDI, not just "
        "prose. Do not add tracks or notes to satisfy a quantity.\n";
}
inline juce::String completeTrackArgument() {
    return "\nFINISHED TRACK, NOT AN ACCOMPANIMENT STUDY: A technically complete chord bed "
        "and pulse are only the setting. Give the listener a concrete focal idea worth "
        "remembering: an articulated subject with a beginning, continuation and an arrival, "
        "not scattered cue notes decorating the backing. In the existing statement and "
        "continuation fields specify its recognizable interval/rhythm gesture and how "
        "its next phrase answers or extends that gesture. Realize the complete statement "
        "when it is introduced, not a promise reserved for a later writing block. "
        "A subject can be economical and hypnotic: do not turn Club into constant solos. "
        "Give one secondary thought its own connected statement and destination, related "
        "to the subject through response, contrast or harmonic consequence, rather than "
        "more incidental accents. Let at least one later focal return change what the "
        "listener understood about that idea: an answered tension, a changed harmonic "
        "meaning, or a fulfilled register/duration destination chosen by you. More notes "
        "or a higher energy label do not establish that consequence. Let support yield "
        "when the statement needs attention, while retaining the ensemble's dance pulse. "
        "Do not solve this by adding tracks or continuous melodic activity. Distinguish "
        "the final arrival from an ordinary loop boundary through its preparation and "
        "audible consequence; once earned, do not reopen the melodic question with a "
        "fresh flurry merely to fill the remaining time. Choose the final sustain and "
        "release in proportion to the actual phrase, not a mandatory last-bar chord.\n";
}
inline juce::String connectedArrival() {
    return "\nCONNECTED DIALOGUE AND TERMINAL SENTENCE: Plan the interval/rhythm of a "
        "speaker's continuation in relation to the actual end of the preceding thought, "
        "not merely to the current chord. A secondary voice should answer something "
        "specific (an unfinished contour, displaced accent, held tension), then reach "
        "its own destination. Repeated groove underneath is welcome; it is not by itself "
        "the continuation of a melodic question. In speaking_span and arrival, distinguish "
        "the end of the melodic argument from the subsequent instrumental release. "
        "For the final scene, use statement, continuation and arrival to name the "
        "recognizable recall, its connected answering gesture and its last destination "
        "in temporal order. Choose their spacing musically rather than stretching "
        "a short phrase across the allocated section. If a pause separates preparation "
        "from arrival, identify the actual voice and gesture carrying that expectation; "
        "an unchanged backing loop does not automatically bridge it. Alternatively "
        "complete the sentence earlier and let the remaining time be an intentional "
        "instrumental release, without restarting the speaker on a detached final tonic. "
        "A long tonic can be a good destination, but its duration cannot substitute "
        "for the answering phrase that earns it. Keep the established dance meter and "
        "allow anticipation and silence; no minimum activity, fixed pause limit, extra "
        "solo or compulsory cadence formula. Choose and write all pitches, onsets and "
        "durations yourself. Preserve successful rhythmic identities while connecting "
        "their musical consequences.\n";
}
inline juce::String musicalStandard() {
    return "\nMUSICAL STANDARD / DEVELOPMENT WITH PURPOSE: Judge musical quality by the "
        "listener's ability to recognize, anticipate and follow meaningful consequences, "
        "not by note count, complexity, perpetual change or correctness of scale alone. "
        "IDENTITY: establish a distinctive pitch/rhythm gesture and enough recurrence to "
        "remember it. DIRECTION: a phrase has a departure, a felt destination and a reason "
        "to continue or stop; let durations, accents and contour articulate that direction. "
        "DIALOGUE: a responding voice has a complete thought of its own, connected to an "
        "actual preceding gesture; neither a duplicate nor an isolated cue. HIERARCHY: "
        "choose what the ear follows and what supports it, with compatible registers and "
        "intentional overlaps. GROOVE: preserve the recognizable metric anchor and its "
        "phase; expressive accents and phrasing live inside that common physical reference. "
        "DEVELOPMENT: change the meaning of familiar material through a chosen destination, "
        "voicing, rhythmic emphasis, phrase length, register or interaction; use only the "
        "changes that this scene earns, not all devices at once. CONTRAST: a held plateau "
        "and silence can create expectation, but layer count or louder velocities alone "
        "do not constitute a new musical thought. HARMONY: voice the actual simultaneous "
        "notes so tension has an intelligible preparation and consequence under the tonal "
        "policy; being in key alone is insufficient. FORM: preparation should make the "
        "return matter; complete the connected terminal sentence already planned, then "
        "allow release instead of reopening it to exhibit more development. These are "
        "contextual listening criteria, not mandatory devices or numerical acceptance gates.\n"
        "SCENE DECISION: In musical_decision, retained_identity names the concrete gesture "
        "or anchor the listener recognizes. development names what changes in THIS scene "
        "relative to its predecessor and why now, or why a sustained plateau is needed. "
        "relationship names the actual owners and how the accompaniment responds to the "
        "speaker. audible_payoff names the expected audible result and its location in "
        "the scene, using actual contour, rhythm, destination or voicing, not adjectives "
        "like richer or more emotional. Reconcile this decision with each voice's existing "
        "statement/continuation/arrival and layer states; a promise without notes is not "
        "development. An opening establishes rather than transforms; a release completes "
        "rather than introduces. No compulsory change every fixed number of bars.\n"
        "FOUNDATION AND SECOND THOUGHT: Distinguish a stable sub anchor from a moving bass "
        "if both exist. Keep the anchor dependable; allow the moving bass a purposeful "
        "phrase destination or response at a chosen formal point instead of treating "
        "both roles as immutable accompaniment. Respect cycle-bound permitted attacks; "
        "pitch, gate, accent or selected omission can change meaning without shifting "
        "the grid. A non-core expressive line may be phrase_flexible when planning. "
        "The secondary melodic thought deserves a connected statement, continuation "
        "and consequence in its own natural speaking time, with a recognizable later "
        "relationship if useful. Do not stretch a handful of cues over a whole section. "
        "MIDDLE-PHRASE CONTINUATION: For a scene that promises melodic development, "
        "compose the journey, not only the departure and a prolonged target. In the "
        "existing continuation field name the related answering gesture, its metric "
        "grouping and the intermediate destination that leads to the final one. Give "
        "a substantive later transformation its own musical_arc and concrete reference "
        "when the opening reference no longer describes it; do not leave all concrete "
        "phrase material confined to the introduction and coda. Use only the arcs "
        "actually needed, not one per window or section by default. A held arrival "
        "can follow a developed thought; repeatedly reattacking the same destination "
        "does not itself enact a promised continuation. If a long hold is the intention, "
        "say whether another voice or the moving harmony carries its consequence, or "
        "whether this is a deliberately static plateau. Do not label a plateau as an "
        "extended melodic journey. Let subsequent phrases recall the earlier contour "
        "while answering it through a chosen rhythm, register or harmonic destination. "
        "Preserve Club's metric identity, natural speaking time and the connected final "
        "sentence. This is not a maximum note length, minimum activity rule or command "
        "to decorate every sustain. No obligatory extra run, octave jump or new theme. "
        "Do not compensate with extra tracks, runs, arbitrary syncopations or busy pads. "
        "Make supporting chord voicings respond to the foreground while sustained "
        "harmonic continuity remains audible. All musical decisions and MIDI notes "
        "remain AI-authored; no host-generated variations or filler.\n";
}
inline juce::String brief() {
    return "\nENSEMBLE SCENE COMPOSITION: Each formal section is one scene, possibly longer "
        "than several technical writing windows. Plan ensemble_scenes, one per section. "
        "inherited_material names a recognizable earlier musical idea, or the opening "
        "premise. voices describes the selected owners with actual melodic/harmonic "
        "material, where their statements develop and whom they answer or prepare. "
        "Do not merely list instruments or energy adjectives. Choose a second thought "
        "with its own contour and continuation, internal harmonic movement and connective "
        "gestures when the work benefits. These are possible functions, not compulsory "
        "extra tracks. Tie named melodic thoughts to the concrete musical_arcs and their "
        "references. For each voice, statement identifies its actual pitch/interval and "
        "metrical gesture; continuation specifies how that same thought proceeds, answers "
        "another owner or returns with changed meaning; arrival identifies its audible "
        "destination and relative bar location. For a stable pad or pulse explicitly "
        "describe continuity, not a fabricated melodic sentence. For a secondary melodic "
        "voice, an isolated cue is not a completed argument: plan its connected trajectory "
        "and consequence, not obligatory activity or a minimum number of notes. These "
        "commitments must agree with musical_arcs, references and layer section states; "
        "do not promise a response during the owner's declared silence. "
        "A line may take focus while another sustains, then return transformed. "
        "Do not make every voice wait for the protagonist to stop or speak constantly. "
        "The transition must grow from this scene and prepare the next; consequence says "
        "what is actually changed or fulfilled. Respect layer activity and shared Club "
        "meter. Keep hypnotic passages, but do not substitute a perpetual backing loop "
        "for a scene's material. Never add a voice merely to reach a track count.\n" + foregroundContinuity() + completeTrackArgument() + connectedArrival() + musicalStandard();
}
inline bool validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!root.hasProperty("ensemble_scenes")) return true;
    const auto* entries=root.getProperty("ensemble_scenes", {}).getArray();
    if (!entries || entries->size()!=static_cast<int>(plan.sections.size())) {
        error="Club ensemble scenes must describe every section"; return false;
    }
    std::set<int> covered;
    std::set<std::string> known;
    for (const auto& owner:plan.instruments) known.insert(owner.id);
    for (const auto& scene:*entries) {
        const auto index=scene.getProperty("section_index", {});
        const auto n=static_cast<int>(index);
        if (!index.isInt() || n<0 || n>=entries->size() || !covered.insert(n).second ||
            !known.contains(scene.getProperty("foreground_id", {}).toString().toStdString())) {
            error="Club ensemble scene has invalid section or focus"; return false;
        }
        for (const auto* key:{"inherited_material","transition","consequence"})
            if (scene.getProperty(key, {}).toString().trim().isEmpty()) {error="Club ensemble scene lacks musical connection";return false;}
        // Old accepted plans remain usable; new schemas require this decision.
        // Validate its structure, never score its aesthetic choices or rewrite MIDI.
        if (scene.hasProperty("musical_decision")) {
            const auto decision = scene.getProperty("musical_decision", {});
            for (const auto* key : {"retained_identity", "development", "relationship", "audible_payoff"})
                if (!decision.getProperty(key, {}).isString() || decision.getProperty(key, {}).toString().trim().isEmpty()) {
                    error="Club ensemble scene has an incomplete musical decision"; return false;
                }
        }
        const auto* voices=scene.getProperty("voices", {}).getArray();
        std::set<std::string> owners;
        if (!voices || voices->isEmpty() || voices->size()>12) {error="Club ensemble scene has invalid voices";return false;}
        for (const auto& voice:*voices) {
            const auto id=voice.getProperty("owner_id", {}).toString().toStdString();
            if (!known.contains(id) || !owners.insert(id).second) {error="Club ensemble scene has unknown or repeated owner";return false;}
            for (const auto* key:{"material","relationship","speaking_span"})
                if (voice.getProperty(key, {}).toString().trim().isEmpty()) {error="Club ensemble voice lacks material";return false;}
            if (voice.hasProperty("statement") || voice.hasProperty("continuation") || voice.hasProperty("arrival"))
                for (const auto* key:{"statement","continuation","arrival"})
                    if (voice.getProperty(key, {}).toString().trim().isEmpty()) {error="Club ensemble voice has an incomplete phrase commitment";return false;}
        }
    }
    return true;
}
inline juce::String context(const SongPlan& plan, std::size_t sectionIndex) {
    const auto root=juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* entries=root.getProperty("ensemble_scenes", {}).getArray();
    if (!entries) return {};
    juce::String out="\nCOMPOSE THIS ENSEMBLE SCENE JOINTLY; accepted notes remain immutable.\n";
    for (int offset=-1;offset<=1;++offset) for (const auto& scene:*entries)
        if (static_cast<int>(scene.getProperty("section_index", -2))==static_cast<int>(sectionIndex)+offset)
            out << (offset<0?"PREVIOUS SCENE: ":offset>0?"NEXT SCENE (do not play early): ":"CURRENT SCENE: ")
                << juce::JSON::toString(scene,true)<<"\n";
    return out + "The writing window is not a new scene. Continue its unfinished statements, "
        "coordinate the foreground, answering voice and moving inner harmony, and prepare "
        "the planned handoff through actual notes. A resting lead need not leave only "
        "anonymous backing; use the scene's other meaningful material without filling "
        "every silence. Realize statement, continuation and arrival in notes: retain the "
        "identity of the statement while carrying its argument toward its destination. "
        "A long note may be a meaningful arrival; neither speed nor density proves "
        "development. Do not restart all arguments or invent another theme each window.\n" + foregroundContinuity() + completeTrackArgument() + connectedArrival() + musicalStandard();
}
}
