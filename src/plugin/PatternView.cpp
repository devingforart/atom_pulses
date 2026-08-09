#include "PatternView.h"

#include "LookAndFeel.h"

#include <algorithm>

namespace pulso::plugin {

void PatternView::paint(juce::Graphics& graphics) {
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(colours::panel);
    graphics.fillRoundedRectangle(bounds, 12.0f);
    const auto inner = bounds.reduced(14.0f);

    graphics.setColour(colours::panelRaised);
    for (int index = 0; index <= 16; ++index) {
        const auto x = inner.getX() + inner.getWidth() * static_cast<float>(index) / 16.0f;
        graphics.drawVerticalLine(juce::roundToInt(x), inner.getY(), inner.getBottom());
    }

    const auto pattern = processor.currentPattern();
    if (!pattern || pattern->notes.empty()) {
        graphics.setColour(colours::muted);
        graphics.setFont(15.0f);
        graphics.drawFittedText("Press NEW VARIATION or play a chord", inner.toNearestInt(),
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
        graphics.setColour((note.channel == 10 ? colours::accentHot : colours::accent).withAlpha(alpha));
        graphics.fillRoundedRectangle(x, y, width, 7.0f, 3.0f);
    }
}

} // namespace pulso::plugin

