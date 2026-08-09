#pragma once

#include "LookAndFeel.h"
#include "PatternView.h"
#include "PluginProcessor.h"

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
    void configureKnob(juce::Slider&, juce::Label&, const juce::String&);

    PulsoAudioProcessor& processor;
    PulsoLookAndFeel pulsoLookAndFeel;
    PatternView patternView;
    juce::Label title;
    juce::Label subtitle;
    juce::Label status;
    juce::ComboBox roleBox;
    juce::ComboBox rootBox;
    juce::ComboBox scaleBox;
    juce::Slider followSlider;
    juce::Slider riskSlider;
    juce::Slider spaceSlider;
    juce::Slider gainSlider;
    juce::Label followLabel;
    juce::Label riskLabel;
    juce::Label spaceLabel;
    juce::Label gainLabel;
    juce::TextButton variationButton{"NEW VARIATION"};
    juce::ToggleButton previewButton{"PREVIEW"};
    juce::ToggleButton thruButton{"MIDI THRU"};

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment> roleAttachment;
    std::unique_ptr<ComboAttachment> rootAttachment;
    std::unique_ptr<ComboAttachment> scaleAttachment;
    std::unique_ptr<SliderAttachment> followAttachment;
    std::unique_ptr<SliderAttachment> riskAttachment;
    std::unique_ptr<SliderAttachment> spaceAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<ButtonAttachment> previewAttachment;
    std::unique_ptr<ButtonAttachment> thruAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessorEditor)
};

} // namespace pulso::plugin

