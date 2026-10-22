#pragma once

#include "core/SongComposer.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {

// An AI-authored, optional delivery sketch. This module supplies context and
// validates its representation; it never generates, stretches or edits MIDI.
class AiMelodicDiscourse final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static juce::String extendSchema(const juce::String& schema);
    [[nodiscard]] static juce::String planningBrief(double bpm, CompositionBehavior behavior = CompositionBehavior::Adaptive);
    [[nodiscard]] static int planningOutputTokenBudget(int sections);
    [[nodiscard]] static juce::String macroBrief(CompositionBehavior behavior = CompositionBehavior::Adaptive);
    [[nodiscard]] static std::vector<std::vector<std::size_t>> writingGroups(
        const SongPlan& plan, const std::vector<std::size_t>& owners);
    [[nodiscard]] static bool validate(const juce::var& phrase, double beats,
        int minimumPitch, int maximumPitch, juce::String& error);
    [[nodiscard]] static juce::String phraseBrief(const juce::var& phrase,
        double phraseStart, double windowStart, double windowEnd, double bpm);
    [[nodiscard]] static juce::String realizationBrief(double bpm, CompositionBehavior behavior = CompositionBehavior::Adaptive);
    [[nodiscard]] static juce::String actualVoiceMemory(const SongPlan& plan,
        const Pattern& accepted, double windowStart);
};

} // namespace pulso::plugin
