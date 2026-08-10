#pragma once

#include "LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pulso::plugin {

class CompositionProgress final : public juce::Component,
                                  public juce::SettableTooltipClient,
                                  private juce::Timer {
public:
    CompositionProgress();

    void setComposing(bool shouldBeActive, bool isUsingAi,
                      const juce::String& stage, float progress);
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    bool active{};
    bool usingAi{};
    double startedAtMs{};
    float phase{};
    float progress{};
    juce::String stage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompositionProgress)
};

} // namespace pulso::plugin
