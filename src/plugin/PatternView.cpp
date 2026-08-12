#include "PatternView.h"

#include "LookAndFeel.h"
#include "MidiExporter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso::plugin {
namespace {
constexpr auto fullSongTarget = 0;
constexpr auto sectionTarget = -2;
constexpr auto rhythmTarget = -10;
constexpr auto bassTarget = -11;
constexpr auto harmonyTarget = -12;
constexpr auto melodicTextureTarget = -13;
constexpr auto voiceTargetBase = 100;
constexpr auto noTarget = -999;
constexpr auto laneLabelWidth = 210;
constexpr auto laneButtonWidth = 20;

VoiceId resolvedVoice(const NoteEvent& note) {
    return note.voice == VoiceId::Unspecified ? inferVoiceFromChannel(note.channel) : note.voice;
}

juce::Colour colourForFamily(VoiceFamily family) {
    if (family == VoiceFamily::Rhythm) return colours::accentHot;
    if (family == VoiceFamily::Bass) return colours::accent;
    if (family == VoiceFamily::Harmony) return juce::Colour::fromRGB(199, 143, 255);
    if (family == VoiceFamily::Melodic) return colours::accentCounter;
    return juce::Colour::fromRGB(115, 214, 225);
}

juce::String kickStateDisplay(UiLanguage language, KickState state) {
    switch (state) {
        case KickState::Muted: return tr(language, TextId::KickMuted);
        case KickState::Reduced: return tr(language, TextId::KickReduced);
        case KickState::Sparse: return tr(language, TextId::KickSparse);
        case KickState::FourOnFloor: return tr(language, TextId::KickFourOnFloor);
    }
    return {};
}

class VoiceInspector final : public juce::Component, private juce::Timer {
public:
    VoiceInspector(PulsoAudioProcessor& owner, VoiceId selectedVoice)
        : processor(owner), voice(selectedVoice) {
        const auto language = processor.uiLanguage();
        setSize(390, 184);
        title.setText(voiceDisplayName(language, voice).toUpperCase(),
                      juce::dontSendNotification);
        title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, colours::accent);
        soundLabel.setText(tr(language, TextId::PreviewSound), juce::dontSendNotification);
        octaveLabel.setText(tr(language, TextId::Octave), juce::dontSendNotification);
        levelLabel.setText(tr(language, TextId::Level), juce::dontSendNotification);
        for (auto* label : {&soundLabel, &octaveLabel, &levelLabel}) {
            label->setFont(juce::FontOptions(9.5f, juce::Font::bold));
            label->setColour(juce::Label::textColourId, colours::muted);
        }

        sound.addItemList(localizedTimbreChoices(language, voice), 1);
        sound.setJustificationType(juce::Justification::centred);
        sound.setTooltip(tr(language, TextId::SoundTip));
        soundAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, PulsoAudioProcessor::voicePreviewTimbreParameterId(voice), sound);
        sound.onChange = [this] { processor.auditionVoicePreview(voice); };

        constexpr std::array labels{"-12", "0", "+12"};
        constexpr std::array values{-12, 0, 12};
        for (std::size_t index = 0; index < octaveButtons.size(); ++index) {
            auto& button = octaveButtons[index];
            button.setButtonText(labels[index]);
            button.setClickingTogglesState(true);
            button.setRadioGroupId(8107, juce::dontSendNotification);
            button.setTooltip(tr(language, index == 0 ? TextId::OctaveDownTip :
                                           index == 2 ? TextId::OctaveUpTip :
                                                        TextId::OctaveOriginalTip));
            button.onClick = [this, value = values[index]] {
                processor.setVoicePreviewOctave(voice, value);
                processor.auditionVoicePreview(voice);
            };
        }

        level.setSliderStyle(juce::Slider::LinearHorizontal);
        level.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 22);
        level.setTextValueSuffix(" dB");
        level.setTooltip(tr(language, TextId::LevelTip));
        levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, PulsoAudioProcessor::voicePreviewLevelParameterId(voice), level);
        audition.setButtonText(tr(language, TextId::Audition));
        audition.setTooltip(tr(language, TextId::AuditionTip));
        audition.onClick = [this] { processor.auditionVoicePreview(voice); };

        addAndMakeVisible(title);
        addAndMakeVisible(soundLabel);
        addAndMakeVisible(sound);
        addAndMakeVisible(octaveLabel);
        addAndMakeVisible(levelLabel);
        addAndMakeVisible(level);
        addAndMakeVisible(audition);
        for (auto& button : octaveButtons) addAndMakeVisible(button);
        startTimerHz(20);
        timerCallback();
    }

    void paint(juce::Graphics& graphics) override {
        graphics.fillAll(colours::panel);
        graphics.setColour(colours::panelRaised);
        graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(24));
        auto labels = area.removeFromTop(16);
        soundLabel.setBounds(labels.removeFromLeft(198));
        octaveLabel.setBounds(labels.removeFromLeft(122));
        levelLabel.setBounds(labels);
        auto controls = area.removeFromTop(34);
        sound.setBounds(controls.removeFromLeft(190));
        controls.removeFromLeft(8);
        const auto octaveWidth = 40;
        for (auto& button : octaveButtons) button.setBounds(controls.removeFromLeft(octaveWidth).reduced(2, 0));
        area.removeFromTop(12);
        auto bottom = area.removeFromTop(36);
        audition.setBounds(bottom.removeFromRight(92).reduced(2, 1));
        bottom.removeFromRight(8);
        level.setBounds(bottom);
    }

private:
    void timerCallback() override {
        const auto octave = processor.voicePreviewOctave(voice);
        octaveButtons[0].setToggleState(octave == -12, juce::dontSendNotification);
        octaveButtons[1].setToggleState(octave == 0, juce::dontSendNotification);
        octaveButtons[2].setToggleState(octave == 12, juce::dontSendNotification);
    }

    PulsoAudioProcessor& processor;
    VoiceId voice;
    juce::Label title, soundLabel, octaveLabel, levelLabel;
    juce::ComboBox sound;
    std::array<juce::TextButton, 3> octaveButtons;
    juce::Slider level;
    juce::TextButton audition{"AUDITION"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> soundAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttachment;
};
} // namespace

PatternView::PatternView(PulsoAudioProcessor& owner) : processor(owner) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    languageChanged();
}

void PatternView::languageChanged() {
    setTooltip(tr(processor.uiLanguage(), TextId::PatternTip));
    repaint();
}

juce::Rectangle<int> PatternView::dragStripBounds() const noexcept {
    return getLocalBounds().reduced(14).removeFromBottom(34);
}

juce::Rectangle<int> PatternView::sectionStripBounds() const noexcept {
    auto inner = getLocalBounds().reduced(14);
    inner.removeFromBottom(42);
    return inner.removeFromTop(34);
}

juce::Rectangle<int> PatternView::voiceTimelineBounds() const noexcept {
    auto inner = getLocalBounds().reduced(14);
    inner.removeFromBottom(42);
    const auto plan = processor.currentSongPlan();
    if (plan && !plan->sections.empty()) inner.removeFromTop(40);
    return inner;
}

int PatternView::voiceAt(juce::Point<int> point) const noexcept {
    const auto area = voiceTimelineBounds();
    const auto plan = processor.currentSongPlan();
    if (!area.contains(point)) return -1;
    const auto fullArrangement = plan && !plan->sections.empty();
    const auto laneCount = fullArrangement ? static_cast<int>(voiceDefinitions.size()) : 4;
    const auto lane = std::clamp((point.y - area.getY()) * laneCount /
                                 std::max(1, area.getHeight()),
                                 0, laneCount - 1);
    if (fullArrangement) return voiceTargetBase + static_cast<int>(voiceDefinitions[static_cast<std::size_t>(lane)].id);
    constexpr std::array compactVoices{VoiceId::HarmonicFoundation, VoiceId::Lead,
                                       VoiceId::SubBass, VoiceId::CoreDrums};
    return voiceTargetBase + static_cast<int>(compactVoices[static_cast<std::size_t>(lane)]);
}

int PatternView::auditionAt(juce::Point<int> point) const noexcept {
    const auto area = voiceTimelineBounds();
    if (!area.contains(point)) return 0;
    const auto voiceTarget = voiceAt(point);
    if (voiceTarget < voiceTargetBase) return 0;
    const auto relativeX = point.x - area.getX();
    if (relativeX >= laneLabelWidth - laneButtonWidth * 2 && relativeX < laneLabelWidth - laneButtonWidth)
        return voiceTarget - voiceTargetBase + 1;
    if (relativeX >= laneLabelWidth - laneButtonWidth && relativeX < laneLabelWidth)
        return -(voiceTarget - voiceTargetBase + 1);
    return 0;
}

bool PatternView::noteMatchesTarget(const NoteEvent& note, int target) noexcept {
    if (target == fullSongTarget || target == sectionTarget) return true;
    const auto voice = resolvedVoice(note);
    if (target >= voiceTargetBase)
        return static_cast<int>(voice) == target - voiceTargetBase;
    const auto family = voiceDefinition(voice).family;
    if (target == rhythmTarget) return family == VoiceFamily::Rhythm;
    if (target == bassTarget) return family == VoiceFamily::Bass;
    if (target == harmonyTarget) return family == VoiceFamily::Harmony;
    if (target == melodicTextureTarget)
        return family == VoiceFamily::Melodic || family == VoiceFamily::Texture;
    return false;
}

int PatternView::sectionAt(juce::Point<int> point) const noexcept {
    const auto strip = sectionStripBounds();
    const auto plan = processor.currentSongPlan();
    if (!plan || plan->sections.empty() || !strip.contains(point)) return -1;
    const auto relative = static_cast<double>(point.x - strip.getX()) / std::max(1, strip.getWidth());
    const auto bar = std::clamp(static_cast<int>(relative * plan->totalBars), 0, plan->totalBars - 1);
    for (std::size_t index = 0; index < plan->sections.size(); ++index) {
        const auto& section = plan->sections[index];
        if (bar >= section.startBar && bar < section.startBar + section.bars)
            return static_cast<int>(index);
    }
    return -1;
}

int PatternView::channelAt(juce::Point<int> point) const noexcept {
    const auto strip = dragStripBounds();
    if (!strip.contains(point)) return -1;
    constexpr std::array channels{fullSongTarget, rhythmTarget, bassTarget,
                                  harmonyTarget, melodicTextureTarget, sectionTarget};
    const auto index = std::clamp((point.x - strip.getX()) * 6 / std::max(1, strip.getWidth()), 0, 5);
    return channels[static_cast<std::size_t>(index)];
}

bool PatternView::hasNotesForChannel(int channel) const {
    const auto pattern = processor.currentPattern();
    if (!pattern) return false;
    if (channel == sectionTarget) {
        const auto plan = processor.currentSongPlan();
        return plan && selectedSection >= 0 &&
               selectedSection < static_cast<int>(plan->sections.size());
    }
    return std::any_of(pattern->notes.begin(), pattern->notes.end(), [channel](const auto& note) {
        return noteMatchesTarget(note, channel);
    });
}

juce::File PatternView::createExportFile(int channel) const {
    const auto pattern = processor.currentPattern();
    if (!pattern || pattern->notes.empty()) return {};
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("PULSO MIDI Exports");
    Pattern exportPattern = *pattern;
    juce::String role;
    auto exportStartBeat = 0.0;
    auto exportEndBeat = pattern->lengthBeats;
    if (channel == sectionTarget) {
        const auto plan = processor.currentSongPlan();
        if (!plan || selectedSection < 0 || selectedSection >= static_cast<int>(plan->sections.size())) return {};
        const auto& section = plan->sections[static_cast<std::size_t>(selectedSection)];
        const auto start = section.startBar * plan->beatsPerBar;
        const auto end = (section.startBar + section.bars) * plan->beatsPerBar;
        exportStartBeat = start;
        exportEndBeat = end;
        exportPattern.notes.clear();
        for (const auto& note : pattern->notes) {
            if (note.startBeat >= end || note.endBeat() <= start) continue;
            auto sliced = note;
            sliced.startBeat = std::max(0.0, note.startBeat - start);
            sliced.durationBeats = std::min(note.endBeat(), end) - std::max(note.startBeat, start);
            if (sliced.durationBeats > 0.0) exportPattern.notes.push_back(sliced);
        }
        exportPattern.controls.clear();
        for (const auto& control : pattern->controls) {
            if (control.beat < start || control.beat >= end) continue;
            auto sliced = control;
            sliced.beat -= start;
            exportPattern.controls.push_back(sliced);
        }
        exportPattern.expressions.clear();
        for (const auto& expression : pattern->expressions) {
            if (expression.beat < start || expression.beat >= end) continue;
            auto sliced = expression;
            sliced.beat -= start;
            exportPattern.expressions.push_back(sliced);
        }
        exportPattern.markers = {{0.0, section.name}};
        exportPattern.lengthBeats = section.bars * plan->beatsPerBar;
        role = "Section_" + juce::String(selectedSection + 1) + "_" +
               juce::String::fromUTF8(section.name.c_str()).retainCharacters(
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    } else {
        role = channel == rhythmTarget ? "Rhythm" : channel == bassTarget ? "Bass" :
               channel == harmonyTarget ? "Harmony" : channel == melodicTextureTarget ? "Melodic_Texture" :
               channel >= voiceTargetBase
                   ? juce::String(voiceDefinition(static_cast<VoiceId>(channel - voiceTargetBase)).key.data())
                   : "Full_Song";
        if (channel != fullSongTarget) {
            exportPattern.notes.erase(std::remove_if(exportPattern.notes.begin(), exportPattern.notes.end(),
                [channel](const auto& note) { return !noteMatchesTarget(note, channel); }),
                exportPattern.notes.end());
            exportPattern.controls.erase(std::remove_if(exportPattern.controls.begin(), exportPattern.controls.end(),
                [channel](const auto& control) {
                    NoteEvent probe;
                    probe.channel = control.channel;
                    probe.voice = control.voice;
                    return !noteMatchesTarget(probe, channel);
                }), exportPattern.controls.end());
            exportPattern.expressions.erase(std::remove_if(exportPattern.expressions.begin(), exportPattern.expressions.end(),
                [channel](const auto& expression) {
                    NoteEvent probe;
                    probe.channel = expression.channel;
                    probe.voice = expression.voice;
                    return !noteMatchesTarget(probe, channel);
                }), exportPattern.expressions.end());
        }
    }
    const auto stem = "PULSO_DNA_" + juce::String(processor.currentCompositionSeed()) + "_" +
                      juce::String(processor.currentVariationIndex()) + "_" + role;
    const auto file = folder.getNonexistentChildFile(stem, ".mid", false);
    MidiExportOptions options;
    options.bpm = processor.currentTempo();
    options.timeSignatureNumerator = processor.currentTimeSignatureNumerator();
    options.timeSignatureDenominator = processor.currentTimeSignatureDenominator();
    options.channelFilter = 0;
    options.clipName = stem;
    if (const auto plan = processor.currentSongPlan()) {
        options.includeKeySignature = true;
        options.rootPitchClass = plan->rootPitchClass;
        options.scale = plan->scale;
        for (const auto& section : plan->sections) {
            for (const auto& event : section.harmonicEvents) {
                const auto absoluteBeat = (section.startBar + event.barOffset) * plan->beatsPerBar +
                                          event.beatOffset;
                if (absoluteBeat < exportStartBeat || absoluteBeat >= exportEndBeat) continue;
                const auto chord = std::find_if(plan->chordPalette.begin(), plan->chordPalette.end(),
                    [&](const auto& candidate) { return candidate.id == event.chordId; });
                const auto label = chord == plan->chordPalette.end() || chord->label.empty()
                    ? event.chordId : chord->label;
                options.chordMarkers.push_back({absoluteBeat - exportStartBeat, label});
            }
        }
    }
    return writePatternToMidiFile(exportPattern, file, options) ? file : juce::File{};
}

void PatternView::showTimbreMenu(VoiceId voice, const juce::MouseEvent& event) {
    const auto target = juce::Rectangle<int>{event.getPosition().x, event.getPosition().y, 1, 1};
    juce::CallOutBox::launchAsynchronously(std::make_unique<VoiceInspector>(processor, voice),
                                           target, this);
}

void PatternView::mouseDown(const juce::MouseEvent& event) {
    if (const auto audition = auditionAt(event.getPosition()); audition != 0) {
        const auto voice = static_cast<VoiceId>(std::abs(audition) - 1);
        if (audition > 0) processor.toggleVoiceSolo(voice);
        else processor.toggleVoiceMute(voice);
        const auto language = processor.uiLanguage();
        feedback = voiceDisplayName(language, voice).toUpperCase() + " " + bullet() + " " +
            tr(language, audition > 0 ? (processor.isVoiceSolo(voice) ? TextId::Solo : TextId::SoloOff)
                                      : (processor.isVoiceMuted(voice) ? TextId::Muted : TextId::MuteOff));
        armedChannel = noTarget;
        repaint();
        return;
    }
    if (const auto voiceTarget = voiceAt(event.getPosition()); voiceTarget >= voiceTargetBase) {
        const auto relativeX = event.x - voiceTimelineBounds().getX();
        if (relativeX >= 0 && relativeX < laneLabelWidth - laneButtonWidth * 2) {
            const auto voice = static_cast<VoiceId>(voiceTarget - voiceTargetBase);
            showTimbreMenu(voice, event);
            armedChannel = noTarget;
            return;
        }
    }
    if (const auto section = sectionAt(event.getPosition()); section >= 0) {
        selectedSection = section;
        const auto plan = processor.currentSongPlan();
        const auto rhythm = plan && section < static_cast<int>(plan->sections.size())
            ? kickStateDisplay(processor.uiLanguage(),
                  plan->sections[static_cast<std::size_t>(section)].rhythm.kickState)
            : juce::String{};
        feedback = tr(processor.uiLanguage(), TextId::Section) + " " + juce::String(section + 1) +
                   " " + bullet() + " " + rhythm;
        armedChannel = noTarget;
        repaint();
        return;
    }
    armedChannel = channelAt(event.getPosition());
    if (armedChannel == -1) armedChannel = noTarget;
    if (armedChannel == noTarget) {
        const auto voice = voiceAt(event.getPosition());
        if (voice >= voiceTargetBase) armedChannel = voice;
    }
    dragAttempted = false;
    if (armedChannel != noTarget && !hasNotesForChannel(armedChannel)) armedChannel = noTarget;
    repaint();
}

void PatternView::mouseDrag(const juce::MouseEvent& event) {
    if (armedChannel == noTarget || dragAttempted || dragInProgress ||
        event.getDistanceFromDragStart() < 6)
        return;
    dragAttempted = true;
    const auto file = createExportFile(armedChannel);
    if (!file.existsAsFile()) {
        feedback = tr(processor.uiLanguage(), TextId::ExportFailed);
        repaint();
        return;
    }

    dragInProgress = true;
    feedback = tr(processor.uiLanguage(), TextId::DropIntoAbleton);
    repaint();
    const auto safeThis = juce::Component::SafePointer<PatternView>(this);
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        {file.getFullPathName()}, false, this, [safeThis] {
            if (safeThis == nullptr) return;
            safeThis->dragInProgress = false;
            safeThis->armedChannel = noTarget;
            safeThis->feedback = tr(safeThis->processor.uiLanguage(), TextId::MidiReady);
            safeThis->repaint();
        });
    if (!started) {
        dragInProgress = false;
        feedback = tr(processor.uiLanguage(), TextId::DragUnavailable);
        repaint();
    }
}

void PatternView::mouseUp(const juce::MouseEvent&) {
    if (!dragInProgress) armedChannel = noTarget;
    repaint();
}

void PatternView::mouseMove(const juce::MouseEvent& event) {
    auto next = channelAt(event.getPosition());
    if (next == -1) next = voiceAt(event.getPosition());
    if (next == -1) next = noTarget;
    const auto relativeX = event.x - voiceTimelineBounds().getX();
    setMouseCursor(next >= voiceTargetBase && relativeX >= 0 &&
                           relativeX < laneLabelWidth - laneButtonWidth * 2
                       ? juce::MouseCursor::PointingHandCursor
                       : juce::MouseCursor::DraggingHandCursor);
    if (next == hoverChannel) return;
    hoverChannel = next;
    repaint();
}

void PatternView::mouseExit(const juce::MouseEvent&) {
    hoverChannel = noTarget;
    repaint();
}

void PatternView::paint(juce::Graphics& graphics) {
    const auto language = processor.uiLanguage();
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(colours::panel);
    graphics.fillRoundedRectangle(bounds, 12.0f);
    auto inner = bounds.reduced(14.0f);
    const auto dragStrip = inner.removeFromBottom(34.0f);
    inner.removeFromBottom(8.0f);

    const auto pattern = processor.currentPattern();
    const auto plan = processor.currentSongPlan();
    const auto hasSongPlan = plan && !plan->sections.empty();
    if (hasSongPlan) {
        const auto sectionStrip = inner.removeFromTop(34.0f);
        inner.removeFromTop(6.0f);
        for (std::size_t index = 0; index < plan->sections.size(); ++index) {
            const auto& section = plan->sections[index];
            auto sectionBounds = sectionStrip;
            sectionBounds.setX(sectionStrip.getX() + sectionStrip.getWidth() *
                static_cast<float>(section.startBar) / static_cast<float>(plan->totalBars));
            sectionBounds.setWidth(sectionStrip.getWidth() * static_cast<float>(section.bars) /
                                   static_cast<float>(plan->totalBars));
            sectionBounds.reduce(1.0f, 1.0f);
            const auto colour = colours::accentCounter.interpolatedWith(
                colours::accentHot, static_cast<float>(section.energy));
            graphics.setColour(colour.withAlpha(index == static_cast<std::size_t>(selectedSection)
                                                    ? 0.92f : 0.52f));
            graphics.fillRoundedRectangle(sectionBounds, 5.0f);
            if (index == static_cast<std::size_t>(selectedSection)) {
                graphics.setColour(colours::text);
                graphics.drawRoundedRectangle(sectionBounds, 5.0f, 1.5f);
            }
            if (sectionBounds.getWidth() > 42.0f) {
                graphics.setColour(colours::background);
                graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
                auto label = juce::String::fromUTF8(section.name.c_str()).toUpperCase();
                if (sectionBounds.getWidth() > 78.0f)
                    label += " " + bullet() + " " +
                             kickStateDisplay(language, section.rhythm.kickState);
                graphics.drawFittedText(label,
                                        sectionBounds.toNearestInt().reduced(4, 0),
                                        juce::Justification::centred, 1);
            }
        }
    }
    const auto beatsPerBar = hasSongPlan ? plan->beatsPerBar
                                         : static_cast<double>(processor.currentTimeSignatureNumerator());
    const auto bars = hasSongPlan ? plan->totalBars
        : pattern && pattern->lengthBeats > 0.0
            ? std::max(1, static_cast<int>(std::lround(pattern->lengthBeats / beatsPerBar)))
            : processor.currentPhraseBars();
    std::vector<VoiceId> laneVoices;
    if (hasSongPlan) {
        for (const auto& definition : voiceDefinitions) laneVoices.push_back(definition.id);
    } else {
        laneVoices = {VoiceId::HarmonicFoundation, VoiceId::Lead,
                      VoiceId::SubBass, VoiceId::CoreDrums};
    }
    constexpr auto labelWidth = static_cast<float>(laneLabelWidth);
    auto timeline = inner;
    timeline.removeFromLeft(labelWidth);
    const auto laneHeight = inner.getHeight() / static_cast<float>(laneVoices.size());
    for (std::size_t lane = 0; lane < laneVoices.size(); ++lane) {
        const auto voice = laneVoices[lane];
        const auto laneBounds = juce::Rectangle<float>{inner.getX(), inner.getY() + lane * laneHeight,
                                                       inner.getWidth(), laneHeight};
        if (lane % 2 == 1) {
            graphics.setColour(colours::panelRaised.withAlpha(0.28f));
            graphics.fillRect(laneBounds);
        }
        auto activeInSelection = true;
        if (hasSongPlan && selectedSection >= 0 && selectedSection < static_cast<int>(plan->sections.size())) {
            const auto& active = plan->sections[static_cast<std::size_t>(selectedSection)].activeVoices;
            activeInSelection = std::find(active.begin(), active.end(), voice) != active.end();
        }
        const auto voiceTarget = voiceTargetBase + static_cast<int>(voice);
        const auto highlighted = armedChannel == voiceTarget || hoverChannel == voiceTarget;
        auto timbreArea = laneBounds.withWidth(labelWidth - 40.0f).reduced(1.0f, 1.0f);
        if (highlighted) {
            graphics.setColour(colourForFamily(voiceDefinition(voice).family).withAlpha(0.10f));
            graphics.fillRoundedRectangle(timbreArea, 3.0f);
        }
        graphics.setColour((highlighted ? colourForFamily(voiceDefinition(voice).family) : colours::muted)
                               .withAlpha(activeInSelection ? 1.0f : 0.32f));
        graphics.setFont(juce::FontOptions(hasSongPlan ? 8.6f : 9.5f, juce::Font::bold));
        const auto voiceName = voiceDisplayName(language, voice).toUpperCase();
        const auto timbres = localizedTimbreChoices(language, voice);
        const auto timbreIndex = std::clamp(processor.voicePreviewTimbre(voice), 0,
                                            std::max(0, timbres.size() - 1));
        auto soundName = timbres[timbreIndex].toUpperCase();
        const auto octave = processor.voicePreviewOctave(voice);
        const auto levelDb = processor.voicePreviewLevelDb(voice);
        if (octave != 0) soundName += octave > 0 ? "  +12" : "  -12";
        if (std::abs(levelDb) >= 0.05f)
            soundName += "  " + juce::String(levelDb, 1) + "DB";
        graphics.drawFittedText(juce::String("::  ") + voiceName + "  >  " + soundName,
                                timbreArea.toNearestInt().reduced(3, 0),
                                juce::Justification::centredLeft, 1);
        auto soloButton = laneBounds.withX(laneBounds.getX() + labelWidth - 40.0f).withWidth(19.0f).reduced(1.0f);
        auto muteButton = laneBounds.withX(laneBounds.getX() + labelWidth - 20.0f).withWidth(19.0f).reduced(1.0f);
        const auto solo = processor.isVoiceSolo(voice);
        const auto muted = processor.isVoiceMuted(voice);
        graphics.setColour((solo ? colours::accent : colours::panelRaised).withAlpha(solo ? 0.95f : 0.62f));
        graphics.fillRoundedRectangle(soloButton, 3.0f);
        graphics.setColour(solo ? colours::background : colours::muted);
        graphics.drawText("S", soloButton.toNearestInt(), juce::Justification::centred);
        graphics.setColour((muted ? colours::accentHot : colours::panelRaised).withAlpha(muted ? 0.95f : 0.62f));
        graphics.fillRoundedRectangle(muteButton, 3.0f);
        graphics.setColour(muted ? colours::background : colours::muted);
        graphics.drawText("M", muteButton.toNearestInt(), juce::Justification::centred);
        graphics.setColour(colours::panelRaised);
        graphics.drawHorizontalLine(juce::roundToInt(laneBounds.getBottom()),
                                    timeline.getX(), timeline.getRight());
    }
    const auto barStride = bars <= 16 ? 1 : std::max(1, bars / 16);
    for (auto bar = 0; bar <= bars; ++bar) {
        if (bar != bars && bar % barStride != 0) continue;
        const auto x = timeline.getX() + timeline.getWidth() * static_cast<float>(bar) /
                                          static_cast<float>(bars);
        graphics.setColour(colours::muted.withAlpha(0.42f));
        graphics.drawVerticalLine(juce::roundToInt(x), timeline.getY(), timeline.getBottom());
        if (bar < bars) {
            graphics.setColour(colours::muted.withAlpha(0.65f));
            graphics.setFont(10.0f);
            graphics.drawText(juce::String(bar + 1), juce::roundToInt(x) + 4,
                              juce::roundToInt(timeline.getY()), 22, 13, juce::Justification::left);
        }
    }

    if (!pattern || pattern->notes.empty()) {
        graphics.setColour(colours::muted);
        graphics.setFont(15.0f);
        graphics.drawFittedText(tr(language, TextId::EmptyPattern), inner.toNearestInt(),
                                juce::Justification::centred, 1);
    } else {
        for (std::size_t lane = 0; lane < laneVoices.size(); ++lane) {
            const auto voice = laneVoices[lane];
            auto minPitch = 127;
            auto maxPitch = 0;
            for (const auto& note : pattern->notes) {
                if (resolvedVoice(note) != voice) continue;
                minPitch = std::min(minPitch, note.pitch);
                maxPitch = std::max(maxPitch, note.pitch);
            }
            const auto pitchSpan = std::max(1, maxPitch - minPitch + 1);
            for (const auto& note : pattern->notes) {
                if (resolvedVoice(note) != voice) continue;
                const auto x = timeline.getX() + static_cast<float>(note.startBeat / pattern->lengthBeats) * timeline.getWidth();
                const auto width = std::max(3.0f, static_cast<float>(note.durationBeats / pattern->lengthBeats) * timeline.getWidth());
                const auto normalizedPitch = static_cast<float>(note.pitch - minPitch) / static_cast<float>(pitchSpan);
                const auto y = inner.getY() + lane * laneHeight + laneHeight - 8.0f -
                               normalizedPitch * std::max(3.0f, laneHeight - 16.0f);
                const auto colour = colourForFamily(voiceDefinition(voice).family);
                graphics.setColour(colour.withAlpha(juce::jmap(static_cast<float>(note.velocity),
                                                               1.0f, 127.0f, 0.48f, 1.0f)));
                graphics.fillRoundedRectangle(x, y, width, std::max(2.0f, std::min(6.0f, laneHeight * 0.32f)), 2.0f);
            }
        }
    }

    // The audio thread publishes only atomic transport facts. Painting and text formatting
    // stay here on the message thread, so the moving arrangement playhead is real-time safe.
    const auto arrangementLength = pattern && pattern->lengthBeats > 0.0
        ? pattern->lengthBeats : static_cast<double>(bars) * beatsPerBar;
    if (arrangementLength > 0.0) {
        auto arrangementBeat = std::fmod(processor.currentTransportBeat(), arrangementLength);
        if (arrangementBeat < 0.0) arrangementBeat += arrangementLength;
        const auto normalized = static_cast<float>(arrangementBeat / arrangementLength);
        const auto playheadX = timeline.getX() + timeline.getWidth() * normalized;
        const auto playheadTop = hasSongPlan ? sectionStripBounds().toFloat().getY() : timeline.getY();

        graphics.setColour(colours::accent.withAlpha(0.16f));
        graphics.fillRect(playheadX - 3.0f, playheadTop, 6.0f, timeline.getBottom() - playheadTop);
        graphics.setColour(colours::accent);
        graphics.drawVerticalLine(juce::roundToInt(playheadX), playheadTop, timeline.getBottom());
        juce::Path marker;
        marker.addTriangle(playheadX - 5.0f, playheadTop, playheadX + 5.0f, playheadTop,
                           playheadX, playheadTop + 7.0f);
        graphics.fillPath(marker);

        const auto currentBar = std::clamp(static_cast<int>(arrangementBeat / beatsPerBar), 0, bars - 1);
        juce::String sectionName;
        if (hasSongPlan) {
            for (const auto& section : plan->sections) {
                if (currentBar >= section.startBar && currentBar < section.startBar + section.bars) {
                    sectionName = juce::String::fromUTF8(section.name.c_str()).toUpperCase();
                    break;
                }
            }
        }
        const auto seconds = static_cast<int>(std::floor(arrangementBeat * 60.0 /
                                                          std::max(1.0, processor.currentTempo())));
        auto readout = tr(language, processor.hostIsPlaying() ? TextId::Play : TextId::Paused);
        if (!processor.hasHostTransport()) readout = tr(language, TextId::Preview);
        juce::String playheadText = readout + "  |  " + tr(language, TextId::Bar) + " " +
                                    juce::String(currentBar + 1);
        if (sectionName.isNotEmpty()) playheadText += "  |  " + sectionName;
        playheadText += "  |  " + juce::String(seconds / 60).paddedLeft('0', 2) + ":" +
                        juce::String(seconds % 60).paddedLeft('0', 2);
        constexpr auto readoutWidth = 210.0f;
        const auto readoutX = std::clamp(playheadX - readoutWidth * 0.5f,
                                         timeline.getX(), timeline.getRight() - readoutWidth);
        auto readoutBounds = juce::Rectangle<float>{readoutX, timeline.getY(), readoutWidth, 16.0f};
        graphics.setColour(colours::background.withAlpha(0.88f));
        graphics.fillRoundedRectangle(readoutBounds, 3.0f);
        graphics.setColour(colours::accent);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawFittedText(playheadText, readoutBounds.toNearestInt().reduced(4, 0),
                                juce::Justification::centred, 1);
    }

    const std::array labels{tr(language, TextId::FullSong), tr(language, TextId::Rhythm),
                            tr(language, TextId::Bass), tr(language, TextId::Harmony),
                            tr(language, TextId::LeadsFx), tr(language, TextId::Section)};
    constexpr std::array exportChannels{fullSongTarget, rhythmTarget, bassTarget,
                                        harmonyTarget, melodicTextureTarget, sectionTarget};
    for (auto index = 0; index < 6; ++index) {
        auto cell = dragStrip.toNearestInt();
        const auto cellWidth = cell.getWidth() / 6;
        cell.setX(cell.getX() + index * cellWidth);
        cell.setWidth(index == 5 ? dragStrip.toNearestInt().getRight() - cell.getX() : cellWidth);
        cell.reduce(3, 1);
        const auto channel = exportChannels[static_cast<std::size_t>(index)];
        const auto enabled = hasNotesForChannel(channel);
        const auto highlighted = channel == armedChannel || channel == hoverChannel;
        graphics.setColour((highlighted ? colours::accent : colours::panelRaised)
                               .withAlpha(enabled ? (highlighted ? 0.90f : 0.72f) : 0.22f));
        graphics.fillRoundedRectangle(cell.toFloat(), 6.0f);
        graphics.setColour((highlighted ? colours::background : colours::muted)
                               .withAlpha(enabled ? 1.0f : 0.35f));
        graphics.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        graphics.drawText(juce::String("::  ") + labels[static_cast<std::size_t>(index)], cell,
                          juce::Justification::centred);
    }

    if (feedback.isNotEmpty()) {
        graphics.setColour(colours::accent);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(feedback, inner.removeFromTop(18.0f).toNearestInt(),
                          juce::Justification::centredRight);
    }
}

} // namespace pulso::plugin
