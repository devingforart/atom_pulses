#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {
namespace {

int durationFromText(juce::String text) {
    text = text.trim().toLowerCase();
    if (text.isEmpty() || text == "idea" || text == "loop") return 0;
    if (text.containsChar(':')) {
        const auto parts = juce::StringArray::fromTokens(text, ":", "");
        if (parts.size() == 2)
            return std::clamp(parts[0].getIntValue() * 60 + parts[1].getIntValue(), 30, 1800);
    }
    const auto value = text.retainCharacters("0123456789.").getDoubleValue();
    if (value <= 0.0) return 0;
    const auto seconds = text.contains("sec") ? value : value * 60.0;
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
    setResizeLimits(920, 650, 1500, 980);
    setSize(1120, 760);

    title.setText("PULSO", juce::dontSendNotification);
    title.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, colours::accent);
    subtitle.setText(juce::String("AI COMPOSITION BROWSER · v") + JucePlugin_VersionString,
                     juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, colours::muted);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);
    aiBadge.setJustificationType(juce::Justification::centred);
    aiBadge.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    aiBadge.setColour(juce::Label::textColourId, colours::background);
    aiBadge.setColour(juce::Label::backgroundColourId,
                      processor.aiAvailable() ? colours::accent : colours::accentHot);

    promptLabel.setText("DESCRIBE THE IDEA (OPTIONAL)", juce::dontSendNotification);
    promptLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    promptLabel.setColour(juce::Label::textColourId, colours::muted);
    durationLabel.setText("SONG LENGTH", juce::dontSendNotification);
    durationLabel.setJustificationType(juce::Justification::centredRight);
    durationLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    durationLabel.setColour(juce::Label::textColourId, colours::muted);
    prompt.setText(processor.currentCreativeDirection(), false);
    prompt.setTextToShowWhenEmpty("e.g. intimate nocturnal soul, memorable hook, tension that blooms in bar 7…",
                                  colours::muted);
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
        generateButton.setButtonText(seconds > 0 ? "COMPOSE SONG" : "GENERATE IDEA");
    };
    processor.setTargetSongDurationSeconds(initialDuration);
    generateButton.setButtonText("COMPOSE SONG");

    ideaTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    ideaTitle.setColour(juce::Label::textColourId, colours::text);
    ideaDescription.setFont(juce::FontOptions(12.0f));
    ideaDescription.setColour(juce::Label::textColourId, colours::muted);

    generateButton.onClick = [this] { processor.requestGenerateIdea(); };
    nextButton.onClick = [this] { processor.requestNextIdea(); };
    regenerateButton.onClick = [this] { processor.requestRegenerateUnlocked(); };
    undoButton.onClick = [this] { processor.requestUndo(); };
    compositionProgress.onCancel = [this] { processor.cancelGeneration(); };

    soundWorld.addItemList({"AUTO · DEEP PROGRESSIVE", "DEEP PROGRESSIVE", "ORGANIC MOTION",
                            "ANALOG WARMTH", "DUB SPACE", "MINIMAL PULSE", "HYPNOTIC NIGHT",
                            "CINEMATIC ARC", "DARK CLUB"}, 1);
    soundWorld.setJustificationType(juce::Justification::centred);

    configureLock(lockButtons[0], PulsoAudioProcessor::Layer::Harmony,
                  "HARMONY + FX · Preserve foundation, pulses, upper harmony, atmosphere and transitions while other families change.");
    configureLock(lockButtons[1], PulsoAudioProcessor::Layer::Melody,
                  "MELODIC · Keep lead and countermelody exactly while regenerating other voice families.");
    configureLock(lockButtons[2], PulsoAudioProcessor::Layer::Bass,
                  "BASS · Keep sub bass and movement bass exactly while regenerating unlocked voices.");
    configureLock(lockButtons[3], PulsoAudioProcessor::Layer::Drums,
                  "RHYTHM · Keep core drums plus low and high percussion exactly while other families change.");

    generateButton.setTooltip("Ask GPT to compose a complete coherent idea. If no API key is available, PULSO uses its local engine honestly.");
    nextButton.setTooltip("Create the next idea. Locked layers remain note-for-note identical; unlocked layers are recomposed.");
    regenerateButton.setTooltip("Recompose only unlocked layers around everything you decided to keep.");
    undoButton.setTooltip("Restore the complete previous idea. Press again to toggle back.");
    previewButton.setTooltip("Enable the multitimbral reference ensemble and selected sound world. MIDI export and output are unaffected.");
    performanceButton.setTooltip("OFF keeps every onset on the exact sixteenth-note grid. ON adds one deterministic performance pass: stable backbeat, coherent hat lift and tiny voice-specific offsets. Dragged MIDI remains perfectly quantized; recording PULSO's MIDI output captures the performed timing.");
    soundWorld.setTooltip("Choose a complete preview sound world for all fifteen voices. AUTO reads the creative direction; manual choices audition the same MIDI through different instruments, drums, space and colour. Monitoring changes, composition does not.");
    thruButton.setTooltip("Also pass incoming MIDI to the output alongside PULSO's composition.");
    prompt.setTooltip("Describe mood, movement, instrumentation or narrative in natural language. Leave empty for an autonomous idea.");
    promptLabel.setTooltip(prompt.getTooltip());
    duration.setTooltip("Target duration. Use 9:00 or '9 min' for a full song; type IDEA for a short compositional sketch.");
    durationLabel.setTooltip(duration.getTooltip());
    title.setTooltip("PULSO turns compositional intent into editable multi-track MIDI.");
    subtitle.setTooltip("The installed version and current product mode.");
    status.setTooltip("Host tempo, phrase length, idea lineage and transport state.");
    aiBadge.setTooltip("GPT status is explicit. PULSO never labels local fallback output as AI-generated.");
    ideaTitle.setTooltip("Title and tonal centre proposed for the current composition.");
    ideaDescription.setTooltip("The compositional intention behind the current idea.");
    patternView.setTooltip("Fifteen dynamically orchestrated voices. Click S or M beside any lane to solo or mute it during MIDI output and preview; this never changes exports. Drag a lane, family, SECTION or FULL SONG into Ableton.");

    for (auto* component : std::array<juce::Component*, 23>{
             &title, &subtitle, &status, &aiBadge, &promptLabel, &durationLabel, &prompt, &duration, &ideaTitle,
             &ideaDescription, &generateButton, &nextButton, &regenerateButton, &undoButton,
             &previewButton, &performanceButton, &soundWorld, &thruButton, &lockButtons[0], &lockButtons[1], &lockButtons[2],
             &lockButtons[3], &patternView})
        addAndMakeVisible(component);

    addChildComponent(compositionProgress);

    previewAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "preview", previewButton);
    performanceAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "performance", performanceButton);
    soundWorldAttachment = std::make_unique<ChoiceAttachment>(processor.parameters, "previewWorld", soundWorld);
    thruAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "thru", thruButton);
    startTimerHz(20);
}

PulsoAudioProcessorEditor::~PulsoAudioProcessorEditor() { setLookAndFeel(nullptr); }

void PulsoAudioProcessorEditor::configureLock(juce::ToggleButton& button,
                                               PulsoAudioProcessor::Layer layer,
                                               const juce::String& tooltip) {
    constexpr std::array names{"LOCK HARMONY + FX", "LOCK MELODIC", "LOCK BASS", "LOCK RHYTHM"};
    button.setButtonText(names[static_cast<std::size_t>(layer)]);
    button.setToggleState(processor.isLayerLocked(layer), juce::dontSendNotification);
    button.setTooltip(tooltip);
    button.onClick = [this, &button, layer] { processor.setLayerLocked(layer, button.getToggleState()); };
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
    subtitle.setBounds(header.removeFromLeft(280).translated(0, 3));
    aiBadge.setBounds(header.removeFromLeft(150).reduced(8, 11));
    status.setBounds(header);
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

    auto ideaHeader = area.removeFromTop(48);
    ideaTitle.setBounds(ideaHeader.removeFromTop(26));
    ideaDescription.setBounds(ideaHeader);
    area.removeFromTop(8);

    auto locks = area.removeFromTop(34);
    for (auto& button : lockButtons) {
        button.setBounds(locks.removeFromLeft(locks.getWidth() /
                         static_cast<int>(&lockButtons.back() - &button + 1)).reduced(3, 0));
    }
    area.removeFromTop(8);
    patternView.setBounds(area.removeFromTop(std::max(260, area.getHeight() - 62)));
    compositionProgress.setBounds(patternView.getBounds());
    area.removeFromTop(10);

    auto actions = area;
    previewButton.setBounds(actions.removeFromLeft(130));
    actions.removeFromLeft(8);
    performanceButton.setBounds(actions.removeFromLeft(150));
    actions.removeFromLeft(8);
    soundWorld.setBounds(actions.removeFromLeft(170));
    actions.removeFromLeft(8);
    thruButton.setBounds(actions.removeFromLeft(110));
    undoButton.setBounds(actions.removeFromRight(90));
    actions.removeFromRight(8);
    nextButton.setBounds(actions.removeFromRight(130));
    actions.removeFromRight(8);
    regenerateButton.setBounds(actions.removeFromRight(210));
}

void PulsoAudioProcessorEditor::timerCallback() {
    const auto composing = processor.isComposing();
    compositionProgress.setComposing(composing, processor.aiAvailable(),
                                     processor.currentAiStatus(),
                                     processor.currentGenerationProgress());
    generateButton.setEnabled(!composing);
    nextButton.setEnabled(!composing);
    regenerateButton.setEnabled(!composing);
    undoButton.setEnabled(!composing);
    prompt.setEnabled(!composing);
    duration.setEnabled(!composing);
    for (auto& button : lockButtons) button.setEnabled(!composing);

    aiBadge.setText(processor.currentAiStatus(), juce::dontSendNotification);
    soundWorld.changeItemText(1, "AUTO · " + processor.currentPreviewWorldName());
    ideaTitle.setText(processor.currentIdeaTitle(), juce::dontSendNotification);
    ideaDescription.setText(processor.currentIdeaDescription(), juce::dontSendNotification);
    status.setText(juce::String(processor.currentTempo(), 1) + " BPM  ·  " +
                       juce::String(processor.currentPhraseBars()) + " BARS  ·  IDEA " +
                       juce::String(processor.currentCompositionSeed()) + "." +
                       juce::String(processor.currentVariationIndex()),
                   juce::dontSendNotification);
    constexpr std::array layers{PulsoAudioProcessor::Layer::Harmony,
                                PulsoAudioProcessor::Layer::Melody,
                                PulsoAudioProcessor::Layer::Bass,
                                PulsoAudioProcessor::Layer::Drums};
    for (std::size_t index = 0; index < layers.size(); ++index)
        lockButtons[index].setToggleState(processor.isLayerLocked(layers[index]),
                                          juce::dontSendNotification);
    patternView.repaint();
}

} // namespace pulso::plugin
