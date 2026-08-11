#include "MidiExporter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso::plugin {
namespace {

constexpr int ticksPerQuarterNote = 960;

juce::String trackNameForVoice(VoiceId voice) {
    return "PULSO " + juce::String(voiceDefinition(voice).name.data());
}

VoiceId resolvedVoice(VoiceId voice, int channel) {
    return voice == VoiceId::Unspecified ? inferVoiceFromChannel(channel) : voice;
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
    for (const auto& marker : pattern.markers) {
        if (!std::isfinite(marker.beat) || marker.beat < 0.0 || marker.beat >= pattern.lengthBeats)
            continue;
        auto message = juce::MidiMessage::textMetaEvent(
            6, juce::String::fromUTF8(marker.name.c_str()));
        message.setTimeStamp(marker.beat * ticksPerQuarterNote);
        conductor.addEvent(message);
    }
    addEndOfTrack(conductor, phraseEnd);
    midiFile.addTrack(conductor);

    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> populatedVoices{};
    const auto selected = [&](VoiceId voice, int channel) {
        const auto resolved = resolvedVoice(voice, channel);
        return (options.channelFilter == 0 || channel == options.channelFilter) &&
               (options.voiceFilter < 0 || static_cast<int>(resolved) == options.voiceFilter) &&
               (options.familyFilter < 0 ||
                static_cast<int>(voiceDefinition(resolved).family) == options.familyFilter);
    };
    for (const auto& note : pattern.notes) {
        if (note.channel >= 1 && note.channel <= 16 && selected(note.voice, note.channel))
            populatedVoices[static_cast<std::size_t>(resolvedVoice(note.voice, note.channel))] = true;
    }
    for (const auto& control : pattern.controls)
        if (control.channel >= 1 && control.channel <= 16 && selected(control.voice, control.channel))
            populatedVoices[static_cast<std::size_t>(resolvedVoice(control.voice, control.channel))] = true;
    for (const auto& expression : pattern.expressions)
        if (expression.channel >= 1 && expression.channel <= 16 && selected(expression.voice, expression.channel))
            populatedVoices[static_cast<std::size_t>(resolvedVoice(expression.voice, expression.channel))] = true;

    auto exportedNotes = 0;
    for (std::size_t voiceIndex = 0; voiceIndex < populatedVoices.size(); ++voiceIndex) {
        if (!populatedVoices[voiceIndex]) continue;
        const auto voice = static_cast<VoiceId>(voiceIndex);
        juce::MidiMessageSequence track;
        auto trackName = juce::MidiMessage::textMetaEvent(3, trackNameForVoice(voice));
        trackName.setTimeStamp(0.0);
        track.addEvent(trackName);

        for (const auto& note : pattern.notes) {
            if (resolvedVoice(note.voice, note.channel) != voice ||
                !selected(note.voice, note.channel)) continue;
            const auto start = std::clamp(note.startBeat, 0.0, pattern.lengthBeats) *
                               ticksPerQuarterNote;
            const auto end = std::clamp(note.endBeat(), note.startBeat, pattern.lengthBeats) *
                             ticksPerQuarterNote;
            auto noteOn = juce::MidiMessage::noteOn(
                std::clamp(note.channel, 1, 16), std::clamp(note.pitch, 0, 127),
                static_cast<juce::uint8>(std::clamp(note.velocity, 1, 127)));
            noteOn.setTimeStamp(start);
            track.addEvent(noteOn);
            auto noteOff = juce::MidiMessage::noteOff(std::clamp(note.channel, 1, 16),
                                                       std::clamp(note.pitch, 0, 127));
            noteOff.setTimeStamp(std::max(start + 1.0, end));
            track.addEvent(noteOff);
            ++exportedNotes;
        }
        for (const auto& control : pattern.controls) {
            if (resolvedVoice(control.voice, control.channel) != voice ||
                !selected(control.voice, control.channel)) continue;
            auto message = juce::MidiMessage::controllerEvent(
                std::clamp(control.channel, 1, 16), std::clamp(control.controller, 0, 127),
                std::clamp(control.value, 0, 127));
            message.setTimeStamp(std::clamp(control.beat, 0.0, pattern.lengthBeats) *
                                 ticksPerQuarterNote);
            track.addEvent(message);
        }
        for (const auto& expression : pattern.expressions) {
            if (resolvedVoice(expression.voice, expression.channel) != voice ||
                !selected(expression.voice, expression.channel)) continue;
            const auto channel = std::clamp(expression.channel, 1, 16);
            juce::MidiMessage message;
            switch (expression.type) {
                case ExpressionEventType::PitchBend:
                    message = juce::MidiMessage::pitchWheel(channel,
                        std::clamp(expression.value, 0, 16383));
                    break;
                case ExpressionEventType::ChannelPressure:
                    message = juce::MidiMessage::channelPressureChange(channel,
                        std::clamp(expression.value, 0, 127));
                    break;
                case ExpressionEventType::PolyAftertouch:
                    message = juce::MidiMessage::aftertouchChange(channel,
                        std::clamp(expression.note, 0, 127),
                        std::clamp(expression.value, 0, 127));
                    break;
            }
            message.setTimeStamp(std::clamp(expression.beat, 0.0, pattern.lengthBeats) *
                                 ticksPerQuarterNote);
            track.addEvent(message);
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
