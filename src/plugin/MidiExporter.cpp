#include "MidiExporter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso::plugin {
namespace {

constexpr int ticksPerQuarterNote = 960;

juce::String trackNameForChannel(int channel) {
    if (channel == 1) return "PULSO Bass";
    if (channel == 2) return "PULSO Melody";
    if (channel == 3) return "PULSO Harmony";
    if (channel == 10) return "PULSO Drums";
    return "PULSO MIDI " + juce::String(channel);
}

void addEndOfTrack(juce::MidiMessageSequence& sequence, double timestamp) {
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(timestamp);
    sequence.addEvent(end);
}

} // namespace

bool writePatternToMidiFile(const Pattern& pattern, const juce::File& destination,
                            const MidiExportOptions& options) {
    if (pattern.notes.empty() || pattern.lengthBeats <= 0.0 ||
        options.bpm <= 0.0 || options.timeSignatureNumerator <= 0 ||
        options.timeSignatureDenominator <= 0)
        return false;

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarterNote);
    const auto phraseEnd = std::max(1.0, pattern.lengthBeats * ticksPerQuarterNote);

    juce::MidiMessageSequence conductor;
    auto name = juce::MidiMessage::textMetaEvent(3, options.clipName);
    name.setTimeStamp(0.0);
    conductor.addEvent(name);
    auto tempo = juce::MidiMessage::tempoMetaEvent(
        std::max(1, static_cast<int>(std::lround(60000000.0 / options.bpm))));
    tempo.setTimeStamp(0.0);
    conductor.addEvent(tempo);
    auto signature = juce::MidiMessage::timeSignatureMetaEvent(
        options.timeSignatureNumerator, options.timeSignatureDenominator);
    signature.setTimeStamp(0.0);
    conductor.addEvent(signature);
    addEndOfTrack(conductor, phraseEnd);
    midiFile.addTrack(conductor);

    std::array<bool, 17> populatedChannels{};
    for (const auto& note : pattern.notes) {
        if (note.channel >= 1 && note.channel <= 16 &&
            (options.channelFilter == 0 || note.channel == options.channelFilter))
            populatedChannels[static_cast<std::size_t>(note.channel)] = true;
    }

    auto exportedNotes = 0;
    for (auto channel = 1; channel <= 16; ++channel) {
        if (!populatedChannels[static_cast<std::size_t>(channel)]) continue;
        juce::MidiMessageSequence track;
        auto trackName = juce::MidiMessage::textMetaEvent(3, trackNameForChannel(channel));
        trackName.setTimeStamp(0.0);
        track.addEvent(trackName);

        for (const auto& note : pattern.notes) {
            if (note.channel != channel) continue;
            const auto start = std::clamp(note.startBeat, 0.0, pattern.lengthBeats) *
                               ticksPerQuarterNote;
            const auto end = std::clamp(note.endBeat(), note.startBeat, pattern.lengthBeats) *
                             ticksPerQuarterNote;
            auto noteOn = juce::MidiMessage::noteOn(
                channel, std::clamp(note.pitch, 0, 127),
                static_cast<juce::uint8>(std::clamp(note.velocity, 1, 127)));
            noteOn.setTimeStamp(start);
            track.addEvent(noteOn);
            auto noteOff = juce::MidiMessage::noteOff(channel, std::clamp(note.pitch, 0, 127));
            noteOff.setTimeStamp(std::max(start + 1.0, end));
            track.addEvent(noteOff);
            ++exportedNotes;
        }
        addEndOfTrack(track, phraseEnd);
        track.updateMatchedPairs();
        midiFile.addTrack(track);
    }

    if (exportedNotes == 0) return false;
    destination.getParentDirectory().createDirectory();
    juce::FileOutputStream output(destination);
    if (!output.openedOk()) return false;
    const auto written = midiFile.writeTo(output, 1);
    output.flush();
    return written && output.getStatus().wasOk();
}

} // namespace pulso::plugin
