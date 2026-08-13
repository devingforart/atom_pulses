#pragma once

#include "core/MusicTypes.h"

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

    enum class DrumKit : std::uint8_t { TR808 = 0, TR909, ModernClub, Organic, Count };
    enum class BassTone : std::uint8_t { DeepSub = 0, WarmAnalog, RollingReese, AcidPluck, Count };
    enum class HarmonyTone : std::uint8_t { DeepPad = 0, WarmPoly, HouseOrgan, Glass, Count };
    enum class MelodyTone : std::uint8_t { WarmMono = 0, SoftPluck, Air, Bell, Count };

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setSoundWorld(int world) noexcept;
    void setDrumKit(int kit) noexcept;
    void setBassTone(int tone) noexcept;
    void setHarmonyTone(int tone) noexcept;
    void setMelodyTone(int tone) noexcept;
    void setVoiceTimbre(VoiceId voice, int selection) noexcept;
    void setVoiceTranspose(VoiceId voice, int semitones) noexcept;
    void setVoiceLevelDb(VoiceId voice, float decibels) noexcept;
    void allNotesOff(int midiChannel, bool allowTailOff) noexcept;
    void renderNextBlock(juce::AudioBuffer<float>&, const juce::MidiBuffer&,
                         int startSample, int numSamples) noexcept;

private:
    static constexpr std::size_t drumVoiceCount = 24;
    // The preview is a sketch instrument, while exported MIDI retains the full score.
    // A 24-voice ceiling keeps dense orchestration inside a 256-sample host deadline.
    static constexpr std::size_t tonalVoiceCount = 24;

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
        float variant{};
        float pan{};
        float previousNoise{};
        float filterLeft{};
        float filterRight{};
        float filterAlpha{1.0f};
        float expressionGain{1.0f};
        float expressiveBrightness{0.5f};
        float expressivePressure{};
        int note{-1};
        int sourceNote{-1};
        int channel{};
        bool active{};
        bool releasing{};
        bool heldByPedal{};
        bool oneShot{};
        VoiceKind kind{VoiceKind::Lead};
        InstrumentSoundModel soundModel{InstrumentSoundModel::Generic};
        std::uint32_t noiseState{1};
        std::uint64_t age{};
        float outputGain{1.0f};
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
    [[nodiscard]] VoiceId voiceForKind(VoiceKind) const noexcept;
    [[nodiscard]] DrumKit effectiveDrumKit(VoiceKind) const noexcept;
    [[nodiscard]] BassTone effectiveBassTone(VoiceKind) const noexcept;
    [[nodiscard]] HarmonyTone effectiveHarmonyTone(VoiceKind) const noexcept;
    [[nodiscard]] MelodyTone effectiveMelodyTone(VoiceKind) const noexcept;
    [[nodiscard]] int voiceTranspose(VoiceKind) const noexcept;
    [[nodiscard]] float voiceLevelGain(VoiceKind) const noexcept;

    std::array<Voice, drumVoiceCount> drumVoices{};
    std::array<Voice, tonalVoiceCount> tonalVoices{};
    std::vector<float> delayLeft;
    std::vector<float> delayRight;
    std::vector<float> roomLeft;
    std::vector<float> roomRight;
    std::size_t delayWrite{};
    std::size_t roomWrite{};
    std::array<float, 16> channelExpression{};
    std::array<float, 16> channelModulation{};
    std::array<float, 16> channelBrightness{};
    std::array<float, 16> channelPitchBend{};
    std::array<float, 16> channelPressure{};
    std::array<bool, 16> channelSustain{};
    std::array<InstrumentSoundModel, 16> channelInstrument{};
    std::array<std::array<float, 128>, 16> polyAftertouch{};
    double sampleRate{44100.0};
    SoundWorld soundWorld{SoundWorld::DeepProgressive};
    DrumKit drumKit{DrumKit::TR909};
    BassTone bassTone{BassTone::WarmAnalog};
    HarmonyTone harmonyTone{HarmonyTone::DeepPad};
    MelodyTone melodyTone{MelodyTone::WarmMono};
    std::array<int, static_cast<std::size_t>(VoiceId::Count)> voiceTimbres{};
    std::array<int, static_cast<std::size_t>(VoiceId::Count)> voiceTransposes{};
    std::array<float, static_cast<std::size_t>(VoiceId::Count)> voiceLevelGains{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::uint64_t ageCounter{};
};

} // namespace pulso::plugin
