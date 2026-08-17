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

int majorKeySignature(int rootPitchClass) noexcept {
    constexpr std::array signatures{0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
    return signatures[static_cast<std::size_t>(positiveModulo(rootPitchClass, 12))];
}

juce::String departmentName(ScoreDepartment department) {
    if (department == ScoreDepartment::Rhythm) return "rhythm";
    if (department == ScoreDepartment::Melody) return "melody";
    return "harmony";
}

bool supportsSustainPedal(InstrumentSoundModel model, VoiceId voice,
                          std::uint16_t partId) noexcept {
    switch (model) {
        case InstrumentSoundModel::Piano:
        case InstrumentSoundModel::Harp:
        case InstrumentSoundModel::AnalogPad:
        case InstrumentSoundModel::PolySynth:
            return true;
        case InstrumentSoundModel::Generic:
            return partId == 0 && (voice == VoiceId::HarmonicFoundation ||
                                   voice == VoiceId::HarmonicUpper ||
                                   voice == VoiceId::Atmosphere);
        default:
            return false;
    }
}

double maximumPedalSpan(InstrumentSoundModel model) noexcept {
    if (model == InstrumentSoundModel::Harp) return 1.5;
    if (model == InstrumentSoundModel::AnalogPad || model == InstrumentSoundModel::PolySynth)
        return 4.0;
    return 2.0;
}

bool writeCompanionManifest(const Pattern& pattern, const juce::File& midi,
                            const MidiExportOptions& options) {
    auto root = new juce::DynamicObject();
    root->setProperty("schema_version", 2);
    root->setProperty("midi_file", midi.getFileName());
    root->setProperty("title", options.clipName);
    root->setProperty("bpm", options.bpm);
    root->setProperty("length_beats", pattern.lengthBeats);
    root->setProperty("narrative_audited", pattern.narrativeAuditPerformed);
    root->setProperty("narrative_score", pattern.narrativeScore);
    root->setProperty("ai_authored_note_ratio", pattern.aiAuthoredNoteRatio);
    root->setProperty("primary_voice_authorship_coverage", pattern.primaryVoiceAuthorshipCoverage);
    root->setProperty("thematic_recall_ratio", pattern.thematicRecallRatio);
    juce::Array<juce::var> parts;
    for (const auto& part : pattern.parts) {
        const auto noteCount = std::count_if(pattern.notes.begin(), pattern.notes.end(),
            [&](const auto& note) { return note.partId == part.id; });
        if (noteCount == 0) continue;
        auto item = new juce::DynamicObject();
        item->setProperty("part_id", static_cast<int>(part.id));
        item->setProperty("track_name", "PULSO " + juce::String::fromUTF8(part.name.c_str()));
        item->setProperty("catalog_id", juce::String::fromUTF8(part.catalogId.c_str()));
        item->setProperty("department", departmentName(part.department));
        item->setProperty("source_voice", juce::String(voiceDefinition(part.sourceVoice).key.data()));
        item->setProperty("role", juce::String::fromUTF8(part.role.c_str()));
        item->setProperty("orchestral_function", juce::String::fromUTF8(part.orchestralFunction.c_str()));
        item->setProperty("articulation", juce::String::fromUTF8(part.articulation.c_str()));
        item->setProperty("divisi_voices", part.divisiVoices);
        item->setProperty("live_device", juce::String::fromUTF8(part.liveDevice.c_str()));
        item->setProperty("live_preset_intent", juce::String::fromUTF8(part.livePresetIntent.c_str()));
        item->setProperty("note_count", static_cast<int>(noteCount));
        parts.add(juce::var(item));
    }
    root->setProperty("parts", parts);
    return midi.withFileExtension(".pulso.json").replaceWithText(
        juce::JSON::toString(juce::var(root), true), false, false, "\n");
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
    if (options.includeKeySignature) {
        const auto relativeMajor = options.scale == ScaleKind::Minor
            ? positiveModulo(options.rootPitchClass + 3, 12)
            : options.scale == ScaleKind::Dorian ? positiveModulo(options.rootPitchClass - 2, 12)
            : options.scale == ScaleKind::Mixolydian ? positiveModulo(options.rootPitchClass - 7, 12)
            : options.rootPitchClass;
        auto key = juce::MidiMessage::keySignatureMetaEvent(
            majorKeySignature(relativeMajor), options.scale == ScaleKind::Minor);
        key.setTimeStamp(0.0);
        conductor.addEvent(key);
    }
    for (const auto& marker : pattern.markers) {
        if (!std::isfinite(marker.beat) || marker.beat < 0.0 || marker.beat >= pattern.lengthBeats)
            continue;
        auto message = juce::MidiMessage::textMetaEvent(
            6, juce::String::fromUTF8(marker.name.c_str()));
        message.setTimeStamp(marker.beat * ticksPerQuarterNote);
        conductor.addEvent(message);
    }
    for (const auto& marker : options.chordMarkers) {
        if (!std::isfinite(marker.beat) || marker.beat < 0.0 || marker.beat >= pattern.lengthBeats)
            continue;
        auto message = juce::MidiMessage::textMetaEvent(
            6, "Chord: " + juce::String::fromUTF8(marker.name.c_str()));
        message.setTimeStamp(marker.beat * ticksPerQuarterNote);
        conductor.addEvent(message);
    }
    addEndOfTrack(conductor, phraseEnd);
    midiFile.addTrack(conductor);

    const auto selected = [&](VoiceId voice, int channel) {
        const auto resolved = resolvedVoice(voice, channel);
        return (options.channelFilter == 0 || channel == options.channelFilter) &&
               (options.voiceFilter < 0 || static_cast<int>(resolved) == options.voiceFilter) &&
               (options.familyFilter < 0 ||
                static_cast<int>(voiceDefinition(resolved).family) == options.familyFilter);
    };

    struct ExportTrack {
        std::uint16_t partId{};
        VoiceId voice{VoiceId::Unspecified};
        juce::String name;
        double prominence{0.5};
        InstrumentSoundModel soundModel{InstrumentSoundModel::Generic};
    };
    std::vector<ExportTrack> exportTracks;
    for (const auto& part : pattern.parts) {
        if (std::none_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.partId == part.id && selected(note.voice, note.channel);
            })) continue;
        exportTracks.push_back({part.id, part.sourceVoice,
            "PULSO " + juce::String::fromUTF8(part.name.c_str()), part.prominence, part.soundModel});
    }
    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> legacyVoices{};
    for (const auto& note : pattern.notes) {
        if (note.partId != 0 || note.channel < 1 || note.channel > 16 ||
            !selected(note.voice, note.channel)) continue;
        legacyVoices[static_cast<std::size_t>(resolvedVoice(note.voice, note.channel))] = true;
    }
    for (std::size_t voiceIndex = 0; voiceIndex < legacyVoices.size(); ++voiceIndex)
        if (legacyVoices[voiceIndex]) {
            const auto voice = static_cast<VoiceId>(voiceIndex);
            exportTracks.push_back({0, voice, trackNameForVoice(voice), 0.5,
                                    InstrumentSoundModel::Generic});
        }

    auto exportedNotes = 0;
    for (const auto& target : exportTracks) {
        juce::MidiMessageSequence track;
        auto trackName = juce::MidiMessage::textMetaEvent(3, target.name);
        trackName.setTimeStamp(0.0);
        track.addEvent(trackName);
        const auto resetChannel = std::clamp(voiceDefinition(target.voice).midiChannel, 1, 16);
        auto initialPedalReset = juce::MidiMessage::controllerEvent(resetChannel, 64, 0);
        initialPedalReset.setTimeStamp(0.0);
        track.addEvent(initialPedalReset);

        const auto noteBelongsToTarget = [&](const NoteEvent& note) {
            return (target.partId == 0
                        ? note.partId == 0 && resolvedVoice(note.voice, note.channel) == target.voice
                        : note.partId == target.partId) &&
                   selected(note.voice, note.channel);
        };

        for (const auto& note : pattern.notes) {
            if ((target.partId == 0 ? note.partId != 0 || resolvedVoice(note.voice, note.channel) != target.voice
                                    : note.partId != target.partId) ||
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
        std::array<int, 128> lastControlValue{};
        std::array<double, 128> lastControlBeat{};
        lastControlValue.fill(-1);
        lastControlBeat.fill(-1.0e9);
        for (const auto& control : pattern.controls) {
            const auto belongs = target.partId == 0
                ? control.partId == 0 && resolvedVoice(control.voice, control.channel) == target.voice
                : control.partId == target.partId ||
                    (control.partId == 0 && resolvedVoice(control.voice, control.channel) == target.voice);
            if (!belongs ||
                !selected(control.voice, control.channel)) continue;
            const auto controller = std::clamp(control.controller, 0, 127);
            auto value = std::clamp(control.value, 0, 127);
            if (controller == 64) {
                // CC64 is an instrument capability, not a generic expression lane.
                // Never forward voice-level pedal into winds, brass, strings, basses,
                // percussion or short articulations. For compatible instruments,
                // rebuild each down-event as a bounded, note-aware phrase gesture.
                if (value < 64 ||
                    !supportsSustainPedal(target.soundModel, target.voice, target.partId))
                    continue;
                const auto span = maximumPedalSpan(target.soundModel);
                const auto sourceOn = std::clamp(control.beat, 0.0, pattern.lengthBeats);
                const auto firstNote = std::find_if(pattern.notes.begin(), pattern.notes.end(),
                    [&](const auto& note) {
                        return noteBelongsToTarget(note) &&
                               note.startBeat >= sourceOn - 0.0001 &&
                               note.startBeat < sourceOn + span;
                    });
                if (firstNote == pattern.notes.end()) continue;
                const auto pedalOnBeat = std::max(sourceOn, firstNote->startBeat);
                const auto hardOff = std::min(pattern.lengthBeats, pedalOnBeat + span);
                auto pedalPhraseEnd = firstNote->endBeat();
                for (auto note = std::next(firstNote); note != pattern.notes.end(); ++note) {
                    if (!noteBelongsToTarget(*note)) continue;
                    if (note->startBeat >= hardOff || note->startBeat > pedalPhraseEnd + 0.25) break;
                    pedalPhraseEnd = std::max(pedalPhraseEnd, note->endBeat());
                }
                const auto pedalOffBeat = std::min(hardOff, pedalPhraseEnd + 0.125);
                if (pedalOffBeat <= pedalOnBeat + 1.0 / ticksPerQuarterNote) continue;
                auto pedalOn = juce::MidiMessage::controllerEvent(
                    std::clamp(control.channel, 1, 16), 64, 96);
                pedalOn.setTimeStamp(pedalOnBeat * ticksPerQuarterNote);
                track.addEvent(pedalOn);
                auto pedalOff = juce::MidiMessage::controllerEvent(
                    std::clamp(control.channel, 1, 16), 64, 0);
                pedalOff.setTimeStamp(pedalOffBeat * ticksPerQuarterNote);
                track.addEvent(pedalOff);
                continue;
            }
            if (target.partId != 0 && controller == 11)
                value = std::clamp(static_cast<int>(std::lround(
                    value * (0.86 + target.prominence * 0.22))), 0, 127);
            else if (target.partId != 0 && controller == 74)
                value = std::clamp(value + (static_cast<int>(target.soundModel) % 7 - 3) * 3, 0, 127);
            const auto continuous = controller == 1 || controller == 11 || controller == 74;
            if (continuous && lastControlValue[static_cast<std::size_t>(controller)] >= 0 &&
                std::abs(value - lastControlValue[static_cast<std::size_t>(controller)]) < 3 &&
                control.beat - lastControlBeat[static_cast<std::size_t>(controller)] < 2.0)
                continue;
            auto message = juce::MidiMessage::controllerEvent(
                std::clamp(control.channel, 1, 16), controller, value);
            message.setTimeStamp(std::clamp(control.beat, 0.0, pattern.lengthBeats) *
                                 ticksPerQuarterNote);
            track.addEvent(message);
            lastControlValue[static_cast<std::size_t>(controller)] = value;
            lastControlBeat[static_cast<std::size_t>(controller)] = control.beat;
        }
        auto lastPressureValue = -1;
        auto lastPressureBeat = -1.0e9;
        for (const auto& expression : pattern.expressions) {
            const auto belongs = target.partId == 0
                ? expression.partId == 0 && resolvedVoice(expression.voice, expression.channel) == target.voice
                : expression.partId == target.partId ||
                    (expression.partId == 0 && resolvedVoice(expression.voice, expression.channel) == target.voice);
            if (!belongs ||
                !selected(expression.voice, expression.channel)) continue;
            if (expression.type == ExpressionEventType::ChannelPressure && lastPressureValue >= 0 &&
                std::abs(expression.value - lastPressureValue) < 3 &&
                expression.beat - lastPressureBeat < 1.0)
                continue;
            if (expression.type == ExpressionEventType::PolyAftertouch && target.partId != 0 &&
                std::none_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                    return note.partId == target.partId && note.pitch == expression.note &&
                           note.startBeat <= expression.beat + 0.001 && note.endBeat() >= expression.beat - 0.001;
                })) continue;
            const auto channel = std::clamp(expression.channel, 1, 16);
            auto expressionValue = expression.value;
            if (target.partId != 0 && expression.type != ExpressionEventType::PitchBend)
                expressionValue = std::clamp(static_cast<int>(std::lround(
                    expressionValue * (0.88 + target.prominence * 0.18))), 0, 127);
            juce::MidiMessage message;
            switch (expression.type) {
                case ExpressionEventType::PitchBend:
                    message = juce::MidiMessage::pitchWheel(channel,
                        std::clamp(expressionValue, 0, 16383));
                    break;
                case ExpressionEventType::ChannelPressure:
                    message = juce::MidiMessage::channelPressureChange(channel,
                        std::clamp(expressionValue, 0, 127));
                    break;
                case ExpressionEventType::PolyAftertouch:
                    message = juce::MidiMessage::aftertouchChange(channel,
                        std::clamp(expression.note, 0, 127),
                        std::clamp(expressionValue, 0, 127));
                    break;
            }
            message.setTimeStamp(std::clamp(expression.beat, 0.0, pattern.lengthBeats) *
                                 ticksPerQuarterNote);
            track.addEvent(message);
            if (expression.type == ExpressionEventType::ChannelPressure) {
                lastPressureValue = expression.value;
                lastPressureBeat = expression.beat;
            }
        }
        for (const auto [controller, value] : std::array<std::pair<int, int>, 3>{
                 std::pair{64, 0}, std::pair{1, 0}, std::pair{123, 0}}) {
            auto reset = juce::MidiMessage::controllerEvent(resetChannel, controller, value);
            reset.setTimeStamp(phraseEnd);
            track.addEvent(reset);
        }
        auto bendReset = juce::MidiMessage::pitchWheel(resetChannel, 8192);
        bendReset.setTimeStamp(phraseEnd);
        track.addEvent(bendReset);
        auto pressureReset = juce::MidiMessage::channelPressureChange(resetChannel, 0);
        pressureReset.setTimeStamp(phraseEnd);
        track.addEvent(pressureReset);
        addEndOfTrack(track, phraseEnd);
        track.sort();
        midiFile.addTrack(track);
    }

    if (exportedNotes == 0) return false;
    destination.getParentDirectory().createDirectory();
    juce::FileOutputStream output(destination);
    if (!output.openedOk()) return false;
    const auto written = midiFile.writeTo(output, 1);
    output.flush();
    const auto success = written && output.getStatus().wasOk();
    return success && writeCompanionManifest(pattern, destination, options);
}

} // namespace pulso::plugin
