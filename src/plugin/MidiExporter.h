#pragma once

#include "core/MusicTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace pulso::plugin {

struct MidiExportOptions {
    double bpm{120.0};
    int timeSignatureNumerator{4};
    int timeSignatureDenominator{4};
    int channelFilter{}; // 0 exports every channel.
    int voiceFilter{-1}; // -1 exports every voice.
    int familyFilter{-1}; // -1 exports every family.
    juce::String clipName{"PULSO"};
};

[[nodiscard]] bool writePatternToMidiFile(const Pattern&, const juce::File&,
                                          const MidiExportOptions&);

} // namespace pulso::plugin
