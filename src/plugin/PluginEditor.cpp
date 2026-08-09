#include "PluginEditor.h"

namespace pulso::plugin {

PulsoAudioProcessorEditor::PulsoAudioProcessorEditor(PulsoAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner), patternView(owner) {
    setLookAndFeel(&pulsoLookAndFeel);
    setResizable(true, true);
    setResizeLimits(720, 460, 1200, 760);
    setSize(860, 540);

    title.setText("PULSO", juce::dontSendNotification);
    title.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, colours::accent);
    subtitle.setText("CONTEXT-AWARE MIDI INSTRUMENT", juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, colours::muted);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);

    roleBox.addItemList({"Bass", "Percussion", "Countermelody"}, 1);
    rootBox.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scaleBox.addItemList({"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"}, 1);

    for (auto* component : std::array<juce::Component*, 18>{
             &title, &subtitle, &status, &roleBox, &rootBox, &scaleBox, &followSlider, &riskSlider,
             &spaceSlider, &gainSlider, &followLabel, &riskLabel, &spaceLabel, &gainLabel,
             &variationButton, &previewButton, &thruButton, &patternView})
        addAndMakeVisible(component);

    configureKnob(followSlider, followLabel, "FOLLOW");
    configureKnob(riskSlider, riskLabel, "RISK");
    configureKnob(spaceSlider, spaceLabel, "SPACE");
    configureKnob(gainSlider, gainLabel, "OUTPUT");
    gainSlider.setTextValueSuffix(" dB");
    variationButton.onClick = [this] { processor.requestVariation(); };

    roleAttachment = std::make_unique<ComboAttachment>(processor.parameters, "role", roleBox);
    rootAttachment = std::make_unique<ComboAttachment>(processor.parameters, "root", rootBox);
    scaleAttachment = std::make_unique<ComboAttachment>(processor.parameters, "scale", scaleBox);
    followAttachment = std::make_unique<SliderAttachment>(processor.parameters, "follow", followSlider);
    riskAttachment = std::make_unique<SliderAttachment>(processor.parameters, "risk", riskSlider);
    spaceAttachment = std::make_unique<SliderAttachment>(processor.parameters, "space", spaceSlider);
    gainAttachment = std::make_unique<SliderAttachment>(processor.parameters, "gain", gainSlider);
    previewAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "preview", previewButton);
    thruAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "thru", thruButton);
    startTimerHz(20);
}

PulsoAudioProcessorEditor::~PulsoAudioProcessorEditor() { setLookAndFeel(nullptr); }

void PulsoAudioProcessorEditor::configureKnob(juce::Slider& slider, juce::Label& label,
                                               const juce::String& text) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, colours::muted);
}

void PulsoAudioProcessorEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(colours::background);
    graphics.setColour(colours::panelRaised.withAlpha(0.7f));
    graphics.drawHorizontalLine(76, 24.0f, static_cast<float>(getWidth() - 24));
}

void PulsoAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(52);
    title.setBounds(header.removeFromLeft(130));
    subtitle.setBounds(header.removeFromLeft(260).translated(0, 3));
    status.setBounds(header);
    area.removeFromTop(18);

    auto selectors = area.removeFromTop(42);
    roleBox.setBounds(selectors.removeFromLeft(220));
    selectors.removeFromLeft(10);
    rootBox.setBounds(selectors.removeFromLeft(90));
    selectors.removeFromLeft(10);
    scaleBox.setBounds(selectors.removeFromLeft(160));
    selectors.removeFromLeft(16);
    previewButton.setBounds(selectors.removeFromLeft(100));
    selectors.removeFromLeft(8);
    thruButton.setBounds(selectors.removeFromLeft(105));
    area.removeFromTop(16);

    patternView.setBounds(area.removeFromTop(190));
    area.removeFromTop(14);
    auto controls = area;
    variationButton.setBounds(controls.removeFromRight(170).withSizeKeepingCentre(170, 48));
    controls.removeFromRight(16);
    const auto knobWidth = controls.getWidth() / 4;
    auto placeKnob = [&](juce::Slider& slider, juce::Label& label) {
        auto cell = controls.removeFromLeft(knobWidth);
        label.setBounds(cell.removeFromTop(20));
        slider.setBounds(cell.reduced(4));
    };
    placeKnob(followSlider, followLabel);
    placeKnob(riskSlider, riskLabel);
    placeKnob(spaceSlider, spaceLabel);
    placeKnob(gainSlider, gainLabel);
}

void PulsoAudioProcessorEditor::timerCallback() {
    status.setText(juce::String(processor.currentTempo(), 1) + " BPM  •  " +
                       (processor.hostIsPlaying() ? "HOST PLAYING" : "PREVIEW CLOCK"),
                   juce::dontSendNotification);
    patternView.repaint();
}

} // namespace pulso::plugin

