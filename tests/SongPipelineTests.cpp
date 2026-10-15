#include "plugin/SongGenerationPipeline.h"
#include "plugin/AiComposer.h"
#include "plugin/AiPhraseArchitecture.h"
#include "plugin/JointWindowRecovery.h"
#include "core/SovereignScoreRenderer.h"

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
    try {
        using namespace pulso;
        using namespace pulso::plugin;
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
        check(AiPhraseArchitecture::windowBrief(phraseJson, orderPlan, 0, 0, 8)
                  .contains("state the interrupted ascent") &&
              !AiPhraseArchitecture::windowBrief(phraseJson, orderPlan, 0, 0, 8)
                  .contains("transform the ascent"),
              "window writer should receive only the relevant phrase intentions");
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
