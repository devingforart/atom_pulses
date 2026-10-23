#pragma once

#include "core/SongComposer.h"

#include <juce_core/juce_core.h>

namespace pulso::plugin {

struct PhraseRealizationCounts {
    int silentSpeakers{};
    int missingResponses{};
};

// AI-authored musical intentions for an isolated local A/B experiment.
// This is context for the note writer, never a source of procedural MIDI.
class AiPhraseArchitecture final {
public:
    [[nodiscard]] static juce::String schema(CompositionBehavior behavior = CompositionBehavior::Adaptive,
                                             const juce::String& direction = {});
    [[nodiscard]] static juce::String schema(const SongPlan& plan,
                                             const juce::String& direction = {});
    [[nodiscard]] static juce::String prompt(const SongPlan& plan,
                                             const juce::String& direction,
                                             bool longFormArc = false);
    [[nodiscard]] static bool validate(const juce::String& json,
                                       const SongPlan& plan, juce::String& error,
                                       bool enforceActiveSections = true);
    [[nodiscard]] static juce::String windowBrief(const juce::String& json,
                                                   const SongPlan& plan,
                                                   std::size_t sectionIndex,
                                                   int firstBar, int bars);
    // Read-only comparison of AI phrase promises against the rendered MIDI.
    // Silence remains an artistic option; the reviewer decides whether the
    // observed handoff makes that silence meaningful.
    [[nodiscard]] static juce::String realizationBrief(const juce::String& json,
        const SongPlan& plan, const Pattern& midi);
    [[nodiscard]] static PhraseRealizationCounts measureRealization(
        const juce::String& json, const SongPlan& plan, const Pattern& midi);
    // Only completed AI-declared phrases become due. Held notes count; an
    // unfinished phrase or an intentional rest within a window is not a failure.
    [[nodiscard]] static std::vector<std::string> missingCompletedPhraseOwners(
        const juce::String& json, const SongPlan& plan, const Pattern& midi,
        double windowStart, double windowEnd, juce::String* evidence = nullptr);
    // Carry the AI's final speaker handoff into the MIDI audit. No notes are
    // generated or changed; the declared phrase becomes an auditable contract.
    static void applyTerminalHandoff(const juce::String& json, SongPlan& plan);
    [[nodiscard]] static juce::String longFormArcSchema();
    [[nodiscard]] static juce::String longFormArcPrompt(const SongPlan& plan,
                                                        const juce::String& direction,
                                                        const juce::String& phraseJson);
    [[nodiscard]] static bool attachLongFormArc(juce::String& phraseJson,
                                                 const juce::String& arcJson,
                                                 const SongPlan& plan,
                                                 juce::String& error);
    [[nodiscard]] static juce::String groundedStorySchema();
    [[nodiscard]] static juce::String groundedStoryPrompt(const SongPlan& plan,
        const juce::String& direction, const juce::String& phraseJson,
        const Pattern& openingMidi);
    [[nodiscard]] static bool attachGroundedStory(juce::String& phraseJson,
        const juce::String& storyJson, const SongPlan& plan, juce::String& error);
};

} // namespace pulso::plugin
