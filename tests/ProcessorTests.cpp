#include "TestSupport.h"

#include "plugin/PluginProcessor.h"
#include "plugin/MidiExporter.h"
#include "plugin/AiComposer.h"
#include "plugin/PreviewSynth.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <map>
#include <set>
#include <thread>
#include <vector>

namespace pulso::plugin {
struct ProcessorTestAccess {
    static bool publish(PulsoAudioProcessor& processor, std::uint64_t serial) {
        PulsoAudioProcessor::RealtimePattern result;
        result.serial = serial;
        result.epoch = 1;
        return processor.pushGeneratedPattern(result);
    }

    static std::vector<std::uint64_t> drain(PulsoAudioProcessor& processor) {
        std::vector<std::uint64_t> serials;
        PulsoAudioProcessor::RealtimePattern result;
        while (processor.popGeneratedPattern(result)) serials.push_back(result.serial);
        return serials;
    }
};
} // namespace pulso::plugin

namespace {

class TestPlayHead final : public juce::AudioPlayHead {
public:
    double ppq{};
    double bpm{120.0};
    bool playing{};

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setPpqPosition(ppq);
        info.setBpm(bpm);
        info.setIsPlaying(playing);
        return info;
    }
};

bool containsNoteOn(const juce::MidiBuffer& midi) {
    return std::any_of(midi.begin(), midi.end(), [](const auto metadata) {
        return metadata.getMessage().isNoteOn();
    });
}

bool containsPanic(const juce::MidiBuffer& midi) {
    return std::any_of(midi.begin(), midi.end(), [](const auto metadata) {
        const auto message = metadata.getMessage();
        return message.isController() && message.getControllerNumber() == 123;
    });
}

float peakMagnitude(const juce::AudioBuffer<float>& audio) {
    auto peak = 0.0f;
    for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
        peak = std::max(peak, audio.getMagnitude(channel, 0, audio.getNumSamples()));
    return peak;
}

void advance(TestPlayHead& playHead, int samples, double sampleRate) {
    if (playHead.playing)
        playHead.ppq += static_cast<double>(samples) / sampleRate * playHead.bpm / 60.0;
}

std::vector<float> renderPreviewWithBlockSize(int blockSize) {
    constexpr auto totalSamples = 8192;
    pulso::plugin::PreviewSynth synth;
    synth.prepare(44100.0);
    synth.setSoundWorld(2);
    synth.setDrumKit(2);
    std::vector<std::pair<int, juce::MidiMessage>> timeline{
        {0, juce::MidiMessage::controllerEvent(3, 11, 104)},
        {0, juce::MidiMessage::noteOn(3, 48, static_cast<juce::uint8>(94))},
        {173, juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(112))},
        {907, juce::MidiMessage::noteOn(10, 42, static_cast<juce::uint8>(82))},
        {1201, juce::MidiMessage::noteOn(2, 67, static_cast<juce::uint8>(88))},
        {2231, juce::MidiMessage::noteOff(3, 48)},
        {3107, juce::MidiMessage::pitchWheel(2, 8576)},
        {4099, juce::MidiMessage::noteOff(2, 67)},
        {6003, juce::MidiMessage::allNotesOff(2)},
    };
    std::vector<float> rendered(static_cast<std::size_t>(totalSamples) * 2u);
    for (auto start = 0; start < totalSamples; start += blockSize) {
        const auto count = std::min(blockSize, totalSamples - start);
        juce::AudioBuffer<float> block(2, count);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& [absoluteSample, message] : timeline)
            if (absoluteSample >= start && absoluteSample < start + count)
                midi.addEvent(message, absoluteSample - start);
        synth.renderNextBlock(block, midi, 0, count);
        for (auto sample = 0; sample < count; ++sample) {
            rendered[static_cast<std::size_t>(start + sample) * 2u] = block.getSample(0, sample);
            rendered[static_cast<std::size_t>(start + sample) * 2u + 1u] = block.getSample(1, sample);
        }
    }
    return rendered;
}

} // namespace

int main(int argc, char** argv) {
    try {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const auto preview64 = renderPreviewWithBlockSize(64);
    const auto preview511 = renderPreviewWithBlockSize(511);
    require(preview64.size() == preview511.size(), "Preview block-size test produced inconsistent output sizes");
    auto maximumBlockDelta = 0.0f;
    for (std::size_t index = 0; index < preview64.size(); ++index)
        maximumBlockDelta = std::max(maximumBlockDelta, std::abs(preview64[index] - preview511[index]));
    require(maximumBlockDelta < 1.0e-6f,
            "Preview audio must be sample-identical across host buffer sizes; max delta=" +
                std::to_string(maximumBlockDelta));
    if (argc > 2 && juce::String(argv[1]) == "--audit-midi") {
        juce::File file(juce::String::fromUTF8(argv[2]));
        juce::FileInputStream input(file);
        juce::MidiFile midi;
        require(input.openedOk() && midi.readFrom(input), "Could not read MIDI audit target");
        const auto division = std::max(1, static_cast<int>(midi.getTimeFormat()));
        std::cout << "[MIDI] file=" << file.getFullPathName() << " tracks=" << midi.getNumTracks()
                  << " division=" << division << '\n';
        for (auto trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex) {
            const auto* track = midi.getTrack(trackIndex);
            std::map<std::pair<int, int>, std::vector<double>> active;
            auto noteOns = 0, orphanOffs = 0, overlaps = 0, controls = 0, sustainEvents = 0;
            auto sustain = 0, minimumPitch = 128, maximumPitch = -1;
            auto maximumDuration = 0.0, lastNote = 0.0, end = 0.0;
            auto notesReleasedUnderSustain = 0;
            auto maximumPedalExtension = 0.0;
            std::vector<double> pendingSustainReleases;
            std::vector<std::pair<double, int>> sustainTimeline;
            juce::String trackName{"Track " + juce::String(trackIndex)};
            for (auto eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex) {
                const auto message = track->getEventPointer(eventIndex)->message;
                const auto tick = message.getTimeStamp();
                end = std::max(end, tick);
                if (message.isTrackNameEvent()) {
                    trackName = message.getTextFromTextMetaEvent();
                } else if (message.isNoteOn()) {
                    const auto key = std::pair{message.getChannel(), message.getNoteNumber()};
                    if (!active[key].empty()) ++overlaps;
                    active[key].push_back(tick);
                    ++noteOns;
                    minimumPitch = std::min(minimumPitch, message.getNoteNumber());
                    maximumPitch = std::max(maximumPitch, message.getNoteNumber());
                    lastNote = std::max(lastNote, tick);
                } else if (message.isNoteOff()) {
                    const auto key = std::pair{message.getChannel(), message.getNoteNumber()};
                    if (active[key].empty()) ++orphanOffs;
                    else {
                        maximumDuration = std::max(maximumDuration, tick - active[key].front());
                        active[key].erase(active[key].begin());
                    }
                    if (sustain >= 64) {
                        ++notesReleasedUnderSustain;
                        pendingSustainReleases.push_back(tick);
                    }
                } else if (message.isController()) {
                    ++controls;
                    if (message.getControllerNumber() == 64) {
                        ++sustainEvents;
                        sustain = message.getControllerValue();
                        sustainTimeline.emplace_back(tick, sustain);
                        if (sustain < 64) {
                            for (const auto release : pendingSustainReleases)
                                maximumPedalExtension = std::max(maximumPedalExtension, tick - release);
                            pendingSustainReleases.clear();
                        }
                    }
                }
            }
            auto dangling = std::size_t{};
            for (const auto& [key, values] : active) dangling += values.size();
            std::cout << "[TRACK] index=" << trackIndex << " name=\"" << trackName
                      << "\" notes=" << noteOns << " pitch="
                      << (maximumPitch < 0 ? "-" : std::to_string(minimumPitch) + "-" + std::to_string(maximumPitch))
                      << " overlaps=" << overlaps << " orphan_off=" << orphanOffs
                      << " dangling_on=" << dangling << " max_duration_beats="
                      << maximumDuration / division << " controls=" << controls
                      << " cc64=" << sustainEvents << " sustain_left_on=" << (sustain >= 64)
                      << " last_note_beat=" << lastNote / division << " end_beat=" << end / division << '\n';
            if (trackName.containsIgnoreCase("Low Horn")) {
                std::cout << "[SUSTAIN] released_under_pedal=" << notesReleasedUnderSustain
                          << " max_extension_beats=" << maximumPedalExtension / division << " timeline=";
                for (const auto& [tick, value] : sustainTimeline)
                    std::cout << tick / division << ':' << value << ',';
                std::cout << '\n';
            }
        }
        return 0;
    }
    if (argc > 1 && juce::String(argv[1]) == "--live-cancel") {
        juce::String error;
        pulso::SongPlan plan;
        std::stop_source stopSource;
        const auto started = std::chrono::steady_clock::now();
        std::jthread request([&] {
            plan = pulso::plugin::AiComposer::planSong(
                "A long progressive composition used to verify network cancellation",
                540, 270, 120.0, 4.0, 919191, stopSource.get_token(), error);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stopSource.request_stop();
        request.join();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        require(elapsed < std::chrono::seconds(4) && plan.sections.empty() &&
                    error.containsIgnoreCase("cancel"),
                "Cancelling a live OpenAI request must interrupt blocking network I/O promptly");
        std::cout << "[PASS] Live OpenAI cancellation | elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
        return 0;
    }
    if (argc > 1 && juce::String(argv[1]) == "--live-ai") {
        juce::String error;
        std::stop_source stopSource;
        const auto plan = pulso::plugin::AiComposer::planSong(
            "An evolving instrumental journey with restraint, thematic recall and a decisive resolution",
            60, 30, 120.0, 4.0, 424242, stopSource.get_token(), error);
        const auto authoredVoices = std::count_if(plan.voices.begin(), plan.voices.end(), [](const auto& voice) {
            return voice.performance.authored;
        });
        const auto sectionsWithTwoChords = std::count_if(plan.sections.begin(), plan.sections.end(), [](const auto& section) {
            return section.harmonicEvents.size() >= 2;
        });
        pulso::GenerationContext liveContext;
        liveContext.rootPitchClass = plan.rootPitchClass;
        liveContext.scale = plan.scale;
        liveContext.beatsPerBar = plan.beatsPerBar;
        liveContext.seed = plan.seed;
        pulso::CompositionRenderReport liveReport;
        const auto liveSong = pulso::SongComposer{}.render(plan, liveContext, {}, &liveReport);
        std::cout << "[INFO] Live plan | error=" << error << " voices=" << plan.voices.size()
                  << " authored_voices=" << authoredVoices << " instruments=" << plan.instruments.size()
                  << " sections=" << plan.sections.size() << " sections_with_2_chords=" << sectionsWithTwoChords
                  << " bars=" << plan.totalBars << " motifs=" << plan.rhythmMotifs.size()
                  << " chords=" << plan.chordPalette.size()
                  << " cells=" << plan.performanceScore.cells.size()
                  << " narrative=" << liveReport.narrative.score
                  << " coverage=" << liveReport.narrative.primaryVoiceCoverage
                  << " recall=" << liveReport.narrative.thematicRecallRatio << '\n';
        require(error.isEmpty(), "Live OpenAI song-plan request failed: " + error.toStdString());
        require(plan.sections.size() >= 3 && plan.voices.size() >= 7 && plan.instruments.size() >= 12 &&
                    plan.totalBars == 30 &&
                    plan.rhythmMotifs.size() >= 2 && !plan.rhythmLanguage.description.empty() &&
                    plan.chordPalette.size() >= 4 && !plan.harmonicLanguage.description.empty() &&
                    !liveSong.notes.empty() && !plan.performanceScore.cells.empty() &&
                    std::all_of(plan.performanceScore.cells.begin(), plan.performanceScore.cells.end(), [](const auto& cell) {
                        return !cell.themeId.empty() && !cell.narrativeFunction.empty();
                    }) && liveReport.narrative.primaryVoiceCoverage >= 0.45 &&
                    (!liveReport.narrative.foregroundExpected ||
                     (liveReport.narrative.foregroundNotes >= 8 &&
                      liveReport.narrative.foregroundAiAuthorshipRatio >= 0.85)) &&
                    (!liveReport.narrative.movementBassExpected ||
                     (liveReport.narrative.movementBassNotes >= 8 &&
                      liveReport.narrative.movementBassAiAuthorshipRatio >= 0.75)) &&
                    (liveReport.narrative.thematicPlacements < 3 ||
                     liveReport.narrative.thematicRecallRatio >= 0.35) &&
                    std::all_of(plan.voices.begin(), plan.voices.end(), [](const auto& voice) {
                        return voice.performance.authored &&
                               voice.performance.expressionDepth >= 0.0 &&
                               voice.performance.expressionDepth <= 1.0;
                    }) &&
                    std::all_of(plan.sections.begin(), plan.sections.end(), [](const auto& section) {
                        return !section.rhythm.motifId.empty() && section.harmonicEvents.size() >= 2;
                    }),
                "Live OpenAI response did not satisfy the dynamic-orchestration contract");
        std::cout << "[PASS] Live structured song plan | voices=" << plan.voices.size()
                  << " instruments=" << plan.instruments.size()
                  << " sections=" << plan.sections.size()
                  << " rhythm_motifs=" << plan.rhythmMotifs.size()
                  << " chords=" << plan.chordPalette.size() << '\n';
        return 0;
    }
    // The deterministic regression suite must never inherit a developer/user API key.
    // Network behavior is covered only by the explicit --live-ai and --live-cancel modes.
   #if JUCE_WINDOWS
    _putenv_s("OPENAI_API_KEY", "");
   #else
    unsetenv("OPENAI_API_KEY");
   #endif
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;

    std::array<float, 8> worldEnergy{};
    for (auto world = 0; world < static_cast<int>(worldEnergy.size()); ++world) {
        pulso::plugin::PreviewSynth drumPreview;
        drumPreview.prepare(sampleRate);
        drumPreview.setSoundWorld(world);
        juce::AudioBuffer<float> kitAudio(2, 8192);
        kitAudio.clear();
        juce::MidiBuffer kitMidi;
        kitMidi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(118)), 0);
        kitMidi.addEvent(juce::MidiMessage::noteOn(10, 42, static_cast<juce::uint8>(92)), 900);
        kitMidi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(108)), 1800);
        kitMidi.addEvent(juce::MidiMessage::noteOn(10, 46, static_cast<juce::uint8>(88)), 3000);
        drumPreview.renderNextBlock(kitAudio, kitMidi, 0, kitAudio.getNumSamples());
        for (auto channel = 0; channel < kitAudio.getNumChannels(); ++channel)
            for (auto sample = 0; sample < kitAudio.getNumSamples(); ++sample)
                worldEnergy[static_cast<std::size_t>(world)] += std::abs(kitAudio.getSample(channel, sample));
        require(worldEnergy[static_cast<std::size_t>(world)] > 1.0f,
                "Every preview sound world must render audible GM percussion");
    }
    const auto [quietestWorld, loudestWorld] = std::minmax_element(worldEnergy.begin(), worldEnergy.end());
    require(*loudestWorld - *quietestWorld > 1.0f,
            "Preview sound worlds must have measurably different sonic envelopes");

    std::array<std::array<float, 256>, 8> worldFingerprints{};
    for (auto world = 0; world < static_cast<int>(worldFingerprints.size()); ++world) {
        pulso::plugin::PreviewSynth fingerprintSynth;
        fingerprintSynth.prepare(sampleRate);
        fingerprintSynth.setSoundWorld(world);
        juce::AudioBuffer<float> fingerprintAudio(2, 4096);
        fingerprintAudio.clear();
        juce::MidiBuffer fingerprintMidi;
        fingerprintMidi.addEvent(juce::MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(110)), 0);
        fingerprintSynth.renderNextBlock(fingerprintAudio, fingerprintMidi, 0,
                                         fingerprintAudio.getNumSamples());
        for (auto sample = 0; sample < 256; ++sample)
            worldFingerprints[static_cast<std::size_t>(world)][static_cast<std::size_t>(sample)] =
                fingerprintAudio.getSample(0, 1024 + sample);
    }
    auto clearlyDifferentWorlds = 0;
    for (std::size_t world = 1; world < worldFingerprints.size(); ++world) {
        auto difference = 0.0f;
        for (std::size_t sample = 0; sample < worldFingerprints[world].size(); ++sample)
            difference += std::abs(worldFingerprints[world][sample] - worldFingerprints[0][sample]);
        if (difference / static_cast<float>(worldFingerprints[world].size()) > 0.001f)
            ++clearlyDifferentWorlds;
    }
    require(clearlyDifferentWorlds >= 6,
            "Sound worlds must switch oscillator families, not merely rename one Game Boy timbre");

    const auto renderKitFingerprint = [&](int kit, int note) {
        pulso::plugin::PreviewSynth synth;
        synth.prepare(sampleRate);
        synth.setDrumKit(kit);
        juce::AudioBuffer<float> audio(2, 2048);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(110)), 0);
        synth.renderNextBlock(audio, midi, 0, audio.getNumSamples());
        std::array<float, 512> fingerprint{};
        for (std::size_t sample = 0; sample < fingerprint.size(); ++sample)
            fingerprint[sample] = audio.getSample(0, static_cast<int>(sample + 32));
        return fingerprint;
    };
    for (const auto note : {36, 38, 42}) {
        const auto kit808 = renderKitFingerprint(0, note);
        const auto kit909 = renderKitFingerprint(1, note);
        auto kitDifference = 0.0f;
        for (std::size_t sample = 0; sample < kit808.size(); ++sample)
            kitDifference += std::abs(kit808[sample] - kit909[sample]);
        require(kitDifference / static_cast<float>(kit808.size()) > 0.005f,
                "808 and 909 kick, snare and hat must each use genuinely different models");
    }

    const auto renderVoiceOverride = [&](int note, pulso::VoiceId voice, int selection) {
        pulso::plugin::PreviewSynth synth;
        synth.prepare(sampleRate);
        synth.setDrumKit(1);
        synth.setVoiceTimbre(voice, selection);
        juce::AudioBuffer<float> audio(2, 2048);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(110)), 0);
        synth.renderNextBlock(audio, midi, 0, audio.getNumSamples());
        std::array<float, 512> fingerprint{};
        for (std::size_t sample = 0; sample < fingerprint.size(); ++sample)
            fingerprint[sample] = audio.getSample(0, static_cast<int>(sample + 32));
        return fingerprint;
    };
    const auto snare909 = renderVoiceOverride(38, pulso::VoiceId::SnareClap, 0);
    const auto snare808Override = renderVoiceOverride(38, pulso::VoiceId::SnareClap, 1);
    const auto hatUnaffected = renderVoiceOverride(42, pulso::VoiceId::SnareClap, 1);
    const auto hat909 = renderVoiceOverride(42, pulso::VoiceId::ClosedHats, 0);
    auto snareOverrideDifference = 0.0f;
    auto unrelatedHatDifference = 0.0f;
    for (std::size_t sample = 0; sample < snare909.size(); ++sample) {
        snareOverrideDifference += std::abs(snare909[sample] - snare808Override[sample]);
        unrelatedHatDifference += std::abs(hatUnaffected[sample] - hat909[sample]);
    }
    require(snareOverrideDifference / 512.0f > 0.005f && unrelatedHatDifference < 0.000001f,
            "A snare override must audibly change only snare while hats keep their own selection");

    {
        pulso::plugin::PreviewSynth denseSynth;
        denseSynth.prepare(sampleRate);
        juce::AudioBuffer<float> denseAudio(2, 4096);
        denseAudio.clear();
        juce::MidiBuffer denseMidi;
        for (auto note = 36; note < 96; ++note)
            denseMidi.addEvent(juce::MidiMessage::noteOn(3, note, static_cast<juce::uint8>(96)), 0);
        denseSynth.renderNextBlock(denseAudio, denseMidi, 0, denseAudio.getNumSamples());
        require(peakMagnitude(denseAudio) < 2.0f,
                "Dense orchestration must be gain-staged before the master limiter, not crushed by it");
    }

    const auto renderProcessorAudition = [&](int selection) {
        pulso::plugin::PulsoAudioProcessor auditionProcessor;
        TestPlayHead auditionPlayHead;
        auditionProcessor.setPlayHead(&auditionPlayHead);
        auditionProcessor.prepareToPlay(sampleRate, 8192);
        auditionProcessor.parameters.getParameter("preview")->setValueNotifyingHost(0.0f);
        auditionProcessor.setVoicePreviewTimbre(pulso::VoiceId::SnareClap, selection);
        auditionProcessor.auditionVoicePreview(pulso::VoiceId::SnareClap);
        juce::AudioBuffer<float> audio(2, 8192);
        audio.clear();
        juce::MidiBuffer midi;
        auditionProcessor.processBlock(audio, midi);
        std::array<float, 1024> fingerprint{};
        for (std::size_t sample = 0; sample < fingerprint.size(); ++sample)
            fingerprint[sample] = audio.getSample(0, static_cast<int>(sample + 32));
        auditionProcessor.releaseResources();
        auditionProcessor.setPlayHead(nullptr);
        return fingerprint;
    };
    const auto processorSnare808 = renderProcessorAudition(1);
    const auto processorSnare909 = renderProcessorAudition(2);
    auto processorAuditionDifference = 0.0f;
    for (std::size_t sample = 0; sample < processorSnare808.size(); ++sample)
        processorAuditionDifference += std::abs(processorSnare808[sample] - processorSnare909[sample]);
    require(processorAuditionDifference / 1024.0f > 0.001f,
            "Per-row host parameter changes must reach the audible processor preview end to end");

    for (const auto note : {38, 42}) {
        pulso::plugin::PreviewSynth roundRobinSynth;
        roundRobinSynth.prepare(sampleRate);
        roundRobinSynth.setDrumKit(1);
        juce::AudioBuffer<float> audio(2, 4096);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(105)), 0);
        midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(105)), 2048);
        roundRobinSynth.renderNextBlock(audio, midi, 0, audio.getNumSamples());
        auto repeatedHitDifference = 0.0f;
        for (auto sample = 32; sample < 544; ++sample)
            repeatedHitDifference += std::abs(audio.getSample(0, sample) -
                                               audio.getSample(0, sample + 2048));
        require(repeatedHitDifference / 512.0f > 0.0005f,
                "Repeated snare and hat hits must have stable analog variation, not cloned attacks");
    }

    const auto renderInstrumentFingerprint = [&](int family, int tone) {
        pulso::plugin::PreviewSynth synth;
        synth.prepare(sampleRate);
        const auto channel = family == 0 ? 1 : family == 1 ? 3 : 2;
        if (family == 0) synth.setBassTone(tone);
        else if (family == 1) synth.setHarmonyTone(tone);
        else synth.setMelodyTone(tone);
        juce::AudioBuffer<float> audio(2, 4096);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(channel, family == 0 ? 43 : 67,
                                                 static_cast<juce::uint8>(108)), 0);
        synth.renderNextBlock(audio, midi, 0, audio.getNumSamples());
        std::array<float, 512> fingerprint{};
        for (std::size_t sample = 0; sample < fingerprint.size(); ++sample)
            fingerprint[sample] = audio.getSample(0, static_cast<int>(sample + 1024));
        return fingerprint;
    };
    for (auto family = 0; family < 3; ++family) {
        const auto first = renderInstrumentFingerprint(family, 0);
        const auto last = renderInstrumentFingerprint(family, 3);
        auto difference = 0.0f;
        for (std::size_t sample = 0; sample < first.size(); ++sample)
            difference += std::abs(first[sample] - last[sample]);
        require(difference / static_cast<float>(first.size()) > 0.0005f,
                "Every instrument family selector must audibly change its synthesis model");
    }

    const auto renderVoiceControl = [&](int transpose, float levelDb) {
        pulso::plugin::PreviewSynth synth;
        synth.prepare(sampleRate);
        synth.setBassTone(0);
        synth.setVoiceTranspose(pulso::VoiceId::SubBass, transpose);
        synth.setVoiceLevelDb(pulso::VoiceId::SubBass, levelDb);
        juce::AudioBuffer<float> audio(2, 8192);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 43, static_cast<juce::uint8>(112)), 0);
        synth.renderNextBlock(audio, midi, 0, audio.getNumSamples());
        auto crossings = 0;
        auto energy = 0.0f;
        for (auto sample = 1025; sample < 8192; ++sample) {
            const auto previous = audio.getSample(0, sample - 1);
            const auto current = audio.getSample(0, sample);
            if ((previous < 0.0f && current >= 0.0f) || (previous >= 0.0f && current < 0.0f)) ++crossings;
            energy += std::abs(current);
        }
        return std::pair{crossings, energy};
    };
    const auto octaveDown = renderVoiceControl(-12, 0.0f);
    const auto originalRegister = renderVoiceControl(0, 0.0f);
    const auto octaveUp = renderVoiceControl(12, 0.0f);
    const auto quieterRegister = renderVoiceControl(0, -12.0f);
    require(originalRegister.first > octaveDown.first * 1.55 &&
                octaveUp.first > originalRegister.first * 1.55,
            "Per-lane -12/0/+12 controls must produce real octave changes in preview audio");
    require(quieterRegister.second < originalRegister.second * 0.32f &&
                quieterRegister.second > originalRegister.second * 0.18f,
            "Per-lane dB control must apply the expected independent preview gain");

    pulso::plugin::PreviewSynth switchedWorldSynth;
    pulso::plugin::PreviewSynth unchangedWorldSynth;
    switchedWorldSynth.prepare(sampleRate);
    unchangedWorldSynth.prepare(sampleRate);
    juce::MidiBuffer heldNoteMidi;
    heldNoteMidi.addEvent(juce::MidiMessage::noteOn(2, 64, static_cast<juce::uint8>(105)), 0);
    juce::AudioBuffer<float> switchedAudio(2, 2048);
    juce::AudioBuffer<float> unchangedAudio(2, 2048);
    switchedAudio.clear();
    unchangedAudio.clear();
    switchedWorldSynth.renderNextBlock(switchedAudio, heldNoteMidi, 0, 2048);
    unchangedWorldSynth.renderNextBlock(unchangedAudio, heldNoteMidi, 0, 2048);
    switchedWorldSynth.setSoundWorld(7);
    juce::MidiBuffer noNewNotes;
    switchedAudio.clear();
    unchangedAudio.clear();
    switchedWorldSynth.renderNextBlock(switchedAudio, noNewNotes, 0, 2048);
    unchangedWorldSynth.renderNextBlock(unchangedAudio, noNewNotes, 0, 2048);
    auto liveSwitchDifference = 0.0f;
    for (auto sample = 0; sample < 2048; ++sample)
        liveSwitchDifference += std::abs(switchedAudio.getSample(0, sample) -
                                         unchangedAudio.getSample(0, sample));
    require(liveSwitchDifference / 2048.0f > 0.001f,
            "Changing world must audibly replace the timbre of already sustained preview notes");

    auto renderWithExpression = [&](int expression) {
        pulso::plugin::PreviewSynth expressiveSynth;
        expressiveSynth.prepare(sampleRate);
        juce::AudioBuffer<float> expressiveAudio(2, 4096);
        expressiveAudio.clear();
        juce::MidiBuffer expressiveMidi;
        expressiveMidi.addEvent(juce::MidiMessage::controllerEvent(2, 11, expression), 0);
        expressiveMidi.addEvent(juce::MidiMessage::controllerEvent(2, 74, expression), 0);
        expressiveMidi.addEvent(juce::MidiMessage::noteOn(2, 67, static_cast<juce::uint8>(105)), 1);
        expressiveSynth.renderNextBlock(expressiveAudio, expressiveMidi, 0, expressiveAudio.getNumSamples());
        auto energy = 0.0f;
        for (auto sample = 0; sample < expressiveAudio.getNumSamples(); ++sample)
            energy += std::abs(expressiveAudio.getSample(0, sample));
        return energy;
    };
    require(renderWithExpression(112) > renderWithExpression(32) * 2.0f,
            "Preview must interpret CC11/74 dynamics instead of flattening the composed expression");

    auto renderPitchGesture = [&](int bend, int pressure) {
        pulso::plugin::PreviewSynth expressiveSynth;
        expressiveSynth.prepare(sampleRate);
        juce::AudioBuffer<float> expressiveAudio(2, 4096);
        expressiveAudio.clear();
        juce::MidiBuffer expressiveMidi;
        expressiveMidi.addEvent(juce::MidiMessage::pitchWheel(2, bend), 0);
        expressiveMidi.addEvent(juce::MidiMessage::channelPressureChange(2, pressure), 0);
        expressiveMidi.addEvent(juce::MidiMessage::noteOn(2, 67, static_cast<juce::uint8>(105)), 1);
        expressiveMidi.addEvent(juce::MidiMessage::aftertouchChange(2, 67, pressure), 2);
        expressiveSynth.renderNextBlock(expressiveAudio, expressiveMidi, 0, expressiveAudio.getNumSamples());
        return expressiveAudio;
    };
    const auto neutralGesture = renderPitchGesture(8192, 0);
    const auto expressiveGesture = renderPitchGesture(12288, 100);
    auto gestureDifference = 0.0f;
    for (auto sample = 0; sample < neutralGesture.getNumSamples(); ++sample)
        gestureDifference += std::abs(neutralGesture.getSample(0, sample) -
                                      expressiveGesture.getSample(0, sample));
    require(gestureDifference / neutralGesture.getNumSamples() > 0.001f,
            "Preview must audibly interpret pitch bend, pressure and poly-aftertouch");

    pulso::plugin::PreviewSynth ensemblePreview;
    ensemblePreview.prepare(sampleRate);
    ensemblePreview.setSoundWorld(0);
    juce::AudioBuffer<float> ensemblePreviewAudio(2, 16384);
    ensemblePreviewAudio.clear();
    juce::MidiBuffer ensemblePreviewMidi;
    for (auto channel = 1; channel <= 9; ++channel)
        ensemblePreviewMidi.addEvent(juce::MidiMessage::noteOn(channel, 36 + channel * 5,
                                                               static_cast<juce::uint8>(96)), channel * 120);
    ensemblePreview.renderNextBlock(ensemblePreviewAudio, ensemblePreviewMidi, 0,
                                    ensemblePreviewAudio.getNumSamples());
    require(ensemblePreviewAudio.getMagnitude(0, 0, ensemblePreviewAudio.getNumSamples()) > 0.001f &&
                ensemblePreviewAudio.getMagnitude(1, 0, ensemblePreviewAudio.getNumSamples()) > 0.001f,
            "All nine tonal roles must render through the multitimbral stereo preview");
    const auto renderInstrumentModel = [&](pulso::InstrumentSoundModel model, int channel, int note) {
        pulso::plugin::PreviewSynth synth;
        synth.prepare(sampleRate);
        juce::AudioBuffer<float> output(2, 4096);
        output.clear();
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(channel, 119, static_cast<int>(model)), 0);
        events.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(108)), 1);
        synth.renderNextBlock(output, events, 0, output.getNumSamples());
        return output;
    };
    const auto pianoPreview = renderInstrumentModel(pulso::InstrumentSoundModel::Piano, 3, 64);
    const auto stringPreview = renderInstrumentModel(pulso::InstrumentSoundModel::HighStrings, 3, 64);
    auto instrumentDifference = 0.0f;
    for (auto sample = 0; sample < pianoPreview.getNumSamples(); ++sample)
        instrumentDifference += std::abs(pianoPreview.getSample(0, sample) - stringPreview.getSample(0, sample));
    require(instrumentDifference / pianoPreview.getNumSamples() > 0.0005f,
            "InstrumentPart identities must create audibly distinct preview models on the same role and pitch");

    pulso::plugin::PulsoAudioProcessor processor;
    require(processor.liveDeploymentMode() ==
                pulso::plugin::PulsoAudioProcessor::LiveDeploymentMode::FullOrchestration,
            "Full orchestration must be the default Live deployment mode");
    require(std::abs(processor.parameters.getRawParameterValue("space")->load()) < 0.0001f &&
                std::abs(processor.parameters.getRawParameterValue("groove")->load()) < 0.0001f,
            "Retired Space and Groove controls must always default to zero");
    require(processor.parameters.getParameter("previewWorld") != nullptr,
            "The selectable preview sound world must be a persistent host parameter");
    require(processor.parameters.getParameter("previewDrumKit") != nullptr &&
                processor.parameters.getParameter("previewBassTone") != nullptr &&
                processor.parameters.getParameter("previewHarmonyTone") != nullptr &&
                processor.parameters.getParameter("previewMelodyTone") != nullptr &&
                static_cast<int>(processor.parameters.getRawParameterValue("previewDrumKit")->load()) == 1,
            "Drum and tonal instrument choices must be persistent, with 909 as the quality-first default");
    for (std::size_t voice = 0; voice < static_cast<std::size_t>(pulso::VoiceId::Count); ++voice) {
        const auto id = juce::String("previewVoice") + juce::String(static_cast<int>(voice)).paddedLeft('0', 2);
        const auto octaveId = juce::String("previewOctave") + juce::String(static_cast<int>(voice)).paddedLeft('0', 2);
        const auto levelId = juce::String("previewLevel") + juce::String(static_cast<int>(voice)).paddedLeft('0', 2);
        require(processor.parameters.getParameter(id) != nullptr &&
                    processor.parameters.getParameter(octaveId) != nullptr &&
                    processor.parameters.getParameter(levelId) != nullptr &&
                    pulso::plugin::PulsoAudioProcessor::voicePreviewTimbreChoices(
                        static_cast<pulso::VoiceId>(voice)).size() == 5,
                "Every visible lane must expose persistent sound, octave and level controls");
    }
    require(processor.parameters.getParameter("performance") != nullptr &&
                processor.parameters.getRawParameterValue("performance")->load() < 0.5f,
            "Human Performance must be a persistent button that defaults to exact timing");
    require(processor.parameters.getParameter("language") != nullptr &&
                processor.uiLanguage() == pulso::plugin::UiLanguage::Spanish,
            "The complete interface language must be a persistent parameter and default to Spanish");
    const auto accentedTranslation = pulso::plugin::tr(
        pulso::plugin::UiLanguage::Spanish, pulso::plugin::TextId::Subtitle);
    require(accentedTranslation.containsChar(0x00d3),
            "Spanish accents must be decoded as real Unicode code points: " +
                accentedTranslation.toStdString());
    require(pulso::plugin::bullet().length() == 1 && pulso::plugin::bullet()[0] == 0x00b7,
            "The middle-dot separator must be one U+00B7 code point");
    {
        pulso::plugin::PulsoAudioProcessor continuityProcessor;
        TestPlayHead stoppedPlayHead;
        stoppedPlayHead.playing = false;
        continuityProcessor.setPlayHead(&stoppedPlayHead);
        continuityProcessor.prepareToPlay(sampleRate, blockSize);
        continuityProcessor.parameters.getParameter("preview")->setValueNotifyingHost(0.0f);
        continuityProcessor.auditionVoicePreview(pulso::VoiceId::Lead);
        juce::AudioBuffer<float> firstBlock(2, blockSize);
        juce::AudioBuffer<float> eventFreeBlock(2, blockSize);
        juce::MidiBuffer noMidi;
        firstBlock.clear();
        continuityProcessor.processBlock(firstBlock, noMidi);
        noMidi.clear();
        eventFreeBlock.clear();
        continuityProcessor.processBlock(eventFreeBlock, noMidi);
        require(peakMagnitude(firstBlock) > 0.0001f && peakMagnitude(eventFreeBlock) > 0.0001f,
                "The VST audio callback must render sustained voices through MIDI-empty host blocks");
        continuityProcessor.releaseResources();
        continuityProcessor.setPlayHead(nullptr);
    }
    processor.toggleVoiceSolo(pulso::VoiceId::Lead);
    require(processor.isVoiceSolo(pulso::VoiceId::Lead) &&
                processor.isVoiceAudible(pulso::VoiceId::Lead) &&
                !processor.isVoiceAudible(pulso::VoiceId::SubBass),
            "Solo must isolate one voice without modifying the composition");
    processor.toggleVoiceMute(pulso::VoiceId::Lead);
    require(processor.isVoiceMuted(pulso::VoiceId::Lead) &&
                !processor.isVoiceAudible(pulso::VoiceId::Lead),
            "Mute must take precedence over solo for predictable auditioning");
    processor.toggleVoiceMute(pulso::VoiceId::Lead);
    processor.toggleVoiceSolo(pulso::VoiceId::Lead);
    processor.setCreativeDirection("spacious dub echoes with restrained movement");
    require(processor.currentPreviewWorldName() == "DUB SPACE",
            "AUTO preview must translate creative-direction vocabulary into a sound world");
    TestPlayHead playHead;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    playHead.playing = true;
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(92)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(92)), 0);

    auto producedPattern = false;
    for (auto attempt = 0; attempt < 250 && !producedPattern; ++attempt) {
        processor.processBlock(audio, midi);
        producedPattern = containsNoteOn(midi);
        midi.clear();
        advance(playHead, blockSize, sampleRate);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(producedPattern, "The worker must publish a playable pattern without blocking audio");
    const auto processedTransportBeat = playHead.ppq -
        static_cast<double>(blockSize) / sampleRate * playHead.bpm / 60.0;
    require(processor.hasHostTransport() && processor.hostIsPlaying() &&
                std::abs(processor.currentTransportBeat() - processedTransportBeat) < 0.000001,
            "The UI playhead must receive the exact host PPQ position through lock-free state");
    require(peakMagnitude(audio) <= 1.0f, "Preview output must remain below digital full scale");
    const auto composedPattern = processor.currentPattern();
    require(composedPattern != nullptr, "The processor must expose its composed phrase");
    require(std::all_of(composedPattern->notes.begin(), composedPattern->notes.end(), [](const auto& note) {
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.000001 &&
                       std::abs(note.endBeat() * 16.0 - std::round(note.endBeat() * 16.0)) < 0.000001;
            }), "The processor must publish exact onsets with fine expressive note-off timing");
    for (const auto channel : {1, 2, 3, 10})
        require(std::any_of(composedPattern->notes.begin(), composedPattern->notes.end(),
                            [=](const auto& note) { return note.channel == channel; }),
                "Every idea must coordinate harmony, bass, melody and drums");

    const auto identityBeforeCancel = processor.currentCompositionSeed();
    const auto variationBeforeCancel = processor.currentVariationIndex();
    const auto titleBeforeCancel = processor.currentIdeaTitle();
    const auto notesBeforeCancel = composedPattern->notes;
    processor.setTargetSongDurationSeconds(540);
    processor.requestGenerateIdea();
    require(processor.isComposing(), "A requested song must enter a cancellable composing state");
    processor.cancelGeneration();
    for (auto attempt = 0; attempt < 120 && processor.isComposing(); ++attempt) {
        midi.clear();
        processor.processBlock(audio, midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!processor.isComposing() &&
                processor.currentCompositionSeed() == identityBeforeCancel &&
                processor.currentVariationIndex() == variationBeforeCancel &&
                processor.currentIdeaTitle() == titleBeforeCancel &&
                processor.currentPattern()->notes == notesBeforeCancel,
            "Cancel must be transactional: keep MIDI, metadata and idea lineage");
    require(processor.currentAiStatus().containsIgnoreCase("cancelled"),
            "A cancelled operation must finish with an explicit non-blocking status");
    processor.setTargetSongDurationSeconds(0);

    // Live may suspend processBlock while its transport/device is reconfiguring. Fill the
    // three usable ring slots, then prove later publications coalesce without blocking.
    {
        pulso::plugin::PulsoAudioProcessor suspendedHost;
        for (std::uint64_t publication = 1; publication <= 7; ++publication)
            require(pulso::plugin::ProcessorTestAccess::publish(suspendedHost, publication),
                    "A suspended audio callback must not reject or block result publication");
        const auto published = pulso::plugin::ProcessorTestAccess::drain(suspendedHost);
        require(published == std::vector<std::uint64_t>({1, 2, 3, 7}),
                "A full result queue must retain queued work and coalesce overflow to the newest score");
    }

    const auto exportFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("PULSO Test Exports");
    const auto ensembleFile = exportFolder.getNonexistentChildFile("ensemble", ".mid", false);
    pulso::plugin::MidiExportOptions exportOptions;
    exportOptions.bpm = 127.5;
    exportOptions.clipName = "PULSO Export Test";
    require(pulso::plugin::writePatternToMidiFile(*composedPattern, ensembleFile, exportOptions),
            "The current composition must export to a standard MIDI file");
    juce::FileInputStream ensembleInput(ensembleFile);
    juce::MidiFile ensembleMidi;
    require(ensembleInput.openedOk() && ensembleMidi.readFrom(ensembleInput),
            "The exported MIDI file must be readable by a DAW");
    require(ensembleMidi.getTimeFormat() == 960 && ensembleMidi.getNumTracks() == 5,
            "Ensemble export must contain conductor, harmony, melody, bass and drums tracks");

    const auto structuredExample = juce::String(R"json({
      "title":"Afterglow","key":"C minor","summary":"A clear four-layer phrase","bars":1,
      "harmony":[{"start":0,"duration":4,"pitch":60,"velocity":70}],
      "melody":[{"start":0,"duration":1,"pitch":67,"velocity":92}],
      "bass":[{"start":0,"duration":2,"pitch":36,"velocity":88}],
      "drums":[{"start":0,"duration":0.25,"pitch":36,"velocity":100},
               {"start":1,"duration":0.25,"pitch":39,"velocity":92},
               {"start":1.5,"duration":0.125,"pitch":42,"velocity":72},
               {"start":2.5,"duration":0.25,"pitch":46,"velocity":76}]
    })json");
    pulso::plugin::AiComposition parsedComposition;
    juce::String parseError;
    require(pulso::plugin::AiComposer::structuredOutputSchemaIsValid(),
            "The schema sent to OpenAI must be valid JSON");
    require(pulso::plugin::AiComposer::parseCompositionJson(structuredExample, 1,
                                                             parsedComposition, parseError),
            "Structured GPT output must validate into a playable composition");
    require(parsedComposition.pattern.notes.size() == 7 && parsedComposition.title == "Afterglow" &&
                std::any_of(parsedComposition.pattern.notes.begin(), parsedComposition.pattern.notes.end(),
                    [](const auto& note) { return note.voice == pulso::VoiceId::SnareClap; }) &&
                std::any_of(parsedComposition.pattern.notes.begin(), parsedComposition.pattern.notes.end(),
                    [](const auto& note) { return note.voice == pulso::VoiceId::ClosedHats; }) &&
                std::any_of(parsedComposition.pattern.notes.begin(), parsedComposition.pattern.notes.end(),
                    [](const auto& note) { return note.voice == pulso::VoiceId::OpenHatsShaker; }),
            "Structured composition metadata and all four layers must survive parsing");

    const auto songPlanExample = juce::String(R"json({
      "title":"The Long Return","key":"F# major","summary":"A complete dramatic arc",
      "root_pitch_class":2,"mode":"minor","rhythm_language":{"description":"A displaced acoustic-electric dialogue","pulse_stability":0.42,"backbeat_gravity":0.78,"syncopation":0.64,"ghost_density":0.32,"velocity_contrast":0.71,"timing_freedom":0.28,"orchestration_motion":0.67,"silence_bias":0.35,"call_response":0.74},
      "harmonic_language":{"description":"Dark gravity with one luminous modal pivot","tonal_gravity":0.68,"modal_fluidity":0.52,"chromaticism":0.41,"extension_richness":0.76,"inversion_motion":0.64,"voice_leading_smoothness":0.84,"harmonic_rhythm_activity":0.46,"pedal_tone_affinity":0.58,"ambiguity":0.61,"cadence_strength":0.72},
      "chord_palette":[
        {"id":"dm9","label":"D minor ninth","root_pitch_class":2,"bass_pitch_class":2,"pitch_classes":[0,2,5,9],"function":"tonic","voicing":"open","tension":0.18},
        {"id":"ebmaj7_g","label":"Eb major seven over G","root_pitch_class":3,"bass_pitch_class":7,"pitch_classes":[2,3,7,10],"function":"chromatic","voicing":"drop_2","tension":0.62},
        {"id":"a7sus","label":"A suspended dominant","root_pitch_class":9,"bass_pitch_class":9,"pitch_classes":[2,4,7,9],"function":"dominant","voicing":"quartal","tension":0.81},
        {"id":"gpedal","label":"G pedal colour","root_pitch_class":5,"bass_pitch_class":7,"pitch_classes":[0,5,7,10],"function":"pedal","voicing":"cluster","tension":0.54}
      ],"motif_intervals":[0,3,7,5],"rhythm_motifs":[{
        "id":"A","bars":1,"steps_per_bar":16,"kick":"1000100010001000","snare_clap":"0000200000002000",
        "closed_hats":"0010001000100010","open_hats_shaker":"0000001000000020",
        "low_percussion":"0000010000010000","high_percussion":"0001000000100000",
        "ornaments":[{"step":14,"instrument":"tom_mid","velocity":76,"duration_steps":0.75}]
      }],"voices":[
        {"id":"core_drums","function":"Pulse anchor","interaction":"Leaves space at cadences","activity":0.8,"syncopation":0.4,"minimum_pitch":35,"maximum_pitch":81},
        {"id":"sub_bass","function":"Tonal gravity","interaction":"Supports harmonic rhythm","activity":0.7,"syncopation":0.2,"minimum_pitch":28,"maximum_pitch":48},
        {"id":"movement_bass","function":"Forward motion","interaction":"Answers the sub bass","activity":0.5,"syncopation":0.5,"minimum_pitch":36,"maximum_pitch":62},
        {"id":"harmonic_foundation","function":"Voice-led foundation","interaction":"Frames the lead","activity":0.6,"syncopation":0.1,"minimum_pitch":45,"maximum_pitch":76},
        {"id":"harmonic_pulse","function":"Rhythmic harmony","interaction":"Answers percussion","activity":0.5,"syncopation":0.6,"minimum_pitch":50,"maximum_pitch":84},
        {"id":"lead","function":"Carries the motif","interaction":"Alternates with countermelody","activity":0.6,"syncopation":0.4,"minimum_pitch":55,"maximum_pitch":92,"performance_intent":"Sing with a restrained late bloom","articulation":"legato","dynamic_contour":"phrase_arc","vibrato":"late_expressive","pitch_gesture":"gentle_bends","expression_depth":0.72,"brightness":0.61,"humanization":0.48,"sustain_pedal":false},
        {"id":"atmosphere","function":"Long-range depth","interaction":"Bridges sparse sections","activity":0.4,"syncopation":0.0,"minimum_pitch":42,"maximum_pitch":92}
      ],"performance_score":{"cells":[{"id":"development_lead_answer","length_beats":4,"owned_voices":["lead"],"notes":[{"beat":0.375,"duration":0.625,"pitch":62,"velocity":91,"voice":"lead","metric_intent":"deliberate_displacement"},{"beat":2.25,"duration":0.5,"pitch":65,"velocity":76,"voice":"lead","metric_intent":"strict_grid"}],"controls":[{"beat":0,"controller":11,"value":72,"voice":"lead"},{"beat":2.25,"controller":11,"value":104,"voice":"lead"}]}],"placements":[{"cell_id":"development_lead_answer","section_index":1,"start_beat":0,"repeats":16,"transpose":0,"velocity_scale":1,"time_scale":1,"purpose":"Cello answers and transforms the lead question","voice_map":[{"from":"lead","to":"countermelody"}],"retrograde":false,"invert_contour":true,"inversion_axis":64,"fragment_start":0,"fragment_end":4,"metric_intent":"strict_grid"}]},"sections":[
        {"name":"Prologue","function":"Introduce motif fragments","harmonic_direction":"Tonic ambiguity","motif_treatment":"Fragment","bars":8,"energy":0.2,"tension":0.2,"density":0.3,"motif_variant":0,"tonal_center_pitch_class":2,"mode_hint":"D minor with suspended colour","harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"dm9","emphasis":0.4,"purpose":"Establish home"},{"bar_offset":4,"beat_offset":0,"chord_id":"gpedal","emphasis":0.55,"purpose":"Open ambiguity"}],"active_voices":["harmonic_foundation","lead","atmosphere"],"kick_state":"reduced","kick_continuity":"sectional","percussion_density":0.2,"rhythmic_syncopation":0.2,"swing":0.08,"rhythm_motif_id":"A","rhythm_mutations":[],"rhythm_gestures":[]},
        {"name":"Development","function":"Transform the theme","harmonic_direction":"Move away from tonic","motif_treatment":"Sequence","bars":16,"energy":0.7,"tension":0.8,"density":0.7,"motif_variant":2,"tonal_center_pitch_class":3,"mode_hint":"Eb luminous pivot","harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"ebmaj7_g","emphasis":0.72,"purpose":"Pivot chromatically"},{"bar_offset":7,"beat_offset":2,"chord_id":"a7sus","emphasis":0.86,"purpose":"Create dominant suspension"}],"active_voices":["core_drums","sub_bass","harmonic_foundation","harmonic_pulse","lead"],"kick_state":"four_on_floor","kick_continuity":"required","percussion_density":0.7,"rhythmic_syncopation":0.5,"swing":0.1,"rhythm_motif_id":"A","rhythm_mutations":[{"bar_offset":6,"lane":"low_percussion","operation":"shift","step":5,"amount":1,"velocity":72,"purpose":"Answer the phrase"}],"rhythm_gestures":[{"bar_offset":7,"type":"drop_last_kick","beat":3,"intensity":0.6},{"bar_offset":15,"type":"double_kick","beat":3.75,"intensity":0.75}]},
        {"name":"Coda","function":"Resolve the argument","harmonic_direction":"Final tonic","motif_treatment":"Cadential recall","bars":8,"energy":0.3,"tension":0.1,"density":0.3,"motif_variant":0,"tonal_center_pitch_class":2,"mode_hint":"D minor home","harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"a7sus","emphasis":0.58,"purpose":"Prepare resolution"},{"bar_offset":4,"beat_offset":0,"chord_id":"dm9","emphasis":0.9,"purpose":"Resolve home"}],"active_voices":["sub_bass","harmonic_foundation","lead","atmosphere"],"kick_state":"muted","kick_continuity":"sectional","percussion_density":0.2,"rhythmic_syncopation":0.1,"swing":0.05,"rhythm_motif_id":"A","rhythm_mutations":[],"rhythm_gestures":[]}
      ]
    })json");
    pulso::SongPlan parsedSongPlan;
    require(pulso::plugin::AiComposer::songPlanSchemaIsValid() &&
                pulso::plugin::AiComposer::parseSongPlanJson(songPlanExample, 64, 32, 120.0,
                                                              4.0, 77, parsedSongPlan, parseError,
                                                              pulso::TonalPolicy::Expanded),
            "Structured GPT song architecture must validate independently from MIDI rendering");
    require(parsedSongPlan.sections.size() == 3 && parsedSongPlan.voices.size() >= 10 &&
                parsedSongPlan.instruments.size() >= 12 &&
                !parsedSongPlan.instrumentCastAuthored &&
                parsedSongPlan.sections[1].activeVoices.size() >= 8 && parsedSongPlan.totalBars == 32 &&
                parsedSongPlan.sections.back().startBar == 24 && parsedSongPlan.key == "D minor" &&
                parsedSongPlan.sections[1].rhythm.kickState == pulso::KickState::FourOnFloor &&
                parsedSongPlan.rhythmMotifs.size() == 1 &&
                parsedSongPlan.rhythmLanguage.description == "A displaced acoustic-electric dialogue" &&
                parsedSongPlan.rhythmMotifs.front().ornaments.size() == 1 &&
                parsedSongPlan.rhythmMotifs.front().ornaments.front().instrument == pulso::RhythmInstrument::TomMid &&
                parsedSongPlan.harmonicLanguage.description == "Dark gravity with one luminous modal pivot" &&
                parsedSongPlan.chordPalette.size() == 4 &&
                parsedSongPlan.chordPalette[1].pitchClasses == std::vector<int>({2, 3, 7, 10}) &&
                parsedSongPlan.chordPalette[1].bassPitchClass == 7 &&
                parsedSongPlan.chordPalette[1].voicing == pulso::VoicingStrategy::Drop2 &&
                parsedSongPlan.sections[1].tonalCenterPitchClass == 3 &&
                parsedSongPlan.sections[1].harmonicEvents.size() == 2 &&
                std::abs(parsedSongPlan.sections[1].harmonicEvents[1].beatOffset - 2.0) < 0.001 &&
                parsedSongPlan.sections[1].rhythm.mutations.size() == 1 &&
                parsedSongPlan.sections[1].rhythm.gestures.size() == 2 &&
                parsedSongPlan.performanceScore.cells.size() == 1 &&
                parsedSongPlan.performanceScore.placements.size() == 1 &&
                parsedSongPlan.performanceScore.placements.front().invertContour &&
                parsedSongPlan.performanceScore.placements.front().voiceMap.size() == 1 &&
                parsedSongPlan.performanceScore.placements.front().voiceMap.front().to ==
                    pulso::VoiceId::Countermelody &&
                std::any_of(parsedSongPlan.voices.begin(), parsedSongPlan.voices.end(), [](const auto& voice) {
                    return voice.id == pulso::VoiceId::Lead && voice.performance.authored &&
                           voice.performance.vibrato == pulso::VibratoStyle::LateExpressive &&
                           voice.performance.pitchGesture == pulso::PitchGesture::GentleBends &&
                           std::abs(voice.performance.expressionDepth - 0.72) < 0.001;
                }),
            "Validated song architecture must preserve voices and contiguous section metadata");
    pulso::GenerationContext authoredPerformanceContext;
    authoredPerformanceContext.role = pulso::Role::Ensemble;
    authoredPerformanceContext.seed = parsedSongPlan.seed;
    const auto authoredPerformance = pulso::SongComposer{}.render(parsedSongPlan,
                                                                   authoredPerformanceContext);
    require(std::any_of(authoredPerformance.expressions.begin(), authoredPerformance.expressions.end(),
                [](const auto& event) {
                    return event.voice == pulso::VoiceId::Countermelody &&
                           event.type == pulso::ExpressionEventType::PitchBend && event.value != 8192;
                }) && std::any_of(authoredPerformance.expressions.begin(), authoredPerformance.expressions.end(),
                [](const auto& event) {
                    return event.voice == pulso::VoiceId::Countermelody &&
                           event.type == pulso::ExpressionEventType::PolyAftertouch;
                }),
            "AI-authored remapped performance must reach its audible target with bends and aftertouch");
    require(std::any_of(authoredPerformance.notes.begin(), authoredPerformance.notes.end(),
                [](const auto& note) {
                    return note.voice == pulso::VoiceId::Countermelody &&
                           std::abs(note.startBeat - 32.5) < 0.001;
                }),
            "The explicit AI performance must preserve its mapped phrase on the publication grid");
    require(std::none_of(authoredPerformance.notes.begin(), authoredPerformance.notes.end(),
                [](const auto& note) {
                    return note.voice == pulso::VoiceId::Countermelody &&
                           note.startBeat >= 32.0 && note.startBeat < 48.0 &&
                           std::abs(std::fmod(note.startBeat - 32.0, 4.0) - 0.5) > 0.001 &&
                           std::abs(std::fmod(note.startBeat - 32.0, 4.0) - 2.25) > 0.001;
                }),
            "The explicit AI performance must replace only the mapped response voice");
    const auto authoredSubBarBeat = (8.0 + 7.0) * 4.0 + 2.0;
    require(std::any_of(authoredPerformance.notes.begin(), authoredPerformance.notes.end(),
                [&](const auto& note) {
                    return note.voice == pulso::VoiceId::HarmonicFoundation &&
                           std::abs(note.startBeat - authoredSubBarBeat) < 0.001 &&
                           std::find(parsedSongPlan.chordPalette[2].pitchClasses.begin(),
                                     parsedSongPlan.chordPalette[2].pitchClasses.end(),
                                     pulso::positiveModulo(note.pitch, 12)) !=
                               parsedSongPlan.chordPalette[2].pitchClasses.end();
                }),
            "A GPT-authored sub-bar chord must survive rendering and tonal validation into exported MIDI notes");

    const auto orchestrationPlan = pulso::SongComposer::createLocalPlan(
        "Deep symphonic orchestral long-form test", 120, 120.0, 4.0, 8841, 2, pulso::ScaleKind::Minor);
    pulso::GenerationContext orchestrationFoundation;
    orchestrationFoundation.role = pulso::Role::Ensemble;
    orchestrationFoundation.seed = orchestrationPlan.seed;
    pulso::CompositionRenderReport orchestrationReport;
    const auto orchestration = pulso::SongComposer{}.render(orchestrationPlan,
                                                             orchestrationFoundation, {},
                                                             &orchestrationReport);
    require(orchestrationReport.harmonicWindows >= orchestrationPlan.sections.size() &&
                orchestrationReport.productionReady(),
            "A full arrangement must pass the exact-timeline tonal audit before publication");
    const auto orchestrationFile = exportFolder.getNonexistentChildFile("orchestration", ".mid", false);
    exportOptions.channelFilter = 0;
    exportOptions.clipName = "PULSO Orchestration Test";
    exportOptions.includeKeySignature = true;
    exportOptions.rootPitchClass = orchestrationPlan.rootPitchClass;
    exportOptions.scale = orchestrationPlan.scale;
    exportOptions.chordMarkers = {{0.0, orchestrationPlan.chordPalette.front().label}};
    require(pulso::plugin::writePatternToMidiFile(orchestration, orchestrationFile, exportOptions),
            "A dynamically orchestrated song must export as standard multitrack MIDI");
    juce::FileInputStream orchestrationInput(orchestrationFile);
    juce::MidiFile orchestrationMidi;
    std::set<std::uint16_t> populatedParts;
    std::set<pulso::VoiceId> populatedLegacyVoices;
    for (const auto& note : orchestration.notes) {
        if (note.partId > 0) populatedParts.insert(note.partId);
        else populatedLegacyVoices.insert(note.voice);
    }
    const auto expectedOrchestralTracks = 1 + static_cast<int>(populatedParts.size() +
                                                               populatedLegacyVoices.size());
    require(orchestrationInput.openedOk() && orchestrationMidi.readFrom(orchestrationInput) &&
                orchestrationMidi.getNumTracks() == expectedOrchestralTracks &&
                expectedOrchestralTracks > 16 && populatedLegacyVoices.empty(),
            "Full-song export must create one DAW track per populated orchestral instrument");
    require(orchestrationFile.withFileExtension(".pulso.json").existsAsFile(),
            "Every orchestral MIDI export must include a companion Ableton rack-assignment manifest");
    std::set<std::uint16_t> melodicParts;
    std::set<std::uint16_t> harmonicParts;
    for (const auto& note : orchestration.notes) {
        const auto part = std::find_if(orchestration.parts.begin(), orchestration.parts.end(),
            [&](const auto& candidate) { return candidate.id == note.partId; });
        if (part == orchestration.parts.end()) continue;
        if (part->department == pulso::ScoreDepartment::Melody) melodicParts.insert(part->id);
        if (part->department == pulso::ScoreDepartment::Harmony) harmonicParts.insert(part->id);
    }
    require(melodicParts.size() >= 2 && harmonicParts.size() >= 4 &&
                orchestrationReport.orchestration.foregroundChanges >= orchestrationPlan.sections.size() &&
                orchestrationReport.orchestration.chamberSections > 0,
            "The orchestral score must distribute real material, rotate foreground and include chamber breathing");
    require(!orchestration.controls.empty() &&
                !orchestration.expressions.empty() &&
                orchestration.markers.size() == orchestrationPlan.sections.size(),
            "Long-form export must retain expressive CC data and structural section markers");
    auto exportedPitchBend = false;
    auto exportedPressure = false;
    auto exportedAftertouch = false;
    auto exportedSustain = false;
    auto exportedKeySignature = false;
    auto exportedChordMarker = false;
    auto orphanNoteOffs = 0;
    auto invalidInstrumentPedals = 0;
    auto excessivePedalWindows = 0;
    for (auto track = 0; track < orchestrationMidi.getNumTracks(); ++track) {
        const auto* sequence = orchestrationMidi.getTrack(track);
        if (sequence == nullptr) continue;
        std::map<std::pair<int, int>, int> activeNotes;
        juce::String exportedTrackName;
        for (auto event = 0; event < sequence->getNumEvents(); ++event) {
            const auto message = sequence->getEventPointer(event)->message;
            if (message.isTrackNameEvent()) exportedTrackName = message.getTextFromTextMetaEvent();
        }
        const auto sourcePart = std::find_if(orchestration.parts.begin(), orchestration.parts.end(),
            [&](const auto& part) {
                return exportedTrackName == "PULSO " + juce::String::fromUTF8(part.name.c_str());
            });
        const auto pedalCapable = sourcePart != orchestration.parts.end() &&
            (sourcePart->soundModel == pulso::InstrumentSoundModel::Piano ||
             sourcePart->soundModel == pulso::InstrumentSoundModel::Harp ||
             sourcePart->soundModel == pulso::InstrumentSoundModel::AnalogPad ||
             sourcePart->soundModel == pulso::InstrumentSoundModel::PolySynth);
        auto pedalOnTick = -1.0;
        for (auto event = 0; event < sequence->getNumEvents(); ++event) {
            const auto message = sequence->getEventPointer(event)->message;
            const auto key = std::pair{message.getChannel(), message.getNoteNumber()};
            if (message.isNoteOn()) ++activeNotes[key];
            else if (message.isNoteOff()) {
                if (activeNotes[key] == 0) ++orphanNoteOffs;
                else --activeNotes[key];
            }
            exportedPitchBend = exportedPitchBend || message.isPitchWheel();
            exportedPressure = exportedPressure || message.isChannelPressure();
            exportedAftertouch = exportedAftertouch || message.isAftertouch();
            exportedSustain = exportedSustain || (message.isController() &&
                                                   message.getControllerNumber() == 64);
            if (message.isController() && message.getControllerNumber() == 64) {
                if (message.getControllerValue() >= 64) {
                    if (!pedalCapable) ++invalidInstrumentPedals;
                    pedalOnTick = message.getTimeStamp();
                } else if (pedalOnTick >= 0.0) {
                    if ((message.getTimeStamp() - pedalOnTick) / 960.0 > 4.001)
                        ++excessivePedalWindows;
                    pedalOnTick = -1.0;
                }
            }
            exportedKeySignature = exportedKeySignature || message.isKeySignatureMetaEvent();
            exportedChordMarker = exportedChordMarker || (message.isTextMetaEvent() &&
                message.getTextFromTextMetaEvent().startsWith("Chord: "));
        }
    }
    require(exportedPitchBend && exportedPressure && exportedAftertouch && exportedSustain,
            "Exported MIDI must contain bends, pressure, per-note aftertouch and sustain pedal");
    require(orphanNoteOffs == 0,
            "Exported orchestral tracks must never contain synthetic or orphan note-offs");
    require(invalidInstrumentPedals == 0 && excessivePedalWindows == 0,
            "Sustain pedal must be instrument-aware and every exported pedal window must be bounded");
    require(exportedKeySignature && exportedChordMarker,
            "Full-song MIDI must expose its key signature and exact chord markers to the DAW");

    pulso::Pattern unsafePedalPattern;
    unsafePedalPattern.lengthBeats = 128.0;
    unsafePedalPattern.parts = {
        {1, "french_horns", "Pedal Policy Horn", pulso::VoiceId::HarmonicFoundation,
         pulso::ScoreDepartment::Harmony, "horizon", 40, 76, 0.8, pulso::InstrumentSoundModel::Horns},
        {2, "flute", "Pedal Policy Flute", pulso::VoiceId::HarmonicFoundation,
         pulso::ScoreDepartment::Harmony, "air", 55, 96, 0.7, pulso::InstrumentSoundModel::Flute},
        {3, "piano", "Pedal Policy Piano", pulso::VoiceId::HarmonicFoundation,
         pulso::ScoreDepartment::Harmony, "keys", 36, 96, 0.8, pulso::InstrumentSoundModel::Piano}};
    unsafePedalPattern.notes = {
        {0.0, 1.0, 48, 80, 3, pulso::VoiceId::HarmonicFoundation, 1},
        {0.0, 1.0, 72, 80, 3, pulso::VoiceId::HarmonicFoundation, 2},
        {0.0, 1.0, 60, 80, 3, pulso::VoiceId::HarmonicFoundation, 3},
        {1.25, 0.5, 64, 76, 3, pulso::VoiceId::HarmonicFoundation, 3}};
    unsafePedalPattern.controls = {
        {0.0, 64, 96, 3, pulso::VoiceId::HarmonicFoundation},
        {96.0, 64, 0, 3, pulso::VoiceId::HarmonicFoundation}};
    const auto pedalPolicyFile = exportFolder.getNonexistentChildFile("pedal-policy", ".mid", false);
    require(pulso::plugin::writePatternToMidiFile(unsafePedalPattern, pedalPolicyFile, exportOptions),
            "Instrument-aware pedal policy fixture must export");
    juce::FileInputStream pedalPolicyInput(pedalPolicyFile);
    juce::MidiFile pedalPolicyMidi;
    require(pedalPolicyInput.openedOk() && pedalPolicyMidi.readFrom(pedalPolicyInput),
            "Instrument-aware pedal policy fixture must be readable");
    auto forbiddenPedalDowns = 0;
    auto pianoPedalDowns = 0;
    auto pianoMaximumPedalTicks = 0.0;
    for (auto trackIndex = 0; trackIndex < pedalPolicyMidi.getNumTracks(); ++trackIndex) {
        const auto* sequence = pedalPolicyMidi.getTrack(trackIndex);
        juce::String name;
        for (auto event = 0; sequence != nullptr && event < sequence->getNumEvents(); ++event)
            if (sequence->getEventPointer(event)->message.isTrackNameEvent())
                name = sequence->getEventPointer(event)->message.getTextFromTextMetaEvent();
        auto downTick = -1.0;
        for (auto event = 0; sequence != nullptr && event < sequence->getNumEvents(); ++event) {
            const auto message = sequence->getEventPointer(event)->message;
            if (!message.isController() || message.getControllerNumber() != 64) continue;
            if (message.getControllerValue() >= 64) {
                if (name.contains("Horn") || name.contains("Flute")) ++forbiddenPedalDowns;
                if (name.contains("Piano")) { ++pianoPedalDowns; downTick = message.getTimeStamp(); }
            } else if (name.contains("Piano") && downTick >= 0.0) {
                pianoMaximumPedalTicks = std::max(pianoMaximumPedalTicks,
                                                  message.getTimeStamp() - downTick);
                downTick = -1.0;
            }
        }
    }
    require(forbiddenPedalDowns == 0 && pianoPedalDowns > 0 &&
                pianoMaximumPedalTicks <= 2.001 * 960.0,
            "Legacy section-wide CC64 must be removed from winds/brass and bounded for piano");
    pedalPolicyFile.deleteFile();
    pedalPolicyFile.withFileExtension(".pulso.json").deleteFile();
    require(std::all_of(orchestration.expressions.begin(), orchestration.expressions.end(),
        [&](const auto& expression) {
            if (expression.type != pulso::ExpressionEventType::PolyAftertouch) return true;
            return std::any_of(orchestration.notes.begin(), orchestration.notes.end(),
                [&](const auto& note) {
                    return note.voice == expression.voice && note.pitch == expression.note &&
                           note.startBeat <= expression.beat + 0.0001 &&
                           note.endBeat() >= expression.beat - 0.0001;
                });
        }), "Poly-aftertouch must reference a final audible note after tonal repair");

    const auto bassFile = exportFolder.getNonexistentChildFile("bass", ".mid", false);
    exportOptions.channelFilter = 1;
    exportOptions.chordMarkers.clear();
    require(pulso::plugin::writePatternToMidiFile(*composedPattern, bassFile, exportOptions),
            "A role-specific MIDI clip must export independently");
    juce::FileInputStream bassInput(bassFile);
    juce::MidiFile bassMidi;
    require(bassInput.openedOk() && bassMidi.readFrom(bassInput) && bassMidi.getNumTracks() == 2,
            "A filtered clip must contain only conductor and selected role tracks");

    playHead.playing = false;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsPanic(midi), "Stopping transport must emit an explicit MIDI panic");

    for (auto block = 0; block < 1400; ++block) {
        midi.clear();
        processor.processBlock(audio, midi);
    }
    require(peakMagnitude(audio) < 0.001f,
            "Preview voices and spatial effects must decay below -60 dB after transport stops");

    playHead.ppq = 0.10;
    playHead.playing = true;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsNoteOn(midi), "Starting inside a sounding note must retrigger that note");
    require(peakMagnitude(audio) > 0.0001f, "Retriggered preview must produce audio immediately");

    advance(playHead, blockSize, sampleRate);
    midi.clear();
    processor.processBlock(audio, midi);
    playHead.ppq = 4.10;
    midi.clear();
    processor.processBlock(audio, midi);
    require(containsPanic(midi), "A transport seek must clean up active MIDI notes");
    require(containsNoteOn(midi), "A transport seek must recover notes overlapping the destination");

    processor.requestVariation();
    const auto originalDnaSeed = composedPattern->seed;
    const auto originalComposition = composedPattern->notes;
    auto variationPanic = false;
    std::shared_ptr<const pulso::Pattern> variedPattern;
    for (auto attempt = 0; attempt < 180; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        variationPanic = variationPanic || containsPanic(midi);
        const auto candidate = processor.currentPattern();
        if (variationPanic && candidate && candidate->notes != originalComposition) {
            variedPattern = candidate;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(variationPanic, "Replacing a live pattern must clean up notes from the previous pattern");
    require(variedPattern && variedPattern->seed == originalDnaSeed,
            "Regenerate Unlocked must preserve the composition family seed");
    require(variedPattern->notes != originalComposition,
            "Regenerate Unlocked must create a real transformation rather than a duplicate");

    processor.requestNewComposition();
    auto receivedNewDna = false;
    for (auto attempt = 0; attempt < 100 && !receivedNewDna; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        if (const auto candidate = processor.currentPattern())
            receivedNewDna = candidate->seed != originalDnaSeed;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(receivedNewDna, "Next Idea must replace the persistent composition identity");

    const auto beforeSelective = processor.currentPattern();
    require(beforeSelective != nullptr, "Selective regeneration needs a current idea");
    processor.setLayerLocked(pulso::plugin::PulsoAudioProcessor::Layer::Melody, true);
    processor.requestRegenerateUnlocked();
    std::shared_ptr<const pulso::Pattern> afterSelective;
    for (auto attempt = 0; attempt < 120; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        const auto candidate = processor.currentPattern();
        if (candidate && candidate->notes != beforeSelective->notes) {
            afterSelective = candidate;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(afterSelective != nullptr, "Regenerate Unlocked must publish a transformed idea");
    const auto notesForChannel = [](const pulso::Pattern& pattern, int channel) {
        std::vector<pulso::NoteEvent> notes;
        std::copy_if(pattern.notes.begin(), pattern.notes.end(), std::back_inserter(notes),
                     [channel](const auto& note) { return note.channel == channel; });
        return notes;
    };
    require(notesForChannel(*beforeSelective, 2) == notesForChannel(*afterSelective, 2),
            "A locked melody must remain note-for-note identical");
    require(notesForChannel(*beforeSelective, 1) != notesForChannel(*afterSelective, 1) ||
                notesForChannel(*beforeSelective, 10) != notesForChannel(*afterSelective, 10),
            "At least one unlocked accompaniment layer must be recomposed");

    processor.requestUndo();
    auto undoRestored = false;
    for (auto attempt = 0; attempt < 120 && !undoRestored; ++attempt) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
        if (const auto candidate = processor.currentPattern())
            undoRestored = candidate->notes == beforeSelective->notes;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(undoRestored, "Undo must restore the complete previous composition");

    auto* preview = processor.parameters.getParameter("preview");
    require(preview != nullptr, "Preview parameter must exist");
    preview->setValueNotifyingHost(0.0f);
    for (auto block = 0; block < 700; ++block) {
        advance(playHead, blockSize, sampleRate);
        midi.clear();
        processor.processBlock(audio, midi);
    }
    const auto disabledPreviewTail = peakMagnitude(audio);
    require(disabledPreviewTail < 0.001f,
            "Disabling preview must decay below -60 dB without a stuck voice; observed peak=" +
                std::to_string(disabledPreviewTail));

    preview->setValueNotifyingHost(1.0f);
    auto longestCallback = std::chrono::microseconds::zero();
    std::vector<std::chrono::microseconds> callbackDurations;
    callbackDurations.reserve(2000);
    for (auto block = 0; block < 2000; ++block) {
        playHead.playing = block % 97 != 0;
        if (block % 113 == 0) playHead.ppq = std::fmod(block * 0.137, 16.0);
        if (block % 31 == 0) processor.requestVariation();
        midi.clear();
        const auto started = std::chrono::steady_clock::now();
        processor.processBlock(audio, midi);
        const auto callbackDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        longestCallback = std::max(longestCallback, callbackDuration);
        callbackDurations.push_back(callbackDuration);
        for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
            for (auto sample = 0; sample < audio.getNumSamples(); ++sample)
                require(std::isfinite(audio.getSample(channel, sample)),
                        "Stress transport must never produce NaN or infinity");
        require(peakMagnitude(audio) <= 1.0f, "Limiter must contain every stress-test block");
        advance(playHead, blockSize, sampleRate);
    }
    std::sort(callbackDurations.begin(), callbackDurations.end());
    const auto callbackP99 = callbackDurations[callbackDurations.size() * 99 / 100];
    // This executable is not launched by an ASIO real-time thread, so Windows may
    // occasionally deschedule it for Defender/GUI work. Require the distribution to
    // meet the real 5.33 ms budget and separately reject catastrophic host stalls.
   #if JUCE_DEBUG
    // Bounds-checked, unoptimised JUCE rendering is a diagnostic build, not the binary
    // delivered to Live. Keep it protected against stalls without pretending it has the
    // same deadline as Release; the Release suite below the packaging gate retains 5 ms.
    constexpr auto callbackBudget = std::chrono::milliseconds(8);
   #else
    constexpr auto callbackBudget = std::chrono::milliseconds(5);
   #endif
    require(callbackP99 < callbackBudget && longestCallback < std::chrono::milliseconds(50),
            "At 48 kHz/256 samples the callback distribution must fit its 5.33 ms deadline; p99=" +
            std::to_string(callbackP99.count()) + " us max=" + std::to_string(longestCallback.count()) + " us");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr && editor->getWidth() == 1120 && editor->getHeight() == 760,
            "The VST editor must open at the Ableton-compatible fallback geometry");
    const auto editorSnapshot = editor->createComponentSnapshot(editor->getLocalBounds(), true);
    require(editorSnapshot.isValid() && editorSnapshot.getWidth() == 1120 &&
                editorSnapshot.getHeight() == 760,
            "The VST editor must produce a visible rendered frame before host attachment");
    auto tooltipCount = 0;
    for (auto index = 0; index < editor->getNumChildComponents(); ++index) {
        if (auto* tooltip = dynamic_cast<juce::TooltipClient*>(editor->getChildComponent(index))) {
            require(tooltip->getTooltip().isNotEmpty(),
                    "Every direct UX control must provide contextual help");
            ++tooltipCount;
        }
    }
    require(tooltipCount >= 19, "The streamlined visible interface must remain fully covered by tooltips");
    editor.reset();

    processor.parameters.getParameter("previewWorld")->setValueNotifyingHost(1.0f);
    processor.parameters.getParameter("language")->setValueNotifyingHost(0.0f);
    processor.setVoicePreviewTimbre(pulso::VoiceId::SnareClap, 1);
    processor.setVoicePreviewTimbre(pulso::VoiceId::ClosedHats, 2);
    processor.setVoicePreviewTimbre(pulso::VoiceId::Lead, 4);
    processor.setVoicePreviewOctave(pulso::VoiceId::SnareClap, -12);
    processor.parameters.getParameter("previewLevel12")->setValueNotifyingHost(
        processor.parameters.getParameter("previewLevel12")->convertTo0to1(-7.5f));
    processor.toggleVoiceSolo(pulso::VoiceId::HarmonicPulse);
    processor.toggleVoiceMute(pulso::VoiceId::ClosedHats);
    processor.setLiveDeploymentMode(
        pulso::plugin::PulsoAudioProcessor::LiveDeploymentMode::QuickThreeStem);
    if (const auto pattern = processor.currentPattern(); pattern != nullptr && !pattern->parts.empty())
        processor.setPartLiveDevice(pattern->parts.front().id, "Instrument Rack");
    juce::MemoryBlock savedState;
    processor.getStateInformation(savedState);
    pulso::plugin::PulsoAudioProcessor restored;
    restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    require(restored.currentCompositionSeed() == processor.currentCompositionSeed() &&
                restored.currentVariationIndex() == processor.currentVariationIndex(),
            "Composition DNA and lineage must survive a DAW project reload");
    require(restored.liveDeploymentMode() ==
                pulso::plugin::PulsoAudioProcessor::LiveDeploymentMode::QuickThreeStem,
            "The explicit Live deployment mode must survive a DAW project reload");
    require(restored.isVoiceSolo(pulso::VoiceId::HarmonicPulse) &&
                restored.isVoiceMuted(pulso::VoiceId::ClosedHats) &&
                !restored.isVoiceAudible(pulso::VoiceId::ClosedHats),
            "Per-voice solo and mute choices must survive an Ableton project reload");
    const auto restoredPatternSnapshot = restored.currentPattern();
    const auto originalPatternSnapshot = processor.currentPattern();
    const auto reloadCounts = [&] {
        if (restoredPatternSnapshot == nullptr || originalPatternSnapshot == nullptr)
            return std::string{"null snapshot"};
        return std::string{"notes "} + std::to_string(originalPatternSnapshot->notes.size()) + "/" +
            std::to_string(restoredPatternSnapshot->notes.size()) + ", controls " +
            std::to_string(originalPatternSnapshot->controls.size()) + "/" +
            std::to_string(restoredPatternSnapshot->controls.size()) + ", expressions " +
            std::to_string(originalPatternSnapshot->expressions.size()) + "/" +
            std::to_string(restoredPatternSnapshot->expressions.size()) + ", parts " +
            std::to_string(originalPatternSnapshot->parts.size()) + "/" +
            std::to_string(restoredPatternSnapshot->parts.size());
    }();
    require(restoredPatternSnapshot != nullptr && originalPatternSnapshot != nullptr &&
                restoredPatternSnapshot->notes == originalPatternSnapshot->notes &&
                restoredPatternSnapshot->controls == originalPatternSnapshot->controls &&
                restoredPatternSnapshot->expressions == originalPatternSnapshot->expressions &&
                restoredPatternSnapshot->parts == originalPatternSnapshot->parts,
            "The complete approved AI composition must survive a DAW project reload exactly: " + reloadCounts);
    require(restoredPatternSnapshot->parts.empty() ||
                restoredPatternSnapshot->parts.front().liveDevice == "Instrument Rack",
            "A manual Live-native instrument choice must survive a DAW project reload");
    const auto restoredPlan = restored.currentSongPlan();
    const auto originalPlan = processor.currentSongPlan();
    if (originalPlan != nullptr && !originalPlan->sections.empty())
        require(restoredPlan != nullptr &&
                    restoredPlan->chordPalette.size() == originalPlan->chordPalette.size() &&
                    restoredPlan->instrumentCastAuthored == originalPlan->instrumentCastAuthored &&
                    restoredPlan->harmonicLanguage.description == originalPlan->harmonicLanguage.description &&
                    restoredPlan->sections.size() == originalPlan->sections.size() &&
                    !restoredPlan->sections.empty() &&
                    restoredPlan->sections.front().harmonicEvents.size() ==
                        originalPlan->sections.front().harmonicEvents.size(),
                "HarmonicLanguage, chord palette and section timeline must survive a DAW project reload");
    require(std::abs(restored.parameters.getRawParameterValue("space")->load()) < 0.0001f &&
                std::abs(restored.parameters.getRawParameterValue("groove")->load()) < 0.0001f,
            "Reloading a project must keep retired controls fixed at zero");
    require(std::abs(restored.parameters.getRawParameterValue("previewWorld")->load() - 8.0f) < 0.0001f,
            "The selected preview sound world must survive a DAW project reload");
    require(restored.uiLanguage() == pulso::plugin::UiLanguage::English,
            "The selected interface and tooltip language must survive a DAW project reload");
    require(restored.voicePreviewTimbre(pulso::VoiceId::SnareClap) == 1 &&
                restored.voicePreviewTimbre(pulso::VoiceId::ClosedHats) == 2 &&
                restored.voicePreviewTimbre(pulso::VoiceId::Lead) == 4,
            "Every per-lane preview sound must survive an Ableton project reload");
    require(restored.voicePreviewOctave(pulso::VoiceId::SnareClap) == -12 &&
                std::abs(restored.voicePreviewLevelDb(pulso::VoiceId::SnareClap) + 7.5f) < 0.01f,
            "Per-lane octave and level must survive an Ableton project reload");

    const auto bridgeTestDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("PULSO Live Bridge Test", {}, false);
    bridgeTestDirectory.createDirectory();
    pulso::plugin::LiveDeploymentOptions deployment;
    deployment.title = "Processor Test Deployment";
    deployment.bpm = 123.0;
    pulso::Pattern deploymentPattern;
    deploymentPattern.lengthBeats = 8.0;
    deploymentPattern.parts = {
        {1, "kick_drum", "Kick Drum", pulso::VoiceId::CoreDrums,
         pulso::ScoreDepartment::Rhythm, "pulse", 35, 36, 1.0,
         pulso::InstrumentSoundModel::Kick},
        {2, "piano", "Piano", pulso::VoiceId::HarmonicFoundation,
         pulso::ScoreDepartment::Harmony, "foundation", 36, 96, 0.8,
         pulso::InstrumentSoundModel::Piano},
        {3, "violin_1", "Violin I", pulso::VoiceId::Lead,
         pulso::ScoreDepartment::Melody, "lead", 55, 103, 0.9,
         pulso::InstrumentSoundModel::HighStrings}};
    deploymentPattern.notes = {
        {0.0, 0.25, 36, 110, 10, pulso::VoiceId::CoreDrums, 1},
        {0.0, 4.0, 60, 82, 3, pulso::VoiceId::HarmonicFoundation, 2},
        {1.0, 1.0, 67, 96, 2, pulso::VoiceId::Lead, 3}};
    deploymentPattern.controls = {
        {0.0, 11, 58, 2, pulso::VoiceId::Lead, 0},
        {1.0, 74, 92, 2, pulso::VoiceId::Lead, 3},
        {7.5, 11, 12, 2, pulso::VoiceId::Lead, 0},
        {0.0, 64, 96, 3, pulso::VoiceId::HarmonicFoundation, 2},
        {2.0, 64, 0, 3, pulso::VoiceId::HarmonicFoundation, 2}};
    deploymentPattern.expressions = {
        {1.0, pulso::ExpressionEventType::PitchBend, 8700, -1, 2,
         pulso::VoiceId::Lead, 0},
        {1.5, pulso::ExpressionEventType::PolyAftertouch, 72, 67, 2,
         pulso::VoiceId::Lead, 3},
        {7.5, pulso::ExpressionEventType::ChannelPressure, 100, -1, 2,
         pulso::VoiceId::Lead, 0}};
    deploymentPattern.parts[0].liveDevice = "Drum Rack";
    deploymentPattern.parts[0].livePresetIntent = "tight modern acoustic kick";
    deploymentPattern.parts[1].liveDevice = "Electric";
    deploymentPattern.parts[1].livePresetIntent = "warm intimate keys";
    deploymentPattern.parts[2].liveDevice = "Instrument Rack";
    deploymentPattern.parts[2].livePresetIntent = "lyrical orchestral violin";
    juce::String deploymentStatus;
    const auto deploymentWritten = pulso::plugin::writeLiveDeploymentRequest(
        deploymentPattern, deployment, deploymentStatus, bridgeTestDirectory);
    require(deploymentWritten,
            "A complete orchestral pattern must produce a Live deployment request: " +
                deploymentStatus.toStdString() + " directory=" +
                bridgeTestDirectory.getFullPathName().toStdString());
    const auto deploymentJson = juce::JSON::parse(
        bridgeTestDirectory.getChildFile("request.json").loadFileAsString());
    auto* deploymentObject = deploymentJson.getDynamicObject();
    require(deploymentObject != nullptr &&
                static_cast<int>(deploymentObject->getProperty("schema_version")) == 10 &&
                deploymentObject->getProperty("sound_engine").toString() == "ableton_live_native" &&
                deploymentObject->getProperty("expression_delivery").toString() ==
                    "native_editable_with_lossless_midi_source" &&
                deploymentObject->getProperty("production_domain").toString() == "adaptive" &&
                deploymentObject->getProperty("production_mode_source").toString() == "adaptive_inference" &&
                !static_cast<bool>(deploymentObject->getProperty("electronic_production_audited")) &&
                !deploymentObject->hasProperty("electronic_production_score") &&
                deploymentObject->getProperty("sound_world").toString().isNotEmpty() &&
                deploymentObject->getProperty("deployment_mode").toString() == "full_orchestration" &&
                deploymentObject->hasProperty("narrative_audited") &&
                deploymentObject->hasProperty("creative_ready") &&
                deploymentObject->hasProperty("foreground_ai_authorship_ratio") &&
                deploymentObject->getProperty("tracks").getArray() != nullptr &&
                deploymentObject->getProperty("tracks").getArray()->size() == 3,
            "Full orchestration must create one versioned editable Live track per populated instrument");
    const auto* nativeTracks = deploymentObject->getProperty("tracks").getArray();
    require(nativeTracks != nullptr && std::all_of(nativeTracks->begin(), nativeTracks->end(), [](const auto& value) {
                const auto* track = value.getDynamicObject();
                const auto* notes = track == nullptr ? nullptr : track->getProperty("notes").getArray();
                return track != nullptr && track->getProperty("sound_source").toString() == "live_native" &&
                       track->getProperty("native_device").toString().isNotEmpty() &&
                       track->getProperty("playback_mode").toString().isNotEmpty() &&
                       track->getProperty("same_pitch_overlap_policy").toString() == "trim_previous" &&
                       track->getProperty("timbre_priority").toString().isNotEmpty() &&
                       static_cast<double>(track->getProperty("minimum_intent_fidelity")) >= 0.35 &&
                       static_cast<double>(track->getProperty("release_max_seconds")) > 0.0 &&
                       track->getProperty("controls").getArray() != nullptr &&
                       track->getProperty("expressions").getArray() != nullptr &&
                       static_cast<int>(track->getProperty("expression_projection_version")) == 1 &&
                       track->getProperty("sound_world").toString().isNotEmpty() &&
                       track->getProperty("timbre_signature").getDynamicObject() != nullptr &&
                       track->hasProperty("sound_selection_seed") &&
                       track->hasProperty("sound_variation") &&
                       track->hasProperty("sound_locked") &&
                       track->getProperty("device_candidates").getArray() != nullptr &&
                       notes != nullptr && std::all_of(notes->begin(), notes->end(), [](const auto& noteValue) {
                           const auto* note = noteValue.getDynamicObject();
                           return note != nullptr && note->hasProperty("origin") &&
                                  note->hasProperty("narrative_id");
                       });
            }), "Every deployed part must carry a validated Live-native sound contract without VST identifiers");
    const auto* kickDeployment = (*nativeTracks)[0].getDynamicObject();
    require(kickDeployment != nullptr && kickDeployment->getProperty("native_device").toString() == "Drum Rack" &&
                kickDeployment->getProperty("preset_intent").toString() == "tight modern acoustic kick" &&
                !kickDeployment->hasProperty("plugin_identifier") && !kickDeployment->hasProperty("plugin_name"),
            "AI-selected native device and timbral intent must reach Live without any legacy plug-in contract");
    const auto* kickCandidates = kickDeployment->getProperty("device_candidates").getArray();
    require(kickCandidates != nullptr && kickCandidates->contains("909 Core Kit.adg") &&
                kickCandidates->contains("808 Core Kit.adg") && !kickCandidates->contains("Drum Rack"),
            "Rhythm deployment must fall back to populated kits, never an empty Drum Rack container");
    const auto* violinDeployment = (*nativeTracks)[2].getDynamicObject();
    require(violinDeployment != nullptr && violinDeployment->getProperty("controls").getArray()->size() == 2 &&
                violinDeployment->getProperty("expressions").getArray()->size() == 2,
            "Voice and part expression must follow audible phrases without leaking distant generic curves");
    const auto* violinCandidates = violinDeployment->getProperty("device_candidates").getArray();
    require(violinCandidates != nullptr && violinCandidates->contains("violin 1 orchestral") &&
                !violinCandidates->contains("Instrument Rack") && violinCandidates->contains("Wavetable"),
            "Orchestral deployment must search by instrument identity and end on an audible synth fallback");
    deployment.aggregateDepartmentStems = true;
    require(pulso::plugin::writeLiveDeploymentRequest(
                deploymentPattern, deployment, deploymentStatus, bridgeTestDirectory),
            "Quick deployment must produce a three-stem request");
    const auto quickDeploymentJson = juce::JSON::parse(
        bridgeTestDirectory.getChildFile("request.json").loadFileAsString());
    auto* quickDeploymentObject = quickDeploymentJson.getDynamicObject();
    require(quickDeploymentObject != nullptr &&
                quickDeploymentObject->getProperty("deployment_mode").toString() == "quick_3_stem" &&
                quickDeploymentObject->getProperty("tracks").getArray() != nullptr &&
                quickDeploymentObject->getProperty("tracks").getArray()->size() == 3,
            "Quick 3-stem must remain an explicit lightweight deployment option");
    const auto* quickTracks = quickDeploymentObject->getProperty("tracks").getArray();
    require(quickTracks != nullptr && std::all_of(quickTracks->begin(), quickTracks->end(), [](const auto& value) {
                const auto* track = value.getDynamicObject();
                return track != nullptr && track->getProperty("catalog_id").toString().isNotEmpty();
            }), "Quick stems must also carry a strict musical identity for native sound resolution");
    auto rejectedDeployment = deploymentPattern;
    rejectedDeployment.productionAuditPerformed = true;
    rejectedDeployment.productionReady = false;
    require(!pulso::plugin::writeLiveDeploymentRequest(
                rejectedDeployment, deployment, deploymentStatus, bridgeTestDirectory) &&
                deploymentStatus.contains("PRODUCTION GATE"),
            "Live deployment must reject an audited score that failed production readiness");
    bridgeTestDirectory.deleteRecursively();

    ensembleFile.deleteFile();
    bassFile.deleteFile();
    orchestrationFile.deleteFile();

    processor.releaseResources();
    processor.setPlayHead(nullptr);
    std::cout << "[PASS] Processor transport, panic, recovery and preview ceiling"
              << " | stress_p99_callback_us=" << callbackP99.count()
              << " max_us=" << longestCallback.count() << '\n';
    return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] Processor: " << exception.what() << '\n';
        return 1;
    }
}
