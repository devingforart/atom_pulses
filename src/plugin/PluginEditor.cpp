#include "PluginEditor.h"

#include "ApiCredentialStore.h"

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

ApiSettingsPanel::ApiSettingsPanel() {
    setOpaque(true);
    heading.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    heading.setColour(juce::Label::textColourId, colours::accent);
    explanation.setColour(juce::Label::textColourId, colours::muted);
    explanation.setJustificationType(juce::Justification::topLeft);
    explanation.setMinimumHorizontalScale(1.0f);
    keyLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    keyLabel.setColour(juce::Label::textColourId, colours::muted);
    keyEditor.setMultiLine(false);
    keyEditor.setReturnKeyStartsNewLine(false);
    keyEditor.setPasswordCharacter(0x2022);
    keyEditor.setInputRestrictions(512);
    keyEditor.onReturnKey = [this] { saveKey(); };
    credentialStatus.setColour(juce::Label::textColourId, colours::muted);
    credentialStatus.setJustificationType(juce::Justification::centredLeft);
    saveButton.onClick = [this] { saveKey(); };
    testButton.onClick = [this] { testKey(); };
    removeButton.onClick = [this] { removeKey(); };
    closeButton.onClick = [this] { setVisible(false); };

    keyEditor.setComponentID("api-key-input");
    saveButton.setComponentID("api-key-save");
    testButton.setComponentID("api-key-test");
    removeButton.setComponentID("api-key-remove");
    closeButton.setComponentID("api-key-close");
    for (auto* component : std::array<juce::Component*, 9>{
             &heading, &explanation, &keyLabel, &keyEditor, &credentialStatus,
             &saveButton, &testButton, &removeButton, &closeButton})
        addAndMakeVisible(component);
    setLanguage(UiLanguage::English);
}

ApiSettingsPanel::~ApiSettingsPanel() {
    connectionThread.request_stop();
}

void ApiSettingsPanel::paint(juce::Graphics& graphics) {
    graphics.fillAll(colours::panel);
    graphics.setColour(colours::accent.withAlpha(0.75f));
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 10.0f, 1.5f);
}

void ApiSettingsPanel::resized() {
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(32);
    closeButton.setBounds(header.removeFromRight(36));
    heading.setBounds(header);
    area.removeFromTop(10);
    explanation.setBounds(area.removeFromTop(48));
    area.removeFromTop(12);
    keyLabel.setBounds(area.removeFromTop(18));
    keyEditor.setBounds(area.removeFromTop(40));
    area.removeFromTop(8);
    credentialStatus.setBounds(area.removeFromTop(34));
    area.removeFromTop(12);
    auto buttons = area.removeFromTop(40);
    saveButton.setBounds(buttons.removeFromLeft(126));
    buttons.removeFromLeft(8);
    testButton.setBounds(buttons.removeFromLeft(126));
    buttons.removeFromLeft(8);
    removeButton.setBounds(buttons.removeFromLeft(126));
}

void ApiSettingsPanel::setLanguage(UiLanguage value) {
    language = value;
    const auto spanish = language == UiLanguage::Spanish;
    heading.setText(spanish ? juce::String::fromUTF8("CONFIGURACI\xC3\x93N IA")
                            : "AI CONFIGURATION", juce::dontSendNotification);
    explanation.setText(spanish
        ? juce::String::fromUTF8("La clave se guarda cifrada para tu usuario de Windows. Nunca se incorpora al proyecto de Ableton, al preset ni a los registros.")
        : "The key is stored securely for your Windows user. It is never embedded in the Ableton project, preset or logs.",
        juce::dontSendNotification);
    keyLabel.setText(spanish ? "OPENAI API KEY" : "OPENAI API KEY", juce::dontSendNotification);
    keyEditor.setTextToShowWhenEmpty(spanish ? "Pega una clave nueva para reemplazar la actual"
                                             : "Paste a new key to replace the current one",
                                     colours::muted);
    saveButton.setButtonText(spanish ? "GUARDAR" : "SAVE");
    testButton.setButtonText(testing ? (spanish ? "PROBANDO..." : "TESTING...")
                                     : (spanish ? "PROBAR" : "TEST"));
    removeButton.setButtonText(spanish ? "ELIMINAR" : "REMOVE");
    closeButton.setButtonText("X");
    keyEditor.setTooltip(spanish ? "Pega la clave completa. El contenido permanece oculto y no se vuelve a mostrar."
                                 : "Paste the complete key. Its contents remain hidden and are never shown again.");
    saveButton.setTooltip(spanish ? "Guarda la clave mediante el Administrador de credenciales de Windows."
                                  : "Save the key with Windows Credential Manager.");
    testButton.setTooltip(spanish ? "Comprueba autenticaci\xC3\xB3n y conectividad sin componer ni consumir una generaci\xC3\xB3n."
                                  : "Check authentication and connectivity without composing or consuming a generation.");
    removeButton.setTooltip(spanish ? "Elimina la clave guardada por PULSO en Windows."
                                    : "Remove the key PULSO saved in Windows.");
    closeButton.setTooltip(spanish ? "Cerrar configuraci\xC3\xB3n" : "Close settings");
    refreshStatus();
}

void ApiSettingsPanel::refreshStatus() {
    if (testing) return;
    const auto spanish = language == UiLanguage::Spanish;
    switch (ApiCredentialStore::source()) {
        case ApiCredentialSource::WindowsCredentialManager:
            credentialStatus.setText(spanish ? juce::String::fromUTF8("Conectada: clave protegida por Windows")
                                             : "Connected: key protected by Windows",
                                     juce::dontSendNotification);
            credentialStatus.setColour(juce::Label::textColourId, colours::accent);
            break;
        case ApiCredentialSource::Environment:
            credentialStatus.setText(spanish ? "Conectada mediante OPENAI_API_KEY"
                                             : "Connected through OPENAI_API_KEY",
                                     juce::dontSendNotification);
            credentialStatus.setColour(juce::Label::textColourId, colours::accentCounter);
            break;
        case ApiCredentialSource::None:
            credentialStatus.setText(spanish ? "No hay una clave configurada"
                                             : "No API key configured",
                                     juce::dontSendNotification);
            credentialStatus.setColour(juce::Label::textColourId, colours::accentHot);
            break;
    }
}

void ApiSettingsPanel::saveKey() {
    juce::String error;
    if (!ApiCredentialStore::save(keyEditor.getText(), error)) {
        credentialStatus.setText(error, juce::dontSendNotification);
        credentialStatus.setColour(juce::Label::textColourId, colours::accentHot);
        return;
    }
    keyEditor.clear();
    refreshStatus();
    if (onCredentialChanged) onCredentialChanged();
}

void ApiSettingsPanel::testKey() {
    if (testing) return;
    auto candidate = keyEditor.getText().trim();
    if (candidate.isEmpty() && !ApiCredentialStore::hasKey()) {
        credentialStatus.setText(language == UiLanguage::Spanish ? "Pega o guarda una clave primero"
                                                                 : "Paste or save a key first",
                                 juce::dontSendNotification);
        credentialStatus.setColour(juce::Label::textColourId, colours::accentHot);
        return;
    }
    testing = true;
    saveButton.setEnabled(false);
    testButton.setEnabled(false);
    removeButton.setEnabled(false);
    setLanguage(language);
    credentialStatus.setText(language == UiLanguage::Spanish ? "Comprobando conexi\xC3\xB3n..."
                                                             : "Checking connection...",
                             juce::dontSendNotification);
    const auto safe = juce::Component::SafePointer<ApiSettingsPanel>(this);
    connectionThread = std::jthread([safe, candidate](std::stop_token token) {
        juce::String detail;
        const auto success = AiComposer::testApiConnection(candidate, token, detail);
        juce::MessageManager::callAsync([safe, success, detail] {
            if (safe != nullptr) safe->finishTest(success, detail);
        });
    });
}

void ApiSettingsPanel::finishTest(bool success, const juce::String& detail) {
    testing = false;
    saveButton.setEnabled(true);
    testButton.setEnabled(true);
    removeButton.setEnabled(true);
    setLanguage(language);
    const auto spanish = language == UiLanguage::Spanish;
    credentialStatus.setText(success
        ? (detail.isNotEmpty() ? detail : (spanish ? "Conexi\xC3\xB3n verificada" : "Connection verified"))
        : detail, juce::dontSendNotification);
    credentialStatus.setColour(juce::Label::textColourId,
                               success ? colours::accent : colours::accentHot);
}

void ApiSettingsPanel::removeKey() {
    juce::String error;
    if (!ApiCredentialStore::removeSaved(error)) {
        credentialStatus.setText(error, juce::dontSendNotification);
        credentialStatus.setColour(juce::Label::textColourId, colours::accentHot);
        return;
    }
    keyEditor.clear();
    refreshStatus();
    if (onCredentialChanged) onCredentialChanged();
}

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
    apiSettingsButton.onClick = [this] {
        apiSettingsPanel.refreshStatus();
        apiSettingsPanel.setVisible(true);
        apiSettingsPanel.toFront(true);
    };
    apiSettingsPanel.onCredentialChanged = [this] { repaint(); };

    languageSelector.setComponentID("language-selector");
    apiSettingsButton.setComponentID("api-settings");
    apiSettingsPanel.setComponentID("api-settings-panel");
    prompt.setComponentID("prompt-input");
    duration.setComponentID("duration-input");
    generateButton.setComponentID("compose-song");
    patternView.setComponentID("midi-vision");
    deployLiveButton.setComponentID("create-in-live");

    for (auto* component : std::array<juce::Component*, 11>{
             &title, &status, &promptLabel, &durationLabel, &prompt, &duration,
             &generateButton, &languageSelector, &apiSettingsButton, &patternView, &deployLiveButton})
        addAndMakeVisible(component);

    addChildComponent(compositionProgress);
    addChildComponent(apiSettingsPanel);

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
    apiSettingsButton.setButtonText(processor.aiAvailable()
        ? (language == UiLanguage::Spanish ? "IA CONECTADA" : "AI CONNECTED")
        : (language == UiLanguage::Spanish ? "CONFIGURAR IA" : "SET UP AI"));
    apiSettingsButton.setTooltip(language == UiLanguage::Spanish
        ? juce::String::fromUTF8("Configura, prueba o elimina tu clave de OpenAI. La clave no se guarda en el proyecto.")
        : "Configure, test or remove your OpenAI key. The key is not stored in the project.");
    apiSettingsPanel.setLanguage(language);
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
    apiSettingsButton.setBounds(header.removeFromRight(150).reduced(5, 9));
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
    const auto panelWidth = std::min(520, getWidth() - 48);
    const auto panelHeight = std::min(300, getHeight() - 48);
    apiSettingsPanel.setBounds((getWidth() - panelWidth) / 2,
                               (getHeight() - panelHeight) / 2,
                               panelWidth, panelHeight);
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
    apiSettingsButton.setEnabled(!composing);
    apiSettingsButton.setButtonText(processor.aiAvailable()
        ? (language == UiLanguage::Spanish ? "IA CONECTADA" : "AI CONNECTED")
        : (language == UiLanguage::Spanish ? "CONFIGURAR IA" : "SET UP AI"));

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
