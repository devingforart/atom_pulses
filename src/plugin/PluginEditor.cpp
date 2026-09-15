#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso::plugin {
namespace {

int durationFromText(juce::String text) {
    text = text.trim().toLowerCase();
    if (text.isEmpty() || text == "idea" || text == "loop" || text == "boceto") return 0;
    if (text.containsChar(':')) {
        const auto parts = juce::StringArray::fromTokens(text, ":", "");
        if (parts.size() == 2)
            return std::clamp(parts[0].getIntValue() * 60 + parts[1].getIntValue(), 30, 1800);
    }
    const auto value = text.retainCharacters("0123456789.").getDoubleValue();
    if (value <= 0.0) return 0;
    const auto seconds = text.contains("sec") || text.contains("seg") ? value : value * 60.0;
    return std::clamp(static_cast<int>(std::lround(seconds)), 30, 1800);
}

juce::String durationText(int seconds) {
    if (seconds <= 0) return "IDEA";
    return juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
}

} // namespace

PulsoAudioProcessorEditor::PulsoAudioProcessorEditor(PulsoAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner), patternView(owner), tooltipWindow(this, 350) {
    setLookAndFeel(&pulsoLookAndFeel);
    setResizable(true, true);
    // Live remembers the last VST frame. Raising the minimum beyond that cached frame can
    // leave a valid editor attached to a zero/blank peer on some Windows DPI layouts.
    // Keep the proven-compatible geometry and let the lower panels adapt within it.
    setResizeLimits(1040, 650, 1500, 1020);
    setSize(1120, 760);

    title.setText("PULSO", juce::dontSendNotification);
    title.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, colours::accent);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);

    promptLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    promptLabel.setColour(juce::Label::textColourId, colours::muted);
    durationLabel.setJustificationType(juce::Justification::centredRight);
    durationLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    durationLabel.setColour(juce::Label::textColourId, colours::muted);
    prompt.setText(processor.currentCreativeDirection(), false);
    prompt.setMultiLine(false);
    prompt.setReturnKeyStartsNewLine(false);
    prompt.onTextChange = [this] { processor.setCreativeDirection(prompt.getText()); };
    prompt.onReturnKey = [this] { processor.requestGenerateIdea(); };

    const auto initialDuration = processor.targetSongDurationSeconds() > 0
        ? processor.targetSongDurationSeconds() : 210;
    duration.setText(durationText(initialDuration), false);
    duration.setJustification(juce::Justification::centred);
    duration.setInputRestrictions(8, "0123456789:.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ");
    duration.onTextChange = [this] {
        const auto seconds = durationFromText(duration.getText());
        processor.setTargetSongDurationSeconds(seconds);
        generateButton.setButtonText(tr(processor.uiLanguage(), seconds > 0
            ? TextId::ComposeSong : TextId::GenerateIdea));
    };
    processor.setTargetSongDurationSeconds(initialDuration);

    generateButton.onClick = [this] { processor.requestGenerateIdea(); };
    compositionProgress.onCancel = [this] { processor.cancelGeneration(); };

    languageSelector.addItemList({"ENGLISH", juce::String::fromUTF8("ESPA\xC3\x91OL")}, 1);
    languageSelector.setJustificationType(juce::Justification::centred);

    // The reduced workflow has no hidden creative switches. Old projects may still
    // carry these values, so opening the editor explicitly restores the single,
    // predictable orchestration path exposed by this UI.
    processor.setLiveDeploymentMode(PulsoAudioProcessor::LiveDeploymentMode::FullOrchestration);
    processor.setOrchestrationIntent(PulsoAudioProcessor::OrchestrationIntent::Adaptive);
    for (const auto layer : {PulsoAudioProcessor::Layer::Harmony,
                             PulsoAudioProcessor::Layer::Melody,
                             PulsoAudioProcessor::Layer::Bass,
                             PulsoAudioProcessor::Layer::Drums})
        processor.setLayerLocked(layer, false);

    deployLiveButton.onClick = [this] {
        const auto midiOnly = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
        processor.deployCurrentSongToLive(false, midiOnly);
    };

    languageSelector.setComponentID("language-selector");
    prompt.setComponentID("prompt-input");
    duration.setComponentID("duration-input");
    generateButton.setComponentID("compose-song");
    patternView.setComponentID("midi-vision");
    deployLiveButton.setComponentID("create-in-live");

    for (auto* component : std::array<juce::Component*, 10>{
             &title, &status, &promptLabel, &durationLabel, &prompt, &duration,
             &generateButton, &languageSelector, &patternView, &deployLiveButton})
        addAndMakeVisible(component);

    addChildComponent(compositionProgress);

    languageAttachment = std::make_unique<ChoiceAttachment>(processor.parameters, "language", languageSelector);
    languageSelector.onChange = [safe = juce::Component::SafePointer<PulsoAudioProcessorEditor>(this)] {
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->applyTranslations();
        });
    };
    applyTranslations();
    startTimerHz(20);
}

PulsoAudioProcessorEditor::~PulsoAudioProcessorEditor() { setLookAndFeel(nullptr); }

void PulsoAudioProcessorEditor::applyTranslations() {
    const auto language = processor.uiLanguage();
    displayedLanguage = language;
    promptLabel.setText(tr(language, TextId::PromptLabel), juce::dontSendNotification);
    durationLabel.setText(tr(language, TextId::DurationLabel), juce::dontSendNotification);
    prompt.setTextToShowWhenEmpty(tr(language, TextId::PromptPlaceholder), colours::muted);
    generateButton.setButtonText(tr(language, processor.targetSongDurationSeconds() > 0
        ? TextId::ComposeSong : TextId::GenerateIdea));
    deployLiveButton.setButtonText(language == UiLanguage::Spanish ? "CREAR EN LIVE" : "CREATE IN LIVE");

    generateButton.setTooltip(tr(language, TextId::GenerateTip));
    prompt.setTooltip(tr(language, TextId::PromptTip));
    promptLabel.setTooltip(prompt.getTooltip());
    duration.setTooltip(tr(language, TextId::DurationTip));
    durationLabel.setTooltip(duration.getTooltip());
    title.setTooltip(tr(language, TextId::TitleTip));
    status.setTooltip(tr(language, TextId::StatusTip));
    languageSelector.setTooltip(tr(language, TextId::LanguageTip));
    deployLiveButton.setTooltip(language == UiLanguage::Spanish
        ? juce::String::fromUTF8("Crea todas las pistas MIDI con una paleta neutral y predecible de Live. Mant\xC3\xA9n Shift al hacer clic para crear solamente el MIDI, sin instrumentos.")
        : "Creates every MIDI track with a neutral, predictable Live palette. Hold Shift while clicking to create MIDI only, without instruments.");
    patternView.languageChanged();
    compositionProgress.setLanguage(language);

    resized();
    repaint();
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
    languageSelector.setBounds(header.removeFromRight(120).reduced(5, 9));
    status.setBounds(header.reduced(8, 0));
    area.removeFromTop(18);

    auto promptLabels = area.removeFromTop(18);
    durationLabel.setBounds(promptLabels.removeFromRight(100));
    promptLabel.setBounds(promptLabels);
    auto promptRow = area.removeFromTop(46);
    generateButton.setBounds(promptRow.removeFromRight(180));
    promptRow.removeFromRight(10);
    duration.setBounds(promptRow.removeFromRight(90));
    promptRow.removeFromRight(10);
    prompt.setBounds(promptRow);
    area.removeFromTop(12);

    auto liveRow = area.removeFromBottom(48);
    deployLiveButton.setBounds(liveRow.removeFromRight(220).reduced(0, 4));
    area.removeFromBottom(10);
    patternView.setBounds(area);
    compositionProgress.setBounds(patternView.getBounds());
}

void PulsoAudioProcessorEditor::timerCallback() {
    if (displayedLanguage != processor.uiLanguage()) applyTranslations();
    const auto language = processor.uiLanguage();
    const auto composing = processor.isComposing();
    compositionProgress.setComposing(composing, processor.aiAvailable(),
                                     localizeStatus(language, processor.currentAiStatus()),
                                     processor.currentGenerationProgress());
    generateButton.setEnabled(!composing);
    prompt.setEnabled(!composing);
    duration.setEnabled(!composing);
    languageSelector.setEnabled(!composing);

    status.setText(localizeStatus(language, processor.currentAiStatus()) + "  " + bullet() + "  " +
                       juce::String(processor.currentTempo(), 1) + " BPM  " + bullet() + "  " +
                       juce::String(processor.currentPhraseBars()) + " " + tr(language, TextId::Bars) +
                       "  " + bullet() + "  " + tr(language, TextId::Idea) + " " +
                       juce::String(processor.currentCompositionSeed()) + "." +
                       juce::String(processor.currentVariationIndex()),
                   juce::dontSendNotification);
    deployLiveButton.setEnabled(processor.liveBridgeAvailable() && !composing && processor.currentPattern() != nullptr &&
                                !processor.currentPattern()->notes.empty());
    if (processor.currentLiveDeployStatus().isNotEmpty())
        deployLiveButton.setTooltip(tr(language, TextId::DeployLiveTip) + "\n" +
                                    processor.currentLiveDeployStatus() + "\n\n" +
                                    processor.currentLiveDeploymentReport());
    patternView.repaint();
}

} // namespace pulso::plugin
