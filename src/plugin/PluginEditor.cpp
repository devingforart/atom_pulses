#include "PluginEditor.h"

namespace pulso::plugin {

PulsoAudioProcessorEditor::PulsoAudioProcessorEditor(PulsoAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner), patternView(owner) {
    setLookAndFeel(&pulsoLookAndFeel);
    setResizable(true, true);
    setResizeLimits(900, 560, 1400, 900);
    setSize(1080, 640);

    title.setText("PULSO", juce::dontSendNotification);
    title.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, colours::accent);
    subtitle.setText(juce::String("COHERENT GENERATIVE MIDI · v") + JucePlugin_VersionString,
                     juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, colours::muted);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);

    roleBox.addItemList({"Bass", "Percussion", "Countermelody"}, 1);
    rootBox.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scaleBox.addItemList({"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"}, 1);
    phraseBox.addItemList({"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 1);
    modeBox.addItemList({"Loop", "Evolve"}, 1);

    for (auto* component : std::array<juce::Component*, 26>{
             &title, &subtitle, &status, &roleBox, &rootBox, &scaleBox, &phraseBox, &modeBox,
             &followSlider, &riskSlider, &spaceSlider, &repetitionSlider, &complexitySlider,
             &developmentSlider, &gainSlider, &followLabel, &riskLabel, &spaceLabel,
             &repetitionLabel, &complexityLabel, &developmentLabel, &gainLabel, &variationButton,
             &previewButton, &thruButton, &patternView})
        addAndMakeVisible(component);

    configureKnob(followSlider, followLabel, "FOLLOW");
    configureKnob(riskSlider, riskLabel, "RISK");
    configureKnob(spaceSlider, spaceLabel, "SPACE");
    configureKnob(repetitionSlider, repetitionLabel, "REPEAT");
    configureKnob(complexitySlider, complexityLabel, "COMPLEXITY");
    configureKnob(developmentSlider, developmentLabel, "DEVELOP");
    configureKnob(gainSlider, gainLabel, "OUTPUT");
    gainSlider.setTextValueSuffix(" dB");
    variationButton.onClick = [this] { processor.requestVariation(); };

    roleAttachment = std::make_unique<ComboAttachment>(processor.parameters, "role", roleBox);
    rootAttachment = std::make_unique<ComboAttachment>(processor.parameters, "root", rootBox);
    scaleAttachment = std::make_unique<ComboAttachment>(processor.parameters, "scale", scaleBox);
    phraseAttachment = std::make_unique<ComboAttachment>(processor.parameters, "phraseBars", phraseBox);
    modeAttachment = std::make_unique<ComboAttachment>(processor.parameters, "mode", modeBox);
    followAttachment = std::make_unique<SliderAttachment>(processor.parameters, "follow", followSlider);
    riskAttachment = std::make_unique<SliderAttachment>(processor.parameters, "risk", riskSlider);
    spaceAttachment = std::make_unique<SliderAttachment>(processor.parameters, "space", spaceSlider);
    repetitionAttachment = std::make_unique<SliderAttachment>(processor.parameters, "repetition", repetitionSlider);
    complexityAttachment = std::make_unique<SliderAttachment>(processor.parameters, "complexity", complexitySlider);
    developmentAttachment = std::make_unique<SliderAttachment>(processor.parameters, "development", developmentSlider);
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
    label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
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
    subtitle.setBounds(header.removeFromLeft(310).translated(0, 3));
    status.setBounds(header);
    area.removeFromTop(18);

    auto selectors = area.removeFromTop(42);
    roleBox.setBounds(selectors.removeFromLeft(190));
    selectors.removeFromLeft(8);
    rootBox.setBounds(selectors.removeFromLeft(70));
    selectors.removeFromLeft(8);
    scaleBox.setBounds(selectors.removeFromLeft(140));
    selectors.removeFromLeft(8);
    phraseBox.setBounds(selectors.removeFromLeft(115));
    selectors.removeFromLeft(8);
    modeBox.setBounds(selectors.removeFromLeft(105));
    selectors.removeFromLeft(14);
    previewButton.setBounds(selectors.removeFromLeft(95));
    selectors.removeFromLeft(8);
    thruButton.setBounds(selectors.removeFromLeft(105));
    area.removeFromTop(16);

    patternView.setBounds(area.removeFromTop(250));
    area.removeFromTop(14);
    auto controls = area;
    variationButton.setBounds(controls.removeFromRight(170).withSizeKeepingCentre(170, 48));
    controls.removeFromRight(16);
    const auto knobWidth = controls.getWidth() / 7;
    auto placeKnob = [&](juce::Slider& slider, juce::Label& label) {
        auto cell = controls.removeFromLeft(knobWidth);
        label.setBounds(cell.removeFromTop(20));
        slider.setBounds(cell.reduced(3));
    };
    placeKnob(followSlider, followLabel);
    placeKnob(riskSlider, riskLabel);
    placeKnob(spaceSlider, spaceLabel);
    placeKnob(repetitionSlider, repetitionLabel);
    placeKnob(complexitySlider, complexityLabel);
    placeKnob(developmentSlider, developmentLabel);
    placeKnob(gainSlider, gainLabel);
}

void PulsoAudioProcessorEditor::timerCallback() {
    status.setText(juce::String(processor.currentTempo(), 1) + " BPM  |  " +
                       juce::String(processor.currentPhraseBars()) + " BARS  |  " +
                       (processor.hostIsPlaying() ? "HOST PLAYING" : "PREVIEW CLOCK"),
                   juce::dontSendNotification);
    patternView.repaint();
}

} // namespace pulso::plugin
