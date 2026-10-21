#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {
// AI-authored reference material and relationships; never a MIDI generator.
class AiPhraseDevelopment final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static bool groundworkEnabled();
    [[nodiscard]] static bool consequentialEnabled();
    [[nodiscard]] static juce::String developmentStrategies(const juce::String&);
    [[nodiscard]] static juce::String macroSchema(const juce::String&);
    [[nodiscard]] static juce::String groundworkBrief();
    [[nodiscard]] static bool validateGroundwork(const juce::var&, double, juce::String&);
    [[nodiscard]] static juce::String groundworkContext(const juce::String&);
    [[nodiscard]] static juce::String extendSchema(const juce::String&);
    [[nodiscard]] static juce::String bindCastSchema(const juce::String&, const SongPlan&);
    [[nodiscard]] static bool validate(const juce::var&, const SongPlan&, juce::String&);
    [[nodiscard]] static juce::String castingBrief();
    [[nodiscard]] static juce::String planningBrief();
    [[nodiscard]] static juce::String realizationBrief();
    [[nodiscard]] static juce::String reviewBrief(const SongPlan&, const Pattern&,
        const std::vector<std::size_t>&);
};
}
