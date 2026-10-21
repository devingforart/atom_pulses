#include "plugin/AiEnsembleDiscourse.h"
#include "plugin/AiPhraseArchitecture.h"
#include "plugin/AiCompleteDialogue.h"
#include "plugin/AiMelodicDiscourse.h"
#include "plugin/AiComposer.h"
#include "plugin/AiThematicSubject.h"
#include "core/SovereignScoreRenderer.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void environmentFlag(const char* name, const juce::String& value) {
#ifdef _WIN32
    _putenv_s(name, value.toRawUTF8());
#else
    setenv(name, value.toRawUTF8(), 1);
#endif
}
void flag(const juce::String& value) {
    environmentFlag("PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT", value);
}
}
int main() {
    using namespace pulso;
    using namespace pulso::plugin;
    const auto previous = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT", {});
    const auto previousDialogue = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", {});
    const auto previousMelodic = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", {});
    const auto previousSubject = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", {});
    const auto previousProsody = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_PHRASE_PROSODY_EXPERIMENT", {});
    const auto previousPitched = juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", {});
    try {
        environmentFlag("PULSO_AI_PHRASE_PROSODY_EXPERIMENT", {});
        environmentFlag("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", {});
        flag({});
        const auto original = AiPhraseArchitecture::schema();
        check(AiEnsembleDiscourse::extendSchema(original) == original,
            "disabled experiment changed the accepted schema");
        check(AiEnsembleDiscourse::realizationBrief().isEmpty(),
            "disabled experiment leaked a writing instruction");
        flag("1");
        const auto schema = juce::JSON::parse(AiPhraseArchitecture::schema());
        check(schema.getProperty("properties", {}).getProperty("phrases", {})
            .getProperty("items", {}).getProperty("properties", {})
            .getProperty("ensemble_spans", {}).isObject(), "schema extension missing");
        SongPlan plan;
        for (const auto* id : {"speaker", "partner", "bed"}) {
            InstrumentAssignment part;
            part.id = id;
            plan.instruments.push_back(part);
        }
        const auto phrase = juce::JSON::parse(R"json({"ensemble_spans":[{
          "beat":0,"duration":8,"foreground_ids":["speaker","partner"],
          "pulse_carrier_id":"bed","listening_thread":"related contrary motion",
          "arrival_beat":8,"arrival_goal":"prepared common arrival",
          "parts":[{"instrument_id":"bed","action":"sustain a rich inner tension",
            "attack_relation":"recognize the partners' prepared arrival"}]}]})json");
        juce::String error;
        check(AiEnsembleDiscourse::validate(phrase, 8, plan, error),
            "independent simultaneous foregrounds must remain available");
        plan.phraseArchitectureJson = "{\"phrases\":[" +
            juce::JSON::toString(phrase).toStdString() + "]}";
        check(AiEnsembleDiscourse::foregroundIds(plan) ==
            std::set<std::string>{"speaker", "partner"},
            "all AI-selected foreground partners must enter coordinated writing");
        auto compound = juce::JSON::parse(juce::JSON::toString(phrase));
        auto* parts = compound.getDynamicObject()->getProperty("ensemble_spans").getArray()
            ->getReference(0).getDynamicObject()->getProperty("parts").getArray();
        const auto additionalInstruction = parts->getReference(0);
        parts->add(additionalInstruction);
        check(AiEnsembleDiscourse::validate(compound, 8, plan, error),
            "two textual instructions for one part must not be treated as duplicate notes");
        check(AiEnsembleDiscourse::validate(juce::JSON::parse("{}"), 8, plan, error),
            "legacy phrase plans must remain readable");
        const auto brief = AiEnsembleDiscourse::phraseBrief(phrase, 16, 20, 24);
        check(brief.contains("-4.00..4.00") && brief.contains("arrival=4.00"),
            "cross-window context must retain the full AI intention without clipping");
        check(AiEnsembleDiscourse::phraseBrief(phrase, 0, 8, 16).isEmpty(),
            "a span ending at the window start must not be replayed");
        auto memoryPlan = plan;
        SongSection opening;
        opening.startBar = 0;
        opening.bars = 8;
        memoryPlan.sections = {opening};
        memoryPlan.phraseArchitectureJson = R"json({"phrases":[{
            "section_index":0,"local_start_bar":0,"bars":2,
            "speaker_id":"speaker","response_id":"partner"}]})json";
        Pattern memoryMidi;
        for (int i = 0; i < 20; ++i) {
            NoteEvent note;
            note.partId = 1;
            note.startBeat = i;
            note.durationBeats = 1;
            note.pitch = 69 + i % 12;
            note.velocity = 88;
            memoryMidi.notes.push_back(note);
        }
        const auto snapshot = memoryMidi.notes;
        const auto memory = AiEnsembleDiscourse::actualPhraseMemory(memoryPlan, memoryMidi, 20);
        check(memory.contains("5.00/74/1.00/88") && memory.contains("phrase=0.00..8.00"),
            "memory must retain the first statement's middle, not only its first four notes");
        check(AiEnsembleDiscourse::actualPhraseMemory(memoryPlan, memoryMidi, 4).isEmpty(),
            "unfinished phrases must not be described as completed statements");
        check(memoryMidi.notes.size() == snapshot.size() &&
            memoryMidi.notes[5].startBeat == snapshot[5].startBeat &&
            memoryMidi.notes[5].durationBeats == snapshot[5].durationBeats,
            "providing historical context must not alter MIDI");
        auto dialoguePlan = memoryPlan;
        dialoguePlan.narrativeSpine.protagonistInstrumentId = "speaker";
        dialoguePlan.instruments[0].sourceVoice = VoiceId::Lead;
        dialoguePlan.instruments[1].sourceVoice = VoiceId::Lead;
        for (auto& part : dialoguePlan.instruments) {
            part.minimumPitch = 48; part.maximumPitch = 90;
        }
        const auto owners = AiCompleteDialogue::owners(dialoguePlan, {0, 1, 2});
        check(owners == std::vector<std::size_t>{0, 1},
            "complete dialogue must follow AI speaker identities, not hard-coded instruments");
        auto harmonicAnswer = dialoguePlan;
        harmonicAnswer.instruments[2].sourceVoice = VoiceId::HarmonicFoundation;
        harmonicAnswer.phraseArchitectureJson = R"json({"phrases":[{
            "speaker_id":"speaker","response_id":"bed"}]})json";
        check(AiCompleteDialogue::owners(harmonicAnswer, {0, 1, 2}) ==
            std::vector<std::size_t>{0, 2},
            "an AI-selected harmonic answer must not be excluded by a melodic family label");
        environmentFlag("PULSO_AI_PHRASE_PROSODY_EXPERIMENT", "1");
        auto prosodyPlan = dialoguePlan;
        prosodyPlan.instruments[2].sourceVoice = VoiceId::MovementBass;
        prosodyPlan.phraseArchitectureJson = R"json({"phrases":[{
          "speaker_id":"speaker","response_id":"partner","ensemble_spans":[{
          "beat":0,"duration":8,"foreground_ids":["speaker"],"pulse_carrier_id":"bed",
          "listening_thread":"a related rhythmic sentence","arrival_beat":8,
          "arrival_goal":"a shared prepared release","parts":[],
          "joint_voice_ids":["speaker","partner","bed"],
          "rhythmic_identity":"an eighth-based thought with a deliberate anticipation",
          "articulation_intent":"connected continuation and a sentence-ending breath",
          "metric_supports":[{"beat":2.5,"function":"an intentional anticipation"}]}]}]})json";
        const auto prosodyPhrase = juce::JSON::parse(juce::String::fromUTF8(prosodyPlan.phraseArchitectureJson.c_str()))
            .getProperty("phrases", {})[0];
        check(AiEnsembleDiscourse::validate(prosodyPhrase, 8, prosodyPlan, error),
            "a declared phrase-prosody relationship must accept intentional syncopation");
        auto offsetPhrase = juce::JSON::parse(juce::JSON::toString(prosodyPhrase));
        auto* offsetSpan = offsetPhrase.getDynamicObject()->getProperty("ensemble_spans").getArray()
            ->getReference(0).getDynamicObject();
        offsetSpan->setProperty("beat", 2);
        offsetSpan->setProperty("duration", 4);
        offsetSpan->removeProperty("arrival_beat");
        offsetSpan->setProperty("arrival_offset", 3);
        check(AiEnsembleDiscourse::validate(offsetPhrase, 8, prosodyPlan, error) &&
            AiEnsembleDiscourse::phraseBrief(offsetPhrase, 16, 16, 24).contains("arrival=5.00"),
            "span-relative arrivals must be explicit and translated only in context, never guessed");
        check(AiCompleteDialogue::owners(prosodyPlan, {0, 1, 2}) == std::vector<std::size_t>{0, 1, 2},
            "the AI-selected moving bass must be coauthored with its melodic partners");
        check(AiCompleteDialogue::owners(prosodyPlan, {0, 1}) == std::vector<std::size_t>{0, 1},
            "prosody must not invent an unavailable owner");
        environmentFlag("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", "1");
        auto allPitchedPlan = dialoguePlan;
        allPitchedPlan.instruments[2].sourceVoice = VoiceId::HarmonicFoundation;
        check(AiCompleteDialogue::owners(allPitchedPlan, {0, 1, 2}) == std::vector<std::size_t>{0, 1, 2},
            "full pitched authorship must include secondary support omitted from the foreground plan");
        auto unpitched = allPitchedPlan;
        unpitched.instruments[2].sourceVoice = VoiceId::CoreDrums;
        check(AiCompleteDialogue::owners(unpitched, {0, 1, 2}) == std::vector<std::size_t>{0, 1},
            "full pitched authorship must not turn unpitched percussion into a melodic lane");
        environmentFlag("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", {});
        const auto prosodySchema = AiPhraseArchitecture::schema();
        check(prosodySchema.contains("joint_voice_ids") && prosodySchema.contains("metric_supports") &&
            prosodySchema.contains("arrival_offset") && !prosodySchema.contains("arrival_beat"),
            "the planner must return structured relationships, not just new prose instructions");
        const auto plannedNotesBefore = memoryMidi.notes.size();
        const auto prosodyPrompt = AiCompleteDialogue::prompt("new work", prosodyPlan,
            AiCompleteDialogue::owners(prosodyPlan, {0, 1, 2}), AiCompleteDialogue::blocks(prosodyPlan)[0], memoryMidi);
        check(prosodyPrompt.contains("JOINT PHRASE REALIZATION") && prosodyPrompt.contains("joint_voice_ids") &&
              memoryMidi.notes.size() == plannedNotesBefore,
            "planned supports must be context, never automatically inserted MIDI notes");
        auto invalidProsody = juce::JSON::parse(juce::JSON::toString(prosodyPhrase));
        invalidProsody.getDynamicObject()->getProperty("ensemble_spans").getArray()->getReference(0)
            .getDynamicObject()->getProperty("metric_supports").getArray()->getReference(0)
            .getDynamicObject()->setProperty("beat", 9);
        check(!AiEnsembleDiscourse::validate(invalidProsody, 8, prosodyPlan, error),
            "out-of-span support coordinates must not enter the writer context");
        environmentFlag("PULSO_AI_PHRASE_PROSODY_EXPERIMENT", {});
        check(AiCompleteDialogue::owners(prosodyPlan, {0, 1, 2}) == std::vector<std::size_t>{0, 1},
            "disabled prosody must preserve the previous ownership path");
        check(!AiPhraseArchitecture::schema().contains("metric_supports"),
            "experimental planning must not change the approved shared schema");
        const auto blocks = AiCompleteDialogue::blocks(dialoguePlan);
        check(blocks.size() == 1 && blocks[0].size() == 1 && blocks[0][0].bars == 8,
            "one musical span must not be split into default 16-bar calls");
        auto longPlan = dialoguePlan;
        longPlan.sections[0].bars = 95;
        const auto bounded = AiCompleteDialogue::blocks(longPlan);
        check(bounded.size() == 2 && bounded[0][0].bars == 48 && bounded[1][0].bars == 47,
            "long-form payloads must remain bounded without imposing note patterns");
        check(!juce::JSON::parse(AiCompleteDialogue::schema(dialoguePlan, owners)).isVoid(),
            "complete-dialogue schema must be JSON");
        const auto noteSchema = juce::JSON::parse(AiCompleteDialogue::schema(dialoguePlan, owners))
            .getProperty("properties", {}).getProperty("windows", {}).getProperty("items", {})
            .getProperty("properties", {}).getProperty("parts", {}).getProperty("properties", {})
            .getProperty("speaker", {}).getProperty("items", {}).getProperty("properties", {});
        check(static_cast<int>(noteSchema.getProperty("pitch", {}).getProperty("maximum", 0)) == 102 &&
            static_cast<double>(noteSchema.getProperty("beat", {}).getProperty("multipleOf", 0)) == .25,
            "structured output must express the same technical range and grid already required by the parser");
        HarmonicChord tonic;
        tonic.id = "i_test"; tonic.pitchClasses = {2, 5, 9};
        HarmonicChord turn;
        turn.id = "v_test"; turn.pitchClasses = {9, 1, 4};
        dialoguePlan.chordPalette = {tonic, turn};
        HarmonicEvent initial;
        initial.chordId = "i_test";
        HarmonicEvent changed;
        changed.chordId = "v_test"; changed.barOffset = 1; changed.beatOffset = .5;
        dialoguePlan.sections[0].harmonicEvents = {initial, changed};
        const auto normalizedContext = AiCompleteDialogue::prompt("test", dialoguePlan,
            owners, blocks[0], {});
        check(normalizedContext.contains("0.00..4.50 i_test pcs=2,5,9,") &&
            normalizedContext.contains("4.50..32.00 v_test pcs=9,1,4,"),
            "the dialogue writer must receive exact normalized window-local harmonic intervals");
        auto sketchContext = dialoguePlan;
        sketchContext.phraseArchitectureJson = R"json({"phrases":[{"delivery":{
            "structural_tones":[{"beat":0,"duration":1,"pitch":74,
            "purpose":"a transformed bearing intention"}]}}]})json";
        const auto semanticContext = AiCompleteDialogue::prompt("test", sketchContext,
            owners, blocks[0], {});
        check(!semanticContext.contains("structural_tones") &&
            semanticContext.contains("a transformed bearing intention") &&
            sketchContext.phraseArchitectureJson.find("structural_tones") != std::string::npos,
            "the definitive composer needs musical intent, not competing numeric sketch attacks; originals stay intact");
        environmentFlag("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", "1");
        environmentFlag("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", "1");
        const auto semanticSchema = AiPhraseArchitecture::schema();
        check(semanticSchema.contains("bearing_intentions") &&
            !semanticSchema.contains("structural_tones"),
            "the semantic planner must not request unused numerical sketches");
        const auto semanticDelivery = juce::JSON::parse(R"json({"delivery":{
          "proposition":"a rising question", "continuation":"develop its live implication",
          "arrival":"a prepared answer", "connection":"connected movement",
          "dialogue":"the partner contradicts the question",
          "bearing_intentions":["the opening tension finds a changed destination"]}})json");
        check(AiMelodicDiscourse::validate(semanticDelivery, 8, 48, 90, error),
            "semantic delivery must not fail on nonexistent sketch-note coordinates");
        auto invalidIntention = juce::JSON::parse(juce::JSON::toString(semanticDelivery));
        invalidIntention.getDynamicObject()->getProperty("delivery").getDynamicObject()
            ->getProperty("bearing_intentions").getArray()->set(0, 42);
        check(!AiMelodicDiscourse::validate(invalidIntention, 8, 48, 90, error),
            "semantic intentions still require valid representation");
        environmentFlag("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", "");
        check(AiPhraseArchitecture::schema().contains("structural_tones") &&
            !AiPhraseArchitecture::schema().contains("bearing_intentions"),
            "the accepted production planner must retain its original schema");
        environmentFlag("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", previousDialogue);
        environmentFlag("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", previousMelodic);
        PerformanceScore authored;
        check(AiCompleteDialogue::parse(R"json({"windows":[{"section_index":0,"first_bar":0,
          "parts":{"speaker":[{"beat":0,"duration":1,"pitch":69,"velocity":88}],
          "partner":[{"beat":4,"duration":2,"pitch":57,"velocity":72}]}}]})json",
            dialoguePlan, owners, blocks[0], authored, error),
            "complete AI dialogue notes must pass through the existing MIDI parser");
        check(authored.cells.size() == 2 && authored.placements.size() == 2 &&
            authored.cells[0].notes[0].pitch == 69 && authored.cells[0].notes[0].durationBeats == 1,
            "dialogue parsing must not change the AI notes");
        auto carriedPlan = dialoguePlan;
        carriedPlan.totalBars = 16;
        carriedPlan.targetSeconds = 32;
        carriedPlan.bpm = 120;
        carriedPlan.aiSovereign = true;
        auto nextSection = carriedPlan.sections[0];
        nextSection.startBar = 8;
        carriedPlan.sections.push_back(nextSection);
        const auto carriedBlocks = AiCompleteDialogue::blocks(carriedPlan);
        PerformanceScore carriedScore;
        check(AiCompleteDialogue::parse(R"json({"windows":[
          {"section_index":0,"first_bar":0,"parts":{
            "speaker":[{"beat":31.5,"duration":1,"pitch":69,"velocity":88}],"partner":[]}},
          {"section_index":1,"first_bar":0,"parts":{"speaker":[],"partner":[]}}]})json",
            carriedPlan, owners, carriedBlocks[0], carriedScore, error),
            "a complete-dialogue sustain must be representable across sections");
        PerformanceScoreEngine::normalize(carriedScore, 2, {32, 32}, true);
        carriedPlan.performanceScore = carriedScore;
        const auto carriedMidi = SovereignScoreRenderer::render(carriedPlan, {}, nullptr);
        check(carriedMidi.notes.size() == 1 &&
            std::abs(carriedMidi.notes[0].startBeat - 31.5) < .001 &&
            std::abs(carriedMidi.notes[0].durationBeats - 1) < .001 &&
            carriedMidi.notes[0].pitch == 69 && carriedMidi.notes[0].velocity == 88,
            "cross-section sustain must keep the exact AI attack and release without splitting or clipping");
        PerformanceScore rejectedTail;
        check(!AiComposer::parseDirectWindowJson(
            R"json({"speaker":[{"beat":31.5,"duration":1,"pitch":69,"velocity":88}],"partner":[]})json",
            carriedPlan, owners, 0, 0, 8, rejectedTail, error, false, true),
            "legacy section-local clips must keep their original release policy");
        check(!AiComposer::parseDirectWindowJson(
            R"json({"speaker":[{"beat":31.5,"duration":1,"pitch":69,"velocity":88}],"partner":[]})json",
            carriedPlan, owners, 1, 0, 8, rejectedTail, error, false, true, true),
            "sustain support must still reject releases past the whole work");
        environmentFlag("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", "");
        const auto previousSchema = AiPhraseArchitecture::schema();
        check(AiThematicSubject::extendSchema(previousSchema) == previousSchema,
            "the subject experiment must not alter the approved schema when off");
        environmentFlag("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", "1");
        check(AiPhraseArchitecture::schema().contains("musical_subject"),
            "the thematic planner must request a real reference phrase");
        auto subjectPlan = dialoguePlan;
        subjectPlan.totalBars = 8;
        const auto subject = juce::JSON::parse(R"json({"musical_subject":{
          "instrument_id":"speaker","length_beats":4,
          "notes":[{"beat":0,"duration":4,"pitch":69,"velocity":88}],
          "identity":"a sustained question","continuation":"the held implication",
          "arrival":"a provisional bearing tone","recognizable_return":"a changed context"}})json");
        check(AiThematicSubject::validate(subject, subjectPlan, error),
            "valid AI reference notes must remain available without a density quota");
        auto invalidSubject = juce::JSON::parse(juce::JSON::toString(subject));
        invalidSubject.getDynamicObject()->getProperty("musical_subject").getDynamicObject()
            ->getProperty("notes").getArray()->getReference(0).getDynamicObject()
            ->setProperty("duration", 5);
        check(!AiThematicSubject::validate(invalidSubject, subjectPlan, error),
            "reference-note releases outside their own subject must be rejected, not clipped");
        subjectPlan.phraseArchitectureJson = juce::JSON::toString(subject).toStdString();
        subjectPlan.performanceScore = {};
        check(SovereignScoreRenderer::render(subjectPlan, {}, nullptr).notes.empty(),
            "a reference subject must never insert, copy or develop MIDI by itself");
        const auto reviewSchema = AiThematicSubject::rehearsalSchema(subjectPlan, owners);
        check(reviewSchema.contains("decision") && reviewSchema.contains("keep") &&
            reviewSchema.contains("diagnosis") && reviewSchema.contains("windows"),
            "AI rehearsal must allow retaining the original rather than forcing new music");
        Pattern audition;
        audition.notes.push_back({3.25, 1.5, 69, 88, 1, VoiceId::Lead, 1});
        const auto reviewContext = AiThematicSubject::rehearsalPrompt("test",
            subjectPlan, owners, audition);
        check(reviewContext.contains("3.25/69/1.50/88") && audition.notes.size() == 1 &&
            audition.notes[0].durationBeats == 1.5,
            "rehearsal must inspect the actual complete MIDI without editing it by code");
        check(reviewContext.contains("A4 pc=9") && reviewContext.contains("harmony=i_test"),
            "rehearsal must receive verified pitch names and the actual active chord");
        auto biasedPlan = subjectPlan;
        biasedPlan.narrativeSpine.resolution = "UNVERIFIED_POETIC_SUCCESS";
        check(!AiThematicSubject::rehearsalPrompt("test", biasedPlan, owners, audition)
            .contains("UNVERIFIED_POETIC_SUCCESS"),
            "rehearsal must not receive the plan's assertions as evidence that the music succeeds");
        const auto oldFocus = juce::SystemStats::getEnvironmentVariable("PULSO_AI_NARRATIVE_FOCUS", {});
        environmentFlag("PULSO_AI_NARRATIVE_FOCUS", "make the answering sentence audible");
        check(AiThematicSubject::rehearsalPrompt("test", subjectPlan, owners, audition)
            .contains("EXPLICIT LISTENER FEEDBACK FOR THIS EXPERIMENT: make the answering sentence audible"),
            "a listener's musical criticism must reach the actual AI revision, not only its evaluator");
        const auto editSchema = juce::JSON::parse(AiThematicSubject::rehearsalSchema(subjectPlan, owners));
        const auto editDecision = editSchema.getProperty("properties", {}).getProperty("decision", {})
            .getProperty("enum", {});
        check(editDecision.size() == 1 && editDecision[0].toString() == "revise",
            "a commissioned AI edit must deliver a candidate rather than replacing the task with a grade");
        environmentFlag("PULSO_AI_NARRATIVE_FOCUS", oldFocus);
        environmentFlag("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", previousSubject);
        for (const auto* invalid : {
            R"json({"ensemble_spans":"invalid"})json",
            R"json({"ensemble_spans":[{"beat":0,"duration":9}]})json"})
            check(!AiEnsembleDiscourse::validate(juce::JSON::parse(invalid), 8, plan, error),
                "invalid representation was accepted");
        auto invalid = juce::JSON::parse(juce::JSON::toString(phrase));
        invalid.getDynamicObject()->getProperty("ensemble_spans").getArray()
            ->getReference(0).getDynamicObject()->setProperty("pulse_carrier_id", "invented");
        check(!AiEnsembleDiscourse::validate(invalid, 8, plan, error),
            "an invented instrument was accepted");
        flag(previous);
        environmentFlag("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", previousDialogue);
        environmentFlag("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", previousMelodic);
        environmentFlag("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", previousSubject);
        std::cout << "Ensemble discourse representation and isolation passed\n";
        environmentFlag("PULSO_AI_PHRASE_PROSODY_EXPERIMENT", previousProsody);
        environmentFlag("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", previousPitched);
        return 0;
    } catch (const std::exception& exception) {
        flag(previous);
        environmentFlag("PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", previousDialogue);
        environmentFlag("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", previousMelodic);
        environmentFlag("PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT", previousSubject);
        environmentFlag("PULSO_AI_PHRASE_PROSODY_EXPERIMENT", previousProsody);
        environmentFlag("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", previousPitched);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
