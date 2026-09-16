#pragma once

#include "LookAndFeel.h"
#include "PatternView.h"
#include "PluginProcessor.h"
#include "CompositionProgress.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <thread>

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

class ApiSettingsPanel final : public juce::Component {
public:
    ApiSettingsPanel();
    ~ApiSettingsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setLanguage(UiLanguage);
    void refreshStatus();
    std::function<void()> onCredentialChanged;

private:
    void saveKey();
    void testKey();
    void removeKey();
    void finishTest(bool success, const juce::String& detail);

    UiLanguage language{UiLanguage::English};
    juce::Label heading;
    juce::Label explanation;
    juce::Label keyLabel;
    juce::TextEditor keyEditor;
    juce::Label credentialStatus;
    juce::TextButton saveButton;
    juce::TextButton testButton;
    juce::TextButton removeButton;
    juce::TextButton closeButton;
    std::jthread connectionThread;
    bool testing{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ApiSettingsPanel)
};

class PulsoAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PulsoAudioProcessorEditor(PulsoAudioProcessor&);
    ~PulsoAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTranslations();

    PulsoAudioProcessor& processor;
    PulsoLookAndFeel pulsoLookAndFeel;
    PatternView patternView;
    CompositionProgress compositionProgress;
    juce::TooltipWindow tooltipWindow;
    juce::Label title;
    juce::Label status;
    juce::Label promptLabel;
    juce::Label durationLabel;
    juce::TextEditor prompt;
    juce::TextEditor duration;
    juce::TextButton generateButton{"GENERATE IDEA"};
    juce::ComboBox languageSelector;
    juce::TextButton apiSettingsButton{"AI KEY"};
    ApiSettingsPanel apiSettingsPanel;
    MouseOnlyTextButton deployLiveButton{"CREATE IN LIVE"};

    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ChoiceAttachment> languageAttachment;
    UiLanguage displayedLanguage{UiLanguage::English};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessorEditor)
};

} // namespace pulso::plugin
