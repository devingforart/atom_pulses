#include "PluginEditor.h"

namespace pulso::plugin {

PulsoAudioProcessorEditor::PulsoAudioProcessorEditor(PulsoAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner), patternView(owner), tooltipWindow(this, 350) {
    setLookAndFeel(&pulsoLookAndFeel);
    setResizable(true, true);
    setResizeLimits(980, 680, 1500, 980);
    setSize(1120, 760);

    title.setText("PULSO", juce::dontSendNotification);
    title.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, colours::accent);
    subtitle.setText(juce::String("COHERENT GENERATIVE MIDI · v") + JucePlugin_VersionString,
                     juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, colours::muted);
    status.setJustificationType(juce::Justification::centredRight);
    status.setColour(juce::Label::textColourId, colours::muted);
    helpHint.setText("?  HOVER FOR HELP", juce::dontSendNotification);
    helpHint.setJustificationType(juce::Justification::centredRight);
    helpHint.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    helpHint.setColour(juce::Label::textColourId, colours::accent);

    roleBox.addItemList({"Bass", "Percussion", "Countermelody", "Ensemble"}, 1);
    rootBox.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    scaleBox.addItemList({"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"}, 1);
    phraseBox.addItemList({"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 1);
    modeBox.addItemList({"Loop", "Evolve"}, 1);

    for (auto* component : std::array<juce::Component*, 36>{
             &title, &subtitle, &status, &helpHint, &roleBox, &rootBox, &scaleBox, &phraseBox, &modeBox,
             &followSlider, &riskSlider, &spaceSlider, &repetitionSlider, &complexitySlider,
             &developmentSlider, &grooveSlider, &humanizeSlider, &cohesionSlider, &energySlider,
             &gainSlider, &followLabel, &riskLabel, &spaceLabel, &repetitionLabel, &complexityLabel,
             &developmentLabel, &grooveLabel, &humanizeLabel, &cohesionLabel, &energyLabel,
             &gainLabel, &variationButton, &newCompositionButton, &previewButton, &thruButton, &patternView})
        addAndMakeVisible(component);

    configureKnob(followSlider, followLabel, "FOLLOW",
                  "FOLLOW · Seguimiento rítmico\nDetermina cuánto usa PULSO los ataques del MIDI de entrada como referencia.\nAlto: acompaña tu interpretación. Bajo: conserva un pulso más independiente.");
    configureKnob(riskSlider, riskLabel, "RISK",
                  "RISK · Riesgo armónico\nControla la probabilidad de movimientos menos previsibles sin abandonar la escala.\nEmpieza bajo; súbelo cuando quieras tensión o sorpresa.");
    configureKnob(spaceSlider, spaceLabel, "SPACE",
                  "SPACE · Respiración\nIntroduce silencios y reduce la densidad de notas.\nAlto: frase abierta y minimalista. Bajo: interpretación más continua.");
    configureKnob(repetitionSlider, repetitionLabel, "REPEAT",
                  "REPEAT · Identidad del motivo\nControla cuánto se repite la figura reconocible entre compases.\nAlto: hook estable. Bajo: mayor contraste interno.");
    configureKnob(complexitySlider, complexityLabel, "COMPLEXITY",
                  "COMPLEXITY · Detalle local\nAñade síncopas, subdivisiones y notas auxiliares.\nNo cambia la longitud de la frase; cambia cuánta actividad contiene.");
    configureKnob(developmentSlider, developmentLabel, "DEVELOP",
                  "DEVELOP · Arco de la frase\nDefine cuánto evoluciona la idea desde su presentación hasta el último compás.\nAlto: cierre, fill o cadencia más marcados.");
    configureKnob(grooveSlider, grooveLabel, "GROOVE",
                  "GROOVE · Swing compartido\nRetrasa las contras de corchea para abandonar la cuadrícula rígida.\nTodos los roles usan el mismo pocket para seguir sonando como una banda.");
    configureKnob(humanizeSlider, humanizeLabel, "HUMANIZE",
                  "HUMANIZE · Interpretación humana\nAñade microvariaciones deterministas de tiempo y velocidad.\nNo es ruido aleatorio: una misma frase conserva exactamente su feel.");
    configureKnob(cohesionSlider, cohesionLabel, "COHESION",
                  "COHESION · ADN compartido\nControla cuánto conservan los roles y compases el ritmo y contorno del motivo principal.\nAlto: identidad clara. Bajo: mayor libertad y contraste.");
    configureKnob(energySlider, energyLabel, "ENERGY",
                  "ENERGY · Intensidad interpretativa\nAfecta velocidad, densidad de batería, acentos y fuerza del desarrollo.\nÚsalo para adaptar la misma composición a verso, coro o breakdown.");
    configureKnob(gainSlider, gainLabel, "OUTPUT",
                  "OUTPUT · Volumen de preescucha\nAjusta únicamente el sintetizador interno. No modifica velocidades ni el MIDI enviado.\nLa salida está protegida por un limitador a -0.5 dBFS.");
    gainSlider.setTextValueSuffix(" dB");
    variationButton.onClick = [this] { processor.requestVariation(); };
    newCompositionButton.onClick = [this] { processor.requestNewComposition(); };

    title.setTooltip("PULSO genera interpretaciones MIDI coherentes a partir de armonía, escala y ritmo de entrada.");
    subtitle.setTooltip("Versión instalada y propósito del dispositivo. PULSO trabaja localmente y genera MIDI editable.");
    status.setTooltip("Estado global: tempo, longitud, familia DNA.variación y transporte.\nEl número antes del punto cambia con NEW DNA; el segundo avanza con EVOLVE IDEA.");
    helpHint.setTooltip("Deja el cursor 0.35 segundos sobre cualquier control para ver qué hace y cómo afecta la música.");
    roleBox.setTooltip("ROLE · Función musical\nEnsemble: composición coordinada completa.\nBass: bajo en canal 1. Percussion: batería GM en canal 10.\nCountermelody: respuesta melódica en canal 2.");
    rootBox.setTooltip("ROOT · Centro tonal\nSelecciona la nota que funciona como hogar de la escala.\nSe usa como fallback cuando todavía no llegó un acorde MIDI.");
    scaleBox.setTooltip("SCALE · Vocabulario de notas\nMajor: luminoso. Minor: menor natural. Dorian: menor con sexta mayor.\nMixolydian: mayor con séptima menor. Chromatic: permite los 12 sonidos.");
    phraseBox.setTooltip("PHRASE · Longitud estructural\nElige 1, 2, 4, 8 o 16 compases. El patrón completo vuelve a comenzar después de esa longitud.");
    modeBox.setTooltip("MODE · Comportamiento entre vueltas\nLoop repite exactamente la frase.\nEvolve cambia detalles al completar cada vuelta, conservando motivo y función armónica.");
    previewButton.setTooltip("PREVIEW · Audio interno\nActiva el sintetizador de referencia para escuchar PULSO sin otro instrumento.\nDesactívalo cuando uses la salida MIDI con tu propio sintetizador.");
    thruButton.setTooltip("MIDI THRU · Paso de entrada\nActivado: reenvía también las notas que tocas.\nDesactivado: la salida contiene solo la interpretación generada por PULSO.");
    variationButton.setTooltip("EVOLVE IDEA · Variación emparentada\nTransforma el motivo actual mediante respuesta, inversión, desplazamiento o fragmentación.\nConserva el ADN para que el resultado pertenezca a la misma composición.");
    newCompositionButton.setTooltip("NEW DNA · Composición realmente nueva\nCambia el motivo, contorno y arquitectura rítmica de origen.\nÚsalo solo cuando quieras abandonar el hilo musical actual.");
    patternView.setTooltip("PATTERN VIEW · Partitura global\nHorizontal: secciones y tiempo. Vertical: altura MIDI. Brillo: velocidad.\nVerde: bajo/canal 1. Azul: contramelodía/canal 2. Naranja: percusión/canal 10.");

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
    grooveAttachment = std::make_unique<SliderAttachment>(processor.parameters, "groove", grooveSlider);
    humanizeAttachment = std::make_unique<SliderAttachment>(processor.parameters, "humanize", humanizeSlider);
    cohesionAttachment = std::make_unique<SliderAttachment>(processor.parameters, "cohesion", cohesionSlider);
    energyAttachment = std::make_unique<SliderAttachment>(processor.parameters, "energy", energySlider);
    gainAttachment = std::make_unique<SliderAttachment>(processor.parameters, "gain", gainSlider);
    previewAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "preview", previewButton);
    thruAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "thru", thruButton);
    startTimerHz(20);
}

PulsoAudioProcessorEditor::~PulsoAudioProcessorEditor() { setLookAndFeel(nullptr); }

void PulsoAudioProcessorEditor::configureKnob(juce::Slider& slider, juce::Label& label,
                                               const juce::String& text,
                                               const juce::String& tooltip) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, colours::muted);
    slider.setTooltip(tooltip);
    label.setTooltip(tooltip);
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
    helpHint.setBounds(header.removeFromRight(142));
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

    patternView.setBounds(area.removeFromTop(240));
    area.removeFromTop(14);
    auto controls = area;
    auto actions = controls.removeFromRight(170);
    auto actionCentre = actions.withSizeKeepingCentre(170, 112);
    variationButton.setBounds(actionCentre.removeFromTop(48));
    actionCentre.removeFromTop(12);
    newCompositionButton.setBounds(actionCentre.removeFromTop(48));
    controls.removeFromRight(16);
    auto firstRow = controls.removeFromTop(controls.getHeight() / 2);
    auto secondRow = controls;
    auto placeKnob = [](juce::Rectangle<int>& row, int columns,
                        juce::Slider& slider, juce::Label& label) {
        auto cell = row.removeFromLeft(row.getWidth() / columns);
        label.setBounds(cell.removeFromTop(20));
        slider.setBounds(cell.reduced(3));
    };
    placeKnob(firstRow, 6, followSlider, followLabel);
    placeKnob(firstRow, 5, riskSlider, riskLabel);
    placeKnob(firstRow, 4, spaceSlider, spaceLabel);
    placeKnob(firstRow, 3, repetitionSlider, repetitionLabel);
    placeKnob(firstRow, 2, complexitySlider, complexityLabel);
    placeKnob(firstRow, 1, developmentSlider, developmentLabel);
    secondRow.removeFromLeft(secondRow.getWidth() / 12);
    secondRow.removeFromRight(secondRow.getWidth() / 11);
    placeKnob(secondRow, 5, grooveSlider, grooveLabel);
    placeKnob(secondRow, 4, humanizeSlider, humanizeLabel);
    placeKnob(secondRow, 3, cohesionSlider, cohesionLabel);
    placeKnob(secondRow, 2, energySlider, energyLabel);
    placeKnob(secondRow, 1, gainSlider, gainLabel);
}

void PulsoAudioProcessorEditor::timerCallback() {
    status.setText(juce::String(processor.currentTempo(), 1) + " BPM  |  " +
                       juce::String(processor.currentPhraseBars()) + " BARS  |  " +
                       "DNA " + juce::String(processor.currentCompositionSeed()) + "." +
                       juce::String(processor.currentVariationIndex()) + "  |  " +
                       (processor.hostIsPlaying() ? "HOST PLAYING" : "PREVIEW CLOCK"),
                   juce::dontSendNotification);
    patternView.repaint();
}

} // namespace pulso::plugin
