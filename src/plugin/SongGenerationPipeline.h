#pragma once

#include "AiComposer.h"
#include "core/SongComposer.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <stop_token>

namespace pulso::plugin {

// Everything that can change the musical result belongs in this request, not in
// the VST or Cloud host. Host-specific transport, UI and file handling stay out.
enum class SongOrchestrationIntent : std::uint8_t {
    Adaptive = 0, DeepProduction, Symphonic, ClubElectronic
};

struct SongGenerationRequest {
    juce::String direction;
    juce::String supportingContext;
    int targetSeconds{210};
    double bpm{120.0};
    double beatsPerBar{4.0};
    std::uint64_t seed{1};
    std::uint64_t variationIndex{};
    CompositionBehavior behavior{CompositionBehavior::Adaptive};
    SongOrchestrationIntent orchestration{SongOrchestrationIntent::Adaptive};
    bool aiSovereign{};
};

class SongGenerationPipeline final {
public:
    [[nodiscard]] static juce::String aiDirection(const SongGenerationRequest& request);
    [[nodiscard]] static int targetBars(const SongGenerationRequest& request);
    [[nodiscard]] static SongPlan plan(const SongGenerationRequest& request,
        std::stop_token token, juce::String& error,
        const AiSongProgress& progress = {},
        const AiSongCheckpoint& checkpoint = {});
    static void finalizePlan(SongPlan& plan, const SongGenerationRequest& request);
    [[nodiscard]] static Pattern render(const SongPlan& plan,
        const SongGenerationRequest& request,
        const std::function<void(std::size_t, std::size_t, const SongSection&)>& progress = {},
        CompositionRenderReport* report = nullptr);
};

} // namespace pulso::plugin
