#pragma once

#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <set>

namespace pulso::plugin {

// AI-authored listening hierarchy. Representation only: never generates,
// removes, retimes, thins or otherwise modifies the performed MIDI.
class AiEnsembleDiscourse final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static bool prosodyEnabled();
    [[nodiscard]] static juce::String extendSchema(const juce::String& schema);
    [[nodiscard]] static juce::String planningBrief();
    [[nodiscard]] static juce::String realizationBrief();
    [[nodiscard]] static std::set<std::string> foregroundIds(const SongPlan& plan);
    [[nodiscard]] static std::set<std::string> jointVoiceIds(const SongPlan& plan);
    [[nodiscard]] static juce::String actualPhraseMemory(const SongPlan& plan,
        const Pattern& accepted, double windowStart);
    [[nodiscard]] static bool validate(const juce::var& phrase, double beats,
        const SongPlan& plan, juce::String& error);
    [[nodiscard]] static juce::String phraseBrief(const juce::var& phrase,
        double phraseStart, double windowStart, double windowEnd);
};

} // namespace pulso::plugin
