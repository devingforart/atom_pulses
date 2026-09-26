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
// PULSO's native UI uses the same warm canvas, dark ink and oxide accent as
// the public product. Controls remain high-contrast enough for a DAW session.
inline const auto background = juce::Colour::fromRGB(245, 244, 242);
inline const auto panel = juce::Colour::fromRGB(247, 246, 242);
inline const auto panelRaised = juce::Colour::fromRGB(226, 225, 219);
inline const auto panelHover = juce::Colour::fromRGB(235, 234, 228);
inline const auto accent = juce::Colour::fromRGB(216, 79, 43);
inline const auto accentHot = juce::Colour::fromRGB(168, 56, 29);
inline const auto accentCounter = juce::Colour::fromRGB(113, 161, 208);
inline const auto text = juce::Colour::fromRGB(24, 25, 22);
inline const auto muted = juce::Colour::fromRGB(98, 100, 93);
inline const auto line = juce::Colour::fromRGB(185, 186, 179);
inline const auto lineDark = juce::Colour::fromRGB(119, 121, 114);
// The score is deliberately a darker Ableton-like workspace inside the warm
// product shell. It gives the MIDI map a real destination instead of making
// it look like a pale settings table.
inline const auto stage = juce::Colour::fromRGB(29, 31, 28);
inline const auto stageRaised = juce::Colour::fromRGB(39, 41, 38);
inline const auto stageGrid = juce::Colour::fromRGB(72, 74, 69);
inline const auto stageText = juce::Colour::fromRGB(222, 223, 216);
inline const auto stageMuted = juce::Colour::fromRGB(157, 160, 151);
inline const auto rustPale = juce::Colour::fromRGB(241, 198, 184);
inline const auto green = juce::Colour::fromRGB(117, 181, 124);
inline const auto yellow = juce::Colour::fromRGB(223, 182, 78);
} // namespace colours

} // namespace pulso::plugin
