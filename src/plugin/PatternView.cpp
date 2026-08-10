#include "PatternView.h"

#include "LookAndFeel.h"

#include <algorithm>
#include <array>

namespace pulso::plugin {

void PatternView::paint(juce::Graphics& graphics) {
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(colours::panel);
    graphics.fillRoundedRectangle(bounds, 12.0f);
    const auto inner = bounds.reduced(14.0f);

    const auto bars = processor.currentPhraseBars();
    const auto beats = bars * 4;
    constexpr std::array sectionNames{"STATEMENT", "ANSWER", "DEVELOP", "DEVELOP"};
    for (auto bar = 0; bar < bars; ++bar) {
        const auto x = inner.getX() + inner.getWidth() * static_cast<float>(bar) /
                                          static_cast<float>(bars);
        const auto width = inner.getWidth() / static_cast<float>(bars);
        if (bar % 2 == 1) {
            graphics.setColour(colours::panelRaised.withAlpha(0.20f));
            graphics.fillRect(juce::Rectangle<float>{x, inner.getY(), width, inner.getHeight()});
        }
        const auto label = bar == bars - 1 ? "CADENCE" : sectionNames[static_cast<std::size_t>(bar % 4)];
        if (bars <= 8 || bar % 4 == 0 || bar == bars - 1) {
            graphics.setColour(colours::muted.withAlpha(0.50f));
            graphics.setFont(8.5f);
            graphics.drawFittedText(label, juce::Rectangle<int>{juce::roundToInt(x) + 4,
                                        juce::roundToInt(inner.getY()) + 13,
                                        std::max(20, juce::roundToInt(width) - 7), 11},
                                    juce::Justification::left, 1);
        }
    }
    for (int index = 0; index <= beats; ++index) {
        const auto isBar = index % 4 == 0;
        if (!isBar && bars > 8) continue;
        const auto x = inner.getX() + inner.getWidth() * static_cast<float>(index) /
                                          static_cast<float>(beats);
        graphics.setColour(isBar ? colours::muted.withAlpha(0.42f) :
                                   colours::panelRaised.withAlpha(0.70f));
        graphics.drawVerticalLine(juce::roundToInt(x), inner.getY(), inner.getBottom());
        if (isBar && index < beats) {
            graphics.setColour(colours::muted.withAlpha(0.65f));
            graphics.setFont(10.0f);
            graphics.drawText(juce::String(index / 4 + 1), juce::roundToInt(x) + 4,
                              juce::roundToInt(inner.getY()), 22, 13, juce::Justification::left);
        }
    }

    const auto pattern = processor.currentPattern();
    if (!pattern || pattern->notes.empty()) {
        graphics.setColour(colours::muted);
        graphics.setFont(15.0f);
        graphics.drawFittedText("Press EVOLVE IDEA or play a chord", inner.toNearestInt(),
                                juce::Justification::centred, 1);
        return;
    }

    auto minPitch = 127;
    auto maxPitch = 0;
    for (const auto& note : pattern->notes) {
        minPitch = std::min(minPitch, note.pitch);
        maxPitch = std::max(maxPitch, note.pitch);
    }
    const auto pitchSpan = std::max(1, maxPitch - minPitch + 1);
    for (const auto& note : pattern->notes) {
        const auto x = inner.getX() + static_cast<float>(note.startBeat / pattern->lengthBeats) * inner.getWidth();
        const auto width = std::max(3.0f, static_cast<float>(note.durationBeats / pattern->lengthBeats) * inner.getWidth());
        const auto normalizedPitch = static_cast<float>(note.pitch - minPitch) / static_cast<float>(pitchSpan);
        const auto y = inner.getBottom() - 8.0f - normalizedPitch * (inner.getHeight() - 12.0f);
        const auto alpha = juce::jmap(static_cast<float>(note.velocity), 1.0f, 127.0f, 0.45f, 1.0f);
        const auto colour = note.channel == 10 ? colours::accentHot :
                            (note.channel == 2 ? colours::accentCounter : colours::accent);
        graphics.setColour(colour.withAlpha(alpha));
        graphics.fillRoundedRectangle(x, y, width, 7.0f, 3.0f);
    }
}

} // namespace pulso::plugin
