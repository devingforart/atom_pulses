#include "plugin/AiPhraseDevelopment.h"
#include "plugin/AiThematicSubject.h"
#include "plugin/AiPhraseArchitecture.h"
#include "plugin/AiCompleteDialogue.h"
#include "plugin/AiMaterialDialogue.h"
#include "plugin/AiGestureScore.h"
#include "plugin/AiComposer.h"
#include "core/SovereignScoreRenderer.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void flag(const char* name, const juce::String& value) {
#ifdef _WIN32
    _putenv_s(name, value.toRawUTF8());
#else
    setenv(name, value.toRawUTF8(), 1);
#endif
}
struct Restore {
    const char* name;
    juce::String previous;
    Restore(const char* n) : name(n), previous(juce::SystemStats::getEnvironmentVariable(n, {})) {}
    ~Restore() { flag(name, previous); }
};
}
int main() {
    using namespace pulso;
    using namespace pulso::plugin;
    Restore development("PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT");
    Restore thematic("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT");
    Restore groundwork("PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT");
    Restore consequential("PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT");
    Restore wholeArgument("PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT");
    Restore completeDialogue("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT");
    Restore materialDialogue("PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT");
    Restore expectationLed("PULSO_AI_EXPECTATION_LED_EXPERIMENT");
    Restore gestureScore("PULSO_AI_GESTURE_SCORE_EXPERIMENT");
    try {
        flag(gestureScore.name, {});
        flag(expectationLed.name, {});
        flag(materialDialogue.name, {});
        flag(wholeArgument.name, {});
        flag(consequential.name, {});
        flag(groundwork.name, {});
        const juce::String minimalMacro = R"({"type":"object","properties":{"title":{"type":"string"}},"required":["title"],"additionalProperties":false})";
        check(AiPhraseDevelopment::macroSchema(minimalMacro) == minimalMacro, "disabled groundwork changed macro");
        flag(development.name, {}); flag(thematic.name, "1");
        const auto legacy = AiPhraseArchitecture::schema();
        check(!legacy.contains("thematic_materials") && AiPhraseDevelopment::extendSchema(legacy) == legacy,
            "disabled experiment changed the accepted schema");
        flag(development.name, "1");
        flag(groundwork.name, "1");
        const auto macro = juce::JSON::parse(AiPhraseDevelopment::macroSchema(minimalMacro));
        check(macro.getProperty("required", {})[0].toString() == "composition_groundwork" &&
            macro.getProperty("properties", {}).getProperty("title", {}).isObject(),
            "concrete passage must precede the retained macro contract");
        auto passage = juce::JSON::parse(R"json({"composition_groundwork":{
          "key":"C major","length_beats":8,"identity":"a complete thought","formal_consequence":"return changed",
          "voices":[{"role":"speaker","notes":[{"beat":0,"duration":2,"pitch":64,"velocity":80}]}],
          "harmony":[{"beat":0,"duration":8,"bass_pitch":48,"pitch_classes":[0,4,7],"function":"tonic","arrival":"closed"}],
          "phrases":[{"beat":0,"duration":8,"speaker_role":"speaker","relationship":"statement","destination":"tonic"}]
        }})json");
        juce::String passageError;
        const auto passageOriginal = juce::JSON::toString(passage);
        check(AiPhraseDevelopment::validateGroundwork(passage, 32, passageError), "valid joint passage rejected");
        check(juce::JSON::toString(passage) == passageOriginal, "groundwork validation mutated music");
        auto indexed = juce::JSON::parse(passageOriginal);
        auto* indexedPhrase = indexed.getDynamicObject()->getProperty("composition_groundwork").getDynamicObject()
            ->getProperty("phrases").getArray()->getReference(0).getDynamicObject();
        indexedPhrase->removeProperty("speaker_role"); indexedPhrase->setProperty("speaker_voice_index", 0);
        check(AiPhraseDevelopment::validateGroundwork(indexed, 32, passageError),
            "explicit voice index was rejected");
        indexedPhrase->setProperty("speaker_voice_index", 1);
        check(!AiPhraseDevelopment::validateGroundwork(indexed, 32, passageError),
            "out-of-roster reference was silently assigned to a real voice");
        indexedPhrase->setProperty("speaker_voice_index", 0.5);
        check(!AiPhraseDevelopment::validateGroundwork(indexed, 32, passageError),
            "fractional voice index was accepted");
        check(!AiPhraseDevelopment::validateGroundwork(passage, 4, passageError), "passage exceeds requested work");
        passage.getDynamicObject()->getProperty("composition_groundwork").getDynamicObject()
            ->getProperty("voices").getArray()->getReference(0).getDynamicObject()
            ->getProperty("notes").getArray()->getReference(0).getDynamicObject()->setProperty("duration", 9);
        check(!AiPhraseDevelopment::validateGroundwork(passage, 32, passageError), "invalid release silently clipped");
        check(AiPhraseDevelopment::groundworkContext(passageOriginal).contains("complete initial statement"),
            "downstream writer lost jointly authored musical identity");
        flag(consequential.name, "1");
        auto developed = juce::JSON::parse(passageOriginal);
        const auto decision = juce::JSON::parse(R"json({"name":"development",
          "development_strategy":{"retained_identity":"retain the initial falling third",
          "changed_relationship":"intentionally unchanged, preserving an established ostinato",
          "ensemble_behavior":"bass holds while the speaker finishes its thought",
          "listener_consequence":"a prepared arrival without forced novelty"}})json");
        juce::Array<juce::var> developedSections; developedSections.add(decision);
        developed.getDynamicObject()->setProperty("sections", developedSections);
        const auto untouchedDevelopment = juce::JSON::toString(developed);
        check(AiPhraseDevelopment::validateGroundwork(developed, 32, passageError),
            "purposeful stability was incorrectly required to transform notes");
        check(juce::JSON::toString(developed) == untouchedDevelopment,
            "development validation modified the authored passage");
        const auto strategies = juce::JSON::parse(AiPhraseDevelopment::developmentStrategies(untouchedDevelopment));
        check(strategies.size() == 1 && strategies[0].getProperty("section_index", -1) == juce::var(0) &&
            strategies[0].getProperty("decision", {}).getProperty("changed_relationship", {}).toString().contains("unchanged"),
            "downstream planning lost the actual musical decision or its section association");
        const auto minimalSectionsMacro = juce::String(R"json({"type":"object","additionalProperties":false,
          "required":["sections"],"properties":{"sections":{"type":"array","items":{
          "type":"object","additionalProperties":false,"required":["name"],"properties":{"name":{"type":"string"}}}}}})json");
        const auto developedSchema = juce::JSON::parse(AiPhraseDevelopment::macroSchema(minimalSectionsMacro));
        check(developedSchema.getProperty("properties", {}).getProperty("sections", {}).getProperty("items", {})
            .getProperty("properties", {}).getProperty("development_strategy", {}).isObject(),
            "macro schema did not ask for general ensemble development decisions");
        developed.getDynamicObject()->getProperty("sections").getArray()->getReference(0).getDynamicObject()
            ->getProperty("development_strategy").getDynamicObject()->setProperty("ensemble_behavior", "");
        check(!AiPhraseDevelopment::validateGroundwork(developed, 32, passageError),
            "missing descriptive development field was accepted");
        flag(consequential.name, {});
        check(AiPhraseDevelopment::developmentStrategies(untouchedDevelopment).isEmpty(),
            "disabled development choices leaked into the anchor");
        flag(groundwork.name, {});
        const auto schema = AiPhraseArchitecture::schema();
        check(schema.contains("thematic_materials") && schema.contains("material_development"),
            "new planning schema omitted authored secondary material or its relationships");
        SongPlan plan;
        plan.totalBars = 8; plan.beatsPerBar = 4; plan.bpm = 120; plan.aiSovereign = true;
        InstrumentAssignment main; main.id = "main"; main.name = "Speaker";
        main.minimumPitch = 48; main.maximumPitch = 84; main.sourceVoice = VoiceId::Lead;
        InstrumentAssignment answer = main; answer.id = "partner"; answer.name = "Partner";
        plan.instruments = {main, answer};
        SongSection section; section.bars = 8; section.name = "test"; plan.sections = {section};
        const auto bound = juce::JSON::parse(AiPhraseArchitecture::schema(plan));
        const auto allowedOwners = bound.getProperty("properties", {}).getProperty("thematic_materials", {})
            .getProperty("items", {}).getProperty("properties", {}).getProperty("instrument_id", {})
            .getProperty("enum", {});
        check(allowedOwners.size() == 2 && allowedOwners[0].toString() == "main" &&
            allowedOwners[1].toString() == "partner",
            "API schema must bind material ownership to the actual AI-selected cast");
        auto root = juce::JSON::parse(R"json({
          "musical_subject":{"instrument_id":"main","length_beats":4,
            "notes":[{"beat":0,"duration":1,"pitch":69,"velocity":80}],
            "identity":"claim","continuation":"consequence","arrival":"local arrival",
            "recognizable_return":"changed return"},
          "thematic_materials":[{"id":"contrast","instrument_id":"partner","length_beats":4,
            "notes":[{"beat":0.25,"duration":0.75,"pitch":72,"velocity":74}],
            "identity":"independent thought","continuation":"its own answer","arrival":"a destination",
            "recognizable_return":"a changed later encounter","relationship_to_primary":"a contrary implication"}],
          "phrases":[{"material_development":{"material_id":"contrast","inherited_from":"primary",
            "musical_task":"develop the implication","partner_task":"sustain the consequence",
            "heard_consequence":"a prepared destination"}}]})json");
        juce::String error;
        const auto original = juce::JSON::toString(root);
        check(AiThematicSubject::validate(root, plan, error), "valid AI material and subdivision was rejected");
        check(juce::JSON::toString(root) == original, "validating references altered authored notes");
        auto descriptive = juce::JSON::parse(original);
        descriptive.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->getProperty("material_development").getDynamicObject()->setProperty("inherited_from",
                "primary: the delayed descending consequence, answered by the partner's contrary motion");
        check(AiPhraseDevelopment::validate(descriptive, plan, error),
            "a valid free musical inheritance was incorrectly treated as an exact identifier");
        plan.phraseArchitectureJson = original.toStdString();
        check(SovereignScoreRenderer::render(plan, {}, nullptr).notes.empty(),
            "reference material created real MIDI without AI realization");
        flag(completeDialogue.name, "1"); flag(wholeArgument.name, "1");
        auto argument = juce::JSON::parse(original);
        juce::Array<juce::var> argumentIds; argumentIds.add("main");
        argument.getDynamicObject()->setProperty("argument_owner_ids", argumentIds);
        argument.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->setProperty("speaker_id", "main");
        argument.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->setProperty("response_id", "");
        plan.narrativeSpine.protagonistInstrumentId = "main";
        check(AiCompleteDialogue::validateArgumentOwners(argument, plan, error),
            "valid AI-selected argument partnership was rejected");
        plan.phraseArchitectureJson = juce::JSON::toString(argument).toStdString();
        check(AiCompleteDialogue::owners(plan, {0, 1}) == std::vector<std::size_t>{0} &&
            !AiCompleteDialogue::pitchedEnsembleEnabled(),
            "the whole cast was silently included in the reduced argument");
        auto longPlan = plan; longPlan.totalBars = 88;
        longPlan.sections[0].bars = 40;
        auto later = longPlan.sections[0]; later.startBar = 40; later.bars = 48;
        longPlan.sections.push_back(later);
        const auto allWindows = AiCompleteDialogue::blocks(longPlan);
        check(allWindows.size() == 1 && allWindows[0].size() == 2 &&
            allWindows[0][0].bars == 40 && allWindows[0][1].bars == 48,
            "complete argument was partitioned into serial composition requests");
        check(AiPhraseArchitecture::schema(plan).contains("argument_owner_ids"),
            "the AI cannot choose its argument voices through the actual request schema");
        const auto argumentSchemaBefore = AiPhraseArchitecture::schema(plan);
        flag(materialDialogue.name, "1");
        check(AiPhraseArchitecture::schema(plan).contains("material_design") &&
            AiMaterialDialogue::realizationBrief().contains("not just transposed copies"),
            "whole argument did not receive the general material-interaction contract");
        auto materialArgument = juce::JSON::parse(juce::JSON::toString(argument));
        juce::Array<juce::var> bothOwners; bothOwners.add("main"); bothOwners.add("partner");
        materialArgument.getDynamicObject()->setProperty("argument_owner_ids", bothOwners);
        materialArgument.getDynamicObject()->setProperty("material_design", juce::JSON::parse(R"json([{
          "material_id":"primary","recognizable_identity":"a stable rhythmic relation",
          "distinct_contribution":"one theme is sufficient here","interaction":"the bass changes its meaning",
          "motion_partners":[{"instrument_id":"partner","role_change":"sustain while the thought continues"}],
          "development_states":[]}])json"));
        const auto beforeMaterialValidation = juce::JSON::toString(materialArgument);
        check(AiMaterialDialogue::validate(materialArgument, plan, error) &&
            juce::JSON::toString(materialArgument) == beforeMaterialValidation,
            "descriptive validation modified music or required a number of transformations");
        auto monoArgument = juce::JSON::parse(beforeMaterialValidation);
        monoArgument.getDynamicObject()->setProperty("thematic_materials", juce::Array<juce::var>{});
        monoArgument.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->getProperty("material_development").getDynamicObject()->setProperty("material_id", "primary");
        check(AiMaterialDialogue::validate(monoArgument, plan, error),
            "a valid monothematic work was forced to invent secondary themes");
        check(AiMaterialDialogue::accompanimentContext(materialArgument).contains("motion_partners"),
            "the accompaniment lost the material motion partnership context");
        materialArgument.getDynamicObject()->getProperty("material_design").getArray()->getReference(0)
            .getDynamicObject()->setProperty("material_id", "unavailable");
        check(!AiMaterialDialogue::validate(materialArgument, plan, error),
            "an unknown material was silently mapped to the main theme");
        flag(materialDialogue.name, {});
        check(AiPhraseArchitecture::schema(plan) == argumentSchemaBefore &&
            AiMaterialDialogue::realizationBrief().isEmpty(), "disabling material dialogue changed the reference generator");
        const auto referenceDialogue = AiCompleteDialogue::prompt("test", plan, {0}, {{0, 0, 1}}, {});
        flag(expectationLed.name, "1");
        const auto expectationPrompt = AiCompleteDialogue::prompt("test", plan, {0}, {{0, 0, 1}}, {});
        check(AiMaterialDialogue::expectationLedEnabled() &&
            AiPhraseArchitecture::schema(plan) == argumentSchemaBefore &&
            expectationPrompt.contains("EXPECTATION-LED WHOLE SCORE") &&
            expectationPrompt.contains("EXACT NORMALIZED HARMONY") &&
            expectationPrompt.contains("REFERENCE SUBJECT") &&
            !expectationPrompt.contains("EXISTING AI PHRASE INTENTIONS") &&
            !expectationPrompt.contains("JOINT PHRASE REALIZATION"),
            "expectation-led writing stacked old prose or lost exact musical context");
        check(AiPhraseArchitecture::windowBrief(plan.phraseArchitectureJson.c_str(), plan, 0, 0, 1)
            .contains("EXPECTATION-LED ACCOMPANIMENT"), "support lost the notes-first musical instruction");
        flag(gestureScore.name, "1");
        auto gesturePlan = plan;
        gesturePlan.totalBars = 8; gesturePlan.sections.front().bars = 4;
        auto gestureLater = gesturePlan.sections.front(); gestureLater.startBar = 4;
        gesturePlan.sections.push_back(gestureLater);
        const auto gestureWindows = AiCompleteDialogue::blocks(gesturePlan).front();
        const juce::String gestures = R"json({"parts":{"main":[{"start_beat":14,
          "character":"one connected phrase","relationship":"sustains through the section coordinate",
          "notes":[{"beat":0.5,"duration":3.25,"pitch":69,"velocity":91},
          {"beat":4.25,"duration":0.75,"pitch":72,"velocity":73}]}],"partner":[]}})json";
        PerformanceScore gestureParsed;
        check(AiGestureScore::enabled() && AiGestureScore::parse(gestures, gesturePlan, {0, 1},
            gestureWindows, gestureParsed, error), "explicit cross-section gesture parsing failed");
        const auto normalizedGestures = PerformanceScoreEngine::normalize(gestureParsed, 2, {16, 16}, true);
        check(!normalizedGestures.notesRejected && !normalizedGestures.placementsRejected,
            "gesture bookkeeping normalization rejected explicit MIDI");
        gesturePlan.performanceScore = gestureParsed;
        const auto gestureMidi = SovereignScoreRenderer::render(gesturePlan, {}, nullptr);
        check(gestureMidi.notes.size() == 2 &&
            std::any_of(gestureMidi.notes.begin(), gestureMidi.notes.end(), [](const auto& n) {
                return n.startBeat == 14.5 && n.durationBeats == 3.25 && n.pitch == 69 && n.velocity == 91;
            }) && std::any_of(gestureMidi.notes.begin(), gestureMidi.notes.end(), [](const auto& n) {
                return n.startBeat == 18.25 && n.durationBeats == .75 && n.pitch == 72 && n.velocity == 73;
            }), "gesture translation added, clipped, shifted or transformed an authored note");
        const auto gesturePrompt = AiGestureScore::prompt("test", gesturePlan, {0, 1}, gestureWindows, {});
        check(gesturePrompt.contains("ABSOLUTE start..end") && !gesturePrompt.contains("OUTPUT WINDOWS:") &&
            !gesturePrompt.contains("NORMALIZED FORM:") &&
            !gesturePrompt.contains("WINDOW-LOCAL, duration") &&
            juce::JSON::parse(AiGestureScore::schema(gesturePlan, {0, 1})).isObject(),
            "gesture writing retained contradictory window coordinates");
        const auto reviewAlternatives = juce::JSON::parse(AiThematicSubject::rehearsalSchema(gesturePlan, {0, 1}))
            .getProperty("properties", {}).getProperty("review", {}).getProperty("anyOf", {});
        check(reviewAlternatives.isArray() && reviewAlternatives.size() == 2 &&
            reviewAlternatives[0].getProperty("properties", {}).getProperty("windows", {})
                .getProperty("maxItems", -1) == juce::var(0) &&
            reviewAlternatives[1].getProperty("properties", {}).getProperty("windows", {})
                .getProperty("minItems", -1) == juce::var(2),
            "gesture review does not bind keep versus complete explicit revision scope");
        auto unsafeGesture = juce::JSON::parse(gestures);
        unsafeGesture.getProperty("parts", {}).getProperty("main", {}).getArray()->getReference(0)
            .getDynamicObject()->setProperty("start_beat", 31);
        check(!AiGestureScore::parse(juce::JSON::toString(unsafeGesture), gesturePlan, {0, 1},
            gestureWindows, gestureParsed, error), "out-of-bounds gesture was silently clipped");
        auto repeatedGesture = juce::JSON::parse(gestures);
        auto* repeatedNotes = repeatedGesture.getProperty("parts", {}).getProperty("main", {})
            .getArray()->getReference(0).getProperty("notes", {}).getArray();
        auto repeatedNote = juce::JSON::parse(juce::JSON::toString(repeatedNotes->getReference(0)));
        repeatedNote.getDynamicObject()->setProperty("velocity", 60);
        repeatedNotes->add(repeatedNote);
        check(!AiGestureScore::parse(juce::JSON::toString(repeatedGesture), gesturePlan, {0, 1},
            gestureWindows, gestureParsed, error), "ambiguous same-owner unison attacks were silently merged");
        check(AiGestureScore::reviewEvidence(gesturePlan, gestureMidi, {0, 1}).contains("onset gaps") &&
            gestureMidi.notes.size() == 2, "movement observations modified authored MIDI");
        flag(gestureScore.name, {});
        flag(expectationLed.name, {});
        check(AiCompleteDialogue::prompt("test", plan, {0}, {{0, 0, 1}}, {}) == referenceDialogue,
            "disabling expectation-led writing changed the saved reference behavior");
        argumentIds.add("unknown"); argument.getDynamicObject()->setProperty("argument_owner_ids", argumentIds);
        check(!AiCompleteDialogue::validateArgumentOwners(argument, plan, error),
            "unknown argument voice was silently mapped to the cast");
        flag(wholeArgument.name, {});
        plan.phraseArchitectureJson = original.toStdString();
        auto solo = juce::JSON::parse(original);
        solo.getDynamicObject()->setProperty("thematic_materials", juce::Array<juce::var>{});
        solo.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->getProperty("material_development").getDynamicObject()->setProperty("material_id", "primary");
        check(AiPhraseDevelopment::validate(solo, plan, error), "a monothematic work was forced to add material");
        auto invalid = juce::JSON::parse(original);
        invalid.getDynamicObject()->getProperty("phrases").getArray()->getReference(0).getDynamicObject()
            ->getProperty("material_development").getDynamicObject()->setProperty("material_id", "invented");
        check(!AiPhraseDevelopment::validate(invalid, plan, error), "unknown material relationship was accepted");
        invalid = juce::JSON::parse(original);
        invalid.getDynamicObject()->getProperty("thematic_materials").getArray()->getReference(0).getDynamicObject()
            ->getProperty("notes").getArray()->getReference(0).getDynamicObject()->setProperty("duration", 5);
        check(!AiPhraseDevelopment::validate(invalid, plan, error), "invalid reference release was silently clipped");
        const auto prompt = AiPhraseArchitecture::prompt(plan, "test", false);
        check(!prompt.contains("Return one or two significant phrase intentions") &&
            prompt.contains("A section is not itself a phrase"), "section-sized phrase quota survived in new mode");
        check(AiPhraseDevelopment::castingBrief().contains("not an exclusive right") &&
            AiThematicSubject::realizationBrief().contains("Neither must every line derive from primary"),
            "experimental realization still restricts every idea to the primary seed");
        Pattern actual; actual.notes.push_back({1.25, .75, 69, 80, 1, VoiceId::Lead, 1});
        const auto facts = AiPhraseDevelopment::reviewBrief(plan, actual, {0, 1});
        check(facts.contains("subdivided_onsets=1") && facts.contains("0.75:1") && actual.notes.size() == 1,
            "review facts must describe real durations and onsets without changing them");
        PerformanceScore source;
        const auto windows = AiCompleteDialogue::blocks(plan)[0];
        check(AiCompleteDialogue::parse(R"json({"windows":[{"section_index":0,"first_bar":0,
          "parts":{"main":[{"beat":0.5,"duration":2.5,"pitch":69,"velocity":80}],
          "partner":[{"beat":4,"duration":1,"pitch":60,"velocity":72}]}}]})json",
            plan, {0, 1}, windows, source, error), "scoped-review source could not be parsed");
        source.cells.front().controls.push_back({0, 11, 80, VoiceId::Lead, "main"});
        const auto review = juce::JSON::parse(R"json({"decision":"revise","revised_owner_ids":["partner"],
          "windows":[{"section_index":0,"first_bar":0,"parts":{"main":[],
          "partner":[{"beat":4,"duration":1,"pitch":62,"velocity":72}]}}]})json");
        PerformanceScore selected;
        check(AiThematicSubject::applyScopedRehearsal(review, plan, {0, 1}, source, selected, error),
            "explicit owner-scoped AI revision failed");
        PerformanceScoreEngine::normalize(selected, 1, {32}, true);
        auto selectedPlan = plan; selectedPlan.performanceScore = selected;
        const auto selectedMidi = SovereignScoreRenderer::render(selectedPlan, {}, nullptr);
        check(selectedMidi.notes.size() == 2 && std::any_of(selectedMidi.notes.begin(), selectedMidi.notes.end(),
            [](const auto& n) { return n.pitch == 69 && n.startBeat == .5 && n.durationBeats == 2.5 && n.velocity == 80; }) &&
            std::any_of(selectedMidi.notes.begin(), selectedMidi.notes.end(), [](const auto& n) { return n.pitch == 62; }),
            "an empty unselected owner array erased or altered untouched MIDI");
        check(std::any_of(selected.cells.begin(), selected.cells.end(), [](const auto& c) { return !c.controls.empty(); }),
            "notes-only rehearsal removed existing controls");
        auto ambiguous = juce::JSON::parse(juce::JSON::toString(review));
        ambiguous.getDynamicObject()->removeProperty("revised_owner_ids");
        PerformanceScore rejected;
        check(!AiThematicSubject::applyScopedRehearsal(ambiguous, plan, {0, 1}, source, rejected, error),
            "legacy ambiguous empty arrays were accepted as a whole-score replacement");
        auto outside = juce::JSON::parse(juce::JSON::toString(review));
        auto outsideNotes = juce::JSON::parse(R"json([{"beat":0,"duration":1,"pitch":70,"velocity":80}])json");
        outside.getDynamicObject()->getProperty("windows").getArray()->getReference(0).getDynamicObject()
            ->getProperty("parts").getDynamicObject()->setProperty("main", outsideNotes);
        check(!AiThematicSubject::applyScopedRehearsal(outside, plan, {0, 1}, source, rejected, error),
            "AI notes outside the declared revision scope were silently applied or ignored");
        auto silent = juce::JSON::parse(juce::JSON::toString(review));
        silent.getDynamicObject()->getProperty("windows").getArray()->getReference(0).getDynamicObject()
            ->getProperty("parts").getDynamicObject()->setProperty("partner", juce::Array<juce::var>{});
        check(AiThematicSubject::applyScopedRehearsal(silent, plan, {0, 1}, source, selected, error),
            "an explicitly selected owner's intentional rest must remain available");
        PerformanceScoreEngine::normalize(selected, 1, {32}, true);
        selectedPlan.performanceScore = selected;
        check(SovereignScoreRenderer::render(selectedPlan, {}, nullptr).notes.size() == 1,
            "explicit silence of one selected owner affected another owner");
        flag(development.name, {});
        check(AiPhraseArchitecture::schema() == legacy && AiPhraseDevelopment::realizationBrief().isEmpty(),
            "disabling development must restore the exact previous schema");
        std::cout << "Phrase development representation, authorship and isolation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
