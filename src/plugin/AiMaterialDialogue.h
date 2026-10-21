#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>

namespace pulso::plugin {
// AI-authored relationships, never a source of automatically generated MIDI.
class AiMaterialDialogue final {
public:
    [[nodiscard]] static bool enabled();
    [[nodiscard]] static bool expectationLedEnabled();
    [[nodiscard]] static juce::String planningSchema(const juce::String&, const SongPlan&);
    [[nodiscard]] static bool validate(const juce::var&, const SongPlan&, juce::String&);
    [[nodiscard]] static juce::String planningBrief();
    [[nodiscard]] static juce::String realizationBrief();
    [[nodiscard]] static juce::String accompanimentBrief();
    [[nodiscard]] static juce::String accompanimentContext(const juce::var&);
    [[nodiscard]] static juce::String reviewBrief();
};
}
