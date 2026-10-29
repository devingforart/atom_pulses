#include "plugin/AiClubProductionPlan.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
void setting(const char* key, const juce::String& value) {
#ifdef _WIN32
    _putenv_s(key, value.toRawUTF8());
#else
    if (value.isEmpty()) unsetenv(key); else setenv(key, value.toRawUTF8(), 1);
#endif
}
}

int main() {
    using namespace pulso;
    using namespace pulso::plugin;
    try {
        SongPlan plan;
        plan.rootPitchClass = 0;
        plan.scale = ScaleKind::Major;
        plan.chordPalette.emplace_back();
        plan.chordPalette.back().id = "tonic";
        plan.chordPalette.back().pitchClasses = {0, 4, 7};
        InstrumentAssignment part;
        part.id = "bass";
        part.minimumPitch = 36;
        part.maximumPitch = 72;
        plan.instruments.push_back(part);
        auto root = juce::JSON::parse(R"json({"groove_core_ids":["bass"],"tonal_nucleus":{
          "reference_chord_id":"tonic","cycle_beats":4,"meter_carrier_id":"bass",
          "audible_relationship":"bass articulates the shared pulse","lanes":[{
            "instrument_id":"bass","repeat_intent":"retain contour",
            "notes":[{"beat":0,"duration":1,"pitch":48,"velocity":90},
                     {"beat":2,"duration":1.5,"pitch":55,"velocity":75}]}]}})json");
        const auto original = juce::JSON::toString(root);
        const auto previousStrict = juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC", {});
        const auto previousFlag = juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_NUCLEUS_V2", {});
        setting("PULSO_AI_ALLOW_CHROMATIC", "0");
        juce::String error;
        check(club::nucleus::validate(root, plan, error), "valid explicit AI reference rejected");
        check(juce::JSON::toString(root) == original, "validation mutated the AI notes");
        auto pulse = juce::JSON::parse(original);
        auto sketch = pulse.getProperty("tonal_nucleus", {});
        sketch.getDynamicObject()->setProperty("carrier_subdivision_beats", .5);
        check(!club::nucleus::validate(pulse, plan, error), "a false eighth-note declaration was accepted");
        sketch.getDynamicObject()->setProperty("carrier_subdivision_beats", 2);
        check(club::nucleus::validate(pulse, plan, error), "a genuinely declared two-beat pulse was rejected");
        check(club::nucleus::writerBrief(root).contains("retain contour"), "writer lost the actual nucleus");
        auto invalid = juce::JSON::parse(original);
        auto first = invalid.getProperty("tonal_nucleus", {}).getProperty("lanes", {})[0]
            .getProperty("notes", {})[0];
        first.getDynamicObject()->setProperty("duration", 5);
        check(!club::nucleus::validate(invalid, plan, error), "out-of-span reference was silently clipped");
        first.getDynamicObject()->setProperty("duration", 1);
        first.getDynamicObject()->setProperty("pitch", 49);
        check(!club::nucleus::validate(invalid, plan, error), "chromatic reference ignored strict tonal policy");
        first.getDynamicObject()->setProperty("pitch", 84);
        check(!club::nucleus::validate(invalid, plan, error), "out-of-register reference was accepted");
        first.getDynamicObject()->setProperty("pitch", 48);
        invalid.getProperty("tonal_nucleus", {}).getDynamicObject()->setProperty("meter_carrier_id", "ghost");
        check(!club::nucleus::validate(invalid, plan, error), "unknown meter carrier was accepted");
        check(club::nucleus::validate(juce::JSON::parse("{}"), plan, error), "legacy plans lost compatibility");
        setting("PULSO_AI_CLUB_NUCLEUS_V2", "");
        check(!juce::JSON::parse(club::production::schema()).getProperty("properties", {}).hasProperty("tonal_nucleus"),
            "experiment leaked into production schema");
        setting("PULSO_AI_CLUB_NUCLEUS_V2", "1");
        check(juce::JSON::parse(club::production::schema({}, &plan)).getProperty("properties", {}).hasProperty("tonal_nucleus"),
            "experimental schema omitted concrete reference");
        check(!club::production::prompt(plan, "test").contains("No full note arrays yet"),
            "new plan retained a contradictory ban on note sketches");
        check(club::production::windowBrief(juce::JSON::toString(root), 0).contains("CONCRETE TONAL NUCLEUS"),
            "actual reference does not reach writing windows");
        check(club::nucleus::writerBrief(root).contains("0.25-beat collective delay") &&
              club::nucleus::writerBrief(root).contains("offbeat quarters"),
            "continuation instructions lost attack-phase and subdivision distinction");
        SongSection firstSection;
        firstSection.startBar = 0;
        firstSection.bars = 8;
        firstSection.harmonicEvents.emplace_back();
        firstSection.harmonicEvents.back().chordId = "tonic";
        SongSection nextSection = firstSection;
        nextSection.startBar = 8;
        plan.sections = {firstSection, nextSection};
        const auto previousClosure = juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", {});
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "1");
        auto full = juce::JSON::parse(R"({"format":"full_arrangement"})");
        const auto arcSchema = juce::JSON::parse(club::production::schema({}, &plan)).getProperty("properties", {})
            .getProperty("musical_arcs", {}).getProperty("items", {}).getProperty("properties", {});
        check(arcSchema.getProperty("melodic_statement", {}).isObject(), "concrete foreground sentence missing from plan schema");
        const auto statementProps = arcSchema.getProperty("melodic_statement", {}).getProperty("properties", {});
        check(statementProps.hasProperty("rhythmic_identity") && statementProps.hasProperty("vertical_support"),
              "phrase plan omitted rhythm identity or actual vertical support");
        check(club::nucleus::planningBrief().contains("VERTICAL VOICING") &&
              club::nucleus::writerBrief(root).contains("realize vertical_support") &&
              club::nucleus::writerBrief(root).contains("relative attack and accent identity"),
              "ensemble coordination did not reach planning and realization");
        check(club::nucleus::planningBrief().contains("CONNECTED MELODIC ENSEMBLE") &&
              club::nucleus::writerBrief(root).contains("planned long-tone functions"), "ensemble sentence did not reach both stages");
        auto melodicArc = juce::JSON::parse(R"({"start_bar":8,"bars":8,"foreground_id":"bass",
            "melodic_statement":{"development":"recall and answer","held_voice":"bass holds its destination",
            "ensemble_relationship":"sequence supports the foreground","notes":[{"beat":4,"duration":8,"pitch":60,"velocity":80}]}})");
        check(club::nucleus::validateMelodicStatement(melodicArc, plan, error), "valid held melodic anchor rejected");
        check(statementProps.hasProperty("ensemble_parts"), "joint ensemble note plan missing");
        auto jointArc = juce::JSON::parse(juce::JSON::toString(melodicArc));
        auto jointParts = juce::JSON::parse(R"([{"instrument_id":"bass","musical_function":"held support resolves the phrase","notes":[{"beat":4,"duration":8,"pitch":60,"velocity":72}]}])");
        jointArc.getProperty("melodic_statement", {}).getDynamicObject()->setProperty("ensemble_parts", jointParts);
        const auto jointBefore = juce::JSON::toString(jointArc);
        check(club::nucleus::validateMelodicStatement(jointArc, plan, error), "valid joint support rejected");
        check(juce::JSON::toString(jointArc) == jointBefore, "joint validation mutated AI notes");
        auto jointRoot = juce::JSON::parse("{}");
        jointRoot.getDynamicObject()->setProperty("musical_arcs", juce::Array<juce::var>{jointArc});
        const auto jointMapped = club::production::developmentBrief(juce::JSON::toString(jointRoot), 40, 48, 4);
        check(jointMapped.contains("JOINT PHRASE owner=bass") && jointMapped.contains("onset=-4 release=4 pitch=60 velocity=72"),
              "joint support lost owner or cross-window coordinates");
        check(!club::production::developmentBrief(juce::JSON::toString(jointRoot), 44, 48, 4).contains("JOINT PHRASE owner="),
              "released support leaked into a later window");
        jointParts[0].getDynamicObject()->setProperty("instrument_id", "unknown-owner");
        check(!club::nucleus::validateMelodicStatement(jointArc, plan, error), "unknown joint owner accepted");
        jointParts[0].getDynamicObject()->setProperty("instrument_id", "bass");
        jointParts[0].getProperty("notes", {})[0].getDynamicObject()->setProperty("duration", 40);
        check(!club::nucleus::validateMelodicStatement(jointArc, plan, error), "out-of-arc joint note accepted");
        check(club::nucleus::planningBrief().contains("COMPOSE JOINT PHRASE LANDMARKS") &&
              club::nucleus::writerBrief(root).contains("use it instead of stacking both voicings"),
              "joint composition not delivered to planner and writer");
        auto emptySupport = juce::JSON::parse(juce::JSON::toString(melodicArc));
        emptySupport.getProperty("melodic_statement", {}).getDynamicObject()->setProperty("vertical_support", "");
        check(!club::nucleus::validateMelodicStatement(emptySupport, plan, error), "blank support declaration accepted");
        auto arcsRoot = juce::JSON::parse("{}");
        arcsRoot.getDynamicObject()->setProperty("musical_arcs", juce::Array<juce::var>{melodicArc});
        const auto mapped = club::production::developmentBrief(juce::JSON::toString(arcsRoot), 40, 48, 4);
        check(mapped.contains("onset=-4") && mapped.contains("release=4"), "held phrase lost its cross-window coordinates");
        check(club::production::developmentBrief(juce::JSON::toString(arcsRoot), 0, 16, 4).isEmpty(),
              "unrelated arc leaked into the current writing window");
        melodicArc.getProperty("melodic_statement", {}).getProperty("notes", {})[0].getDynamicObject()->setProperty("duration", 40);
        check(!club::nucleus::validateMelodicStatement(melodicArc, plan, error), "out-of-arc sustain accepted");
        melodicArc.getProperty("melodic_statement", {}).getProperty("notes", {}).getArray()->clear();
        check(club::nucleus::validateMelodicStatement(melodicArc, plan, error), "intentional instrumental arc rejected");
        check(club::nucleus::validateMelodicStatement(juce::JSON::parse("{}"), plan, error), "legacy arc lost compatibility");
        check(club::nucleus::planningBrief().contains("PURPOSEFUL ARPEGGIO DEVELOPMENT") &&
              club::nucleus::planningBrief().contains("section-local beats"),
              "new compositions did not receive sequence-development planning");
        check(club::nucleus::writerBrief(root).contains("PURPOSEFUL ARPEGGIO DEVELOPMENT") &&
              club::nucleus::writerBrief(root).contains("once-only phrase answer"),
              "planned sequence development did not reach the MIDI writer");
        check(club::nucleus::sequenceDevelopmentBrief(true).contains("No minimum number") &&
              club::nucleus::sequenceDevelopmentBrief(false).contains("carrier subdivision"),
              "sequence variation lost repetition and pulse safeguards");
        auto terminalPlan = juce::JSON::parse(R"({"format":"full_arrangement","terminal_resolution":{"preparation_beat":0,"arrival_beat":24,"resonance_end_beat":32,"lanes":[{"instrument_id":"bass","last_attack_beat":24,"release_beat":32}]}})");
        check(club::nucleus::validateClosure(terminalPlan, plan, error), "valid terminal plan rejected");
        const auto answer = juce::JSON::parse(R"({"instrument_id":"bass","motif_relationship":"recall then arrival","notes":[{"beat":16,"duration":4,"pitch":67,"velocity":80},{"beat":20,"duration":4,"pitch":64,"velocity":76},{"beat":24,"duration":8,"pitch":60,"velocity":74}]})");
        terminalPlan.getProperty("terminal_resolution", {}).getDynamicObject()->setProperty("melodic_answer", answer);
        check(club::nucleus::validateClosure(terminalPlan, plan, error), "valid authored farewell rejected");
        auto absentVoicePlan = plan;
        auto absentVoice = plan.instruments.front();
        absentVoice.id = "counterline";
        absentVoicePlan.instruments.push_back(absentVoice);
        auto zeroAbsent = juce::JSON::parse(juce::JSON::toString(terminalPlan));
        zeroAbsent.getProperty("terminal_resolution", {}).getProperty("lanes", {}).getArray()->add(
            juce::JSON::parse(R"({"instrument_id":"counterline","last_attack_beat":0,"release_beat":0,"joins_arrival":false})"));
        zeroAbsent.getDynamicObject()->setProperty("layers", juce::JSON::parse(
            R"([{"instrument_id":"counterline","states":[{"section_index":1,"mode":"absent","entry_beat":0,"release_beat":0}]}])"));
        const auto originalZeroAbsent = juce::JSON::toString(zeroAbsent);
        check(club::nucleus::validateClosure(zeroAbsent, absentVoicePlan, error),
              "explicit absent final voice using zero bounds rejected");
        check(juce::JSON::toString(zeroAbsent) == originalZeroAbsent, "absence validation mutated the AI plan");
        zeroAbsent.getProperty("terminal_resolution", {}).getProperty("lanes", {})[1].getDynamicObject()->setProperty("joins_arrival", true);
        check(!club::nucleus::validateClosure(zeroAbsent, absentVoicePlan, error), "absent voice allowed to join arrival");
        zeroAbsent.getProperty("terminal_resolution", {}).getProperty("lanes", {})[1].getDynamicObject()->setProperty("joins_arrival", false);
        zeroAbsent.getProperty("layers", {})[0].getProperty("states", {})[0].getDynamicObject()->setProperty("mode", "active");
        check(!club::nucleus::validateClosure(zeroAbsent, absentVoicePlan, error), "active zero-length gesture accepted as silence");
        zeroAbsent.getDynamicObject()->removeProperty("layers");
        check(!club::nucleus::validateClosure(zeroAbsent, absentVoicePlan, error), "undeclared absence inferred from zero bounds");
        zeroAbsent.getProperty("terminal_resolution", {}).getProperty("lanes", {})[1].getDynamicObject()->setProperty("last_attack_beat", -1);
        zeroAbsent.getProperty("terminal_resolution", {}).getProperty("lanes", {})[1].getDynamicObject()->setProperty("release_beat", -1);
        check(club::nucleus::validateClosure(zeroAbsent, absentVoicePlan, error), "legacy explicit absence stopped working");
        auto badAnswer = juce::JSON::parse(juce::JSON::toString(terminalPlan));
        badAnswer.getProperty("terminal_resolution", {}).getProperty("melodic_answer", {}).getProperty("notes", {})[2].getDynamicObject()->setProperty("duration", 12);
        check(!club::nucleus::validateClosure(badAnswer, plan, error), "farewell exceeded its own release bound");
        badAnswer = juce::JSON::parse(juce::JSON::toString(terminalPlan));
        badAnswer.getProperty("terminal_resolution", {}).getProperty("melodic_answer", {}).getDynamicObject()->setProperty("instrument_id", "unknown");
        check(!club::nucleus::validateClosure(badAnswer, plan, error), "farewell accepted an unknown owner");
        Pattern terminalMidi;
        terminalMidi.notes.emplace_back();
        terminalMidi.notes.back().partId = 1;
        terminalMidi.notes.back().pitch = 60;
        terminalMidi.notes.back().startBeat = 60; // section starts 32, last attack promised at 56
        terminalMidi.notes.back().durationBeats = 1;
        juce::String terminalEvidence;
        const auto terminalJson = juce::JSON::toString(terminalPlan);
        check(club::production::missingLayers(terminalJson, plan, terminalMidi, 1, 32, 64, {0}, terminalEvidence).size() == 1,
            "MIDI ignored AI terminal attack promise");
        check(terminalMidi.notes.back().startBeat == 60, "terminal audit moved a note");
        terminalMidi.notes.back().startBeat = 56;
        terminalMidi.notes.back().durationBeats = 8;
        check(club::production::missingLayers(terminalJson, plan, terminalMidi, 1, 56, 64, {0}, terminalEvidence).empty(),
            "AI-authored terminal arrival and release rejected");
        terminalMidi.notes.back().startBeat = 55.75;
        terminalEvidence.clear();
        check(!club::production::missingLayers(terminalJson, plan, terminalMidi, 1, 48, 64, {0}, terminalEvidence).empty() &&
              terminalEvidence.contains("no attack there"), "sixteenth-early arrival escaped the contract");
        check(terminalMidi.notes.back().startBeat == 55.75, "arrival audit retimed the note");
        terminalMidi.notes.back().startBeat = 56;
        terminalMidi.notes.back().pitch = 62;
        terminalEvidence.clear();
        check(!club::production::missingLayers(terminalJson, plan, terminalMidi, 1, 56, 64, {0}, terminalEvidence).empty() &&
              terminalEvidence.contains("farewell note"), "arrival pitch contradicted the AI-authored phrase");
        terminalMidi.notes.back().pitch = 60;
        auto overlapMidi = terminalMidi;
        auto preceding = terminalMidi.notes.front();
        preceding.startBeat = 55;
        preceding.durationBeats = 1.25;
        overlapMidi.notes.push_back(preceding); // deliberately unsorted, crosses window boundary
        terminalEvidence.clear();
        check(!club::production::missingLayers(terminalJson, plan, overlapMidi, 1, 56, 64, {0}, terminalEvidence).empty() &&
              terminalEvidence.contains("same-pitch overlap"), "cross-window same-pitch retrigger escaped closing audit");
        check(overlapMidi.notes.back().durationBeats == 1.25, "closing audit trimmed a sustain");
        overlapMidi.notes.back().durationBeats = 1;
        terminalEvidence.clear();
        check(club::production::missingLayers(terminalJson, plan, overlapMidi, 1, 56, 64, {0}, terminalEvidence).empty(),
              "touching same-pitch notes were rejected");
        overlapMidi.notes.back().durationBeats = 1.25;
        overlapMidi.notes.back().pitch = 64;
        check(club::production::missingLayers(terminalJson, plan, overlapMidi, 1, 56, 64, {0}, terminalEvidence).empty(),
              "different-pitch legato was rejected");
        overlapMidi.notes.back().pitch = 60;
        check(club::production::missingLayers(terminalJson, plan, overlapMidi, 1, 56, 64, {}, terminalEvidence).empty(),
              "audit repaired an unrequested owner");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "0");
        check(club::production::missingLayers(terminalJson, plan, overlapMidi, 1, 56, 64, {0}, terminalEvidence).empty(),
              "overlap experiment leaked into production defaults");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "1");
        check(club::nucleus::closurePlanningBrief().contains("DURING preparation") &&
              club::nucleus::closurePlanningBrief().contains("single neutral piano"),
              "ensemble closing preparation absent from AI plan instructions");
        const auto oldArchitecture = plan.phraseArchitectureJson;
        plan.phraseArchitectureJson = terminalJson.toStdString();
        check(club::production::requiredArrival(plan, "bass", 1, 4, 4) == 8,
              "arrival was not translated into window coordinates");
        check(club::production::requiredArrival(plan, "bass", 1, 0, 4) < 0 &&
              club::production::requiredArrival(plan, "bass", 0, 0, 8) < 0 &&
              club::production::requiredArrival(plan, "unknown", 1, 0, 8) < 0,
              "arrival protocol leaked to another window or owner");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "0");
        check(club::production::requiredArrival(plan, "bass", 1, 0, 8) < 0,
              "arrival protocol changed production defaults");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "1");
        const auto arraySchema = juce::JSON::parse(R"({"type":"array","maxItems":256,"items":{}})");
        auto offbeatPlan = juce::JSON::parse(terminalJson);
        offbeatPlan.getDynamicObject()->setProperty("groove_core_ids", juce::Array<juce::var>{"bass"});
        offbeatPlan.getDynamicObject()->setProperty("layers", juce::JSON::parse(
            R"([{"instrument_id":"bass","cycle_beats":8,"cycle_origin_beat":0,"timing_mode":"cycle_bound","rhythm_cell":[1.5,3.5,5.5,7.5]}])"));
        plan.phraseArchitectureJson = juce::JSON::toString(offbeatPlan).toStdString();
        check(!club::production::coreAttackAllowed(juce::JSON::toString(offbeatPlan), "bass", 56) &&
              club::production::windowAttackAllowed(plan, "bass", 1, 56),
              "explicit terminal landing was blocked by the recurring offbeat pattern");
        check(!club::production::windowAttackAllowed(plan, "bass", 1, 55.75) &&
              !club::production::windowAttackAllowed(plan, "bass", 0, 24) &&
              club::production::windowAttackAllowed(plan, "bass", 1, 55.5),
              "terminal exception changed ordinary cycle attacks");
        auto permitted = club::production::boundedNoteSchema(32);
        auto* permittedChoices = permitted.getProperty("anyOf", {}).getArray();
        for (int i = permittedChoices->size(); --i >= 0;) {
            const auto beat = static_cast<double>((*permittedChoices)[i].getProperty("properties", {}).getProperty("beat", {}).getProperty("enum", {})[0]);
            if (!club::production::windowAttackAllowed(plan, "bass", 1, 32 + beat)) permittedChoices->remove(i);
        }
        check(club::production::withRequiredArrival(arraySchema, permitted, 24).isObject(),
              "offbeat-to-arrival schema still throws");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "0");
        check(!club::production::windowAttackAllowed(plan, "bass", 1, 56), "arrival exception leaked to production");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "1");
        offbeatPlan.getProperty("terminal_resolution", {}).getProperty("lanes", {})[0].getDynamicObject()->setProperty("joins_arrival", false);
        plan.phraseArchitectureJson = juce::JSON::toString(offbeatPlan).toStdString();
        check(!club::production::windowAttackAllowed(plan, "bass", 1, 56), "nonjoining lane gained an invented arrival");
        plan.phraseArchitectureJson = terminalJson.toStdString();
        const auto resolvedItems = club::production::boundedNoteSchema(32);
        const auto arrivalSchema = club::production::withRequiredArrival(arraySchema, resolvedItems, 24);
        const auto landingSchema = arrivalSchema.getProperty("properties", {}).getProperty("arrival_notes", {});
        check(static_cast<int>(landingSchema.getProperty("minItems", 0)) == 1 &&
              static_cast<double>(landingSchema.getProperty("items", {}).getProperty("properties", {})
                  .getProperty("beat", {}).getProperty("enum", {})[0]) == 24,
              "arrival schema did not require the exact landing");
        check(resolvedItems.getProperty("anyOf", {}).getArray()->size() == 128,
              "arrival schema mutated shared note choices");
        for (const auto& choice : *arrivalSchema.getProperty("properties", {}).getProperty("notes", {})
             .getProperty("items", {}).getProperty("anyOf", {}).getArray())
            check(static_cast<double>(choice.getProperty("properties", {}).getProperty("beat", {}).getProperty("enum", {})[0]) != 24,
                  "ordinary notes can duplicate the arrival container");
        bool incompatible = false;
        try { club::production::withRequiredArrival(arraySchema, resolvedItems, 32); }
        catch (const std::runtime_error&) { incompatible = true; }
        check(incompatible, "incompatible arrival reached paid generation");
        auto response = juce::JSON::parse(R"({"notes":[{"beat":20,"pitch":64,"duration":4,"velocity":76}],
            "arrival_notes":[{"beat":24,"pitch":60,"duration":8,"velocity":74}]})");
        juce::Array<juce::var> events;
        check(club::production::readArrivalNotes(response, 24, events, error) && events.size() == 2 &&
              juce::JSON::toString(events[1]) == juce::JSON::toString(response.getProperty("arrival_notes", {})[0]),
              "arrival decoder changed authored MIDI");
        response.getProperty("arrival_notes", {})[0].getDynamicObject()->setProperty("beat", 23.75);
        events.clear();
        check(!club::production::readArrivalNotes(response, 24, events, error) && events.isEmpty(),
              "arrival decoder accepted an anticipation");
        response.getProperty("arrival_notes", {}).getArray()->clear();
        check(!club::production::readArrivalNotes(response, 24, events, error), "empty landing accepted");
        plan.phraseArchitectureJson = oldArchitecture;
        auto badTerminal = juce::JSON::parse(juce::JSON::toString(terminalPlan));
        badTerminal.getProperty("terminal_resolution", {}).getDynamicObject()->setProperty("arrival_beat", 32);
        check(!club::nucleus::validateClosure(badTerminal, plan, error), "arrival at the cutoff accepted as resonance");
        auto absentTerminal = juce::JSON::parse(juce::JSON::toString(terminalPlan));
        absentTerminal.getProperty("terminal_resolution", {}).getProperty("lanes", {})[0].getDynamicObject()->setProperty("instrument_id", "missing");
        check(!club::nucleus::validateClosure(absentTerminal, plan, error), "unknown terminal owner accepted");
        check(juce::JSON::parse(club::production::schema({}, &plan)).getProperty("properties", {}).hasProperty("terminal_resolution"), "closure schema missing");
        check(club::nucleus::closingBrief(full, plan, 0).isEmpty(), "closure leaked into an interior section");
        check(club::nucleus::closingBrief(full, plan, 1).contains("outer bound"), "final arrangement omitted resolution guidance");
        check(club::nucleus::closingBrief(terminalPlan, plan, 1).contains("motif_relationship"), "authored melodic farewell did not reach the writer");
        check(club::nucleus::closingBrief(juce::JSON::parse(R"({"format":"continuous_section"})"), plan, 1).isEmpty(), "closure changed a loop excerpt");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", "0");
        check(club::nucleus::closingBrief(full, plan, 1).isEmpty(), "closure changed production defaults");
        check(!club::nucleus::planningBrief().contains("PURPOSEFUL ARPEGGIO DEVELOPMENT") &&
              !club::nucleus::writerBrief(root).contains("PURPOSEFUL ARPEGGIO DEVELOPMENT"),
              "sequence development changed the approved production motor");
        check(!juce::JSON::parse(club::production::schema({}, &plan)).getProperty("properties", {})
                  .getProperty("musical_arcs", {}).getProperty("items", {}).getProperty("properties", {}).hasProperty("melodic_statement"),
              "melodic statement schema leaked into production");
        setting("PULSO_AI_CLUB_CLOSURE_EXPERIMENT", previousClosure);
        const auto harmony = club::nucleus::harmonyWindow(plan, 28, 36);
        check(harmony.contains("local [0.00,4.00) absolute [28.00,32.00)") &&
              harmony.contains("local [4.00,8.00) absolute [32.00,36.00)"),
            "window-local harmony lost the preceding held chord or a section boundary");
        setting("PULSO_AI_ALLOW_CHROMATIC", previousStrict);
        setting("PULSO_AI_CLUB_NUCLEUS_V2", previousFlag);
        std::cout << "Club nucleus tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
