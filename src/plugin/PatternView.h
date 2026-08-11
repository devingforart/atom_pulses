#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pulso::plugin {

class PatternView final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit PatternView(PulsoAudioProcessor& owner);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    [[nodiscard]] int channelAt(juce::Point<int>) const noexcept;
    [[nodiscard]] bool hasNotesForChannel(int channel) const;
    [[nodiscard]] juce::Rectangle<int> dragStripBounds() const noexcept;
    [[nodiscard]] juce::Rectangle<int> sectionStripBounds() const noexcept;
    [[nodiscard]] int sectionAt(juce::Point<int>) const noexcept;
    [[nodiscard]] int voiceAt(juce::Point<int>) const noexcept;
    [[nodiscard]] int auditionAt(juce::Point<int>) const noexcept;
    [[nodiscard]] juce::Rectangle<int> voiceTimelineBounds() const noexcept;
    [[nodiscard]] static bool noteMatchesTarget(const NoteEvent&, int) noexcept;
    [[nodiscard]] juce::File createExportFile(int channel) const;
    void showTimbreMenu(VoiceId, const juce::MouseEvent&);

    PulsoAudioProcessor& processor;
    int armedChannel{-999};
    int hoverChannel{-999};
    int selectedSection{};
    bool dragAttempted{};
    bool dragInProgress{};
    juce::String feedback;
};

} // namespace pulso::plugin
