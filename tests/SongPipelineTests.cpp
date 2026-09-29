#include "plugin/SongGenerationPipeline.h"

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
        std::cout << "Song pipeline parity passed: " << vst.notes.size() << " notes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
