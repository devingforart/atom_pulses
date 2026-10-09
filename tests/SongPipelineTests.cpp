#include "plugin/SongGenerationPipeline.h"
#include "plugin/AiComposer.h"
#include "core/SovereignScoreRenderer.h"

#include <cmath>
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
