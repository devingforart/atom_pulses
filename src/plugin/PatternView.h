#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pulso::plugin {

class PatternView final : public juce::Component {
public:
    explicit PatternView(PulsoAudioProcessor& owner) : processor(owner) {}
    void paint(juce::Graphics&) override;

private:
    PulsoAudioProcessor& processor;
};

} // namespace pulso::plugin

