#include "TestSupport.h"

#include "plugin/PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

namespace {

class TestPlayHead final : public juce::AudioPlayHead {
public:
    double ppq{};
    double bpm{120.0};
    bool playing{};

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setPpqPosition(ppq);
        info.setBpm(bpm);
        info.setIsPlaying(playing);
        return info;
    }
};

bool containsNoteOn(const juce::MidiBuffer& midi) {
    return std::any_of(midi.begin(), midi.end(), [](const auto metadata) {
        return metadata.getMessage().isNoteOn();
    });
}

bool containsPanic(const juce::MidiBuffer& midi) {
    return std::any_of(midi.begin(), midi.end(), [](const auto metadata) {
        const auto message = metadata.getMessage();
        return message.isController() && message.getControllerNumber() == 123;
    });
}

float peakMagnitude(const juce::AudioBuffer<float>& audio) {
    auto peak = 0.0f;
    for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
        peak = std::max(peak, audio.getMagnitude(channel, 0, audio.getNumSamples()));
    return peak;
}

void advance(TestPlayHead& playHead, int samples, double sampleRate) {
    if (playHead.playing)
        playHead.ppq += static_cast<double>(samples) / sampleRate * playHead.bpm / 60.0;
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;

    pulso::plugin::PulsoAudioProcessor processor;
    TestPlayHead playHead;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    playHead.playing = true;
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(92)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(92)), 0);

    auto producedPattern = false;
    for (auto attempt = 0; attempt < 250 && !producedPattern; ++attempt) {
        processor.processBlock(audio, midi);
        producedPattern = containsNoteOn(midi);
        midi.clear();
        advance(playHead, blockSize, sampleRate);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(producedPattern, "The worker must publish a playable pattern without blocking audio");
    require(peakMagnitude(audio) <= 1.0f, "Preview output must remain below digital full scale");
    const auto composedPattern = processor.currentPattern();
    require(composedPattern != nullptr, "The processor must expose its composed phrase");
    for (const auto channel : {1, 2, 10})
        require(std::any_of(composedPattern->notes.begin(), composedPattern->notes.end(),
                            [=](const auto& note) { return note.channel == channel; }),
                "Default Ensemble mode must coordinate bass, melody and drums");

    playHead.playing = false;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsPanic(midi), "Stopping transport must emit an explicit MIDI panic");

    for (auto block = 0; block < 40; ++block) {
        midi.clear();
        processor.processBlock(audio, midi);
    }
    require(peakMagnitude(audio) < 0.0001f, "Preview voices must fully release after transport stops");

    playHead.ppq = 0.10;
    playHead.playing = true;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsNoteOn(midi), "Starting inside a sounding note must retrigger that note");
    require(peakMagnitude(audio) > 0.0001f, "Retriggered preview must produce audio immediately");

    advance(playHead, blockSize, sampleRate);
    midi.clear();
    processor.processBlock(audio, midi);
    playHead.ppq = 4.10;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsPanic(midi), "A transport seek must clean up active MIDI notes");
    require(containsNoteOn(midi), "A transport seek must recover notes overlapping the destination");

    processor.requestVariation();
    const auto originalDnaSeed = composedPattern->seed;
    const auto originalComposition = composedPattern->notes;
    auto variationPanic = false;
    for (auto attempt = 0; attempt < 100 && !variationPanic; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        variationPanic = containsPanic(midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(variationPanic, "Replacing a live pattern must clean up notes from the previous pattern");
    const auto variedPattern = processor.currentPattern();
    require(variedPattern != nullptr && variedPattern->seed == originalDnaSeed,
            "Evolve Idea must preserve the composition DNA seed");
    require(variedPattern->notes != originalComposition,
            "Evolve Idea must create a real transformation rather than a duplicate");

    processor.requestNewComposition();
    auto receivedNewDna = false;
    for (auto attempt = 0; attempt < 100 && !receivedNewDna; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        if (const auto candidate = processor.currentPattern())
            receivedNewDna = candidate->seed != originalDnaSeed;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(receivedNewDna, "New DNA must replace the persistent composition identity");

    auto* preview = processor.parameters.getParameter("preview");
    require(preview != nullptr, "Preview parameter must exist");
    preview->setValueNotifyingHost(0.0f);
    for (auto block = 0; block < 40; ++block) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
    }
    require(peakMagnitude(audio) < 0.0001f, "Disabling preview must release audio without a stuck voice");

    preview->setValueNotifyingHost(1.0f);
    auto longestCallback = std::chrono::microseconds::zero();
    for (auto block = 0; block < 2000; ++block) {
        playHead.playing = block % 97 != 0;
        if (block % 113 == 0) playHead.ppq = std::fmod(block * 0.137, 16.0);
        if (block % 31 == 0) processor.requestVariation();
        midi.clear();
        const auto started = std::chrono::steady_clock::now();
        processor.processBlock(audio, midi);
        longestCallback = std::max(longestCallback,
                                   std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - started));
        for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
            for (auto sample = 0; sample < audio.getNumSamples(); ++sample)
                require(std::isfinite(audio.getSample(channel, sample)),
                        "Stress transport must never produce NaN or infinity");
        require(peakMagnitude(audio) <= 1.0f, "Limiter must contain every stress-test block");
        advance(playHead, blockSize, sampleRate);
    }
    require(longestCallback < std::chrono::milliseconds(50),
            "The audio callback must not wait for asynchronous generation");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto tooltipCount = 0;
    for (auto index = 0; index < editor->getNumChildComponents(); ++index) {
        if (auto* tooltip = dynamic_cast<juce::TooltipClient*>(editor->getChildComponent(index))) {
            require(tooltip->getTooltip().isNotEmpty(),
                    "Every direct UX control must provide contextual help");
            ++tooltipCount;
        }
    }
    require(tooltipCount >= 36, "The complete visible interface must be covered by tooltips");
    editor.reset();

    juce::MemoryBlock savedState;
    processor.getStateInformation(savedState);
    pulso::plugin::PulsoAudioProcessor restored;
    restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    require(restored.currentCompositionSeed() == processor.currentCompositionSeed() &&
                restored.currentVariationIndex() == processor.currentVariationIndex(),
            "Composition DNA and lineage must survive a DAW project reload");

    processor.releaseResources();
    processor.setPlayHead(nullptr);
    std::cout << "[PASS] Processor transport, panic, recovery and preview ceiling"
              << " | stress_max_callback_us=" << longestCallback.count() << '\n';
    return 0;
}
