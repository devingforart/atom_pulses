#include "PreviewSynth.h"

#include <cmath>

namespace pulso::plugin {

bool PreviewVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<PreviewSound*>(sound) != nullptr;
}

void PreviewVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) {
    phase = 0.0;
    level = velocity * 0.16f;
    tailOff = 0.0f;
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    phaseDelta = juce::MathConstants<double>::twoPi * frequency / getSampleRate();
}

void PreviewVoice::stopNote(float, bool allowTailOff) {
    if (allowTailOff) {
        if (tailOff == 0.0f) tailOff = 1.0f;
    } else {
        clearCurrentNote();
        phaseDelta = 0.0;
    }
}

void PreviewVoice::renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) {
    if (phaseDelta == 0.0) return;

    for (auto sample = startSample; sample < startSample + numSamples; ++sample) {
        const auto sine = static_cast<float>(std::sin(phase));
        const auto second = static_cast<float>(std::sin(phase * 2.0)) * 0.18f;
        auto value = (sine + second) * level;
        if (tailOff > 0.0f) {
            value *= tailOff;
            tailOff *= 0.992f;
            if (tailOff <= 0.005f) {
                clearCurrentNote();
                phaseDelta = 0.0;
                break;
            }
        }
        for (auto channel = 0; channel < output.getNumChannels(); ++channel)
            output.addSample(channel, sample, value);
        phase += phaseDelta;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
}

void initialisePreviewSynth(juce::Synthesiser& synth) {
    synth.clearVoices();
    synth.clearSounds();
    for (auto index = 0; index < 16; ++index) synth.addVoice(new PreviewVoice());
    synth.addSound(new PreviewSound());
}

} // namespace pulso::plugin

