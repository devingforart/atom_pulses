#include "PreviewSynth.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {
namespace {

constexpr std::array<PreviewSynth::SoundWorld, 8> allWorlds{
    PreviewSynth::SoundWorld::DeepProgressive, PreviewSynth::SoundWorld::OrganicMotion,
    PreviewSynth::SoundWorld::AnalogWarmth, PreviewSynth::SoundWorld::DubSpace,
    PreviewSynth::SoundWorld::MinimalPulse, PreviewSynth::SoundWorld::HypnoticNight,
    PreviewSynth::SoundWorld::CinematicArc, PreviewSynth::SoundWorld::DarkClub
};

float sine(double phase) noexcept { return static_cast<float>(std::sin(phase)); }
float triangle(double phase) noexcept {
    return static_cast<float>(2.0 / juce::MathConstants<double>::pi * std::asin(std::sin(phase)));
}
float polyBlep(double phase, double phaseDelta) noexcept {
    auto t = phase / juce::MathConstants<double>::twoPi;
    t -= std::floor(t);
    const auto dt = std::clamp(std::abs(phaseDelta) / juce::MathConstants<double>::twoPi, 1.0e-7, 0.49);
    if (t < dt) {
        const auto x = t / dt;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt) {
        const auto x = (t - 1.0) / dt;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

float bandlimitedSaw(double phase, double phaseDelta) noexcept {
    auto t = phase / juce::MathConstants<double>::twoPi;
    t -= std::floor(t);
    return static_cast<float>(2.0 * t - 1.0) - polyBlep(phase, phaseDelta);
}

float bandlimitedSquare(double phase, double phaseDelta) noexcept {
    auto value = sine(phase) >= 0.0f ? 1.0f : -1.0f;
    value += polyBlep(phase, phaseDelta);
    value -= polyBlep(phase + juce::MathConstants<double>::pi, phaseDelta);
    return value;
}

} // namespace

const PreviewSynth::WorldProfile& PreviewSynth::profile() const noexcept {
    static constexpr std::array profiles{
        WorldProfile{0.48f, 0.76f, 0.46f, 0.28f, 1.05f, 0.56f, 1.00f}, // Deep Progressive
        WorldProfile{0.62f, 0.88f, 0.36f, 0.12f, 0.82f, 0.72f, 0.82f}, // Organic Motion
        WorldProfile{0.55f, 0.94f, 0.28f, 0.46f, 0.92f, 0.48f, 0.94f}, // Analog Warmth
        WorldProfile{0.34f, 0.82f, 0.88f, 0.22f, 1.28f, 0.84f, 0.90f}, // Dub Space
        WorldProfile{0.72f, 0.52f, 0.18f, 0.18f, 0.58f, 0.38f, 0.78f}, // Minimal Pulse
        WorldProfile{0.42f, 0.68f, 0.62f, 0.32f, 1.16f, 0.76f, 0.96f}, // Hypnotic Night
        WorldProfile{0.58f, 0.86f, 0.78f, 0.16f, 1.42f, 0.92f, 1.08f}, // Cinematic Arc
        WorldProfile{0.70f, 0.50f, 0.32f, 0.58f, 0.72f, 0.52f, 1.18f}  // Dark Club
    };
    return profiles[std::clamp(static_cast<std::size_t>(soundWorld), std::size_t{}, profiles.size() - 1)];
}

void PreviewSynth::prepare(double newSampleRate) noexcept {
    sampleRate = std::max(1.0, newSampleRate);
    delayLeft.assign(static_cast<std::size_t>(sampleRate * 2.0) + 1, 0.0f);
    delayRight.assign(delayLeft.size(), 0.0f);
    roomLeft.assign(static_cast<std::size_t>(sampleRate * 0.9) + 1, 0.0f);
    roomRight.assign(roomLeft.size(), 0.0f);
    reset();
}

void PreviewSynth::reset() noexcept {
    drumVoices = {};
    tonalVoices = {};
    std::fill(delayLeft.begin(), delayLeft.end(), 0.0f);
    std::fill(delayRight.begin(), delayRight.end(), 0.0f);
    std::fill(roomLeft.begin(), roomLeft.end(), 0.0f);
    std::fill(roomRight.begin(), roomRight.end(), 0.0f);
    delayWrite = 0;
    roomWrite = 0;
    channelExpression.fill(1.0f);
    channelModulation.fill(0.0f);
    channelBrightness.fill(0.5f);
    ageCounter = 0;
}

void PreviewSynth::setSoundWorld(int world) noexcept {
    soundWorld = allWorlds[std::clamp(world, 0, static_cast<int>(allWorlds.size()) - 1)];
}

void PreviewSynth::allNotesOff(int midiChannel, bool allowTailOff) noexcept {
    const auto release = [midiChannel, allowTailOff](auto& voices) {
        for (auto& voice : voices) {
            if (!voice.active || (midiChannel > 0 && voice.channel != midiChannel)) continue;
            if (allowTailOff) {
                voice.releasing = true;
                voice.releaseSeconds = std::min(voice.releaseSeconds, 0.18f);
            }
            else voice = {};
        }
    };
    release(drumVoices);
    release(tonalVoices);
}

PreviewSynth::Voice* PreviewSynth::selectVoice(bool drum) noexcept {
    const auto select = [](auto& voices) -> Voice* {
        for (auto& voice : voices)
            if (!voice.active) return &voice;
        return &*std::min_element(voices.begin(), voices.end(), [](const auto& left, const auto& right) {
            const auto leftEnergy = left.level * left.envelope;
            const auto rightEnergy = right.level * right.envelope;
            return leftEnergy == rightEnergy ? left.age < right.age : leftEnergy < rightEnergy;
        });
    };
    return drum ? select(drumVoices) : select(tonalVoices);
}

PreviewSynth::VoiceKind PreviewSynth::kindForChannel(int channel) const noexcept {
    switch (channel) {
        case 1: return VoiceKind::SubBass;
        case 2: return VoiceKind::Lead;
        case 3: return VoiceKind::Foundation;
        case 4: return VoiceKind::Pulse;
        case 5: return VoiceKind::Upper;
        case 6: return VoiceKind::MovementBass;
        case 7: return VoiceKind::Counter;
        case 8: return VoiceKind::Atmosphere;
        case 9: return VoiceKind::Transition;
        default: return VoiceKind::Lead;
    }
}

void PreviewSynth::noteOn(int channel, int note, float velocity) noexcept {
    if (channel == 10) {
        drumNoteOn(note, velocity);
        return;
    }

    auto* voice = selectVoice(false);
    *voice = {};
    voice->active = true;
    voice->channel = std::clamp(channel, 1, 16);
    voice->note = std::clamp(note, 0, 127);
    voice->level = std::clamp(velocity, 0.0f, 1.0f) * 0.095f;
    voice->age = ++ageCounter;
    voice->kind = kindForChannel(voice->channel);
    voice->noiseState = 0x85ebca6bu ^ static_cast<std::uint32_t>(voice->note * 2246822519u) ^
                        static_cast<std::uint32_t>(voice->age);
    const auto& world = profile();
    voice->pan = std::clamp((static_cast<float>(voice->channel) - 5.0f) * 0.09f * world.stereo,
                            -0.58f, 0.58f);
    auto cutoff = 1800.0f + world.brightness * 5600.0f;

    switch (voice->kind) {
        case VoiceKind::SubBass:
            voice->attackSeconds = 0.008f; voice->decaySeconds = 0.18f; voice->sustain = 0.92f;
            voice->releaseSeconds = 0.12f; voice->level *= 1.32f; cutoff = 120.0f + world.brightness * 260.0f; break;
        case VoiceKind::MovementBass:
            voice->attackSeconds = 0.004f; voice->decaySeconds = 0.24f; voice->sustain = 0.52f;
            voice->releaseSeconds = 0.10f; voice->level *= 1.12f; cutoff = 520.0f + world.brightness * 1500.0f; break;
        case VoiceKind::Foundation:
            voice->attackSeconds = 0.10f + world.space * 0.24f; voice->decaySeconds = 0.55f;
            voice->sustain = 0.78f; voice->releaseSeconds = 0.48f + world.space * 0.72f;
            voice->level *= 0.78f; cutoff = 700.0f + world.brightness * 2100.0f; break;
        case VoiceKind::Pulse:
            voice->attackSeconds = 0.002f; voice->decaySeconds = 0.16f + world.decay * 0.18f;
            voice->sustain = 0.20f; voice->releaseSeconds = 0.12f; cutoff = 1300.0f + world.brightness * 4200.0f; break;
        case VoiceKind::Upper:
            voice->attackSeconds = 0.006f; voice->decaySeconds = 0.62f;
            voice->sustain = 0.28f; voice->releaseSeconds = 0.55f; voice->level *= 0.72f;
            cutoff = 2600.0f + world.brightness * 6800.0f; break;
        case VoiceKind::Lead:
            voice->attackSeconds = 0.018f; voice->decaySeconds = 0.30f;
            voice->sustain = 0.68f; voice->releaseSeconds = 0.26f; cutoff = 1500.0f + world.brightness * 4800.0f; break;
        case VoiceKind::Counter:
            voice->attackSeconds = 0.035f; voice->decaySeconds = 0.42f;
            voice->sustain = 0.58f; voice->releaseSeconds = 0.38f; voice->level *= 0.76f;
            cutoff = 1100.0f + world.brightness * 3600.0f; break;
        case VoiceKind::Atmosphere:
            voice->attackSeconds = 0.34f + world.space * 0.55f; voice->decaySeconds = 0.75f;
            voice->sustain = 0.72f; voice->releaseSeconds = 1.1f + world.space;
            voice->level *= 0.48f; cutoff = 420.0f + world.brightness * 1900.0f; break;
        case VoiceKind::Transition:
            voice->attackSeconds = 0.08f; voice->decaySeconds = 0.65f;
            voice->sustain = 0.42f; voice->releaseSeconds = 0.85f; voice->level *= 0.58f;
            cutoff = 900.0f + world.brightness * 6200.0f; break;
        default: break;
    }

    switch (soundWorld) {
        case SoundWorld::OrganicMotion:
            voice->attackSeconds *= 0.78f; voice->releaseSeconds *= 0.72f; cutoff *= 0.82f; break;
        case SoundWorld::AnalogWarmth:
            cutoff *= 0.88f; voice->decaySeconds *= 1.12f; break;
        case SoundWorld::DubSpace:
            cutoff *= 0.58f; voice->releaseSeconds *= 1.65f; voice->sustain *= 0.90f; break;
        case SoundWorld::MinimalPulse:
            voice->attackSeconds *= 0.45f; voice->decaySeconds *= 0.52f;
            voice->releaseSeconds *= 0.46f; voice->sustain *= 0.72f; cutoff *= 1.22f; break;
        case SoundWorld::HypnoticNight:
            cutoff *= 0.72f; voice->releaseSeconds *= 1.28f; break;
        case SoundWorld::CinematicArc:
            voice->attackSeconds *= 1.55f; voice->releaseSeconds *= 1.85f;
            voice->decaySeconds *= 1.40f; cutoff *= 0.76f; break;
        case SoundWorld::DarkClub:
            voice->attackSeconds *= 0.56f; voice->releaseSeconds *= 0.62f; cutoff *= 1.32f; break;
        case SoundWorld::DeepProgressive: break;
        default: break;
    }

    const auto frequency = juce::MidiMessage::getMidiNoteInHertz(voice->note);
    voice->phaseDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    voice->filterAlpha = std::clamp(1.0f - std::exp(-juce::MathConstants<float>::twoPi * cutoff /
                                                    static_cast<float>(sampleRate)), 0.001f, 1.0f);
}

void PreviewSynth::drumNoteOn(int note, float velocity) noexcept {
    auto* voice = selectVoice(true);
    *voice = {};
    voice->active = true;
    voice->oneShot = true;
    voice->channel = 10;
    voice->note = std::clamp(note, 0, 127);
    voice->level = std::clamp(velocity, 0.0f, 1.0f) * profile().drumWeight;
    voice->envelope = 1.0f;
    voice->age = ++ageCounter;
    voice->noiseState = 0x9e3779b9u ^ (static_cast<std::uint32_t>(note) * 2654435761u) ^
                        static_cast<std::uint32_t>(voice->age);

    if (note == 35 || note == 36) voice->kind = VoiceKind::Kick;
    else if (note == 38 || note == 40) voice->kind = VoiceKind::Snare;
    else if (note == 37 || note == 39) voice->kind = VoiceKind::Clap;
    else if (note == 42 || note == 44) voice->kind = VoiceKind::ClosedHat;
    else if (note == 46) voice->kind = VoiceKind::OpenHat;
    else if (note == 49 || note == 51 || note == 52 || note == 55 || note == 57) voice->kind = VoiceKind::Cymbal;
    else if (note == 41 || note == 43 || note == 45 || note == 47 || note == 48 || note == 50)
        voice->kind = VoiceKind::LowPercussion;
    else voice->kind = VoiceKind::HighPercussion;

    const auto& world = profile();
    voice->tone = world.brightness;
    voice->pan = voice->kind == VoiceKind::Kick ? 0.0f :
        std::clamp((static_cast<float>((note * 7) % 17) - 8.0f) * 0.055f * world.stereo, -0.52f, 0.52f);
    auto frequency = 175.0f + static_cast<float>(note - 35) * 8.0f;
    switch (voice->kind) {
        case VoiceKind::Kick:
            frequency = 42.0f + world.warmth * 14.0f; voice->decaySeconds = 0.24f + world.decay * 0.22f; break;
        case VoiceKind::Snare:
            frequency = 165.0f + world.brightness * 55.0f; voice->decaySeconds = 0.14f + world.decay * 0.11f; break;
        case VoiceKind::Clap: frequency = 720.0f; voice->decaySeconds = 0.12f + world.space * 0.11f; break;
        case VoiceKind::ClosedHat: frequency = 6100.0f; voice->decaySeconds = 0.038f + world.decay * 0.034f; break;
        case VoiceKind::OpenHat: frequency = 5500.0f; voice->decaySeconds = 0.18f + world.decay * 0.20f; break;
        case VoiceKind::Cymbal: frequency = 3900.0f; voice->decaySeconds = 0.42f + world.decay * 0.42f; break;
        case VoiceKind::LowPercussion: voice->decaySeconds = 0.16f + world.decay * 0.12f; break;
        case VoiceKind::HighPercussion: frequency += 700.0f; voice->decaySeconds = 0.07f + world.decay * 0.07f; break;
        default: break;
    }
    voice->phaseDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
}

void PreviewSynth::noteOff(int channel, int note, bool allowTailOff) noexcept {
    if (channel == 10) return;
    for (auto& voice : tonalVoices) {
        if (!voice.active || voice.channel != channel || voice.note != note) continue;
        if (allowTailOff) voice.releasing = true;
        else voice = {};
    }
}

void PreviewSynth::handleMessage(const juce::MidiMessage& message) noexcept {
    if (message.isNoteOn()) noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff()) noteOff(message.getChannel(), message.getNoteNumber(), true);
    else if (message.isAllNotesOff()) allNotesOff(message.getChannel(), true);
    else if (message.isAllSoundOff()) allNotesOff(message.getChannel(), false);
    else if (message.isController()) {
        const auto channel = static_cast<std::size_t>(std::clamp(message.getChannel(), 1, 16) - 1);
        const auto value = static_cast<float>(message.getControllerValue()) / 127.0f;
        if (message.getControllerNumber() == 11) channelExpression[channel] = value;
        else if (message.getControllerNumber() == 1) channelModulation[channel] = value;
        else if (message.getControllerNumber() == 74) channelBrightness[channel] = value;
    }
}

float PreviewSynth::nextNoise(Voice& voice) noexcept {
    voice.noiseState = voice.noiseState * 1664525u + 1013904223u;
    return static_cast<float>((voice.noiseState >> 8) & 0x00ffffffu) /
           static_cast<float>(0x00800000u) - 1.0f;
}

float PreviewSynth::renderDrumSample(Voice& voice) noexcept {
    const auto noise = nextNoise(voice);
    const auto highNoise = noise - voice.previousNoise * 0.82f;
    voice.previousNoise = noise;
    const auto t = static_cast<float>(voice.ageSeconds);
    const auto body = sine(voice.phase);
    auto sample = 0.0f;
    switch (voice.kind) {
        case VoiceKind::Kick: {
            const auto sweep = std::exp(-t * 34.0f);
            voice.phase += voice.phaseDelta * (1.0 + 4.8 * sweep);
            sample = body * 0.90f + highNoise * sweep * (0.10f + voice.tone * 0.10f);
            break;
        }
        case VoiceKind::Snare:
            voice.phase += voice.phaseDelta;
            sample = noise * (0.58f + voice.tone * 0.28f) + body * (0.34f - voice.tone * 0.10f); break;
        case VoiceKind::Clap: {
            voice.phase += voice.phaseDelta;
            const auto burst = std::fmod(t * 31.0f, 1.0f) < 0.30f || t > 0.09f ? 1.0f : 0.12f;
            sample = highNoise * burst * 0.86f + body * 0.06f; break;
        }
        case VoiceKind::ClosedHat:
        case VoiceKind::OpenHat:
            voice.phase += voice.phaseDelta; voice.secondaryPhase += voice.phaseDelta * 1.41421356;
            sample = highNoise * 0.58f + sine(voice.phase) * sine(voice.secondaryPhase) * 0.42f; break;
        case VoiceKind::LowPercussion: {
            const auto sweep = std::exp(-t * 18.0f);
            voice.phase += voice.phaseDelta * (1.0 + 0.9 * sweep);
            sample = body * 0.84f + noise * 0.16f; break;
        }
        case VoiceKind::HighPercussion:
            voice.phase += voice.phaseDelta; sample = body * 0.32f + highNoise * 0.68f; break;
        case VoiceKind::Cymbal:
            voice.phase += voice.phaseDelta; voice.secondaryPhase += voice.phaseDelta * 1.6180339;
            sample = highNoise * 0.64f + (sine(voice.phase) + sine(voice.secondaryPhase)) * 0.18f; break;
        default: break;
    }
    switch (soundWorld) {
        case SoundWorld::OrganicMotion: sample = sample * 0.78f + noise * 0.10f; break;
        case SoundWorld::AnalogWarmth: sample = std::tanh(sample * 1.55f) * 0.82f; break;
        case SoundWorld::DubSpace: sample = sample * 0.60f + body * 0.28f; break;
        case SoundWorld::MinimalPulse: sample *= 0.72f; break;
        case SoundWorld::HypnoticNight:
            sample = sample * 0.72f + sine(voice.secondaryPhase * 0.73) * voice.envelope * 0.11f; break;
        case SoundWorld::CinematicArc: sample = sample * 0.78f + noise * 0.16f; break;
        case SoundWorld::DarkClub: sample = std::tanh(sample * 2.55f) * 0.76f; break;
        case SoundWorld::DeepProgressive: break;
        default: break;
    }
    return sample;
}

float PreviewSynth::renderVoiceSample(Voice& voice) noexcept {
    const auto& world = profile();
    const auto s1 = sine(voice.phase);
    const auto s2 = sine(voice.secondaryPhase);
    const auto tri1 = triangle(voice.phase);
    const auto tri2 = triangle(voice.secondaryPhase);
    const auto saw1 = bandlimitedSaw(voice.phase, voice.phaseDelta);
    const auto saw2 = bandlimitedSaw(voice.secondaryPhase, voice.phaseDelta * 2.002);
    const auto square1 = bandlimitedSquare(voice.phase, voice.phaseDelta);
    const auto noise = nextNoise(voice);
    auto colourA = s1;
    auto colourB = tri2;
    switch (soundWorld) {
        case SoundWorld::DeepProgressive:
            colourA = s1 * 0.62f + saw1 * 0.38f; colourB = tri2; break;
        case SoundWorld::OrganicMotion:
            colourA = tri1 * 0.72f + s1 * 0.28f; colourB = tri2 * 0.82f + noise * 0.10f; break;
        case SoundWorld::AnalogWarmth:
            colourA = saw1 * 0.66f + square1 * 0.22f + s1 * 0.12f;
            colourB = saw2 * 0.70f + s2 * 0.30f; break;
        case SoundWorld::DubSpace:
            colourA = s1 * 0.82f + tri1 * 0.18f; colourB = s2 * 0.72f + tri2 * 0.28f; break;
        case SoundWorld::MinimalPulse:
            colourA = square1 * 0.52f + s1 * 0.48f; colourB = saw2 * 0.56f + s2 * 0.44f; break;
        case SoundWorld::HypnoticNight:
            colourA = sine(voice.phase + s2 * 2.35f); colourB = sine(voice.secondaryPhase + s1 * 1.28f); break;
        case SoundWorld::CinematicArc:
            colourA = tri1 * 0.48f + s1 * 0.42f + noise * 0.10f;
            colourB = tri2 * 0.52f + s2 * 0.48f; break;
        case SoundWorld::DarkClub:
            colourA = std::tanh(saw1 * 2.4f); colourB = square1 * 0.48f + saw2 * 0.52f; break;
        default: break;
    }
    auto raw = 0.0f;
    switch (voice.kind) {
        case VoiceKind::SubBass: raw = s1 * 0.76f + colourA * 0.24f; break;
        case VoiceKind::MovementBass: raw = colourA * 0.70f + colourB * 0.30f; break;
        case VoiceKind::Foundation: raw = colourA * 0.44f + colourB * 0.41f + s1 * 0.15f; break;
        case VoiceKind::Pulse: raw = colourA * 0.68f + colourB * 0.32f; break;
        case VoiceKind::Upper:
            raw = sine(voice.phase + colourB * (1.0f + world.brightness * 2.2f)) * 0.72f + colourB * 0.28f; break;
        case VoiceKind::Lead: raw = colourA * 0.62f + colourB * 0.24f + s1 * 0.14f; break;
        case VoiceKind::Counter: raw = tri1 * 0.42f + colourB * 0.42f + s1 * 0.16f; break;
        case VoiceKind::Atmosphere: raw = (colourA + colourB) * 0.36f + noise * 0.20f; break;
        case VoiceKind::Transition: {
            raw = (noise - voice.previousNoise * 0.72f) * 0.72f + s1 * 0.18f;
            voice.previousNoise = noise; break;
        }
        default: return renderDrumSample(voice);
    }

    const auto channelIndex = static_cast<std::size_t>(std::clamp(voice.channel, 1, 16) - 1);
    const auto expressiveAlpha = std::clamp(voice.filterAlpha *
        (0.48f + channelBrightness[channelIndex] * 1.04f), 0.001f, 1.0f);
    voice.filterLeft += expressiveAlpha * (raw - voice.filterLeft);
    raw = voice.filterLeft;
    const auto drive = 1.0f + world.drive * 3.5f;
    raw = std::tanh(raw * drive) / std::tanh(drive);
    auto driftAmount = 0.0;
    if (soundWorld == SoundWorld::OrganicMotion) driftAmount = 0.00055;
    else if (soundWorld == SoundWorld::AnalogWarmth) driftAmount = 0.00115;
    else if (soundWorld == SoundWorld::CinematicArc) driftAmount = 0.00072;
    const auto modulation = channelModulation[channelIndex] * 0.0018;
    const auto drift = 1.0 + driftAmount * std::sin(voice.ageSeconds * 1.7 + voice.note * 0.37) +
                       modulation * std::sin(voice.ageSeconds * 5.1);
    voice.phase += voice.phaseDelta * drift;
    const auto detune = voice.kind == VoiceKind::Foundation || voice.kind == VoiceKind::Atmosphere ?
        1.0035 + world.stereo * 0.003 : 2.002;
    voice.secondaryPhase += voice.phaseDelta * detune / drift;
    return raw;
}

void PreviewSynth::renderVoices(juce::AudioBuffer<float>& output, int startSample,
                                int numSamples) noexcept {
    const auto endSample = startSample + numSamples;
    const auto renderBank = [&](auto& voices) {
        for (auto& voice : voices) {
            if (!voice.active) continue;
            for (auto sample = startSample; sample < endSample; ++sample) {
                if (voice.oneShot) {
                    voice.envelope = std::exp(-static_cast<float>(voice.ageSeconds) /
                                              std::max(0.015f, voice.decaySeconds));
                } else if (voice.releasing) {
                    voice.envelope *= std::exp(-1.0f / (std::max(0.015f, voice.releaseSeconds) *
                                                       static_cast<float>(sampleRate)));
                } else if (voice.ageSeconds < voice.attackSeconds) {
                    voice.envelope = static_cast<float>(voice.ageSeconds) / std::max(0.001f, voice.attackSeconds);
                } else {
                    voice.envelope = voice.sustain + (1.0f - voice.sustain) *
                        std::exp(-(static_cast<float>(voice.ageSeconds) - voice.attackSeconds) /
                                 std::max(0.015f, voice.decaySeconds));
                }
                if (voice.envelope < 0.00025f && (voice.oneShot || voice.releasing)) {
                    voice = {};
                    break;
                }

                const auto raw = voice.oneShot ? renderDrumSample(voice) : renderVoiceSample(voice);
                const auto level = voice.oneShot ? 0.30f : 1.0f;
                const auto expression = channelExpression[static_cast<std::size_t>(
                    std::clamp(voice.channel, 1, 16) - 1)];
                const auto value = raw * voice.level * voice.envelope * level * expression;
                const auto left = value * (voice.pan > 0.0f ? 1.0f - voice.pan * 0.62f : 1.0f);
                const auto right = value * (voice.pan < 0.0f ? 1.0f + voice.pan * 0.62f : 1.0f);
                if (output.getNumChannels() > 0) output.addSample(0, sample, left);
                if (output.getNumChannels() > 1) output.addSample(1, sample, right);
                for (auto channel = 2; channel < output.getNumChannels(); ++channel)
                    output.addSample(channel, sample, value);
                voice.ageSeconds += 1.0 / sampleRate;
                if (voice.phase >= juce::MathConstants<double>::twoPi)
                    voice.phase = std::fmod(voice.phase, juce::MathConstants<double>::twoPi);
                if (voice.secondaryPhase >= juce::MathConstants<double>::twoPi)
                    voice.secondaryPhase = std::fmod(voice.secondaryPhase, juce::MathConstants<double>::twoPi);
            }
        }
    };
    renderBank(drumVoices);
    renderBank(tonalVoices);
}

void PreviewSynth::processEffects(juce::AudioBuffer<float>& output, int startSample,
                                  int numSamples) noexcept {
    if (output.getNumChannels() < 1 || delayLeft.empty() || roomLeft.empty()) return;
    const auto& world = profile();
    const auto delaySeconds = 0.19f + world.space * 0.31f;
    const auto delaySamples = std::clamp(static_cast<std::size_t>(sampleRate * delaySeconds),
                                         std::size_t{1}, delayLeft.size() - 1);
    const auto roomSamplesL = std::clamp(static_cast<std::size_t>(sampleRate * (0.113f + world.space * 0.079f)),
                                         std::size_t{1}, roomLeft.size() - 1);
    const auto roomSamplesR = std::clamp(static_cast<std::size_t>(sampleRate * (0.149f + world.space * 0.091f)),
                                         std::size_t{1}, roomRight.size() - 1);
    const auto wetDelay = world.space * 0.24f;
    const auto wetRoom = world.space * 0.30f;
    const auto delayFeedback = 0.18f + world.space * 0.34f;
    const auto roomFeedback = 0.36f + world.space * 0.36f;

    for (auto sample = startSample; sample < startSample + numSamples; ++sample) {
        const auto dryL = output.getSample(0, sample);
        const auto dryR = output.getNumChannels() > 1 ? output.getSample(1, sample) : dryL;
        const auto delayRead = (delayWrite + delayLeft.size() - delaySamples) % delayLeft.size();
        const auto roomReadL = (roomWrite + roomLeft.size() - roomSamplesL) % roomLeft.size();
        const auto roomReadR = (roomWrite + roomRight.size() - roomSamplesR) % roomRight.size();
        const auto echoL = delayLeft[delayRead];
        const auto echoR = delayRight[delayRead];
        const auto roomL = roomLeft[roomReadL];
        const auto roomR = roomRight[roomReadR];

        delayLeft[delayWrite] = dryL + echoR * delayFeedback;
        delayRight[delayWrite] = dryR + echoL * delayFeedback;
        roomLeft[roomWrite] = dryL * 0.72f + roomR * roomFeedback;
        roomRight[roomWrite] = dryR * 0.72f + roomL * roomFeedback;
        output.setSample(0, sample, dryL + echoL * wetDelay + roomL * wetRoom);
        if (output.getNumChannels() > 1)
            output.setSample(1, sample, dryR + echoR * wetDelay + roomR * wetRoom);
        delayWrite = (delayWrite + 1) % delayLeft.size();
        roomWrite = (roomWrite + 1) % roomLeft.size();
    }
}

void PreviewSynth::renderNextBlock(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi,
                                   int startSample, int numSamples) noexcept {
    const auto blockEnd = startSample + numSamples;
    auto cursor = startSample;
    for (const auto metadata : midi) {
        const auto eventSample = std::clamp(metadata.samplePosition, startSample, blockEnd);
        if (eventSample > cursor) renderVoices(output, cursor, eventSample - cursor);
        handleMessage(metadata.getMessage());
        cursor = eventSample;
    }
    if (cursor < blockEnd) renderVoices(output, cursor, blockEnd - cursor);
    processEffects(output, startSample, numSamples);
}

} // namespace pulso::plugin
