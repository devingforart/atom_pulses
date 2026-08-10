#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>

namespace pulso::plugin {

class PreviewSynth final {
public:
    enum class DrumKit : std::uint8_t { Deep = 0, Organic, Analog, Cinematic, Count };

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setDrumKit(int kit) noexcept;
    void allNotesOff(int midiChannel, bool allowTailOff) noexcept;
    void renderNextBlock(juce::AudioBuffer<float>&, const juce::MidiBuffer&,
                         int startSample, int numSamples) noexcept;

private:
    static constexpr std::size_t drumVoiceCount = 24;
    static constexpr std::size_t tonalVoiceCount = 40;

    enum class VoiceKind : std::uint8_t {
        Tonal, Kick, Snare, Clap, ClosedHat, OpenHat, LowPercussion, HighPercussion, Cymbal
    };

    struct Voice {
        double phase{};
        double phaseDelta{};
        double secondaryPhase{};
        double ageSeconds{};
        float level{};
        float envelope{};
        float decaySeconds{0.2f};
        float tone{0.5f};
        float pan{};
        float previousNoise{};
        int note{-1};
        int channel{};
        bool active{};
        bool releasing{};
        VoiceKind kind{VoiceKind::Tonal};
        std::uint32_t noiseState{1};
        std::uint64_t age{};
    };

    void handleMessage(const juce::MidiMessage&) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void drumNoteOn(int note, float velocity) noexcept;
    void noteOff(int channel, int note, bool allowTailOff) noexcept;
    void renderVoices(juce::AudioBuffer<float>&, int startSample, int numSamples) noexcept;
    [[nodiscard]] Voice* selectVoice(bool drum) noexcept;
    [[nodiscard]] float renderDrumSample(Voice&) noexcept;

    std::array<Voice, drumVoiceCount> drumVoices{};
    std::array<Voice, tonalVoiceCount> tonalVoices{};
    double sampleRate{44100.0};
    float attackDelta{1.0f};
    float releaseMultiplier{0.99f};
    DrumKit drumKit{DrumKit::Deep};
    std::uint64_t ageCounter{};
};

} // namespace pulso::plugin
