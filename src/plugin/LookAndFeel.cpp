#include "LookAndFeel.h"

namespace pulso::plugin {

PulsoLookAndFeel::PulsoLookAndFeel() {
    setColour(juce::Label::textColourId, colours::text);
    setColour(juce::ComboBox::backgroundColourId, colours::panelRaised);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, colours::accent);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::background);
}

void PulsoLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height,
                                        float position, float startAngle, float endAngle, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height))
                            .reduced(9.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto lineWidth = juce::jmax(3.0f, radius * 0.12f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    graphics.setColour(colours::panelRaised);
    graphics.strokePath(backgroundArc, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    graphics.setColour(colours::accent);
    graphics.strokePath(valueArc, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    const auto thumb = centre.getPointOnCircumference(radius, angle);
    graphics.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(thumb));
}

void PulsoLookAndFeel::drawButtonBackground(juce::Graphics& graphics, juce::Button& button,
                                            const juce::Colour&, bool highlighted, bool down) {
    auto colour = button.getToggleState() ? colours::accent : colours::panelRaised;
    if (highlighted) colour = colour.brighter(0.10f);
    if (down) colour = colour.darker(0.12f);
    graphics.setColour(colour);
    graphics.fillRoundedRectangle(button.getLocalBounds().toFloat(), 8.0f);
}

} // namespace pulso::plugin

