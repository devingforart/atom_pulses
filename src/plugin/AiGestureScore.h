#pragma once
#include "AiCompleteDialogue.h"

namespace pulso::plugin {
// A different AI writing representation, not a motif expansion/generation engine.
class AiGestureScore final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static juce::String schema(const SongPlan&, const std::vector<std::size_t>&);
    [[nodiscard]] static juce::String prompt(const juce::String&, const SongPlan&,
        const std::vector<std::size_t>&, const std::vector<DialogueWindow>&, const Pattern&);
    [[nodiscard]] static bool parse(const juce::String&, const SongPlan&,
        const std::vector<std::size_t>&, const std::vector<DialogueWindow>&,
        PerformanceScore&, juce::String&);
    [[nodiscard]] static juce::String reviewEvidence(const SongPlan&, const Pattern&,
        const std::vector<std::size_t>&);
};
}
