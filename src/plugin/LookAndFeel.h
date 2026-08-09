#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pulso::plugin {

class PulsoLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    PulsoLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosition, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
};

namespace colours {
inline const auto background = juce::Colour::fromRGB(15, 17, 22);
inline const auto panel = juce::Colour::fromRGB(24, 27, 34);
inline const auto panelRaised = juce::Colour::fromRGB(34, 38, 47);
inline const auto accent = juce::Colour::fromRGB(105, 239, 174);
inline const auto accentHot = juce::Colour::fromRGB(255, 180, 86);
inline const auto text = juce::Colour::fromRGB(235, 239, 244);
inline const auto muted = juce::Colour::fromRGB(135, 145, 158);
} // namespace colours

} // namespace pulso::plugin

