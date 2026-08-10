#pragma once

#include "core/MusicTypes.h"

#include <juce_core/juce_core.h>

#include <stop_token>

namespace pulso::plugin {

struct AiComposition {
    Pattern pattern;
    juce::String title;
    juce::String key;
    juce::String summary;
};

class AiComposer final {
public:
    [[nodiscard]] static bool hasApiKey();
    [[nodiscard]] static bool structuredOutputSchemaIsValid();
    [[nodiscard]] static AiComposition compose(const juce::String& creativeDirection,
                                               int bars, double bpm,
                                               const Pattern* reference,
                                               std::uint8_t lockedLayers,
                                               std::stop_token,
                                               juce::String& error);
    [[nodiscard]] static bool parseCompositionJson(const juce::String&, int requestedBars,
                                                   AiComposition&, juce::String& error);
};

} // namespace pulso::plugin
