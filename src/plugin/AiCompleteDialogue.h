#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {
struct DialogueWindow {
    std::size_t section{};
    int firstBar{};
    int bars{};
};
// Complete AI melodic statements are authored before the accompaniment.
// Window packing bounds representation only, never prescribes a musical form.
class AiCompleteDialogue final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static bool wholeArgumentEnabled();
    [[nodiscard]] static juce::String argumentPlanningSchema(const juce::String&);
    [[nodiscard]] static juce::String argumentPlanningBrief();
    [[nodiscard]] static bool validateArgumentOwners(const juce::var&, const SongPlan&, juce::String&);
    [[nodiscard]] static bool pitchedEnsembleEnabled();
    [[nodiscard]] static juce::String accompanimentBrief();
    [[nodiscard]] static std::vector<std::size_t> owners(const SongPlan& plan,
        const std::vector<std::size_t>& available);
    [[nodiscard]] static std::vector<std::vector<DialogueWindow>> blocks(const SongPlan& plan);
    [[nodiscard]] static juce::String schema(const SongPlan& plan,
        const std::vector<std::size_t>& owners);
    [[nodiscard]] static juce::String prompt(const juce::String& direction,
        const SongPlan& plan,
        const std::vector<std::size_t>& owners, const std::vector<DialogueWindow>& windows,
        const Pattern& accepted, bool includePlanningIntentions = true,
        bool gestureCoordinates = false);
    [[nodiscard]] static bool parse(const juce::String& json, const SongPlan& plan,
        const std::vector<std::size_t>& owners, const std::vector<DialogueWindow>& windows,
        PerformanceScore& score, juce::String& error);
};
}
