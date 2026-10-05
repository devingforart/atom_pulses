#include "plugin/SongGenerationPipeline.h"
#include "plugin/AiComposer.h"

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
        std::cout << "Song pipeline parity passed: " << vst.notes.size() << " notes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
