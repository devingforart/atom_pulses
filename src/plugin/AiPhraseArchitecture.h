#pragma once

#include "core/SongComposer.h"

#include <juce_core/juce_core.h>

namespace pulso::plugin {

// AI-authored musical intentions for an isolated local A/B experiment.
// This is context for the note writer, never a source of procedural MIDI.
class AiPhraseArchitecture final {
public:
    [[nodiscard]] static juce::String schema();
    [[nodiscard]] static juce::String prompt(const SongPlan& plan,
                                             const juce::String& direction);
    [[nodiscard]] static bool validate(const juce::String& json,
                                       const SongPlan& plan, juce::String& error);
    [[nodiscard]] static juce::String windowBrief(const juce::String& json,
                                                   const SongPlan& plan,
                                                   std::size_t sectionIndex,
                                                   int firstBar, int bars);
};

} // namespace pulso::plugin
