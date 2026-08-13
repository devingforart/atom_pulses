#include "PluginEditor.h"

#include <algorithm>
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
    subtitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, colours::muted);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);
    aiBadge.setJustificationType(juce::Justification::centred);
    aiBadge.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    aiBadge.setColour(juce::Label::textColourId, colours::background);
    aiBadge.setColour(juce::Label::backgroundColourId,
                      processor.aiAvailable() ? colours::accent : colours::accentHot);

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

    ideaTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    ideaTitle.setColour(juce::Label::textColourId, colours::text);
    ideaDescription.setFont(juce::FontOptions(12.0f));
    ideaDescription.setColour(juce::Label::textColourId, colours::muted);

    generateButton.onClick = [this] { processor.requestGenerateIdea(); };
    nextButton.onClick = [this] { processor.requestNextIdea(); };
    regenerateButton.onClick = [this] { processor.requestRegenerateUnlocked(); };
    undoButton.onClick = [this] { processor.requestUndo(); };
    compositionProgress.onCancel = [this] { processor.cancelGeneration(); };

    soundWorld.addItemList(localizedSoundWorlds(UiLanguage::English,
                                                processor.currentPreviewWorldName()), 1);
    soundWorld.setJustificationType(juce::Justification::centred);
    orchestrationIntent.addItem("AUTO ORCHESTRA", 1);
    orchestrationIntent.addItem("DEEP PRODUCTION", 2);
    orchestrationIntent.addItem("SYMPHONIC", 3);
    orchestrationIntent.setSelectedId(static_cast<int>(processor.orchestrationIntent()) + 1,
                                      juce::dontSendNotification);
    orchestrationIntent.setJustificationType(juce::Justification::centred);
    orchestrationIntent.onChange = [this] {
        processor.setOrchestrationIntent(static_cast<PulsoAudioProcessor::OrchestrationIntent>(
            std::clamp(orchestrationIntent.getSelectedId() - 1, 0, 2)));
    };
    languageSelector.addItemList({"ENGLISH", juce::String::fromUTF8("ESPA\xC3\x91OL")}, 1);
    languageSelector.setJustificationType(juce::Justification::centred);

    soundStageLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    soundStageLabel.setColour(juce::Label::textColourId, colours::accent);
    soundStageStatus.setJustificationType(juce::Justification::centredRight);
    soundStageStatus.setColour(juce::Label::textColourId, colours::muted);
    nativeInventory.setJustificationType(juce::Justification::centred);
    nativeInventory.setColour(juce::Label::textColourId, colours::muted);
    liveDeploymentMode.addItem("FULL ORCHESTRATION", 1);
    liveDeploymentMode.addItem("QUICK 3-STEM", 2);
    liveDeploymentMode.setSelectedId(
        processor.liveDeploymentMode() == PulsoAudioProcessor::LiveDeploymentMode::QuickThreeStem ? 2 : 1,
        juce::dontSendNotification);
    liveDeploymentMode.setJustificationType(juce::Justification::centred);
    liveDeploymentMode.onChange = [this] {
        processor.setLiveDeploymentMode(liveDeploymentMode.getSelectedId() == 2
            ? PulsoAudioProcessor::LiveDeploymentMode::QuickThreeStem
            : PulsoAudioProcessor::LiveDeploymentMode::FullOrchestration);
    };
    deployLiveButton.onClick = [this] {
        processor.deployCurrentSongToLive(
            processor.liveDeploymentMode() == PulsoAudioProcessor::LiveDeploymentMode::QuickThreeStem);
    };

    configureLock(lockButtons[0], PulsoAudioProcessor::Layer::Harmony);
    configureLock(lockButtons[1], PulsoAudioProcessor::Layer::Melody);
    configureLock(lockButtons[2], PulsoAudioProcessor::Layer::Bass);
    configureLock(lockButtons[3], PulsoAudioProcessor::Layer::Drums);

    for (auto* component : std::array<juce::Component*, 30>{
             &title, &subtitle, &status, &aiBadge, &promptLabel, &durationLabel, &prompt, &duration,
             &ideaTitle, &ideaDescription, &generateButton, &nextButton, &regenerateButton, &undoButton,
             &previewButton, &performanceButton, &soundWorld, &orchestrationIntent, &languageSelector, &thruButton,
             &lockButtons[0], &lockButtons[1], &lockButtons[2], &lockButtons[3], &patternView,
             &soundStageLabel, &soundStageStatus, &nativeInventory, &liveDeploymentMode, &deployLiveButton})
        addAndMakeVisible(component);

    addChildComponent(compositionProgress);

    previewAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "preview", previewButton);
    performanceAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "performance", performanceButton);
    soundWorldAttachment = std::make_unique<ChoiceAttachment>(processor.parameters, "previewWorld", soundWorld);
    languageAttachment = std::make_unique<ChoiceAttachment>(processor.parameters, "language", languageSelector);
    thruAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "thru", thruButton);
    languageSelector.onChange = [safe = juce::Component::SafePointer<PulsoAudioProcessorEditor>(this)] {
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->applyTranslations();
        });
    };
    applyTranslations();
    startTimerHz(20);
}

PulsoAudioProcessorEditor::~PulsoAudioProcessorEditor() { setLookAndFeel(nullptr); }

void PulsoAudioProcessorEditor::configureLock(juce::ToggleButton& button,
                                               PulsoAudioProcessor::Layer layer) {
    button.setToggleState(processor.isLayerLocked(layer), juce::dontSendNotification);
    button.onClick = [this, &button, layer] {
        processor.setLayerLocked(layer, button.getToggleState());
    };
}

void PulsoAudioProcessorEditor::applyTranslations() {
    const auto language = processor.uiLanguage();
    displayedLanguage = language;
    subtitle.setText(tr(language, TextId::Subtitle) + " " + bullet() + " v" +
                     JucePlugin_VersionString, juce::dontSendNotification);
    promptLabel.setText(tr(language, TextId::PromptLabel), juce::dontSendNotification);
    durationLabel.setText(tr(language, TextId::DurationLabel), juce::dontSendNotification);
    prompt.setTextToShowWhenEmpty(tr(language, TextId::PromptPlaceholder), colours::muted);
    generateButton.setButtonText(tr(language, processor.targetSongDurationSeconds() > 0
        ? TextId::ComposeSong : TextId::GenerateIdea));
    nextButton.setButtonText(tr(language, TextId::NextIdea));
    regenerateButton.setButtonText(tr(language, TextId::RegenerateUnlocked));
    undoButton.setButtonText(tr(language, TextId::Undo));
    previewButton.setButtonText(tr(language, TextId::PreviewAudio));
    performanceButton.setButtonText(tr(language, TextId::HumanPerformance));
    thruButton.setButtonText(tr(language, TextId::MidiThru));
    soundStageLabel.setText(language == UiLanguage::Spanish ? "DIRECTOR DE SONIDO LIVE" :
                            "LIVE SOUND DIRECTOR", juce::dontSendNotification);
    deployLiveButton.setButtonText(language == UiLanguage::Spanish ? "CREAR EN LIVE" : "CREATE IN LIVE");

    constexpr std::array lockNames{TextId::LockHarmony, TextId::LockMelodic,
                                   TextId::LockBass, TextId::LockRhythm};
    constexpr std::array lockTips{TextId::LockHarmonyTip, TextId::LockMelodicTip,
                                  TextId::LockBassTip, TextId::LockRhythmTip};
    for (std::size_t index = 0; index < lockButtons.size(); ++index) {
        lockButtons[index].setButtonText(tr(language, lockNames[index]));
        lockButtons[index].setTooltip(tr(language, lockTips[index]));
    }

    generateButton.setTooltip(tr(language, TextId::GenerateTip));
    nextButton.setTooltip(tr(language, TextId::NextTip));
    regenerateButton.setTooltip(tr(language, TextId::RegenerateTip));
    undoButton.setTooltip(tr(language, TextId::UndoTip));
    previewButton.setTooltip(tr(language, TextId::PreviewTip));
    performanceButton.setTooltip(tr(language, TextId::PerformanceTip));
    soundWorld.setTooltip(tr(language, TextId::SoundWorldTip));
    orchestrationIntent.changeItemText(1, language == UiLanguage::Spanish ? "ORQUESTA AUTO" : "AUTO ORCHESTRA");
    orchestrationIntent.changeItemText(2, language == UiLanguage::Spanish
        ? juce::String::fromUTF8("PRODUCCI\xC3\x93N PROFUNDA") : "DEEP PRODUCTION");
    orchestrationIntent.changeItemText(3, language == UiLanguage::Spanish
        ? juce::String::fromUTF8("SINF\xC3\x93NICO") : "SYMPHONIC");
    orchestrationIntent.setTooltip(language == UiLanguage::Spanish
        ? juce::String::fromUTF8("AUTO adapta el conjunto a tu pedido. PRODUCCI\xC3\x93N PROFUNDA mezcla familias ac\xC3\xBAsticas y electr\xC3\xB3nicas independientes. SINF\xC3\x93NICO prioriza cuerdas divisi, di\xC3\xA1logo de vientos y metales, contrapunto y un arco de c\xC3\xA1mara a tutti.")
        : "AUTO adapts the ensemble to your request. DEEP PRODUCTION combines independent acoustic and electronic families. SYMPHONIC prioritizes divisi strings, woodwind/brass dialogue, counterpoint and a chamber-to-tutti arc.");
    thruButton.setTooltip(tr(language, TextId::ThruTip));
    prompt.setTooltip(tr(language, TextId::PromptTip));
    promptLabel.setTooltip(prompt.getTooltip());
    duration.setTooltip(tr(language, TextId::DurationTip));
    durationLabel.setTooltip(duration.getTooltip());
    title.setTooltip(tr(language, TextId::TitleTip));
    subtitle.setTooltip(tr(language, TextId::SubtitleTip));
    status.setTooltip(tr(language, TextId::StatusTip));
    aiBadge.setTooltip(tr(language, TextId::AiTip));
    ideaTitle.setTooltip(tr(language, TextId::IdeaTitleTip));
    ideaDescription.setTooltip(tr(language, TextId::IdeaDescriptionTip));
    languageSelector.setTooltip(tr(language, TextId::LanguageTip));
    soundStageLabel.setTooltip(tr(language, TextId::SoundStageTip));
    soundStageStatus.setTooltip(tr(language, TextId::SoundStageTip));
    liveDeploymentMode.setTooltip(
        language == UiLanguage::Spanish
            ? "ORQUESTACION COMPLETA crea una pista editable por instrumento y carga sonidos nativos elegidos por la IA. 3 STEMS crea solo Ritmo, Armonia y Melodia."
            : "FULL ORCHESTRATION creates one editable track per instrument and loads AI-selected native sounds. QUICK 3-STEM creates only Rhythm, Harmony and Melody.");
    nativeInventory.setTooltip(language == UiLanguage::Spanish
        ? "Cantidad de dispositivos, presets y Racks nativos que el puente encontro realmente en tu Browser de Live. Nunca incluye VSTs."
        : "Number of native devices, presets and Racks actually found by the bridge in your Live Browser. VSTs are never included.");
    deployLiveButton.setTooltip(language == UiLanguage::Spanish
        ? "Crea o actualiza las pistas en Arrangement, escribe los clips MIDI y carga secuencialmente dispositivos o Racks nativos compatibles."
        : "Creates or updates Arrangement tracks, writes MIDI clips and sequentially loads compatible native devices or Racks.");
    patternView.languageChanged();
    compositionProgress.setLanguage(language);

    const auto selectedWorld = std::max(1, soundWorld.getSelectedId());
    soundWorld.clear(juce::dontSendNotification);
    soundWorld.addItemList(localizedSoundWorlds(language, processor.currentPreviewWorldName()), 1);
    soundWorld.setSelectedId(selectedWorld, juce::dontSendNotification);
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
    subtitle.setBounds(header.removeFromLeft(280).translated(0, 3));
    aiBadge.setBounds(header.removeFromLeft(150).reduced(8, 11));
    languageSelector.setBounds(header.removeFromLeft(105).reduced(5, 9));
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
    patternView.setBounds(area.removeFromTop(std::max(230, area.getHeight() - 126)));
    compositionProgress.setBounds(patternView.getBounds());
    area.removeFromTop(10);

    auto stageHeader = area.removeFromTop(18);
    soundStageLabel.setBounds(stageHeader.removeFromLeft(180));
    soundStageStatus.setBounds(stageHeader);
    auto stage = area.removeFromTop(42);
    liveDeploymentMode.setBounds(stage.removeFromLeft(210).reduced(3, 4));
    deployLiveButton.setBounds(stage.removeFromRight(190).reduced(3, 4));
    nativeInventory.setBounds(stage.reduced(8, 4));
    area.removeFromTop(6);

    auto actions = area;
    previewButton.setBounds(actions.removeFromLeft(130));
    actions.removeFromLeft(8);
    performanceButton.setBounds(actions.removeFromLeft(160));
    actions.removeFromLeft(8);
    soundWorld.setBounds(actions.removeFromLeft(160));
    actions.removeFromLeft(8);
    orchestrationIntent.setBounds(actions.removeFromLeft(160));
    actions.removeFromLeft(8);
    thruButton.setBounds(actions.removeFromLeft(100));
    undoButton.setBounds(actions.removeFromRight(90));
    actions.removeFromRight(8);
    nextButton.setBounds(actions.removeFromRight(130));
    actions.removeFromRight(8);
    regenerateButton.setBounds(actions.removeFromRight(190));
}

void PulsoAudioProcessorEditor::timerCallback() {
    if (displayedLanguage != processor.uiLanguage()) applyTranslations();
    const auto language = processor.uiLanguage();
    const auto composing = processor.isComposing();
    compositionProgress.setComposing(composing, processor.aiAvailable(),
                                     localizeStatus(language, processor.currentAiStatus()),
                                     processor.currentGenerationProgress());
    generateButton.setEnabled(!composing);
    nextButton.setEnabled(!composing);
    regenerateButton.setEnabled(!composing);
    undoButton.setEnabled(!composing);
    prompt.setEnabled(!composing);
    duration.setEnabled(!composing);
    orchestrationIntent.setEnabled(!composing);
    for (auto& button : lockButtons) button.setEnabled(!composing);

    aiBadge.setText(localizeStatus(language, processor.currentAiStatus()),
                    juce::dontSendNotification);
    soundWorld.changeItemText(1, localizedSoundWorlds(language,
                              processor.currentPreviewWorldName())[0]);
    ideaTitle.setText(processor.currentIdeaTitle(), juce::dontSendNotification);
    ideaDescription.setText(processor.currentIdeaDescription(), juce::dontSendNotification);
    status.setText(juce::String(processor.currentTempo(), 1) + " BPM  " + bullet() + "  " +
                       juce::String(processor.currentPhraseBars()) + " " + tr(language, TextId::Bars) +
                       "  " + bullet() + "  " + tr(language, TextId::Idea) + " " +
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
    liveDeploymentMode.setEnabled(!composing);
    soundStageStatus.setText(processor.currentLiveDeployStatus(), juce::dontSendNotification);
    nativeInventory.setText(processor.liveBridgeAvailable() ? processor.currentLiveNativeInventorySummary() :
                            (language == UiLanguage::Spanish ? "ACTIVA PulsoDeployRemote" : "ENABLE PulsoDeployRemote"),
                            juce::dontSendNotification);
    deployLiveButton.setEnabled(processor.liveNativeInventoryReady() && !composing && processor.currentPattern() != nullptr &&
                                !processor.currentPattern()->notes.empty());
    if (processor.currentLiveDeployStatus().isNotEmpty())
        deployLiveButton.setTooltip(tr(language, TextId::DeployLiveTip) + "\n" +
                                    processor.currentLiveDeployStatus() + "\n\n" +
                                    processor.currentLiveDeploymentReport());
    patternView.repaint();
}

} // namespace pulso::plugin
