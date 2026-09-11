#pragma once

#include "core/MusicTypes.h"
#include "core/SongComposer.h"

#include <juce_core/juce_core.h>

#include <stop_token>
#include <functional>

namespace pulso::plugin {

struct AiComposition {
    Pattern pattern;
    juce::String title;
    juce::String key;
    juce::String summary;
};

enum class AiSongStage : std::uint8_t {
    Blueprint = 0,
    PerformanceBlock,
    Recovery,
    Validation
};

struct AiSongProgressUpdate {
    AiSongStage stage{AiSongStage::Blueprint};
    std::size_t completed{};
    std::size_t total{};
    int attempt{1};
    juce::String detail;
};

using AiSongProgress = std::function<void(const AiSongProgressUpdate&)>;

class AiComposer final {
public:
    [[nodiscard]] static bool hasApiKey();
    [[nodiscard]] static juce::String defaultModel();
    [[nodiscard]] static juce::String defaultReasoningEffort();
    [[nodiscard]] static bool structuredOutputSchemaIsValid();
    [[nodiscard]] static bool songPlanSchemaIsValid();
    [[nodiscard]] static bool incrementalSchemasAreValid();
    [[nodiscard]] static std::size_t maximumSongInstruments() noexcept;
    [[nodiscard]] static std::size_t castDetailShardCount(std::size_t instruments) noexcept;
    [[nodiscard]] static std::size_t selectiveRepairShardCount(std::size_t instruments) noexcept;
    [[nodiscard]] static std::size_t performanceBlockCount(std::size_t instruments) noexcept;
    [[nodiscard]] static AiComposition compose(const juce::String& creativeDirection,
                                               int bars, double bpm,
                                               const Pattern* reference,
                                               std::uint8_t lockedLayers,
                                               std::stop_token,
                                               juce::String& error);
    [[nodiscard]] static bool parseCompositionJson(const juce::String&, int requestedBars,
                                                   AiComposition&, juce::String& error);
    [[nodiscard]] static SongPlan planSong(const juce::String& creativeDirection,
                                           int targetSeconds, int totalBars, double bpm,
                                           double beatsPerBar, std::uint64_t seed,
                                           std::stop_token, juce::String& error,
                                           const AiSongProgress& progress = {});
    [[nodiscard]] static bool parseSongPlanJson(const juce::String&, int targetSeconds,
                                                int requestedBars, double bpm,
                                                double beatsPerBar, std::uint64_t seed,
                                                SongPlan&, juce::String& error,
                                                TonalPolicy = TonalPolicy::Consolidated);
};

} // namespace pulso::plugin
