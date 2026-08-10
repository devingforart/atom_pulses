#include "CompositionProgress.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {

CompositionProgress::CompositionProgress() {
    setInterceptsMouseClicks(true, true);
    setVisible(false);
    setTooltip("PULSO is composing in the background. The current idea remains available until the new one is ready.");
    cancelButton.setTooltip("Stop the current request immediately and keep the composition already playing.");
    cancelButton.onClick = [this] {
        cancelButton.setEnabled(false);
        cancelButton.setButtonText("CANCELLING...");
        if (onCancel) onCancel();
    };
    addAndMakeVisible(cancelButton);
}

void CompositionProgress::resized() {
    auto card = getLocalBounds().toFloat().withSizeKeepingCentre(
        juce::jmin(440.0f, static_cast<float>(getWidth()) - 32.0f), 144.0f).toNearestInt();
    cancelButton.setBounds(card.removeFromBottom(34).removeFromRight(118).reduced(4, 2));
}

void CompositionProgress::setComposing(bool shouldBeActive, bool isUsingAi,
                                       const juce::String& nextStage, float nextProgress) {
    usingAi = isUsingAi;
    stage = nextStage;
    progress = std::clamp(nextProgress, 0.0f, 1.0f);
    if (active == shouldBeActive) {
        if (active) repaint();
        return;
    }

    active = shouldBeActive;
    if (active) {
        phase = 0.0f;
        startedAtMs = juce::Time::getMillisecondCounterHiRes();
        cancelButton.setEnabled(true);
        cancelButton.setButtonText("CANCEL");
        setVisible(true);
        toFront(false);
        startTimerHz(30);
    } else {
        stopTimer();
        setVisible(false);
    }
    repaint();
}

void CompositionProgress::timerCallback() {
    phase = std::fmod(phase + 0.055f, 1.0f);
    repaint();
}

void CompositionProgress::paint(juce::Graphics& graphics) {
    if (!active) return;

    graphics.fillAll(colours::background.withAlpha(0.78f));

    auto card = getLocalBounds().toFloat().withSizeKeepingCentre(
        juce::jmin(440.0f, static_cast<float>(getWidth()) - 32.0f), 144.0f);
    graphics.setColour(colours::panelRaised.withAlpha(0.98f));
    graphics.fillRoundedRectangle(card, 14.0f);
    graphics.setColour(colours::accent.withAlpha(0.34f));
    graphics.drawRoundedRectangle(card, 14.0f, 1.0f);

    const auto spinnerBounds = card.removeFromLeft(82.0f);
    const auto centre = spinnerBounds.getCentre();
    constexpr auto dotCount = 10;
    constexpr auto radius = 22.0f;
    for (auto index = 0; index < dotCount; ++index) {
        const auto dotProgress = std::fmod(phase + static_cast<float>(index) / dotCount, 1.0f);
        const auto angle = dotProgress * juce::MathConstants<float>::twoPi;
        const auto dotCentre = centre.getPointOnCircumference(radius, angle);
        graphics.setColour(colours::accent.withAlpha(0.14f + 0.86f * dotProgress));
        const auto diameter = 4.0f + 3.0f * dotProgress;
        graphics.fillEllipse(juce::Rectangle<float>(diameter, diameter).withCentre(dotCentre));
    }

    auto copy = card.reduced(4.0f, 14.0f);
    copy.removeFromBottom(30.0f);
    const auto elapsedSeconds = static_cast<int>(
        (juce::Time::getMillisecondCounterHiRes() - startedAtMs) / 1000.0);

    graphics.setColour(colours::text);
    graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    graphics.drawText(usingAi ? "GPT IS COMPOSING YOUR IDEA" : "COMPOSING YOUR IDEA",
                      copy.removeFromTop(26.0f), juce::Justification::centredLeft, false);

    graphics.setColour(colours::muted);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.drawText("The current composition keeps playing while the new one is prepared.",
                      copy.removeFromTop(22.0f), juce::Justification::centredLeft, false);

    graphics.setColour(colours::accent);
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    const auto timing = stage.isNotEmpty() ? stage + "  ·  " + juce::String(elapsedSeconds) + " s"
        : elapsedSeconds < 1 ? juce::String("DIRECTING HARMONY, MELODY, BASS AND RHYTHM")
                             : juce::String("WORKING  ·  ") + juce::String(elapsedSeconds) + " s";
    graphics.drawText(timing, copy, juce::Justification::centredLeft, false);

    if (progress > 0.0f) {
        auto progressBounds = juce::Rectangle<float>(card.getX() + 4.0f, card.getBottom() - 9.0f,
                                                      card.getWidth() - 8.0f, 3.0f);
        graphics.setColour(colours::background);
        graphics.fillRoundedRectangle(progressBounds, 1.5f);
        progressBounds.setWidth(progressBounds.getWidth() * progress);
        graphics.setColour(colours::accent);
        graphics.fillRoundedRectangle(progressBounds, 1.5f);
    }
}

} // namespace pulso::plugin
