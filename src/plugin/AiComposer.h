#pragma once

#include "core/MusicTypes.h"
#include "core/SongComposer.h"
#include "core/CoherentProofRevision.h"

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
// The final flag marks a rejected diagnostic candidate. It must never replace
// the last accepted checkpoint or be presented as an approved composition.
using AiSongCheckpoint = std::function<void(const SongPlan&, std::size_t, bool, bool)>;

class AiComposer final {
public:
    [[nodiscard]] static bool hasApiKey();
    [[nodiscard]] static bool testApiConnection(const juce::String& candidate,
                                                std::stop_token,
                                                juce::String& error);
    [[nodiscard]] static juce::String defaultModel();
    [[nodiscard]] static juce::String defaultReasoningEffort();
    [[nodiscard]] static bool structuredOutputSchemaIsValid();
    [[nodiscard]] static bool songPlanSchemaIsValid();
    [[nodiscard]] static bool incrementalSchemasAreValid();
    [[nodiscard]] static juce::String performanceSchemaFor(
        const SongPlan&, const std::vector<std::size_t>& assignedInstruments);
    [[nodiscard]] static juce::String directWindowSchemaFor(
        const SongPlan&, const std::vector<std::size_t>& assignedInstruments);
    [[nodiscard]] static bool parseDirectWindowJson(
        const juce::String&, const SongPlan&,
        const std::vector<std::size_t>& assignedInstruments,
        std::size_t sectionIndex, int firstBar, int bars,
        PerformanceScore&, juce::String& error);
    [[nodiscard]] static std::size_t maximumSongInstruments() noexcept;
    [[nodiscard]] static std::size_t castDetailShardCount(std::size_t instruments) noexcept;
    [[nodiscard]] static std::size_t selectiveRepairShardCount(std::size_t instruments) noexcept;
    [[nodiscard]] static std::size_t performanceBlockCount(std::size_t instruments) noexcept;
    // Build the ordered authoring transactions used by the incremental writer.
    // In the AI-only editorial profile the central chord bed and its primary
    // low foundation share one transaction, so their vertical harmony is
    // composed together instead of discovered only by a later audit.
    [[nodiscard]] static std::vector<std::vector<std::size_t>> performanceWritingBlocks(
        const SongPlan&, bool localEditorial);
    [[nodiscard]] static bool castManifestUsesExactCount(std::size_t instruments) noexcept;
    [[nodiscard]] static std::size_t requestedInstrumentCount(const juce::String& direction) noexcept;
    static void applyExplicitInstrumentCommitments(SongPlan&,
                                                   const juce::String& creativeDirection);
    [[nodiscard]] static bool reconcileCastManifest(const juce::String& acceptedManifest,
                                                    const juce::String& supplement,
                                                    std::size_t requestedCount,
                                                    juce::String& reconciledManifest,
                                                    juce::String& error);
    [[nodiscard]] static bool enforceExplicitCastExclusions(
        const juce::String& castManifest, const juce::String& creativeDirection,
        juce::String& sanitizedManifest, std::size_t& removedInstruments,
        juce::String& error);
    [[nodiscard]] static bool bindCastProtagonist(const juce::String& macroBlueprint,
                                                  const juce::String& castManifest,
                                                  juce::String& mergedBlueprint,
                                                  juce::String& error);
    [[nodiscard]] static bool reconcileOrchestrationMatrix(
        const juce::String& macroBlueprint, const juce::String& castManifest,
        juce::String& reconciledManifest, juce::String& report,
        juce::String& error);
    [[nodiscard]] static AiComposition compose(const juce::String& creativeDirection,
                                               int bars, double bpm,
                                               const Pattern* reference,
                                               std::uint8_t lockedLayers,
                                               std::stop_token,
                                               juce::String& error);
    [[nodiscard]] static bool parseCompositionJson(const juce::String&, int requestedBars,
                                                   AiComposition&, juce::String& error);
    [[nodiscard]] static bool parsePerformanceBlockJson(
        const juce::String&, const SongPlan&, const std::vector<std::size_t>& assignedInstruments,
        PerformanceScore&, juce::String& error);
    // Local short-form proof: one absolute-time, jointly authored score.
    [[nodiscard]] static bool parseCoherentProofJson(
        const juce::String&, const SongPlan&, PerformanceScore&, juce::String& error);
    [[nodiscard]] static bool applyCoherentProofRevisionJson(
        const juce::String& original, const juce::String& revision,
        const SongPlan&, const std::vector<CoherentRevisionWindow>&,
        PerformanceScore&, juce::String& error);
    [[nodiscard]] static SongPlan planSong(const juce::String& creativeDirection,
                                           int targetSeconds, int totalBars, double bpm,
                                           double beatsPerBar, std::uint64_t seed,
                                           CompositionBehavior behavior,
                                           std::stop_token, juce::String& error,
                                           const AiSongProgress& progress = {},
                                           const AiSongCheckpoint& checkpoint = {},
                                           bool aiSovereign = false);
    [[nodiscard]] static bool parseSongPlanJson(const juce::String&, int targetSeconds,
                                                int requestedBars, double bpm,
                                                double beatsPerBar, std::uint64_t seed,
                                                SongPlan&, juce::String& error,
                                                TonalPolicy = TonalPolicy::Consolidated);
};

} // namespace pulso::plugin
