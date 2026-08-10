#include "PatternView.h"

#include "LookAndFeel.h"
#include "MidiExporter.h"

#include <algorithm>
#include <array>

namespace pulso::plugin {

PatternView::PatternView(PulsoAudioProcessor& owner) : processor(owner) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

juce::Rectangle<int> PatternView::dragStripBounds() const noexcept {
    return getLocalBounds().reduced(14).removeFromBottom(34);
}

int PatternView::channelAt(juce::Point<int> point) const noexcept {
    const auto strip = dragStripBounds();
    if (!strip.contains(point)) return -1;
    constexpr std::array channels{0, 3, 2, 1, 10};
    const auto index = std::clamp((point.x - strip.getX()) * 5 / std::max(1, strip.getWidth()), 0, 4);
    return channels[static_cast<std::size_t>(index)];
}

bool PatternView::hasNotesForChannel(int channel) const {
    const auto pattern = processor.currentPattern();
    if (!pattern) return false;
    return std::any_of(pattern->notes.begin(), pattern->notes.end(), [channel](const auto& note) {
        return channel == 0 || note.channel == channel;
    });
}

juce::File PatternView::createExportFile(int channel) const {
    const auto pattern = processor.currentPattern();
    if (!pattern || pattern->notes.empty()) return {};
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("PULSO MIDI Exports");
    const auto role = channel == 3 ? "Harmony" : channel == 1 ? "Bass" :
                      channel == 2 ? "Melody" : channel == 10 ? "Drums" : "Ensemble";
    const auto stem = "PULSO_DNA_" + juce::String(processor.currentCompositionSeed()) + "_" +
                      juce::String(processor.currentVariationIndex()) + "_" + role;
    const auto file = folder.getNonexistentChildFile(stem, ".mid", false);
    MidiExportOptions options;
    options.bpm = processor.currentTempo();
    options.timeSignatureNumerator = processor.currentTimeSignatureNumerator();
    options.timeSignatureDenominator = processor.currentTimeSignatureDenominator();
    options.channelFilter = channel;
    options.clipName = stem;
    return writePatternToMidiFile(*pattern, file, options) ? file : juce::File{};
}

void PatternView::mouseDown(const juce::MouseEvent& event) {
    armedChannel = channelAt(event.getPosition());
    dragAttempted = false;
    if (armedChannel >= 0 && !hasNotesForChannel(armedChannel)) armedChannel = -1;
    repaint();
}

void PatternView::mouseDrag(const juce::MouseEvent& event) {
    if (armedChannel < 0 || dragAttempted || dragInProgress ||
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
            safeThis->armedChannel = -1;
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
    if (!dragInProgress) armedChannel = -1;
    repaint();
}

void PatternView::mouseMove(const juce::MouseEvent& event) {
    const auto next = channelAt(event.getPosition());
    if (next == hoverChannel) return;
    hoverChannel = next;
    repaint();
}

void PatternView::mouseExit(const juce::MouseEvent&) {
    hoverChannel = -1;
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
    const auto bars = pattern && pattern->lengthBeats > 0.0
                        ? std::max(1, static_cast<int>(std::lround(pattern->lengthBeats / 4.0)))
                        : processor.currentPhraseBars();
    const auto beats = bars * 4;
    constexpr std::array laneNames{"HARMONY", "MELODY", "BASS", "DRUMS"};
    constexpr std::array channels{3, 2, 1, 10};
    constexpr auto labelWidth = 76.0f;
    auto timeline = inner;
    timeline.removeFromLeft(labelWidth);
    const auto laneHeight = inner.getHeight() / 4.0f;
    for (auto lane = 0; lane < 4; ++lane) {
        const auto laneBounds = juce::Rectangle<float>{inner.getX(), inner.getY() + lane * laneHeight,
                                                       inner.getWidth(), laneHeight};
        if (lane % 2 == 1) {
            graphics.setColour(colours::panelRaised.withAlpha(0.28f));
            graphics.fillRect(laneBounds);
        }
        graphics.setColour(colours::muted);
        graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        graphics.drawText(laneNames[static_cast<std::size_t>(lane)],
                          laneBounds.withWidth(labelWidth - 5.0f).toNearestInt(),
                          juce::Justification::centredLeft);
        graphics.setColour(colours::panelRaised);
        graphics.drawHorizontalLine(juce::roundToInt(laneBounds.getBottom()),
                                    timeline.getX(), timeline.getRight());
    }
    for (int index = 0; index <= beats; ++index) {
        const auto isBar = index % 4 == 0;
        if (!isBar && bars > 8) continue;
        const auto x = timeline.getX() + timeline.getWidth() * static_cast<float>(index) /
                                          static_cast<float>(beats);
        graphics.setColour(isBar ? colours::muted.withAlpha(0.42f) :
                                   colours::panelRaised.withAlpha(0.70f));
        graphics.drawVerticalLine(juce::roundToInt(x), timeline.getY(), timeline.getBottom());
        if (isBar && index < beats) {
            graphics.setColour(colours::muted.withAlpha(0.65f));
            graphics.setFont(10.0f);
            graphics.drawText(juce::String(index / 4 + 1), juce::roundToInt(x) + 4,
                              juce::roundToInt(timeline.getY()), 22, 13, juce::Justification::left);
        }
    }

    if (!pattern || pattern->notes.empty()) {
        graphics.setColour(colours::muted);
        graphics.setFont(15.0f);
        graphics.drawFittedText("Press EVOLVE IDEA or play a chord", inner.toNearestInt(),
                                juce::Justification::centred, 1);
    } else {
        for (auto lane = 0; lane < 4; ++lane) {
            auto minPitch = 127;
            auto maxPitch = 0;
            for (const auto& note : pattern->notes) {
                if (note.channel != channels[static_cast<std::size_t>(lane)]) continue;
                minPitch = std::min(minPitch, note.pitch);
                maxPitch = std::max(maxPitch, note.pitch);
            }
            const auto pitchSpan = std::max(1, maxPitch - minPitch + 1);
            for (const auto& note : pattern->notes) {
                if (note.channel != channels[static_cast<std::size_t>(lane)]) continue;
                const auto x = timeline.getX() + static_cast<float>(note.startBeat / pattern->lengthBeats) * timeline.getWidth();
                const auto width = std::max(3.0f, static_cast<float>(note.durationBeats / pattern->lengthBeats) * timeline.getWidth());
                const auto normalizedPitch = static_cast<float>(note.pitch - minPitch) / static_cast<float>(pitchSpan);
                const auto y = inner.getY() + lane * laneHeight + laneHeight - 8.0f -
                               normalizedPitch * std::max(3.0f, laneHeight - 16.0f);
                const auto colour = note.channel == 10 ? colours::accentHot :
                                    note.channel == 2 ? colours::accentCounter :
                                    note.channel == 3 ? juce::Colour::fromRGB(199, 143, 255) : colours::accent;
                graphics.setColour(colour.withAlpha(juce::jmap(static_cast<float>(note.velocity),
                                                               1.0f, 127.0f, 0.48f, 1.0f)));
                graphics.fillRoundedRectangle(x, y, width, 6.0f, 2.5f);
            }
        }
    }

    constexpr std::array labels{"ALL MIDI", "HARMONY", "MELODY", "BASS", "DRUMS"};
    constexpr std::array exportChannels{0, 3, 2, 1, 10};
    for (auto index = 0; index < 5; ++index) {
        auto cell = dragStrip.toNearestInt();
        const auto cellWidth = cell.getWidth() / 5;
        cell.setX(cell.getX() + index * cellWidth);
        cell.setWidth(index == 4 ? dragStrip.toNearestInt().getRight() - cell.getX() : cellWidth);
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
