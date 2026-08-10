#include "PatternView.h"

#include "LookAndFeel.h"
#include "MidiExporter.h"

#include <algorithm>
#include <array>

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
} // namespace

PatternView::PatternView(PulsoAudioProcessor& owner) : processor(owner) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
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
    if (!plan || plan->sections.empty() || !area.contains(point)) return -1;
    const auto lane = std::clamp((point.y - area.getY()) * static_cast<int>(voiceDefinitions.size()) /
                                 std::max(1, area.getHeight()),
                                 0, static_cast<int>(voiceDefinitions.size()) - 1);
    return voiceTargetBase + lane;
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
    if (channel == sectionTarget) {
        const auto plan = processor.currentSongPlan();
        if (!plan || selectedSection < 0 || selectedSection >= static_cast<int>(plan->sections.size())) return {};
        const auto& section = plan->sections[static_cast<std::size_t>(selectedSection)];
        const auto start = section.startBar * plan->beatsPerBar;
        const auto end = (section.startBar + section.bars) * plan->beatsPerBar;
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
    return writePatternToMidiFile(exportPattern, file, options) ? file : juce::File{};
}

void PatternView::mouseDown(const juce::MouseEvent& event) {
    if (const auto section = sectionAt(event.getPosition()); section >= 0) {
        selectedSection = section;
        feedback = "SECTION " + juce::String(section + 1) + " SELECTED";
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
        feedback = "MIDI EXPORT FAILED";
        repaint();
        return;
    }

    dragInProgress = true;
    feedback = "DROP INTO ABLETON";
    repaint();
    const auto safeThis = juce::Component::SafePointer<PatternView>(this);
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        {file.getFullPathName()}, false, this, [safeThis] {
            if (safeThis == nullptr) return;
            safeThis->dragInProgress = false;
            safeThis->armedChannel = noTarget;
            safeThis->feedback = "MIDI READY";
            safeThis->repaint();
        });
    if (!started) {
        dragInProgress = false;
        feedback = "DRAG NOT AVAILABLE";
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
    if (next == hoverChannel) return;
    hoverChannel = next;
    repaint();
}

void PatternView::mouseExit(const juce::MouseEvent&) {
    hoverChannel = noTarget;
    repaint();
}

void PatternView::paint(juce::Graphics& graphics) {
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
                graphics.drawFittedText(juce::String::fromUTF8(section.name.c_str()).toUpperCase(),
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
    constexpr auto labelWidth = 126.0f;
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
        graphics.setColour((highlighted ? colourForFamily(voiceDefinition(voice).family) : colours::muted)
                               .withAlpha(activeInSelection ? 1.0f : 0.32f));
        graphics.setFont(juce::FontOptions(hasSongPlan ? 8.6f : 9.5f, juce::Font::bold));
        graphics.drawText(juce::String("::  ") + juce::String(voiceDefinition(voice).name.data()).toUpperCase(),
                          laneBounds.withWidth(labelWidth - 5.0f).toNearestInt(),
                          juce::Justification::centredLeft);
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
        graphics.drawFittedText("Press EVOLVE IDEA or play a chord", inner.toNearestInt(),
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

    constexpr std::array labels{"FULL SONG", "RHYTHM", "BASS", "HARMONY", "LEADS + FX", "SECTION"};
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
