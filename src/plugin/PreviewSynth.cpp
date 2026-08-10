#include "PreviewSynth.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {

void PreviewSynth::prepare(double newSampleRate) noexcept {
    sampleRate = std::max(1.0, newSampleRate);
    attackDelta = 1.0f / static_cast<float>(sampleRate * 0.003);
    releaseMultiplier = static_cast<float>(std::exp(std::log(0.0001) / (sampleRate * 0.030)));
    reset();
}

void PreviewSynth::reset() noexcept {
    drumVoices = {};
    tonalVoices = {};
    ageCounter = 0;
}

void PreviewSynth::setDrumKit(int kit) noexcept {
    drumKit = static_cast<DrumKit>(std::clamp(kit, 0, static_cast<int>(DrumKit::Count) - 1));
}

void PreviewSynth::allNotesOff(int midiChannel, bool allowTailOff) noexcept {
    const auto release = [midiChannel, allowTailOff](auto& voices) {
        for (auto& voice : voices) {
            if (!voice.active || (midiChannel > 0 && voice.channel != midiChannel)) continue;
            if (allowTailOff) voice.releasing = true;
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

void PreviewSynth::noteOn(int channel, int note, float velocity) noexcept {
    if (channel == 10) {
        drumNoteOn(note, velocity);
        return;
    }
    auto* selected = selectVoice(false);
    *selected = {};
    selected->active = true;
    selected->channel = std::clamp(channel, 1, 16);
    selected->note = std::clamp(note, 0, 127);
    selected->level = std::clamp(velocity, 0.0f, 1.0f) * 0.10f;
    selected->age = ++ageCounter;
    selected->kind = VoiceKind::Tonal;
    selected->pan = std::clamp((static_cast<float>(channel) - 8.0f) * 0.055f, -0.38f, 0.38f);
    selected->phaseDelta = juce::MathConstants<double>::twoPi *
                           juce::MidiMessage::getMidiNoteInHertz(selected->note) / sampleRate;
}

void PreviewSynth::drumNoteOn(int note, float velocity) noexcept {
    auto* selected = selectVoice(true);
    *selected = {};
    selected->active = true;
    selected->channel = 10;
    selected->note = std::clamp(note, 0, 127);
    selected->level = std::clamp(velocity, 0.0f, 1.0f);
    selected->envelope = 1.0f;
    selected->age = ++ageCounter;
    selected->noiseState = 0x9e3779b9u ^ (static_cast<std::uint32_t>(note) * 2654435761u) ^
                           static_cast<std::uint32_t>(selected->age);

    if (note == 35 || note == 36) selected->kind = VoiceKind::Kick;
    else if (note == 38 || note == 40) selected->kind = VoiceKind::Snare;
    else if (note == 37 || note == 39) selected->kind = VoiceKind::Clap;
    else if (note == 42 || note == 44) selected->kind = VoiceKind::ClosedHat;
    else if (note == 46) selected->kind = VoiceKind::OpenHat;
    else if (note == 49 || note == 51 || note == 52 || note == 55 || note == 57)
        selected->kind = VoiceKind::Cymbal;
    else if (note == 41 || note == 43 || note == 45 || note == 47 || note == 48 || note == 50)
        selected->kind = VoiceKind::LowPercussion;
    else selected->kind = VoiceKind::HighPercussion;

    constexpr std::array kickTune{48.0f, 54.0f, 51.0f, 43.0f};
    constexpr std::array kitDecay{1.0f, 0.82f, 0.62f, 1.35f};
    constexpr std::array kitTone{0.38f, 0.62f, 0.78f, 0.48f};
    const auto kit = static_cast<std::size_t>(drumKit);
    selected->tone = kitTone[kit];
    selected->pan = selected->kind == VoiceKind::Kick ? 0.0f :
        std::clamp((static_cast<float>((note * 7) % 17) - 8.0f) * 0.055f, -0.44f, 0.44f);

    auto frequency = 180.0f + static_cast<float>(note - 35) * 8.0f;
    switch (selected->kind) {
        case VoiceKind::Kick: frequency = kickTune[kit]; selected->decaySeconds = 0.42f * kitDecay[kit]; break;
        case VoiceKind::Snare: frequency = 185.0f; selected->decaySeconds = 0.24f * kitDecay[kit]; break;
        case VoiceKind::Clap: frequency = 720.0f; selected->decaySeconds = 0.18f * kitDecay[kit]; break;
        case VoiceKind::ClosedHat: frequency = 6300.0f; selected->decaySeconds = 0.065f * kitDecay[kit]; break;
        case VoiceKind::OpenHat: frequency = 5700.0f; selected->decaySeconds = 0.34f * kitDecay[kit]; break;
        case VoiceKind::Cymbal: frequency = 4100.0f; selected->decaySeconds = 0.72f * kitDecay[kit]; break;
        case VoiceKind::LowPercussion: selected->decaySeconds = 0.25f * kitDecay[kit]; break;
        case VoiceKind::HighPercussion: frequency += 700.0f; selected->decaySeconds = 0.12f * kitDecay[kit]; break;
        case VoiceKind::Tonal: break;
    }
    selected->phaseDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
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
}

float PreviewSynth::renderDrumSample(Voice& voice) noexcept {
    voice.noiseState = voice.noiseState * 1664525u + 1013904223u;
    const auto noise = static_cast<float>((voice.noiseState >> 8) & 0x00ffffffu) /
                       static_cast<float>(0x00800000u) - 1.0f;
    const auto highNoise = noise - voice.previousNoise * 0.82f;
    voice.previousNoise = noise;
    const auto t = static_cast<float>(voice.ageSeconds);
    const auto body = static_cast<float>(std::sin(voice.phase));
    auto sample = 0.0f;

    switch (voice.kind) {
        case VoiceKind::Kick: {
            const auto sweep = std::exp(-t * 34.0f);
            voice.phase += voice.phaseDelta * (1.0 + 4.5 * sweep);
            sample = body * (0.94f - voice.tone * 0.12f) + highNoise * sweep * 0.18f;
            break;
        }
        case VoiceKind::Snare:
            voice.phase += voice.phaseDelta;
            sample = noise * (0.66f + voice.tone * 0.22f) + body * (0.34f - voice.tone * 0.12f);
            break;
        case VoiceKind::Clap: {
            voice.phase += voice.phaseDelta;
            const auto burst = std::fmod(t * 28.0f, 1.0f) < 0.32f || t > 0.095f ? 1.0f : 0.18f;
            sample = highNoise * burst * 0.82f + body * 0.08f;
            break;
        }
        case VoiceKind::ClosedHat:
        case VoiceKind::OpenHat:
            voice.phase += voice.phaseDelta;
            voice.secondaryPhase += voice.phaseDelta * 1.41421356;
            sample = highNoise * 0.58f +
                     static_cast<float>(std::sin(voice.phase) * std::sin(voice.secondaryPhase)) * 0.42f;
            break;
        case VoiceKind::LowPercussion: {
            const auto sweep = std::exp(-t * 18.0f);
            voice.phase += voice.phaseDelta * (1.0 + 0.9 * sweep);
            sample = body * 0.84f + noise * 0.16f;
            break;
        }
        case VoiceKind::HighPercussion:
            voice.phase += voice.phaseDelta;
            sample = body * 0.38f + highNoise * 0.62f;
            break;
        case VoiceKind::Cymbal:
            voice.phase += voice.phaseDelta;
            voice.secondaryPhase += voice.phaseDelta * 1.6180339;
            sample = highNoise * 0.62f +
                     static_cast<float>(std::sin(voice.phase) + std::sin(voice.secondaryPhase)) * 0.19f;
            break;
        case VoiceKind::Tonal: break;
    }
    if (voice.phase >= juce::MathConstants<double>::twoPi)
        voice.phase = std::fmod(voice.phase, juce::MathConstants<double>::twoPi);
    if (voice.secondaryPhase >= juce::MathConstants<double>::twoPi)
        voice.secondaryPhase = std::fmod(voice.secondaryPhase, juce::MathConstants<double>::twoPi);
    return sample;
}

void PreviewSynth::renderVoices(juce::AudioBuffer<float>& output, int startSample,
                                int numSamples) noexcept {
    const auto endSample = startSample + numSamples;
    const auto renderBank = [&](auto& voices) {
        for (auto& voice : voices) {
            if (!voice.active) continue;
            for (auto sample = startSample; sample < endSample; ++sample) {
                float value{};
                if (voice.kind != VoiceKind::Tonal) {
                    voice.envelope = std::exp(-static_cast<float>(voice.ageSeconds) /
                                              std::max(0.015f, voice.decaySeconds));
                    if (voice.envelope < 0.0003f || voice.releasing) {
                        voice = {};
                        break;
                    }
                    value = renderDrumSample(voice) * voice.level * voice.envelope * 0.32f;
                    voice.ageSeconds += 1.0 / sampleRate;
                } else {
                    if (voice.releasing) {
                        voice.envelope *= releaseMultiplier;
                        if (voice.envelope < 0.0001f) {
                            voice = {};
                            break;
                        }
                    } else {
                        voice.envelope = std::min(1.0f, voice.envelope + attackDelta);
                    }
                    const auto fundamental = static_cast<float>(std::sin(voice.phase));
                    const auto harmonic = static_cast<float>(std::sin(voice.phase * 2.0)) * 0.16f;
                    value = (fundamental + harmonic) * voice.level * voice.envelope;
                    voice.phase += voice.phaseDelta;
                }

                const auto left = value * (voice.pan > 0.0f ? 1.0f - voice.pan * 0.55f : 1.0f);
                const auto right = value * (voice.pan < 0.0f ? 1.0f + voice.pan * 0.55f : 1.0f);
                if (output.getNumChannels() > 0) output.addSample(0, sample, left);
                if (output.getNumChannels() > 1) output.addSample(1, sample, right);
                for (auto channel = 2; channel < output.getNumChannels(); ++channel)
                    output.addSample(channel, sample, value);
                if (voice.phase >= juce::MathConstants<double>::twoPi)
                    voice.phase -= juce::MathConstants<double>::twoPi;
            }
        }
    };
    renderBank(drumVoices);
    renderBank(tonalVoices);
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
}

} // namespace pulso::plugin
