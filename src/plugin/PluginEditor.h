#pragma once

#include "LookAndFeel.h"
#include "PatternView.h"
#include "PluginProcessor.h"
#include "CompositionProgress.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace pulso::plugin {

class PulsoAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PulsoAudioProcessorEditor(PulsoAudioProcessor&);
    ~PulsoAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureLock(juce::ToggleButton&, PulsoAudioProcessor::Layer, const juce::String&);

    PulsoAudioProcessor& processor;
    PulsoLookAndFeel pulsoLookAndFeel;
    PatternView patternView;
    CompositionProgress compositionProgress;
    juce::TooltipWindow tooltipWindow;
    juce::Label title;
    juce::Label subtitle;
    juce::Label status;
    juce::Label aiBadge;
    juce::Label ideaTitle;
    juce::Label ideaDescription;
    juce::Label promptLabel;
    juce::Label durationLabel;
    juce::TextEditor prompt;
    juce::TextEditor duration;
    juce::TextButton generateButton{"GENERATE IDEA"};
    juce::TextButton nextButton{"NEXT IDEA"};
    juce::TextButton regenerateButton{"REGENERATE UNLOCKED"};
    juce::TextButton undoButton{"UNDO"};
    juce::ToggleButton previewButton{"PREVIEW AUDIO"};
    juce::ToggleButton performanceButton{"HUMAN PERFORMANCE"};
    juce::ComboBox soundWorld;
    juce::ToggleButton thruButton{"MIDI THRU"};
    std::array<juce::ToggleButton, 4> lockButtons;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ButtonAttachment> previewAttachment;
    std::unique_ptr<ButtonAttachment> performanceAttachment;
    std::unique_ptr<ChoiceAttachment> soundWorldAttachment;
    std::unique_ptr<ButtonAttachment> thruAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessorEditor)
};

} // namespace pulso::plugin
