#pragma once

#include "LookAndFeel.h"
#include "PatternView.h"
#include "PluginProcessor.h"
#include "CompositionProgress.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace pulso::plugin {

// Destructive host actions must never be activated by Space/Return forwarded
// from the DAW transport. This button accepts pointer activation only.
class MouseOnlyTextButton final : public juce::TextButton {
public:
    explicit MouseOnlyTextButton(const juce::String& text) : juce::TextButton(text) {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    bool keyPressed(const juce::KeyPress&) override { return false; }
};

class PulsoAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PulsoAudioProcessorEditor(PulsoAudioProcessor&);
    ~PulsoAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureLock(juce::ToggleButton&, PulsoAudioProcessor::Layer);
    void applyTranslations();

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
    juce::ComboBox orchestrationIntent;
    juce::ComboBox languageSelector;
    juce::ToggleButton thruButton{"MIDI THRU"};
    juce::Label soundStageLabel;
    juce::Label soundStageStatus;
    juce::Label nativeInventory;
    juce::ComboBox liveDeploymentMode;
    MouseOnlyTextButton deployLiveButton{"CREATE IN LIVE"};
    std::array<juce::ToggleButton, 4> lockButtons;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ButtonAttachment> previewAttachment;
    std::unique_ptr<ButtonAttachment> performanceAttachment;
    std::unique_ptr<ChoiceAttachment> soundWorldAttachment;
    std::unique_ptr<ChoiceAttachment> languageAttachment;
    std::unique_ptr<ButtonAttachment> thruAttachment;
    UiLanguage displayedLanguage{UiLanguage::English};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessorEditor)
};

} // namespace pulso::plugin
