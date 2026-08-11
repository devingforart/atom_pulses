#pragma once

#include "LookAndFeel.h"
#include "Localization.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace pulso::plugin {

class CompositionProgress final : public juce::Component,
                                  public juce::SettableTooltipClient,
                                  private juce::Timer {
public:
    CompositionProgress();

    void setComposing(bool shouldBeActive, bool isUsingAi,
                      const juce::String& stage, float progress);
    void setLanguage(UiLanguage);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onCancel;

private:
    void timerCallback() override;

    bool active{};
    bool usingAi{};
    double startedAtMs{};
    float phase{};
    float progress{};
    juce::String stage;
    juce::TextButton cancelButton{"CANCEL"};
    UiLanguage language{UiLanguage::English};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompositionProgress)
};

} // namespace pulso::plugin
