#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>

namespace pulso::plugin {

class PreviewSynth final {
public:
    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void allNotesOff(int midiChannel, bool allowTailOff) noexcept;
    void renderNextBlock(juce::AudioBuffer<float>&, const juce::MidiBuffer&,
                         int startSample, int numSamples) noexcept;

private:
    static constexpr std::size_t voiceCount = 32;

    struct Voice {
        double phase{};
        double phaseDelta{};
        float level{};
        float envelope{};
        int note{-1};
        int channel{};
        bool active{};
        bool releasing{};
        std::uint64_t age{};
    };

    void handleMessage(const juce::MidiMessage&) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note, bool allowTailOff) noexcept;
    void renderVoices(juce::AudioBuffer<float>&, int startSample, int numSamples) noexcept;

    std::array<Voice, voiceCount> voices{};
    double sampleRate{44100.0};
    float attackDelta{1.0f};
    float releaseMultiplier{0.99f};
    std::uint64_t ageCounter{};
};

} // namespace pulso::plugin
