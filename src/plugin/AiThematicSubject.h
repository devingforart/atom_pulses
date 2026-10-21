#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {
// A complete AI-authored reference phrase, never inserted or transformed by code.
class AiThematicSubject final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static juce::String extendSchema(const juce::String&);
    [[nodiscard]] static juce::String planningBrief(const SongPlan&);
    [[nodiscard]] static juce::String realizationBrief();
    [[nodiscard]] static bool validate(const juce::var&, const SongPlan&, juce::String&);
    [[nodiscard]] static bool rehearsalEnabled();
    [[nodiscard]] static juce::String rehearsalSchema(const SongPlan&,
        const std::vector<std::size_t>&);
    [[nodiscard]] static juce::String rehearsalPrompt(const juce::String&,
        const SongPlan&, const std::vector<std::size_t>&, const Pattern&);
    [[nodiscard]] static bool applyScopedRehearsal(const juce::var&, const SongPlan&,
        const std::vector<std::size_t>&, const PerformanceScore& original,
        PerformanceScore& revised, juce::String& error);
};
}
