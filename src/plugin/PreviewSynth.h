#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace pulso::plugin {

class PreviewSynth final {
public:
    enum class SoundWorld : std::uint8_t {
        DeepProgressive = 0, OrganicMotion, AnalogWarmth, DubSpace,
        MinimalPulse, HypnoticNight, CinematicArc, DarkClub, Count
    };

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setSoundWorld(int world) noexcept;
    void allNotesOff(int midiChannel, bool allowTailOff) noexcept;
    void renderNextBlock(juce::AudioBuffer<float>&, const juce::MidiBuffer&,
                         int startSample, int numSamples) noexcept;

private:
    static constexpr std::size_t drumVoiceCount = 24;
    static constexpr std::size_t tonalVoiceCount = 48;

    enum class VoiceKind : std::uint8_t {
        SubBass, MovementBass, Foundation, Pulse, Upper, Lead, Counter,
        Atmosphere, Transition, Kick, Snare, Clap, ClosedHat, OpenHat,
        LowPercussion, HighPercussion, Cymbal
    };

    struct Voice {
        double phase{};
        double secondaryPhase{};
        double phaseDelta{};
        double ageSeconds{};
        float level{};
        float envelope{};
        float attackSeconds{0.003f};
        float decaySeconds{0.2f};
        float sustain{1.0f};
        float releaseSeconds{0.08f};
        float tone{0.5f};
        float pan{};
        float previousNoise{};
        float filterLeft{};
        float filterRight{};
        float filterAlpha{1.0f};
        int note{-1};
        int channel{};
        bool active{};
        bool releasing{};
        bool oneShot{};
        VoiceKind kind{VoiceKind::Lead};
        std::uint32_t noiseState{1};
        std::uint64_t age{};
    };

    struct WorldProfile {
        float brightness;
        float warmth;
        float space;
        float drive;
        float decay;
        float stereo;
        float drumWeight;
    };

    void handleMessage(const juce::MidiMessage&) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void drumNoteOn(int note, float velocity) noexcept;
    void noteOff(int channel, int note, bool allowTailOff) noexcept;
    void renderVoices(juce::AudioBuffer<float>&, int startSample, int numSamples) noexcept;
    void processEffects(juce::AudioBuffer<float>&, int startSample, int numSamples) noexcept;
    [[nodiscard]] Voice* selectVoice(bool drum) noexcept;
    [[nodiscard]] VoiceKind kindForChannel(int channel) const noexcept;
    [[nodiscard]] float renderVoiceSample(Voice&) noexcept;
    [[nodiscard]] float renderDrumSample(Voice&) noexcept;
    [[nodiscard]] float nextNoise(Voice&) noexcept;
    [[nodiscard]] const WorldProfile& profile() const noexcept;

    std::array<Voice, drumVoiceCount> drumVoices{};
    std::array<Voice, tonalVoiceCount> tonalVoices{};
    std::vector<float> delayLeft;
    std::vector<float> delayRight;
    std::vector<float> roomLeft;
    std::vector<float> roomRight;
    std::size_t delayWrite{};
    std::size_t roomWrite{};
    double sampleRate{44100.0};
    SoundWorld soundWorld{SoundWorld::DeepProgressive};
    std::uint64_t ageCounter{};
};

} // namespace pulso::plugin
