#include "LookAndFeel.h"

namespace pulso::plugin {

PulsoLookAndFeel::PulsoLookAndFeel() {
    setDefaultSansSerifTypefaceName("Segoe UI");
    setColour(juce::Label::textColourId, colours::text);
    setColour(juce::ComboBox::backgroundColourId, colours::panel);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::outlineColourId, colours::line);
    setColour(juce::ComboBox::arrowColourId, colours::accent);
    setColour(juce::TextEditor::backgroundColourId, colours::panel);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::outlineColourId, colours::line);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour(juce::TextEditor::highlightColourId, colours::accent.withAlpha(0.20f));
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::background);
    setColour(juce::TooltipWindow::backgroundColourId, colours::text);
    setColour(juce::TooltipWindow::textColourId, colours::background);
    setColour(juce::TooltipWindow::outlineColourId, colours::accent);
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
    graphics.setColour(colours::line);
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
    const auto id = button.getComponentID();
    const auto primary = id == "compose-song";
    const auto live = id == "create-in-live";
    auto fill = primary ? colours::text : (live ? colours::accent : colours::panel);
    auto border = primary ? colours::text : (live ? colours::accent : colours::lineDark);
    if (button.getToggleState()) {
        fill = colours::accent;
        border = colours::accent;
    }
    if (highlighted) fill = fill.interpolatedWith(primary ? colours::accentHot : colours::panelHover, 0.18f);
    if (down) fill = fill.darker(0.10f);
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    graphics.setColour(fill);
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(border);
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

void PulsoLookAndFeel::drawComboBox(juce::Graphics& graphics, int width, int height,
                                    bool isButtonDown, int buttonX, int buttonY,
                                    int buttonW, int buttonH, juce::ComboBox&) {
    auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                                         static_cast<float>(height) - 1.0f);
    graphics.setColour(isButtonDown ? colours::panelHover : colours::panel);
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(colours::line);
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    juce::Path arrow;
    const auto centreX = static_cast<float>(buttonX + buttonW / 2);
    const auto centreY = static_cast<float>(buttonY + buttonH / 2);
    arrow.addTriangle(centreX - 4.0f, centreY - 2.0f, centreX + 4.0f, centreY - 2.0f,
                      centreX, centreY + 3.0f);
    graphics.setColour(colours::accent);
    graphics.fillPath(arrow);
}

} // namespace pulso::plugin
