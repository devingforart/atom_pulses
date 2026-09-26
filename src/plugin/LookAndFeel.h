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
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
};

namespace colours {
// A plugin is a working surface, not a web page. The native shell takes the
// palette from the site but keeps every working plane in the same dark studio
// environment: graphite, soft contrast and a precise oxide action colour.
inline const auto background = juce::Colour::fromRGB(23, 24, 22);
inline const auto panel = juce::Colour::fromRGB(31, 33, 30);
inline const auto panelRaised = juce::Colour::fromRGB(42, 44, 40);
inline const auto panelHover = juce::Colour::fromRGB(52, 54, 49);
inline const auto accent = juce::Colour::fromRGB(216, 79, 43);
inline const auto accentHot = juce::Colour::fromRGB(240, 120, 85);
inline const auto accentCounter = juce::Colour::fromRGB(113, 161, 208);
inline const auto text = juce::Colour::fromRGB(230, 231, 225);
inline const auto muted = juce::Colour::fromRGB(174, 176, 168);
inline const auto line = juce::Colour::fromRGB(72, 74, 69);
inline const auto lineDark = juce::Colour::fromRGB(59, 61, 56);
// The score is deliberately a darker Ableton-like workspace inside the warm
// product shell. It gives the MIDI map a real destination instead of making
// it look like a pale settings table.
inline const auto stage = juce::Colour::fromRGB(20, 22, 19);
inline const auto stageRaised = juce::Colour::fromRGB(38, 40, 37);
inline const auto stageGrid = juce::Colour::fromRGB(66, 68, 63);
inline const auto stageText = juce::Colour::fromRGB(212, 213, 207);
inline const auto stageMuted = juce::Colour::fromRGB(158, 161, 152);
inline const auto rustPale = juce::Colour::fromRGB(255, 179, 157);
inline const auto green = juce::Colour::fromRGB(117, 181, 124);
inline const auto yellow = juce::Colour::fromRGB(223, 182, 78);
} // namespace colours

} // namespace pulso::plugin
