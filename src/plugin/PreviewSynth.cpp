#include "PreviewSynth.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pulso::plugin {

void PreviewSynth::prepare(double newSampleRate) noexcept {
    sampleRate = std::max(1.0, newSampleRate);
    attackDelta = 1.0f / static_cast<float>(sampleRate * 0.003);
    releaseMultiplier = static_cast<float>(std::exp(std::log(0.0001) / (sampleRate * 0.030)));
    reset();
}

void PreviewSynth::reset() noexcept {
    voices = {};
    ageCounter = 0;
}

void PreviewSynth::allNotesOff(int midiChannel, bool allowTailOff) noexcept {
    for (auto& voice : voices) {
        if (!voice.active || (midiChannel > 0 && voice.channel != midiChannel)) continue;
        if (allowTailOff) {
            voice.releasing = true;
        } else {
            voice = {};
        }
    }
}

void PreviewSynth::noteOn(int channel, int note, float velocity) noexcept {
    auto* selected = static_cast<Voice*>(nullptr);
    for (auto& voice : voices) {
        if (!voice.active) {
            selected = &voice;
            break;
        }
    }
    if (selected == nullptr) {
        selected = &*std::min_element(voices.begin(), voices.end(), [](const auto& left, const auto& right) {
            const auto leftEnergy = left.level * left.envelope;
            const auto rightEnergy = right.level * right.envelope;
            return leftEnergy == rightEnergy ? left.age < right.age : leftEnergy < rightEnergy;
        });
    }

    *selected = {};
    selected->active = true;
    selected->channel = std::clamp(channel, 1, 16);
    selected->note = std::clamp(note, 0, 127);
    selected->level = std::clamp(velocity, 0.0f, 1.0f) * 0.11f;
    selected->age = ++ageCounter;
    selected->phaseDelta = juce::MathConstants<double>::twoPi *
                           juce::MidiMessage::getMidiNoteInHertz(selected->note) / sampleRate;
}

void PreviewSynth::noteOff(int channel, int note, bool allowTailOff) noexcept {
    for (auto& voice : voices) {
        if (!voice.active || voice.channel != channel || voice.note != note) continue;
        if (allowTailOff) {
            voice.releasing = true;
        } else {
            voice = {};
        }
    }
}

void PreviewSynth::handleMessage(const juce::MidiMessage& message) noexcept {
    if (message.isNoteOn()) {
        noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
    } else if (message.isNoteOff()) {
        noteOff(message.getChannel(), message.getNoteNumber(), true);
    } else if (message.isAllNotesOff()) {
        allNotesOff(message.getChannel(), true);
    } else if (message.isAllSoundOff()) {
        allNotesOff(message.getChannel(), false);
    }
}

void PreviewSynth::renderVoices(juce::AudioBuffer<float>& output, int startSample,
                                int numSamples) noexcept {
    const auto endSample = startSample + numSamples;
    for (auto& voice : voices) {
        if (!voice.active) continue;
        for (auto sample = startSample; sample < endSample; ++sample) {
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
            const auto value = (fundamental + harmonic) * voice.level * voice.envelope;
            for (auto channel = 0; channel < output.getNumChannels(); ++channel)
                output.addSample(channel, sample, value);

            voice.phase += voice.phaseDelta;
            if (voice.phase >= juce::MathConstants<double>::twoPi)
                voice.phase -= juce::MathConstants<double>::twoPi;
        }
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
}

} // namespace pulso::plugin
