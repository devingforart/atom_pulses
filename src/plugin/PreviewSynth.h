#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pulso::plugin {

class PreviewSound final : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class PreviewVoice final : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    double phase{};
    double phaseDelta{};
    float level{};
    float tailOff{};
};

void initialisePreviewSynth(juce::Synthesiser& synth);

} // namespace pulso::plugin

