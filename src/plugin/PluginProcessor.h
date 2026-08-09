#pragma once

#include "core/Generator.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace pulso::plugin {

class PulsoAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener {
public:
    PulsoAudioProcessor();
    ~PulsoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void requestVariation() noexcept { variationRequests.fetch_add(1, std::memory_order_relaxed); }
    [[nodiscard]] std::shared_ptr<const Pattern> currentPattern() const noexcept {
        return patternSnapshot.load(std::memory_order_acquire);
    }
    [[nodiscard]] double currentTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
    [[nodiscard]] bool hostIsPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct Transport {
        double startBeat{};
        double endBeat{};
        double beatsPerBar{4.0};
        double bpm{120.0};
        bool isPlaying{};
        bool hostAvailable{};
    };

    Transport readTransport(int numSamples);
    GenerationContext buildContext(double beatsPerBar, std::uint64_t seed) const;
    void collectInput(const juce::MidiBuffer&, double blockStartBeat, double beatsPerBar);
    void regenerate(double beatsPerBar);
    void schedulePattern(const Pattern&, const Transport&, int numSamples, juce::MidiBuffer&) const;
    void parameterChanged(const juce::String&, float) override;

    Generator generator;
    juce::Synthesiser previewSynth;
    std::array<bool, 128> heldNotes{};
    std::vector<SourceNote> recentSourceNotes;
    std::atomic<std::shared_ptr<const Pattern>> patternSnapshot;
    std::atomic<std::uint64_t> variationRequests{1};
    std::uint64_t handledVariationRequests{};
    std::int64_t generatedBar{std::numeric_limits<std::int64_t>::min()};
    double standaloneBeat{};
    double currentSampleRate{44100.0};
    std::atomic<double> tempo{120.0};
    std::atomic<bool> playing{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessor)
};

} // namespace pulso::plugin
