#include "plugin/SongGenerationPipeline.h"
#include "plugin/AiComposer.h"
#include "plugin/AiChromaticPolicy.h"
#include "plugin/AiInstrumentationPolicy.h"
#include "plugin/AiPhraseArchitecture.h"
#include "plugin/AiMelodicDiscourse.h"
#include "plugin/AiClubPolicy.h"
#include "plugin/AiClubProductionPlan.h"
#include "plugin/AiNoteAddress.h"
#include "plugin/JointWindowRecovery.h"
#include "core/SovereignScoreRenderer.h"
#include "core/NarrativeScore.h"
#include "core/HarmonicFloorContext.h"
#include "core/SelectiveRepair.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void samePattern(const pulso::Pattern& left, const pulso::Pattern& right) {
    check(left.notes.size() == right.notes.size(), "hosts rendered different note counts");
    check(left.lengthBeats == right.lengthBeats, "hosts rendered different lengths");
    for (std::size_t i = 0; i < left.notes.size(); ++i) {
        const auto& a = left.notes[i];
        const auto& b = right.notes[i];
        check(a.startBeat == b.startBeat && a.durationBeats == b.durationBeats &&
              a.pitch == b.pitch && a.velocity == b.velocity && a.channel == b.channel &&
              a.voice == b.voice && a.partId == b.partId,
              "hosts rendered different notes from the same request and score");
    }
}

} // namespace

int main() {
    static_assert(pulso::compositionStateBars(pulso::CompositionBehavior::Club) == 64);
    try {
        using namespace pulso;
        using namespace pulso::plugin;
        {
            const auto originalPitch = juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC", {});
            const auto originalCast = juce::SystemStats::getEnvironmentVariable("PULSO_AI_PITCHED_ONLY", {});
            const auto setPolicy = [](const char* key, const juce::String& value) {
#ifdef _WIN32
                _putenv_s(key, value.toRawUTF8());
#else
                if (value.isEmpty()) unsetenv(key);
                else setenv(key, value.toRawUTF8(), 1);
#endif
            };
            setPolicy("PULSO_AI_ALLOW_CHROMATIC", "0");
            setPolicy("PULSO_AI_PITCHED_ONLY", "1");
            SongPlan tonal;
            tonal.rootPitchClass = 0;
            tonal.scale = ScaleKind::Major;
            check(AiChromaticPolicy::allows(tonal, 60) && AiChromaticPolicy::allows(tonal, 84),
                "strict pitch policy must retain every octave of the declared scale");
            check(!AiChromaticPolicy::allows(tonal, 61), "strict pitch policy allowed C sharp in C major");
            tonal.rootPitchClass = 9;
            tonal.scale = ScaleKind::Minor;
            check(!AiChromaticPolicy::allows(tonal, 68), "natural minor must not silently allow a raised leading tone");
            const auto source = juce::String(R"json({"instructions":"Preserve the musical narrative","text":{"format":{"schema":{"type":"object","properties":{"instrument":{"type":"string","enum":["kick_drum","piano","lead_synth"]},"percussion_free":{"type":"boolean"},"rhythm_motifs":{"type":"array","items":{"type":"string"}}}}}}})json");
            const auto prepared = juce::JSON::parse(AiInstrumentationPolicy::prepareRequest(source));
            const auto props = prepared.getProperty("text", {}).getProperty("format", {})
                .getProperty("schema", {}).getProperty("properties", {});
            check(props.getProperty("instrument", {}).getProperty("enum", {}).size() == 2,
                "pitched-only schema must remove drums but retain pitched identities");
            check(static_cast<int>(props.getProperty("rhythm_motifs", {}).getProperty("maxItems", -1)) == 0,
                "pitched-only schemas must disable percussion motifs");
            check(prepared.getProperty("instructions", {}).toString().startsWith("Preserve the musical narrative"),
                "instrument policy must preserve the original musical instructions");
            juce::String policyError;
            check(!AiInstrumentationPolicy::validateCast(R"({"instruments":[{"id":"kit","instrument":"kick_drum","source_voice":"core_drums"}]})", policyError),
                "a percussion cast must not pass the pitched-only boundary");
            check(AiInstrumentationPolicy::validateCast(R"({"instruments":[{"id":"arp","instrument":"lead_synth","source_voice":"lead"}]})", policyError),
                "pitched-only must preserve tonal rhythmic instruments");
            setPolicy("PULSO_AI_ALLOW_CHROMATIC", "1");
            check(AiChromaticPolicy::allows(tonal, 68), "chromatic permission did not enable external pitches");
            check(AiChromaticPolicy::instructions().contains("permitted, not required"),
                "chromatic permission must not demand chromatic notes");
            setPolicy("PULSO_AI_ALLOW_CHROMATIC", originalPitch);
            setPolicy("PULSO_AI_PITCHED_ONLY", originalCast);
        }
        SongPlan orderPlan;
        const auto part = [](std::string id, VoiceId voice, std::string function = {}) {
            InstrumentAssignment owner;
            owner.id = std::move(id);
            owner.name = owner.id;
            owner.instrumentId = owner.id == "upper_body" ? "granular_pad" : "poly_synth";
            owner.sourceVoice = voice;
            owner.orchestralFunction = std::move(function);
            owner.contentLaneId = owner.id;
            return owner;
        };
        orderPlan.instruments = {
            part("bed", VoiceId::HarmonicFoundation, "foundation"),
            part("sub", VoiceId::SubBass),
            part("moving_bass", VoiceId::MovementBass),
            part("speaker", VoiceId::Lead),
            part("upper_body", VoiceId::HarmonicUpper, "body")};
        orderPlan.instruments[0].role = "primary_chord_bed";
        orderPlan.instruments[3].minimumPitch = 69;
        orderPlan.instruments[3].maximumPitch = 91;
        SongSection opening;
        opening.name = "opening";
        opening.startBar = 0;
        opening.bars = 8;
        SongSection answer;
        answer.name = "answer";
        answer.startBar = 8;
        answer.bars = 8;
        orderPlan.sections = {opening, answer};
        const auto phraseSchema = juce::JSON::parse(AiPhraseArchitecture::schema());
        check(!phraseSchema.isVoid(), "AI phrase architecture schema must be valid JSON");
        const juce::String phraseJson = R"json({
          "motif_clue":"ascending third with a delayed final attack",
          "dramatic_question":"the third never reaches its expected landing",
          "answer_principle":"answer that interruption with a changed descent",
          "phrases":[
            {"section_index":0,"local_start_bar":1,"bars":4,
             "speaker_id":"speaker","response_id":"bed",
             "gesture":"state the interrupted ascent","rhythm_shape":"two attacks then a rest",
             "contour":"rise then withhold","register_low":69,"register_high":84,
             "silence_strategy":"leave bar five open","relationship":"bed sustains the question",
             "consequence":"unresolved at the section boundary"},
            {"section_index":1,"local_start_bar":0,"bars":6,
             "speaker_id":"speaker","response_id":"moving_bass",
             "gesture":"transform the ascent into a descending answer",
             "rhythm_shape":"one held attack followed by two short notes",
             "contour":"high to low","register_low":72,"register_high":91,
             "silence_strategy":"rest before final arrival","relationship":"bass answers after the lead",
             "consequence":"arrive below the opening register"}
          ]})json";
        juce::String phraseError;
        check(AiPhraseArchitecture::validate(phraseJson, orderPlan, phraseError),
              "AI phrase architecture should validate for known owners and sections");
        auto deliveryPlan = juce::JSON::parse(phraseJson);
        auto& firstDeliveryPhrase = deliveryPlan.getDynamicObject()
            ->getProperty("phrases").getArray()->getReference(0);
        firstDeliveryPhrase.getDynamicObject()->setProperty("delivery", juce::JSON::parse(R"json({
          "proposition":"rising third leaves its final note pending",
          "continuation":"carry the line toward a changed descending answer",
          "arrival":"a held third before the partner takes the question",
          "connection":"joined motion with a breath after the complete statement",
          "dialogue":"the answer inherits the missing descent rather than restarting",
          "structural_tones":[{"beat":2,"duration":3,"pitch":72,"purpose":"bearing tone"}]
        })json"));
        check(AiPhraseArchitecture::validate(juce::JSON::toString(deliveryPlan),
                  orderPlan, phraseError),
              "a valid AI delivery sketch must coexist with legacy phrase fields");
        check(AiPhraseArchitecture::windowBrief(juce::JSON::toString(deliveryPlan),
                  orderPlan, 0, 0, 8).contains("6.00/3.00/72"),
              "phrase-local bearing tones must be translated into writer-local coordinates");
        auto malformedDelivery = juce::JSON::parse(juce::JSON::toString(deliveryPlan));
        malformedDelivery.getDynamicObject()->getProperty("phrases").getArray()
            ->getReference(0).getDynamicObject()->getProperty("delivery").getDynamicObject()
            ->getProperty("structural_tones").getArray()->getReference(0)
            .getDynamicObject()->setProperty("duration", 40);
        check(!AiPhraseArchitecture::validate(juce::JSON::toString(malformedDelivery),
                  orderPlan, phraseError),
              "an out-of-span AI sketch must be rejected, never clipped or auto-filled");
        const auto originalDiscourse = juce::SystemStats::getEnvironmentVariable(
            "PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", {});
#ifdef _WIN32
        _putenv_s("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", "1");
#else
        setenv("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", "1", 1);
#endif
        const auto deliverySchema = juce::JSON::parse(AiPhraseArchitecture::schema());
        check(deliverySchema.getProperty("properties", {}).getProperty("phrases", {})
                  .getProperty("items", {}).getProperty("properties", {})
                  .getProperty("delivery", {}).isObject(),
              "the experimental structured output must request an actual delivery sketch");
        check(AiMelodicDiscourse::planningBrief(120).contains("0.125 seconds"),
              "the AI must receive durations in perceptual time at the actual tempo");
        check(AiMelodicDiscourse::planningOutputTokenBudget(4) == 11300 &&
                  AiMelodicDiscourse::planningOutputTokenBudget(6) > 11300 &&
                  AiMelodicDiscourse::planningOutputTokenBudget(64) == 20000,
              "the richer AI plan needs bounded output headroom proportional to its sections");
        auto dialoguePlan = orderPlan;
        auto clubPlan = orderPlan;
        clubPlan.compositionBehavior = CompositionBehavior::Club;
        check(club::production::requiresFinaleReview(clubPlan, false, true) &&
                  club::production::requiresFinaleReview(clubPlan, true, false) &&
                  !club::production::requiresFinaleReview(clubPlan, true, true),
              "complete Club songs must reach the ending editor when either ending contract fails");
        auto loopingClub = clubPlan;
        loopingClub.continuousProductionSection = true;
        check(!club::production::requiresFinaleReview(loopingClub, false, false) &&
                  !club::production::requiresFinaleReview(orderPlan, false, false),
              "loop excerpts and other composition engines must not acquire a Club finale obligation");
        const juce::String inheritedMacro = R"({"properties":{"sections":{"minItems":3,"maxItems":24,"items":{"properties":{"bars":{"type":"integer"}}}},"narrative_spine":{"properties":{"acts":{"minItems":3,"maxItems":20}}}}})";
        const auto sectionMacro = juce::JSON::parse(club::production::macroSchema(
            inheritedMacro, CompositionBehavior::Club, "SECCION PLENA de 32 compases", 32));
        const auto songMacro = juce::JSON::parse(club::production::macroSchema(
            inheritedMacro, CompositionBehavior::Club, "complete progressive house song", 96));
        check(static_cast<int>(sectionMacro.getProperty("properties", {}).getProperty("sections", {}).getProperty("minItems", 0)) == 1 &&
                  static_cast<int>(sectionMacro.getProperty("properties", {}).getProperty("sections", {}).getProperty("maxItems", 0)) == 1 &&
                  static_cast<int>(sectionMacro.getProperty("properties", {}).getProperty("sections", {}).getProperty("items", {}).getProperty("properties", {}).getProperty("bars", {}).getProperty("enum", {})[0]) == 32 &&
                  static_cast<int>(songMacro.getProperty("properties", {}).getProperty("sections", {}).getProperty("minItems", 0)) == 3 &&
                  club::production::macroSchema(inheritedMacro, CompositionBehavior::Adaptive, "section", 32) == inheritedMacro,
              "only an explicit Club production section may collapse to one state; full songs and Adaptive keep their form contract");
        const std::vector<std::size_t> grooveOwners{0, 1, 2, 3, 4};
        check(AiMelodicDiscourse::writingGroups(clubPlan, grooveOwners) ==
                  std::vector<std::vector<std::size_t>>{grooveOwners},
              "a compact Club nucleus must be jointly authored, not divided into speakers and companions");
        check(club::grooveBrief().contains("SHARED cycle") &&
                  club::grooveBrief().contains("Choose these positions yourself") &&
                  club::grooveBrief().contains("sustained chord bodies"),
              "Club planning must assign a shared AI-chosen metric grammar without prescribing MIDI");
        auto clubProduction = juce::JSON::parse(R"json({"kind":"club_production_v1",
          "format":"continuous_section","identity":"dark layered loop","focus_owner_id":"",
          "shared_pulse":"joint tonal pulse","layers":[]})json");
        for (const auto& instrument : clubPlan.instruments) {
            auto layer = juce::JSON::parse(R"json({"instrument_id":"","production_role":"test layer",
              "cycle_beats":8,"cycle_origin_beat":0,"rhythm_cell":[0,2,4,6],
              "register_low":48,"register_high":72,"harmonic_job":"complementary harmony",
              "articulation":"sustain","interaction":"retain shared pulse","polyphonic":true,
              "states":[{"section_index":0,"mode":"active","minimum_sounding_fraction":0.5,
                "material":"voiced chord","evolution":"retain"},
                {"section_index":1,"mode":"reduced","minimum_sounding_fraction":0,
                "material":"chosen reply","evolution":"withdraw selectively"}]})json");
            layer.getDynamicObject()->setProperty("instrument_id", juce::String(instrument.id));
            layer.getDynamicObject()->setProperty("register_low", instrument.minimumPitch);
            layer.getDynamicObject()->setProperty("register_high", instrument.maximumPitch);
            clubProduction.getDynamicObject()->getProperty("layers").getArray()->add(layer);
        }
        auto productionJson = juce::JSON::toString(clubProduction);
        check(AiPhraseArchitecture::validate(productionJson, clubPlan, phraseError),
              "Club must accept an exact-cast production plan without a required melodic speaker");
        auto developedProduction = juce::JSON::parse(productionJson);
        auto arc = juce::JSON::parse(R"({"start_bar":0,"bars":16,"foreground_id":"",
          "subject":"a complete rising phrase","continuation":"retain contour and answer its descent",
          "arrival":"arrive on a sustained chord tone","support_response":"pads move inner voices beneath the phrase"})");
        arc.getDynamicObject()->setProperty("foreground_id", juce::String(clubPlan.instruments[0].id));
        developedProduction.getDynamicObject()->setProperty("musical_arcs", juce::Array<juce::var>{arc});
        developedProduction.getDynamicObject()->setProperty("energy_trajectory", juce::Array<juce::var>{
            juce::JSON::parse(R"({"section_index":0,"dramatic_function":"patient discovery","energy_intent":0.35,"density_intent":0.3,"identity_retained":"bass pulse","material_reserved":"complete hook","layer_change":"reveal the pulse","transition_consequence":"creates expectation"})"),
            juce::JSON::parse(R"({"section_index":1,"dramatic_function":"earned arrival","energy_intent":0.78,"density_intent":0.68,"identity_retained":"bass pulse","material_reserved":"final release","layer_change":"return hook with chord body","transition_consequence":"repays the opening"})")});
        check(AiPhraseArchitecture::validate(juce::JSON::toString(developedProduction), clubPlan, phraseError) &&
                  club::production::developmentBrief(juce::JSON::toString(developedProduction), 32, 64, 4)
                    .contains("retain contour") &&
                  club::production::developmentBrief(juce::JSON::toString(developedProduction), 64, 96, 4).isEmpty() &&
                  club::production::windowBrief(juce::JSON::toString(developedProduction), 0)
                    .contains("NEXT ENERGY STATE"),
              "AI musical arguments and energy trajectory must survive technical windows without restarting");
        developedProduction.getDynamicObject()->getProperty("musical_arcs").getArray()->getReference(0)
            .getDynamicObject()->setProperty("foreground_id", "nonexistent");
        check(!AiPhraseArchitecture::validate(juce::JSON::toString(developedProduction), clubPlan, phraseError),
              "musical arcs cannot refer to invented MIDI owners");
        const auto clubSchema = juce::JSON::parse(AiPhraseArchitecture::schema(CompositionBehavior::Club));
        check(!clubSchema.getProperty("properties", {}).hasProperty("phrases") &&
                  clubSchema.getProperty("properties", {}).hasProperty("energy_trajectory"),
              "Club generation must use its own layer schema, not hidden chamber phrases");
        auto productionMidi = Pattern{};
        productionMidi.notes.push_back({0, 8, 60, 70, 1, VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored});
        const auto evidenceBefore = productionMidi.notes.size();
        check(club::production::realization(productionJson, clubPlan, productionMidi).contains("actual_sounding_fraction=0.250") &&
                  productionMidi.notes.size() == evidenceBefore,
              "Club realization must measure union sounding time without fabricating material");
        juce::String layerEvidence;
        check(!club::production::missingLayers(productionJson, clubPlan, productionMidi,
                  0, 0, 32, {0}, layerEvidence).empty() && productionMidi.notes.size() == evidenceBefore,
              "AI-authored sounding-time and polyphony promises must be reviewed without local note repairs");
        productionMidi.notes.push_back({0, 8, 64, 70, 1, VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored});
        check(club::production::missingLayers(productionJson, clubPlan, productionMidi,
                  0, 0, 32, {0}, layerEvidence).empty(),
              "partial activity estimates must not force extra notes or duration changes when actual polyphonic material exists");
        auto productionPlanForWriting = clubPlan;
        productionPlanForWriting.phraseArchitectureJson = productionJson.toStdString();
        auto coreProduction = juce::JSON::parse(productionJson);
        coreProduction.getDynamicObject()->setProperty("groove_core_ids",
            juce::Array<juce::var>{juce::String(clubPlan.instruments[0].id), juce::String(clubPlan.instruments[1].id)});
        const auto coreJson = juce::JSON::toString(coreProduction);
        auto corePlan = productionPlanForWriting;
        corePlan.phraseArchitectureJson = coreJson.toStdString();
        check(club::production::validate(coreJson, corePlan, phraseError) &&
                  club::production::coreAttackAllowed(coreJson, corePlan.instruments[0].id, 32) &&
                  !club::production::coreAttackAllowed(coreJson, corePlan.instruments[0].id, 32.5) &&
                  club::production::coreAttackAllowed(coreJson, corePlan.instruments[4].id, 32.5),
              "core phase is the AI-authored cycle, not arbitrary grid membership or a ban on support syncopation");
        check(AiMelodicDiscourse::writingGroups(corePlan, grooveOwners) ==
                  std::vector<std::vector<std::size_t>>{{0, 1}, {2, 3, 4}},
              "the AI-elected core must be written jointly before support, without deleting owners");
        juce::var phaseResponse(new juce::DynamicObject());
        phaseResponse.getDynamicObject()->setProperty(juce::String(corePlan.instruments[0].id),
            juce::Array<juce::var>{juce::JSON::parse(R"({"beat":0.5,"duration":0.5,"pitch":60,"velocity":80})")});
        PerformanceScore phaseScore;
        check(!AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), corePlan,
                  {0}, 0, 0, 8, phaseScore, phraseError) && phraseError.contains("absolute rhythm phase") && phaseScore.empty(),
              "an unplanned core phase must request AI rewriting, never silently shift notes");
        phaseResponse.getDynamicObject()->getProperty(juce::String(corePlan.instruments[0].id))
            .getArray()->getReference(0).getDynamicObject()->setProperty("beat", 0);
        check(AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), corePlan,
                  {0}, 0, 0, 8, phaseScore, phraseError) &&
                  phaseScore.placements.size() == 1 && phaseScore.placements[0].fragmentEnd == 32,
              "a direct AI passage needs its exact clip span before checkpointing, not only after rendering normalization");
        auto timedProduction = juce::JSON::parse(coreJson);
        auto& timedSupport = timedProduction.getDynamicObject()->getProperty("layers")
            .getArray()->getReference(4);
        timedSupport.getDynamicObject()->setProperty("timing_mode", "cycle_bound");
        timedSupport.getDynamicObject()->setProperty("timing_intent", "share the harmonic arrival without a synthetic MIDI delay");
        const auto timedJson = juce::JSON::toString(timedProduction);
        auto timedPlan = corePlan;
        timedPlan.phraseArchitectureJson = timedJson.toStdString();
        check(club::production::validate(timedJson, timedPlan, phraseError) &&
                  club::production::coreAttackAllowed(timedJson, timedPlan.instruments[4].id, 32) &&
                  !club::production::coreAttackAllowed(timedJson, timedPlan.instruments[4].id, 32.25) &&
                  club::production::coreAttackAllowed(coreJson, corePlan.instruments[4].id, 32.25),
              "a support's explicit timing contract must be honored without imposing it on legacy/free phrases");
        juce::var lateSupport(new juce::DynamicObject());
        lateSupport.getDynamicObject()->setProperty(juce::String(timedPlan.instruments[4].id),
            juce::Array<juce::var>{juce::JSON::parse(R"({"beat":0.25,"duration":1,"pitch":60,"velocity":80})")});
        PerformanceScore untouched;
        check(!AiComposer::parseDirectWindowJson(juce::JSON::toString(lateSupport), timedPlan,
                  {4}, 0, 0, 8, untouched, phraseError) && untouched.empty() &&
                  phraseError.contains("declared absolute rhythm phase"),
              "an unplanned support delay must request AI rewriting, never be silently snapped");
        timedSupport.getDynamicObject()->setProperty("timing_mode", "phrase_flexible");
        check(club::production::coreAttackAllowed(juce::JSON::toString(timedProduction),
                  timedPlan.instruments[4].id, 32.25),
              "intentional syncopation in a flexible phrase must stay possible");
        AiPhraseArchitecture::applyTerminalHandoff(productionJson, productionPlanForWriting);
        auto staged = juce::JSON::parse(coreJson);
        auto state = staged.getProperty("layers", {})[0].getProperty("states", {})[0];
        state.getDynamicObject()->setProperty("entry_beat", 8);
        state.getDynamicObject()->setProperty("release_beat", 16);
        auto stagedPlan = corePlan;
        stagedPlan.phraseArchitectureJson = juce::JSON::toString(staged).toStdString();
        check(club::production::validate(juce::String(stagedPlan.phraseArchitectureJson), stagedPlan, phraseError) &&
                  club::production::activitySpan(stagedPlan, stagedPlan.instruments[0].id, 0) == std::pair<double, double>{8, 16},
              "AI section entry/release decisions must survive the plan without local orchestration");
        check(!AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), stagedPlan,
                  {0}, 0, 0, 8, untouched, phraseError) && phraseError.contains("entry/release"),
              "an early note must be rejected rather than moved to the AI-planned entrance");
        auto note = phaseResponse.getProperty(juce::String(corePlan.instruments[0].id), {})[0];
        note.getDynamicObject()->setProperty("beat", 8);
        note.getDynamicObject()->setProperty("duration", 8.25);
        check(!AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), stagedPlan,
                  {0}, 0, 0, 8, untouched, phraseError) && phraseError.contains("entry/release"),
              "a release after the AI withdrawal must request rewriting, never clipping");
        note.getDynamicObject()->setProperty("duration", 8);
        check(AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), stagedPlan,
                  {0}, 0, 0, 8, untouched, phraseError),
              "the exact authored entrance and complete release remain legal");
        phaseResponse.getDynamicObject()->setProperty(juce::String(corePlan.instruments[0].id), juce::Array<juce::var>{});
        check(AiComposer::parseDirectWindowJson(juce::JSON::toString(phaseResponse), stagedPlan,
                  {0}, 0, 0, 2, untouched, phraseError) && untouched.empty() &&
                  club::production::silentWindow(stagedPlan, {0}, 0, 0, 2),
              "a deliberately delayed core must not be forced to fill an opening window");
        check(club::production::missingLayers(juce::String(stagedPlan.phraseArchitectureJson), stagedPlan,
                  Pattern{}, 0, 0, 8, {0}, layerEvidence).empty(),
              "support recovery must respect an AI-planned delayed entry");
        const auto stagedSchema = juce::JSON::parse(AiComposer::directWindowSchemaFor(stagedPlan, {0}, 0, 8));
        const auto choices = stagedSchema.getProperty("properties", {}).getProperty(
            juce::String(stagedPlan.instruments[0].id), {}).getProperty("items", {}).getProperty("anyOf", {});
        check(choices.size() == 4 && static_cast<double>(choices[0].getProperty("properties", {})
                  .getProperty("beat", {}).getProperty("enum", {})[0]) == 8 &&
                  static_cast<double>(choices[3].getProperty("properties", {})
                  .getProperty("duration", {}).getProperty("maximum", 0)) == 2,
              "generation schema must intersect the AI cycle, entrance and release before any notes are written");
        const auto emptySchema = juce::JSON::parse(AiComposer::directWindowSchemaFor(stagedPlan, {0}, 0, 2));
        check(static_cast<int>(emptySchema.getProperty("properties", {}).getProperty(
                  juce::String(stagedPlan.instruments[0].id), {}).getProperty("maxItems", -1)) == 0,
              "an AI-planned empty window must request an empty note array");
        state.getDynamicObject()->setProperty("release_beat", 100000);
        check(!club::production::validate(juce::JSON::toString(staged), stagedPlan, phraseError) &&
                  phraseError.contains("section_index=0") &&
                  phraseError.contains("release_beat=100000") &&
                  phraseError.contains("allowed range=0..32"),
              "out-of-section activity must identify its exact value and limit for AI revision");
        state.getDynamicObject()->setProperty("entry_beat", 24);
        state.getDynamicObject()->setProperty("release_beat", 8);
        check(!club::production::validate(juce::JSON::toString(staged), stagedPlan, phraseError) &&
                  phraseError.contains("release_beat > entry_beat"),
              "reversed AI activity spans must receive a specific revision reason");
        const auto boundedClubSchema = juce::JSON::parse(AiPhraseArchitecture::schema(stagedPlan, "club"));
        const auto boundedStates = boundedClubSchema.getProperty("properties", {})
            .getProperty("layers", {}).getProperty("items", {}).getProperty("anyOf", {})[0]
            .getProperty("properties", {}).getProperty("states", {})
            .getProperty("items", {}).getProperty("anyOf", {})[0]
            .getProperty("anyOf", {});
        check(static_cast<double>(boundedStates[1].getProperty("properties", {})
                  .getProperty("release_beat", {}).getProperty("maximum", -1)) == 32 &&
                  static_cast<double>(boundedStates[1].getProperty("properties", {})
                  .getProperty("entry_beat", {}).getProperty("maximum", -1)) == 32,
              "Club planning schema must bound each section-local entry and release before AI generation");
        auto batchProduction = juce::JSON::parse(coreJson);
        auto supportEnd = batchProduction.getProperty("layers", {})[4].getProperty("states", {})[0];
        supportEnd.getDynamicObject()->setProperty("entry_beat", 0);
        supportEnd.getDynamicObject()->setProperty("release_beat", 16);
        auto batchPlan = corePlan;
        batchPlan.phraseArchitectureJson = juce::JSON::toString(batchProduction).toStdString();
        check(club::production::windowGroups(batchPlan, {{0, 1}, {2, 3, 4}}, 0, 0, 8) ==
                  std::vector<std::vector<std::size_t>>{{0, 1}, {2, 3}, {4}} &&
                  club::production::windowGroups(corePlan, {{0, 1}, {2, 3, 4}}, 0, 0, 8) ==
                  std::vector<std::vector<std::size_t>>{{0, 1}, {2, 3, 4}},
              "heterogeneous support release schemas must batch without splitting the core or legacy whole-window groups");
        check(productionPlanForWriting.continuousProductionSection && !clubPlan.continuousProductionSection,
              "section scope must be explicit and must not silently change full-song closure semantics");
        const auto sectionFormat = juce::JSON::parse(AiPhraseArchitecture::schema(
            CompositionBehavior::Club, "SECCION PLENA de 32 compases"))
            .getProperty("properties", {}).getProperty("format", {}).getProperty("enum", {});
        check(sectionFormat.size() == 1 && sectionFormat[0].toString() == "continuous_section",
              "an explicit production section must not be relabeled as a complete song by the AI");
        const auto layerWriting = AiComposer::directWindowPromptFor("club loop", productionPlanForWriting,
            Pattern{}, {0, 1, 2, 3, 4}, 0, 0, 8);
        check(layerWriting.contains("FULL PRODUCTION SECTION") &&
                  !layerWriting.contains("ENDING RESPONSIBILITY") &&
                  !layerWriting.contains("protagonist's transformed motif"),
              "the production writer must not inherit chamber endings or mandatory solo development");
        auto wrongMode = clubPlan; wrongMode.compositionBehavior = CompositionBehavior::Adaptive;
        check(!AiPhraseArchitecture::validate(productionJson, wrongMode, phraseError),
              "Club plans must not silently replace the Adaptive phrase engine");
        clubProduction.getDynamicObject()->getProperty("layers").getArray()->getReference(0)
            .getDynamicObject()->getProperty("rhythm_cell").getArray()->add(8);
        check(!AiPhraseArchitecture::validate(juce::JSON::toString(clubProduction), clubPlan, phraseError),
              "cyclic attack offsets at the next cycle boundary must be rejected, not wrapped procedurally");
        const auto clubPhrasePrompt = AiPhraseArchitecture::prompt(clubPlan, "Progressive house", true);
        auto permissionPlan = clubPlan;
        auto laterSection = permissionPlan.sections.front();
        laterSection.name = "Later return";
        laterSection.startBar += laterSection.bars;
        permissionPlan.sections.push_back(laterSection);
        permissionPlan.instruments.front().activeSections = {laterSection.name};
        const auto permissionPrompt = club::production::prompt(permissionPlan, "Complete club song");
        const auto permissionLine = permissionPrompt.fromFirstOccurrenceOf(
            juce::String(permissionPlan.instruments.front().id) + " | ", false, false)
            .upToFirstOccurrenceOf("\n", false, false);
        const auto allowedSections = juce::JSON::parse(permissionLine.fromFirstOccurrenceOf(
            "allowed_section_indices=", false, false));
        check(allowedSections.isArray() && allowedSections.size() == 1 &&
                  static_cast<int>(allowedSections[0]) == static_cast<int>(permissionPlan.sections.size() - 1) &&
                  permissionPrompt.contains("outside that list the state MUST be absent"),
              "Club planning must receive the same per-owner section permissions enforced by validation");
        const auto boundedNotes = club::production::boundedNoteSchema(32).getProperty("anyOf", {});
        check(boundedNotes.size() == 128 &&
                  static_cast<double>(boundedNotes[116].getProperty("properties", {})
                    .getProperty("duration", {}).getProperty("maximum", 0)) == 3.0,
              "output schema must forbid a 3.5-beat note at beat 29 of a 32-beat window before generation");
        TonalAuditReport voicingEvidence;
        voicingEvidence.issues = {
            TonalIssue{.pitch = 64, .otherPitch = 65, .kind = "harsh_overlap", .partId = 1, .otherPartId = 5, .overlapBeats = 8},
            TonalIssue{.pitch = 64, .otherPitch = 77, .kind = "harsh_overlap", .partId = 1, .otherPartId = 5, .overlapBeats = 8},
            TonalIssue{.pitch = 76, .otherPitch = 77, .kind = "harsh_overlap", .partId = 4, .otherPartId = 5, .overlapBeats = .5},
            TonalIssue{.pitch = 76, .otherPitch = 77, .kind = "harsh_overlap", .partId = 4, .otherPartId = 5, .overlapBeats = 1}
        };
        const auto selectedVoicings = club::adjacentPlanes(clubPlan, voicingEvidence);
        check(selectedVoicings.issues.size() == 3 && club::adjacentWeight(selectedVoicings) == 17,
              "Club voicing review must include held compound minor ninths, not just adjacent pitches");
        voicingEvidence.issues = {
            TonalIssue{.pitch = 62, .otherPitch = 84, .kind = "harsh_overlap", .partId = 1, .otherPartId = 5, .overlapBeats = 8},
            TonalIssue{.pitch = 62, .otherPitch = 76, .kind = "harsh_overlap", .partId = 1, .otherPartId = 5, .overlapBeats = 8},
            TonalIssue{.pitch = 64, .otherPitch = 77, .kind = "harsh_overlap", .partId = 1, .otherPartId = 5, .overlapBeats = .5}
        };
        check(club::adjacentPlanes(clubPlan, voicingEvidence).issues.empty(),
              "minor sevenths, ordinary ninths and brief compound passing colors are not held minor-ninth evidence");
        Pattern exposedColor;
        exposedColor.notes = {
            NoteEvent{0, 8, 64, 70, 1, VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored},
            NoteEvent{1, 1, 65, 80, 4, VoiceId::Lead, 4, true, NoteOrigin::AiAuthored}};
        check(club::voicingEvidence(clubPlan, exposedColor).issues.size() == 1 &&
                  club::internalPlaneWeight(clubPlan, exposedColor) == 0,
              "the Club AI reviewer must see actual exposed pad/melody collisions even when both pitches are diatonic");
        exposedColor.notes.back().partId = 1;
        exposedColor.notes.back().voice = VoiceId::HarmonicFoundation;
        check(club::internalPlaneWeight(clubPlan, exposedColor) == 1 &&
                  club::voicingEvidence(clubPlan, exposedColor).issues.size() == 1,
              "a proposed voicing cannot hide a new close cluster inside the same pad");
        exposedColor.notes.back().partId = 4;
        exposedColor.notes.back().voice = VoiceId::Lead;
        exposedColor.notes.back().pitch = 64;
        exposedColor.notes.back().startBeat = .25;
        const auto masking = club::voicingEvidence(clubPlan, exposedColor);
        check(masking.issues.size() == 1 && masking.issues[0].kind == "retrigger_mask",
              "the AI reviewer must see a held support onset just before the same rhythmic pitch");
        exposedColor.notes.back().startBeat = 0;
        check(club::voicingEvidence(clubPlan, exposedColor).issues.empty(),
              "simultaneous unison layering must not be mislabeled as a near-attack retrigger");
        PerformanceScore recoveredNotes;
        PerformanceCell recoveredCell;
        recoveredCell.id = "arbitrary-checkpoint-name";
        recoveredCell.lengthBeats = 32;
        recoveredCell.notes.push_back(AuthoredNote{.beat = 4, .durationBeats = 8,
            .pitch = 64, .instrumentId = "bed"});
        recoveredNotes.cells.push_back(recoveredCell);
        recoveredNotes.placements.push_back(PerformancePlacement{.cellId = recoveredCell.id, .sectionIndex = 0});
        const auto address = findAiNoteAddress(recoveredNotes, clubPlan, 0, 64, 8, 0, 32);
        check(address && address->cellId == recoveredCell.id && address->absoluteBeat == 4,
              "AI pitch review must address recovered notes by ownership and placement, not live cell names");
        check(!findAiNoteAddress(recoveredNotes, clubPlan, 0, 64, 8, 8, 32),
              "pitch review must not reach behind its protected window");
        recoveredNotes.placements[0].repeats = 2;
        check(!findAiNoteAddress(recoveredNotes, clubPlan, 0, 64, 8, 0, 32),
              "pitch review must not change multiple occurrences through a shared cell");
        const auto clubWriting = AiComposer::directWindowPromptFor("Progressive house", clubPlan, Pattern{}, {0, 4}, 0, 0, 8);
        check(!clubWriting.contains("do not merely transpose and repeat") &&
                  !clubWriting.contains("Do not leave a whole section represented by one chord") &&
                  clubWriting.contains("quarter-beat grid"),
              "Club writing must remove contradictory novelty commands while retaining technical timing constraints");
        check(clubWriting.contains("ABSOLUTE song beat") &&
                  clubWriting.contains("NOT permission to restart"),
              "section-local MIDI coordinates must not reset the shared Club cycle phase");
        auto clubOutro = clubPlan;
        clubOutro.totalBars = 24;
        clubOutro.rootPitchClass = 0;
        clubOutro.narrativeSpine.authored = true;
        clubOutro.narrativeSpine.protagonistInstrumentId = "speaker";
        clubOutro.sections = {SongSection{.name = "opening", .startBar = 0, .bars = 8},
            SongSection{.name = "peak", .startBar = 8, .bars = 8},
            SongSection{.name = "outro", .startBar = 16, .bars = 8}};
        HarmonicChord finalChord;
        finalChord.pitchClasses = {0, 4, 7};
        clubOutro.chordPalette = {finalChord};
        clubOutro.sections.back().harmonicEvents = {HarmonicEvent{}};
        clubOutro.narrativeSpine.acts = {
            NarrativeAct{.sectionName = "opening", .stage = NarrativeStage::Premise},
            NarrativeAct{.sectionName = "peak", .stage = NarrativeStage::Climax},
            NarrativeAct{.sectionName = "outro", .stage = NarrativeStage::Resolution}};
        check(HarmonicFloorContext::requiredLayers(clubOutro, 4) == 0,
              "an authored Club premise can introduce bass and arp before sustained chord beds");
        Pattern outroMidi;
        outroMidi.lengthBeats = 96;
        for (const auto start : {0, 32}) {
            auto beat = static_cast<double>(start);
            for (const auto pitch : {72, 76, 74, 72}) {
                outroMidi.notes.push_back({beat++, .75, pitch, 80, 4, VoiceId::Lead, 4, true, NoteOrigin::AiAuthored});
            }
        }
        for (const auto pitch : {48, 52, 55})
            outroMidi.notes.push_back({64, 32, pitch, 70, 1, VoiceId::HarmonicFoundation, 1, true, NoteOrigin::AiAuthored});
        check(NarrativeScoreGate::audit(outroMidi, clubOutro).terminalPhraseReady,
              "a recalled Club theme can yield its final word to a stable sustained tonic outro");
        auto unstableOutro = outroMidi;
        unstableOutro.notes.back().pitch = 56;
        check(!NarrativeScoreGate::audit(unstableOutro, clubOutro).terminalPhraseReady,
              "Club ending assessment must still reject a foreign unresolved terminal pitch");
        auto loopScope = clubOutro;
        loopScope.continuousProductionSection = true;
        const auto loopReview = SelectiveRepair::reviewFinalScore(loopScope, unstableOutro);
        check(std::none_of(loopReview.issues.begin(), loopReview.issues.end(), [](const auto& issue) {
                  return issue.find("final MIDI does not enact") != std::string::npos;
              }), "a continuous production excerpt must not require a full-song final melodic cadence");
        clubOutro.compositionBehavior = CompositionBehavior::Adaptive;
        check(HarmonicFloorContext::requiredLayers(clubOutro, 4) >= 1,
              "Club introduction semantics must preserve Adaptive harmonic-floor requirements");
        check(!NarrativeScoreGate::audit(outroMidi, clubOutro).terminalPhraseReady,
              "Club outro semantics must not relax the existing Adaptive melodic closure contract");
        check(clubPhrasePrompt.contains("64-bar") &&
                  !clubPhrasePrompt.contains("The clue is a seed, NOT the whole melody") &&
                  AiMelodicDiscourse::realizationBrief(120, CompositionBehavior::Club)
                      .contains("repeat literally"),
              "Club planning and writing must agree on deliberate repetition instead of conflicting narrative quotas");
        dialoguePlan.phraseArchitectureJson = phraseJson.toStdString();
        const auto dialogueGroups = AiMelodicDiscourse::writingGroups(
            dialoguePlan, {0, 1, 2, 3, 4});
        check(dialogueGroups == std::vector<std::vector<std::size_t>>{{0, 1, 2, 3}, {4}},
              "AI-declared speakers must be authored together with their harmonic context");
        check(AiMelodicDiscourse::macroBrief().contains("provisional destinations"),
              "the macro plan must permit complete local sentences before final resolution");
#ifdef _WIN32
        _putenv_s("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", originalDiscourse.toRawUTF8());
#else
        if (originalDiscourse.isEmpty()) unsetenv("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT");
        else setenv("PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT", originalDiscourse.toRawUTF8(), 1);
#endif
        check(AiPhraseArchitecture::windowBrief(phraseJson, orderPlan, 0, 0, 8)
                  .contains("state the interrupted ascent") &&
              AiPhraseArchitecture::windowBrief(phraseJson, orderPlan, 0, 0, 8)
                  .contains("window-local beats 4..20") &&
              !AiPhraseArchitecture::windowBrief(phraseJson, orderPlan, 0, 0, 8)
                  .contains("transform the ascent"),
              "window writer should receive only the relevant phrase intentions");
        Pattern phraseMidi;
        NoteEvent spoken;
        spoken.partId = 4;
        spoken.startBeat = 5.0;
        spoken.durationBeats = 1.0;
        spoken.pitch = 72;
        phraseMidi.notes.push_back(spoken);
        const auto realized = AiPhraseArchitecture::realizationBrief(
            phraseJson, orderPlan, phraseMidi);
        check(realized.contains("speaker=speaker 1 attacks") &&
              realized.contains("promised answer=bed 0 attacks") &&
              realized.contains("speaker=speaker 0 attacks"),
              "narrative critic must see sparse and silent promised phrases");
        const auto missing = AiPhraseArchitecture::measureRealization(
            phraseJson, orderPlan, phraseMidi);
        check(missing.silentSpeakers == 1 && missing.missingResponses == 2,
              "AI promise audit must identify absent speakers and answers");
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  phraseJson, orderPlan, phraseMidi, 0, 16).empty(),
              "unfinished phrases must not force attacks into a technical window");
        juce::String absentPhraseEvidence;
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  phraseJson, orderPlan, phraseMidi, 0, 32).empty(),
              "a respondent relationship must not force an answer before its own phrase");
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  phraseJson, orderPlan, phraseMidi, 32, 64, &absentPhraseEvidence) ==
                  std::vector<std::string>{"speaker"},
              "earlier music must not hide an absent speaker in a later phrase");
        check(absentPhraseEvidence.contains("window-local beats 0.00..24.00"),
              "AI repair feedback must locate the missing phrase, not merely name its owner");
        Pattern heldPhraseMidi;
        auto heldAnswer = spoken;
        heldAnswer.startBeat = 3;
        heldAnswer.durationBeats = 2;
        heldPhraseMidi.notes.push_back(heldAnswer);
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  phraseJson, orderPlan, heldPhraseMidi, 0, 32).empty(),
              "a held speaker overlapping a phrase needs no artificial reattack");
        const auto spanningPhrase = phraseJson.replace(
            "\"local_start_bar\":1,\"bars\":4", "\"local_start_bar\":5,\"bars\":4")
            .replace("\"response_id\":\"bed\"", "\"response_id\":\"\"");
        auto crossingMidi = phraseMidi;
        crossingMidi.notes.front().startBeat = 24;
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  spanningPhrase, orderPlan, crossingMidi, 0, 32).empty(),
              "a phrase crossing the window is not yet due");
        check(AiPhraseArchitecture::missingCompletedPhraseOwners(
                  spanningPhrase, orderPlan, crossingMidi, 32, 40).empty(),
              "earlier notes within the same phrase permit a later intentional rest");
        auto handoffPlan = orderPlan;
        handoffPlan.instruments.push_back(
            part("answer_voice", VoiceId::Countermelody));
        const auto handoffPhraseJson = phraseJson.replace(
            "\"speaker_id\":\"speaker\",\"response_id\":\"moving_bass\"",
            "\"speaker_id\":\"answer_voice\",\"response_id\":\"moving_bass\"");
        AiPhraseArchitecture::applyTerminalHandoff(handoffPhraseJson, handoffPlan);
        check(handoffPlan.narrativeSpine.terminalSpeakerInstrumentId ==
                  "answer_voice" &&
              handoffPlan.narrativeSpine.terminalPhraseStartBar == 8,
              "the final MIDI audit must follow the AI's declared speaker handoff");
        AiPhraseArchitecture::applyTerminalHandoff(
            handoffPhraseJson.replace("\"speaker_id\":\"answer_voice\"",
                "\"speaker_id\":\"upper_body\""), handoffPlan);
        check(handoffPlan.narrativeSpine.terminalSpeakerInstrumentId.empty(),
              "a harmonic pad must not masquerade as the terminal melodic speaker");
        const auto crossingPhraseJson = phraseJson.replace(
            "\"local_start_bar\":1,\"bars\":4",
            "\"local_start_bar\":5,\"bars\":4");
        check(AiPhraseArchitecture::validate(crossingPhraseJson, orderPlan, phraseError) &&
              AiPhraseArchitecture::windowBrief(crossingPhraseJson, orderPlan, 1, 0, 8)
                  .contains("state the interrupted ascent"),
              "a phrase may cross a section boundary without losing its intention");
        check(!AiPhraseArchitecture::validate(
            phraseJson.replace("\"response_id\":\"bed\"",
                               "\"response_id\":\"unknown\""),
            orderPlan, phraseError),
            "AI phrase architecture must reject invented instrument relationships");
        auto inactivePhrasePlan = orderPlan;
        inactivePhrasePlan.instruments[3].activeSections = {"answer"};
        check(!AiPhraseArchitecture::validate(phraseJson,
            inactivePhrasePlan, phraseError),
            "an AI phrase must not promise a voice in a scene where the cast makes it inactive");
        check(AiPhraseArchitecture::validate(phraseJson,
            inactivePhrasePlan, phraseError, false),
            "an accepted historical checkpoint remains resumable after tightening fresh-plan validation");
        check(!juce::JSON::parse(AiPhraseArchitecture::longFormArcSchema()).isVoid(),
              "long-form arc schema must be valid JSON");
        auto augmentedPhrase = phraseJson;
        const juce::String arcJson = R"json({"act_links":[
          {"section_index":0,"carrier_id":"speaker","inherited_question":"silence before the third",
           "audible_transformation":"interrupt the ascent","consequence":"question remains",
           "left_open":"the withheld third"},
          {"section_index":1,"carrier_id":"moving_bass","inherited_question":"the withheld third",
           "audible_transformation":"answer below the lead","consequence":"descent resolves",
           "left_open":"final sustain"}]})json";
        check(AiPhraseArchitecture::attachLongFormArc(augmentedPhrase, arcJson,
              orderPlan, phraseError) &&
              AiPhraseArchitecture::windowBrief(augmentedPhrase, orderPlan, 1, 0, 8)
                  .contains("answer below the lead") &&
              AiPhraseArchitecture::windowBrief(augmentedPhrase, orderPlan, 1, 0, 8)
                  .contains("the withheld third"),
              "writer must receive the AI-authored current and previous causal links");
        auto invalidArcPhrase = phraseJson;
        check(!AiPhraseArchitecture::attachLongFormArc(invalidArcPhrase,
              arcJson.replace("\"carrier_id\":\"moving_bass\"",
                              "\"carrier_id\":\"invented\""),
              orderPlan, phraseError) && invalidArcPhrase == phraseJson,
              "unknown arc carrier must not corrupt the accepted phrase checkpoint");
        check(!juce::JSON::parse(AiPhraseArchitecture::groundedStorySchema()).isVoid(),
              "MIDI-grounded story schema must be valid JSON");
        const juce::String storyJson = R"json({"grounded_story":{
          "opening_evidence":"speaker MIDI rises 60 to 64 then rests",
          "related_counteridea":"bass answers that rest with a descent",
          "central_conflict":"the two voices propose different destinations",
          "necessary_turn":"the ascent borrows the bass descent",
          "final_consequence":"both contours resolve together"},"act_links":[
          {"section_index":0,"carrier_id":"speaker","inherited_question":"first rise",
           "audible_transformation":"leave the rest unresolved","consequence":"bass enters",
           "left_open":"a possible descent","score_evidence":"opening notes 60 then 64"},
          {"section_index":1,"carrier_id":"moving_bass","inherited_question":"the rest",
           "audible_transformation":"descend below the ascent","consequence":"lead changes shape",
           "left_open":"shared final pitch","score_evidence":"bass answers after beat 32"}]})json";
        auto groundedPhrase = phraseJson;
        check(AiPhraseArchitecture::attachGroundedStory(groundedPhrase,
              storyJson, orderPlan, phraseError) &&
              AiPhraseArchitecture::windowBrief(groundedPhrase, orderPlan, 1, 0, 8)
                  .contains("bass answers that rest") &&
              AiPhraseArchitecture::windowBrief(groundedPhrase, orderPlan, 1, 0, 8)
                  .contains("bass answers after beat 32"),
              "later windows should inherit the story grounded in opening MIDI");
        auto invalidStoryPhrase = phraseJson;
        check(!AiPhraseArchitecture::attachGroundedStory(invalidStoryPhrase,
              storyJson.replace("\"carrier_id\":\"moving_bass\"",
                                "\"carrier_id\":\"invented\""),
              orderPlan, phraseError) && invalidStoryPhrase == phraseJson,
              "an invented score carrier must not mutate the accepted plan");
        Pattern openingMidi;
        openingMidi.notes.push_back({0, 1, 60, 90, 1, VoiceId::Lead, 4});
        check(AiPhraseArchitecture::groundedStoryPrompt(orderPlan, "test",
              phraseJson, openingMidi).contains("0.00,60,1.00"),
              "story editor must receive actual opening pitches, not only prose");
        const auto focusedSchema = juce::JSON::parse(
            AiComposer::performanceSchemaFor(orderPlan, {3}));
        const auto focusedPitch = focusedSchema.getProperty("properties", {})
            .getProperty("performance_score", {}).getProperty("properties", {})
            .getProperty("cells", {}).getProperty("items", {}).getProperty("properties", {})
            .getProperty("notes", {}).getProperty("items", {}).getProperty("properties", {})
            .getProperty("pitch", {});
        check(static_cast<int>(focusedPitch.getProperty("minimum", {})) == 69 &&
              static_cast<int>(focusedPitch.getProperty("maximum", {})) == 91,
              "focused protagonist schema must enforce the actual playable register");
        const juce::String originalWire = R"json({"block_id":"test","covered_instrument_ids":["speaker"],"performance_score":{"cells":[{"id":"c1","theme_id":"t1","narrative_function":"establish","length_beats":4,"owned_voices":["lead"],"notes":[{"beat":0,"duration":1,"pitch":72,"velocity":91,"voice":"lead","instrument_id":"speaker","metric_intent":"strict_grid"}],"controls":[{"beat":0,"controller":11,"value":88,"voice":"lead","instrument_id":"speaker"}]}],"placements":[{"cell_id":"c1","section_index":0,"start_beat":0,"repeats":1,"transpose":0,"velocity_scale":1,"time_scale":1,"purpose":"establish","voice_map":[],"retrograde":false,"invert_contour":false,"inversion_axis":60,"fragment_start":0,"fragment_end":4,"metric_intent":"strict_grid"}]}})json";
        const juce::String compactWire = R"json({"block_id":"test","covered_instrument_ids":["speaker"],"performance_score":{"cells":[{"id":"c1","theme_id":"t1","narrative_function":"establish","length_beats":4,"owned_voices":["lead"],"notes":[{"b":0,"d":1,"p":72,"v":91,"r":"lead","i":"speaker","m":"strict_grid"}],"controls":[{"b":0,"cc":11,"v":88,"r":"lead","i":"speaker"}]}],"placements":[{"cell_id":"c1","section_index":0,"start_beat":0,"repeats":1,"transpose":0,"velocity_scale":1,"time_scale":1,"purpose":"establish","voice_map":[],"retrograde":false,"invert_contour":false,"inversion_axis":60,"fragment_start":0,"fragment_end":4,"metric_intent":"strict_grid"}]}})json";
        orderPlan.sections.push_back({});
        orderPlan.sections.back().bars = 1;
        PerformanceScore originalScore, compactScore;
        juce::String wireError;
        check(AiComposer::parsePerformanceBlockJson(originalWire, orderPlan, {3}, originalScore, wireError),
              "original performance wire must parse");
        check(AiComposer::parsePerformanceBlockJson(compactWire, orderPlan, {3}, compactScore, wireError),
              "compact performance wire must parse");
        check(originalScore.cells.size() == compactScore.cells.size() &&
              originalScore.placements.size() == compactScore.placements.size() &&
              originalScore.cells[0].notes.size() == compactScore.cells[0].notes.size() &&
              originalScore.cells[0].controls.size() == compactScore.cells[0].controls.size() &&
              originalScore.cells[0].notes[0].pitch == compactScore.cells[0].notes[0].pitch &&
              originalScore.cells[0].notes[0].beat == compactScore.cells[0].notes[0].beat &&
              originalScore.cells[0].notes[0].durationBeats == compactScore.cells[0].notes[0].durationBeats &&
              originalScore.cells[0].notes[0].velocity == compactScore.cells[0].notes[0].velocity &&
              originalScore.cells[0].controls[0].controller == compactScore.cells[0].controls[0].controller &&
              originalScore.cells[0].controls[0].value == compactScore.cells[0].controls[0].value,
              "compact wire must preserve authored MIDI events exactly");
#if defined(_WIN32)
        _putenv_s("PULSO_AI_COMPACT_WIRE", "1");
#else
        setenv("PULSO_AI_COMPACT_WIRE", "1", 1);
#endif
        const auto compactSchema = juce::JSON::parse(AiComposer::performanceSchemaFor(orderPlan, {3}));
        const auto compactProperties = compactSchema.getProperty("properties", {})
            .getProperty("performance_score", {}).getProperty("properties", {})
            .getProperty("cells", {}).getProperty("items", {}).getProperty("properties", {})
            .getProperty("notes", {}).getProperty("items", {}).getProperty("properties", {});
        check(compactProperties.hasProperty("p") && !compactProperties.hasProperty("pitch") &&
              static_cast<int>(compactProperties.getProperty("p", {}).getProperty("minimum", {})) == 69 &&
              static_cast<int>(compactProperties.getProperty("p", {}).getProperty("maximum", {})) == 91,
              "compact performance schema must preserve singleton owner register");
#if defined(_WIN32)
        _putenv_s("PULSO_AI_COMPACT_WIRE", "");
#else
        unsetenv("PULSO_AI_COMPACT_WIRE");
#endif
        const juce::String denseWire = R"json({"block_id":"test","covered_instrument_ids":["speaker"],"performance_score":{"cells":[{"id":"c1","theme_id":"t1","narrative_function":"establish","length_beats":4,"owned_voices":["lead"],"notes":[{"b":0,"d":1,"p":72,"v":91,"i":3}],"controls":[{"b":0,"cc":11,"v":88,"i":3}]}],"placements":[{"cell_id":"c1","section_index":0,"start_beat":0,"repeats":1,"transpose":0,"velocity_scale":1,"time_scale":1,"purpose":"establish","voice_map":[],"retrograde":false,"invert_contour":false,"inversion_axis":60,"fragment_start":0,"fragment_end":4,"metric_intent":"strict_grid"}]}})json";
        const auto originalLocalEditorial = juce::SystemStats::getEnvironmentVariable(
            "PULSO_LOCAL_EDITORIAL_V2", {});
#if defined(_WIN32)
        _putenv_s("PULSO_LOCAL_EDITORIAL_V2", "1");
        _putenv_s("PULSO_AI_DENSE_WIRE", "1");
#else
        setenv("PULSO_LOCAL_EDITORIAL_V2", "1", 1);
        setenv("PULSO_AI_DENSE_WIRE", "1", 1);
#endif
        const auto denseSchema = juce::JSON::parse(AiComposer::performanceSchemaFor(orderPlan, {3}));
        const auto denseProperties = denseSchema.getProperty("properties", {})
            .getProperty("performance_score", {}).getProperty("properties", {})
            .getProperty("cells", {}).getProperty("items", {}).getProperty("properties", {})
            .getProperty("notes", {}).getProperty("items", {}).getProperty("properties", {});
        const auto denseOwners = denseProperties.getProperty("i", {}).getProperty("enum", {}).getArray();
        check(denseOwners != nullptr && denseOwners->size() == 1 &&
              static_cast<int>(denseOwners->getReference(0)) == 3 &&
              !denseProperties.hasProperty("voice") && !denseProperties.hasProperty("metric_intent") &&
              static_cast<int>(denseProperties.getProperty("p", {}).getProperty("minimum", {})) == 69 &&
              static_cast<int>(denseProperties.getProperty("p", {}).getProperty("maximum", {})) == 91,
              "dense wire schema must preserve assigned owner and register without redundant fields");
        PerformanceScore denseScore;
        check(AiComposer::parsePerformanceBlockJson(denseWire, orderPlan, {3}, denseScore, wireError) &&
              denseScore.cells.size() == 1 && denseScore.cells[0].notes.size() == 1 &&
              denseScore.cells[0].controls.size() == 1 &&
              denseScore.cells[0].notes[0].instrumentId == originalScore.cells[0].notes[0].instrumentId &&
              denseScore.cells[0].notes[0].voice == originalScore.cells[0].notes[0].voice &&
              denseScore.cells[0].notes[0].pitch == originalScore.cells[0].notes[0].pitch &&
              denseScore.cells[0].notes[0].beat == originalScore.cells[0].notes[0].beat &&
              denseScore.cells[0].notes[0].durationBeats == originalScore.cells[0].notes[0].durationBeats &&
              denseScore.cells[0].notes[0].velocity == originalScore.cells[0].notes[0].velocity &&
              denseScore.cells[0].controls[0].controller == originalScore.cells[0].controls[0].controller &&
              denseScore.cells[0].controls[0].value == originalScore.cells[0].controls[0].value,
              "dense wire must restore exact authored note and expression-control events");
#if defined(_WIN32)
        _putenv_s("PULSO_AI_DENSE_WIRE", "");
        _putenv_s("PULSO_LOCAL_EDITORIAL_V2", originalLocalEditorial.toRawUTF8());
#else
        unsetenv("PULSO_AI_DENSE_WIRE");
        if (originalLocalEditorial.isEmpty()) unsetenv("PULSO_LOCAL_EDITORIAL_V2");
        else setenv("PULSO_LOCAL_EDITORIAL_V2", originalLocalEditorial.toRawUTF8(), 1);
#endif
        orderPlan.narrativeSpine.protagonistInstrumentId = "speaker";
        orderPlan.productionLanguage.domain = ProductionDomain::ClubElectronic;
        orderPlan.productionLanguage.electronicIntent = .85;
        const auto electronicBlocks = AiComposer::performanceWritingBlocks(orderPlan, true);
        check(electronicBlocks.size() == 5 &&
              electronicBlocks[0] == std::vector<std::size_t>({0}) &&
              electronicBlocks[1] == std::vector<std::size_t>({4}) &&
              electronicBlocks[2] == std::vector<std::size_t>({1}) &&
              electronicBlocks[3] == std::vector<std::size_t>({2}) &&
              electronicBlocks[4] == std::vector<std::size_t>({3}),
              "electronic writer must finish each harmonic body before bass and melody");
        orderPlan.productionLanguage.domain = ProductionDomain::Orchestral;
        orderPlan.productionLanguage.electronicIntent = .2;
        const auto otherBlocks = AiComposer::performanceWritingBlocks(orderPlan, true);
        check(!otherBlocks.empty() &&
              otherBlocks.front() == std::vector<std::size_t>({0, 1}),
              "non-electronic editorial writer changed its established ordering");
        auto layeredPlan = orderPlan;
        layeredPlan.instruments.push_back(
            part("long_atmosphere", VoiceId::Atmosphere, "sustained texture"));
        layeredPlan.instruments.push_back(
            part("answer", VoiceId::Countermelody, "answer"));
        layeredPlan.instruments.back().lineRelationship = "call_response";
        const auto layeredBlocks = AiComposer::performanceWritingBlocks(layeredPlan, true);
        const auto blockOf = [&](const std::size_t owner) {
            for (std::size_t block = 0; block < layeredBlocks.size(); ++block)
                if (std::find(layeredBlocks[block].begin(), layeredBlocks[block].end(),
                        owner) != layeredBlocks[block].end())
                    return block;
            return layeredBlocks.size();
        };
        check(blockOf(5) < blockOf(6),
              "long-lived atmosphere must be written before busy melodic answers");
        const auto focusedBlocks = AiComposer::performanceWritingBlocks(
            layeredPlan, true, true);
        std::vector<std::size_t> establishedOrder;
        std::vector<std::size_t> focusedOrder;
        for (const auto& block : layeredBlocks)
            establishedOrder.insert(establishedOrder.end(), block.begin(), block.end());
        for (const auto& block : focusedBlocks) {
            check(block.size() == 1, "focused authoring must request exactly one owner");
            focusedOrder.insert(focusedOrder.end(), block.begin(), block.end());
        }
        check(focusedOrder == establishedOrder &&
              focusedOrder.size() == layeredPlan.instruments.size(),
              "focused authoring must preserve every AI owner and its established order");
        check(AiComposer::performanceOutputTokenBudget(true, false, 1) > 10000 &&
              AiComposer::performanceOutputTokenBudget(true, false, 2) >
                  AiComposer::performanceOutputTokenBudget(true, false, 1) &&
              AiComposer::performanceOutputTokenBudget(false, true, 1) == 9000 &&
              AiComposer::performanceOutputTokenBudget(false, false, 1) == 16000,
              "long-form editorial output must not repeat a truncated JSON budget");
        const juce::String explicitCastWithFx = R"json({
          "protagonist_instrument_id":"lead",
          "voices":[{"id":"lead"},{"id":"atmosphere"},{"id":"transitions"}],
          "instruments":[
            {"id":"lead","instrument":"lead_synth","source_voice":"lead"},
            {"id":"pad","instrument":"analog_pad","source_voice":"atmosphere"},
            {"id":"fx","instrument":"shimmer_tail","source_voice":"transitions"}
          ]
        })json";
        juce::String developedCast;
        juce::String castError;
        std::size_t removedCastMembers{};
        check(AiComposer::enforceExplicitCastExclusions(explicitCastWithFx,
                  "30 pistas completas y con narrativa", developedCast,
                  removedCastMembers, castError) && removedCastMembers == 1,
              "explicit track count must reject event-only owners before cast details");
        SongGenerationRequest request;
        request.direction = "A dark electronic journey";
        request.targetSeconds = 60;
        request.bpm = 120.0;
        request.seed = 98123;
        request.behavior = CompositionBehavior::Hypnotic;
        check(SongGenerationPipeline::aiDirection(request) == request.direction,
              "adaptive prompt changed musical direction");
        check(SongGenerationPipeline::targetBars(request) ==
              SongComposer::phraseAlignedBars(30), "bar target differs between hosts");

        request.orchestration = SongOrchestrationIntent::ClubElectronic;
        const auto prompt = SongGenerationPipeline::aiDirection(request);
        check(prompt.contains("Production mode: club electronic") &&
              prompt.contains("A dark electronic journey"),
              "orchestration instruction is missing from shared prompt");

        auto plan = SongComposer::createLocalPlan(request.direction.toStdString(),
            request.targetSeconds, request.bpm, request.beatsPerBar, request.seed,
            9, ScaleKind::Minor);
        SongGenerationPipeline::finalizePlan(plan, request);
        check(plan.targetSeconds == 60 && plan.seed == request.seed &&
              plan.compositionBehavior == CompositionBehavior::Hypnotic &&
              plan.productionLanguage.domain == ProductionDomain::ClubElectronic,
              "hosts disagree on finalized song plan");

        const auto vst = SongGenerationPipeline::render(plan, request);
        const auto cloud = SongGenerationPipeline::render(plan, request);
        check(!vst.notes.empty(), "test score rendered no notes");
        samePattern(vst, cloud);

        SongPlan direct;
        direct.beatsPerBar = 4.0;
        InstrumentAssignment bed;
        bed.id = "bed";
        bed.sourceVoice = VoiceId::HarmonicFoundation;
        bed.minimumPitch = 48;
        bed.maximumPitch = 72;
        InstrumentAssignment bass;
        bass.id = "bass";
        bass.sourceVoice = VoiceId::MovementBass;
        bass.minimumPitch = 32;
        bass.maximumPitch = 52;
        direct.instruments = {bed, bass};
        const std::vector<std::size_t> owners{0, 1};
        check(!juce::JSON::parse(AiComposer::directWindowSchemaFor(direct, owners)).isVoid(),
              "direct MIDI schema is invalid");
        PerformanceScore authored;
        juce::String error;
        check(AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":0,"duration":4,"pitch":60,"velocity":88}],"bass":[{"beat":0,"duration":1,"pitch":36,"velocity":92}]})",
            direct, owners, 0, 0, 2, authored, error) &&
            authored.cells.size() == 2 && authored.placements.size() == 2 &&
            authored.cells[0].notes[0].pitch == 60 &&
            authored.cells[1].notes[0].pitch == 36,
            "direct MIDI conversion altered or lost AI-authored notes");
        check(!AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":7.75,"duration":1,"pitch":60,"velocity":88}],"bass":[]})",
            direct, owners, 0, 0, 2, authored, error),
            "direct MIDI parser accepted a note across the window boundary");
        check(!AiComposer::parseDirectWindowJson(
            R"({"bed":[],"bass":[]})", direct, owners, 0, 0, 2,
            authored, error) &&
            AiComposer::parseDirectWindowJson(
                R"({"bed":[],"bass":[]})", direct, owners, 0, 0, 2,
                authored, error, false, true) && authored.empty(),
            "an optional companion group must be allowed an intentional rest");
        SongPlan continuity = direct;
        continuity.totalBars = 4;
        continuity.sections.push_back({});
        continuity.sections[0].name = "Long scene";
        continuity.sections[0].bars = 4;
        continuity.sections[0].harmonicEvents.push_back({3, 0.0, "tonic"});
        check(AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":7.75,"duration":1,"pitch":60,"velocity":88}],"bass":[]})",
            continuity, owners, 0, 0, 2, authored, error, true) &&
            authored.cells.size() == 1 && authored.cells[0].lengthBeats == 8.75,
            "long-form continuity clipped an AI-authored note at a technical boundary");
        const auto continued = PerformanceScoreEngine::normalize(
            authored, 1, {16.0}, true);
        Pattern continuedMidi;
        PerformanceScoreEngine::replaceChunk(continuedMidi, authored, 0, 0.0, 16.0,
            continuity.instruments, true);
        check(continued.notesRejected == 0 && continuedMidi.notes.size() == 1 &&
            std::abs(continuedMidi.notes[0].endBeat() - 8.75) < .001,
            "technical window release was not preserved in the rendered MIDI");
        continuity.sections[0].harmonicEvents[0].barOffset = 2;
        check(!AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":7.75,"duration":1,"pitch":60,"velocity":88}],"bass":[]})",
            continuity, owners, 0, 0, 2, authored, error, true),
            "long-form continuity crossed a real harmonic boundary");
        continuity.sections[0].harmonicEvents[0].barOffset = 3;
        check(!AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":7.75,"duration":9,"pitch":60,"velocity":88}],"bass":[]})",
            continuity, owners, 0, 0, 2, authored, error, true),
            "long-form continuity crossed the end of its section");
        check(!AiComposer::parseDirectWindowJson(
            R"({"bed":[{"beat":0,"duration":1,"pitch":30,"velocity":88}],"bass":[]})",
            direct, owners, 0, 0, 2, authored, error),
            "direct MIDI parser accepted a pitch more than one octave outside its preferred register");
        TonalIssue collision;
        collision.kind = "harsh_overlap";
        collision.beat = 4;
        collision.partId = 1;
        collision.otherPartId = 2;
        collision.overlapBeats = 1;
        TonalIssue chromatic;
        chromatic.kind = "unsupported_chromatic";
        chromatic.beat = 5;
        chromatic.partId = 3;
        check(jointWindowRepairOwners({0, 1, 2, 3},
            {collision, chromatic}, 0, 8) == std::vector<std::size_t>({0, 1, 2}),
            "joint recovery froze one side of an audible collision");
        check(jointWindowRepairOwners({0, 1, 2, 3},
            {collision, chromatic}, 0, 8, 3) ==
                std::vector<std::size_t>({0, 1, 2, 3}),
            "joint recovery omitted a missing active owner");
        collision.beat = 12;
        check(jointWindowRepairOwners({0, 1, 2, 3},
            {collision, chromatic}, 0, 8) == std::vector<std::size_t>({2}),
            "joint recovery included an unrelated window collision");
        std::vector<TonalIssue> broad;
        for (std::uint16_t part = 1; part <= 7; ++part) {
            TonalIssue issue;
            issue.kind = "strong_non_chord";
            issue.beat = 2;
            issue.partId = part;
            broad.push_back(issue);
        }
        check(jointWindowRepairOwners({0, 1, 2, 3, 4, 5, 6, 7},
            broad, 0, 8).size() == 8,
            "a wide conflict graph was not returned to the full AI ensemble");
        SongPlan closedOrchestra;
        closedOrchestra.instrumentCastAuthored = true;
        closedOrchestra.preserveAuthoredCast = true;
        closedOrchestra.instruments = {bed, bass};
        const auto authoredBedId = closedOrchestra.instruments[0].id;
        const auto authoredBassId = closedOrchestra.instruments[1].id;
        SongComposer::normalizePlan(closedOrchestra);
        check(closedOrchestra.instruments.size() == 2 &&
            closedOrchestra.instruments[0].id == authoredBedId &&
            closedOrchestra.instruments[1].id == authoredBassId,
            "orchestral normalization appended generic parts to a closed AI cast");
        check(AiComposer::castManifestHasRange(6, 3) &&
            AiComposer::castManifestHasRange(12, 8),
            "local compact cast schema raised its six-owner ceiling to eight");
        check(AiComposer::requestedInstrumentCount(
                  "dame solamente una pista de una progresion armonica en D#min") == 1 &&
              AiComposer::castManifestUsesExactCount(1),
            "an explicit single harmonic MIDI request lost its one-owner cast contract");
        SongPlan singleHarmony;
        singleHarmony.instrumentCastAuthored = true;
        singleHarmony.preserveAuthoredCast = true;
        singleHarmony.requestedCastCount = 1;
        singleHarmony.instruments = {bed};
        SongComposer::normalizePlan(singleHarmony);
        check(singleHarmony.instruments.size() == 1 &&
              singleHarmony.instruments.front().id == bed.id,
            "single harmonic AI cast acquired unwanted instrumental owners");
        SongPlan narrativePromptPlan;
        narrativePromptPlan.totalBars = 8;
        narrativePromptPlan.sections.push_back({});
        narrativePromptPlan.sections[0].name = "Answer";
        narrativePromptPlan.sections[0].bars = 8;
        narrativePromptPlan.instruments = {bed, bass};
        narrativePromptPlan.rhythmLanguage.description =
            "A short-long tonal question followed by a held answer";
        NarrativeAct act;
        act.sectionName = "Answer";
        act.cause = "The opening phrase stopped before the tonic";
        act.consequence = "The leading tone now resolves in the melody";
        act.unresolvedElement = "An unfinished melodic descent";
        act.resolutionTarget = "A sustained, audible melodic arrival";
        narrativePromptPlan.narrativeSpine.acts.push_back(act);
        narrativePromptPlan.narrativeSpine.protagonistInstrumentId = bed.id;
        const auto narrativePrompt = AiComposer::directWindowPromptFor(
            "A tonal chamber miniature", narrativePromptPlan, Pattern{}, {0, 1},
            0, 0, 8);
        check(narrativePrompt.contains(juce::String::fromUTF8(act.cause.c_str())) &&
            narrativePrompt.contains(juce::String::fromUTF8(act.consequence.c_str())) &&
            narrativePrompt.contains(juce::String::fromUTF8(act.resolutionTarget.c_str())) &&
            narrativePrompt.contains(juce::String::fromUTF8(
                narrativePromptPlan.rhythmLanguage.description.c_str())) &&
            !narrativePrompt.contains("ELECTRONIC INTERPLAY"),
            "orchestral AI window omitted its authored dramatic or rhythmic brief");
        Pattern priorMelody;
        NoteEvent heldNote;
        heldNote.startBeat = 15.75;
        heldNote.durationBeats = 1.0;
        heldNote.pitch = 60;
        heldNote.partId = 1;
        priorMelody.notes.push_back(heldNote);
        const auto continuityPrompt = AiComposer::directWindowPromptFor(
            "A tonal chamber miniature", narrativePromptPlan, priorMelody,
            {0, 1}, 0, 4, 4, {}, false, true);
        check(continuityPrompt.contains("ACTUAL MELODIC DEVELOPMENT LEDGER") &&
            continuityPrompt.contains("ACCEPTED NOTES STILL SOUNDING AT THIS WINDOW START") &&
            continuityPrompt.contains("15.75:60/1.00") &&
            continuityPrompt.contains("MAY release after the technical window end"),
            "long-form writer lost actual motif history or an AI-authored sustain");
        const auto memoryOnlyPrompt = AiComposer::directWindowPromptFor(
            "A tonal chamber miniature", narrativePromptPlan, priorMelody,
            {0, 1}, 0, 4, 4, {}, false, false, false, true);
        check(memoryOnlyPrompt.contains("ACTUAL MELODIC DEVELOPMENT LEDGER") &&
            memoryOnlyPrompt.contains("A faithful return may be essential") &&
            !memoryOnlyPrompt.contains("MAY release after the technical window end") &&
            memoryOnlyPrompt.contains("ACCEPTED NOTES STILL SOUNDING AT THIS WINDOW START"),
            "thematic memory lost sounding-note context or changed release policy");
        Pattern thematicCandidate = priorMelody;
        for (int index = 0; index < 4; ++index) {
            NoteEvent note;
            note.startBeat = 16.0 + index;
            note.durationBeats = .5;
            note.pitch = 60 + index;
            note.partId = 1;
            thematicCandidate.notes.push_back(note);
        }
        const auto thematicReview = AiComposer::thematicReviewPromptFor(
            narrativePromptPlan, priorMelody, thematicCandidate, 0, 4, 4);
        check(thematicReview.contains("Repetition can establish identity") &&
            thematicReview.contains("16.00,1,60,0.50") &&
            thematicReview.contains("19.00,1,63,0.50") &&
            thematicReview.contains("The leading tone now resolves"),
            "thematic editor omitted actual MIDI or the scene's musical cause");
        const auto thematicRewrite = AiComposer::directWindowPromptFor(
            "A tonal chamber miniature", narrativePromptPlan, priorMelody,
            {0}, 0, 4, 4, "Develop the held answer", true, false, true);
        check(thematicRewrite.contains("FOCUSED MUSICAL EDIT") &&
            thematicRewrite.contains("preserving its recognisable motif") &&
            !thematicRewrite.contains("pitch collision"),
            "thematic revision was treated like harmonic collision repair");
        narrativePromptPlan.productionLanguage.domain = ProductionDomain::Hybrid;
        narrativePromptPlan.productionLanguage.electronicIntent = .31;
        check(!AiComposer::directWindowPromptFor("A tonal chamber miniature",
            narrativePromptPlan, Pattern{}, {0, 1}, 0, 0, 8)
            .contains("ELECTRONIC INTERPLAY"),
            "a low-electronic hybrid chamber score received club writing instructions");
        const auto companionPrompt = AiComposer::directWindowPromptFor(
            "A tonal chamber miniature", narrativePromptPlan, Pattern{}, {1},
            0, 0, 8);
        check(companionPrompt.contains("protagonist is NOT a requested owner") &&
            companionPrompt.contains("already accepted and immutable"),
            "companion writing stage did not protect the accepted AI lead");
        // The blueprint parser normalizes internally. Exercise that path, not
        // only a SongPlan which already has the closed-cast flag set.
        juce::String closedBlueprint = R"({"title":"Closed orchestra","key":"C major","root_pitch_class":0,"mode":"major","sections":[{"name":"Opening","bars":8},{"name":"Middle","bars":8},{"name":"Return","bars":8}],"instruments":[)";
        const std::array<std::pair<const char*, const char*>, 10> authoredCast{{
            {"violin_1", "lead"}, {"violin_2", "harmonic_upper"},
            {"viola", "harmonic_foundation"}, {"cello", "movement_bass"},
            {"contrabass", "sub_bass"}, {"oboe", "countermelody"},
            {"clarinet", "countermelody"}, {"bassoon", "harmonic_pulse"},
            {"french_horns", "harmonic_foundation"}, {"flute", "atmosphere"}}};
        for (std::size_t i = 0; i < authoredCast.size(); ++i) {
            if (i != 0) closedBlueprint += ",";
            closedBlueprint += "{\"id\":\"owner_" + juce::String(static_cast<int>(i)) +
                "\",\"instrument\":\"" + authoredCast[i].first +
                "\",\"name\":\"Part " + juce::String(static_cast<int>(i)) +
                "\",\"source_voice\":\"" + authoredCast[i].second + "\"}";
        }
        closedBlueprint += "]}";
        SongPlan parsedClosedCast;
        parsedClosedCast.preserveAuthoredCast = true;
        juce::String closedCastError;
        check(AiComposer::parseSongPlanJson(closedBlueprint, 120, 24, 120.0, 4.0,
            1, parsedClosedCast, closedCastError) &&
            parsedClosedCast.preserveAuthoredCast &&
            parsedClosedCast.instruments.size() == authoredCast.size(),
            "blueprint parser expanded the AI-authored joint-window ensemble");
        SongPlan parsedClub;
        parsedClub.preserveAuthoredCast = true;
        const auto clubBlueprint = closedBlueprint.replaceFirstOccurrenceOf("{", "{\"composition_behavior\":\"club\",");
        check(AiComposer::parseSongPlanJson(clubBlueprint, 120, 24, 120.0, 4.0,
                  1, parsedClub, closedCastError) &&
                  parsedClub.compositionBehavior == CompositionBehavior::Club,
              "Club intent must survive accepted blueprint serialization and resume");
        auto singleSectionClub = juce::JSON::parse(clubBlueprint);
        singleSectionClub.getDynamicObject()->setProperty("sections",
            juce::Array<juce::var>{juce::JSON::parse(R"({"name":"Full body","bars":24})")});
        check(AiComposer::parseSongPlanJson(juce::JSON::toString(singleSectionClub), 48, 24, 120, 4,
                  1, parsedClub, closedCastError) && parsedClub.sections.size() == 1 && parsedClub.sections[0].bars == 24,
              "the real blueprint parser must accept one Club section without inventing dramatic parts");
        for (std::size_t i = 0; i < authoredCast.size(); ++i)
            check(parsedClosedCast.instruments[i].id ==
                "owner_" + std::to_string(i),
                "blueprint parser changed an AI-authored instrument owner");
        // A captured production blueprint can be replayed locally with this
        // optional path; no network call or fixture with user data is needed.
        if (const auto* replayPath = std::getenv("PULSO_REPLAY_BLUEPRINT")) {
            const auto replay = juce::File::getCurrentWorkingDirectory()
                .getChildFile(juce::String::fromUTF8(replayPath)).loadFileAsString();
            const auto source = juce::JSON::parse(replay);
            const auto* sourceInstruments = source.getProperty("instruments", {}).getArray();
            const auto* sourceSections = source.getProperty("sections", {}).getArray();
            check(sourceInstruments != nullptr && sourceSections != nullptr,
                "captured blueprint is missing instruments or sections");
            auto bars = 0;
            for (const auto& section : *sourceSections)
                bars += static_cast<int>(section.getProperty("bars", {}));
            SongPlan replayed;
            replayed.preserveAuthoredCast = true;
            juce::String replayError;
            check(AiComposer::parseSongPlanJson(replay, 120, bars, 120.0, 4.0,
                1, replayed, replayError),
                "captured blueprint failed local parse");
            check(replayed.instruments.size() ==
                static_cast<std::size_t>(sourceInstruments->size()),
                "captured AI cast expanded during blueprint parsing");
            SongComposer::normalizePlan(replayed);
            check(replayed.instruments.size() ==
                static_cast<std::size_t>(sourceInstruments->size()),
                "captured AI cast expanded during caller normalization");
            for (int i = 0; i < sourceInstruments->size(); ++i)
                check(replayed.instruments[static_cast<std::size_t>(i)].id ==
                    (*sourceInstruments)[i].getProperty("id", {}).toString().toStdString(),
                    "captured AI owner was replaced during blueprint parsing");
        }
        check(AiComposer::parseDirectWindowJson(
            R"({"bed":[],"bass":[{"beat":0,"duration":1,"pitch":33,"velocity":92}]})",
            direct, owners, 0, 0, 2, authored, error),
            "direct MIDI parser rejected a nearby AI-authored bass pitch");
        const auto exact = PerformanceScoreEngine::normalize(authored, 1, {8.0}, true);
        check(exact.notesRejected == 0 && authored.cells[0].notes[0].pitch == 33,
              "AI-sovereign normalization retuned a valid authored pitch");
        Pattern exactChunk;
        PerformanceScoreEngine::replaceChunk(exactChunk, authored, 0, 0.0, 8.0,
                                             direct.instruments, true);
        check(exactChunk.notes.size() == 1 && exactChunk.notes[0].pitch == 33,
              "AI-sovereign rendering retuned a valid authored pitch");
        SongPlan longForm;
        longForm.totalBars = 296;
        longForm.beatsPerBar = 4.0;
        longForm.aiSovereign = true;
        longForm.instrumentCastAuthored = true;
        longForm.instruments = {bed};
        SongSection finalSection;
        finalSection.name = "Final section";
        finalSection.startBar = 272;
        finalSection.bars = 24;
        longForm.sections = {finalSection};
        PerformanceCell longCell;
        longCell.id = "authored_final_phrase";
        longCell.lengthBeats = 32.0;
        longCell.ownedVoices = {VoiceId::HarmonicFoundation};
        for (const auto pitch : {50, 54, 57, 61, 64})
            longCell.notes.push_back({0.0, 32.0, pitch, 72,
                VoiceId::HarmonicFoundation, MetricIntent::StrictGrid, bed.id});
        for (const auto pitch : {54, 61, 64, 69})
            longCell.notes.push_back({8.0, 16.0, pitch, 68,
                VoiceId::HarmonicFoundation, MetricIntent::StrictGrid, bed.id});
        longForm.performanceScore.cells = {longCell};
        PerformancePlacement finalPlacement;
        finalPlacement.cellId = longCell.id;
        finalPlacement.sectionIndex = 0;
        finalPlacement.startBeat = 64.0;
        finalPlacement.fragmentEnd = 32.0;
        finalPlacement.timeScale = 2.0;
        longForm.performanceScore.placements = {finalPlacement};
        CompositionRenderReport longFormReport;
        const auto renderedLongForm = SovereignScoreRenderer::render(
            longForm, {}, &longFormReport);
        check(renderedLongForm.lengthBeats == 1184.0 &&
              renderedLongForm.notes.size() == 9 &&
              longFormReport.production.unsafeDurations == 0 &&
              std::all_of(renderedLongForm.notes.begin(), renderedLongForm.notes.end(),
                  [](const auto& note) {
                      return note.endBeat() <= 1184.000001 &&
                          note.origin == NoteOrigin::AiTransformed;
                  }),
              "long-form AI note releases must stay inside the work without changing its authored attacks");
        std::cout << "Song pipeline parity passed: " << vst.notes.size() << " notes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
