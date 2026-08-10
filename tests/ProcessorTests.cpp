#include "TestSupport.h"

#include "plugin/PluginProcessor.h"
#include "plugin/MidiExporter.h"
#include "plugin/AiComposer.h"
#include "plugin/PreviewSynth.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

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

} // namespace

int main(int argc, char** argv) {
    try {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
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
        require(error.isEmpty(), "Live OpenAI song-plan request failed: " + error.toStdString());
        require(plan.sections.size() >= 3 && plan.voices.size() >= 7 && plan.totalBars == 30 &&
                    !plan.rhythmMotifs.empty() &&
                    std::all_of(plan.sections.begin(), plan.sections.end(), [](const auto& section) {
                        return !section.rhythm.motifId.empty();
                    }),
                "Live OpenAI response did not satisfy the dynamic-orchestration contract");
        std::cout << "[PASS] Live structured song plan | voices=" << plan.voices.size()
                  << " sections=" << plan.sections.size()
                  << " rhythm_motifs=" << plan.rhythmMotifs.size() << '\n';
        return 0;
    }
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

    pulso::plugin::PulsoAudioProcessor processor;
    require(std::abs(processor.parameters.getRawParameterValue("space")->load()) < 0.0001f &&
                std::abs(processor.parameters.getRawParameterValue("groove")->load()) < 0.0001f,
            "Retired Space and Groove controls must always default to zero");
    require(processor.parameters.getParameter("previewWorld") != nullptr,
            "The selectable preview sound world must be a persistent host parameter");
    require(processor.parameters.getParameter("performance") != nullptr &&
                processor.parameters.getRawParameterValue("performance")->load() < 0.5f,
            "Human Performance must be a persistent button that defaults to exact timing");
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
    require(peakMagnitude(audio) <= 1.0f, "Preview output must remain below digital full scale");
    const auto composedPattern = processor.currentPattern();
    require(composedPattern != nullptr, "The processor must expose its composed phrase");
    require(std::all_of(composedPattern->notes.begin(), composedPattern->notes.end(), [](const auto& note) {
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.000001 &&
                       std::abs(note.endBeat() * 4.0 - std::round(note.endBeat() * 4.0)) < 0.000001;
            }), "The default processor pattern must be perfectly quantized before audition and export");
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
      "root_pitch_class":2,"mode":"minor","groove_family":"deep_progressive_house","motif_intervals":[0,3,7,5],
      "chord_degrees":[0,5,3,6],"rhythm_motifs":[{
        "id":"A","bars":1,"steps_per_bar":16,"kick":"1000100010001000","snare_clap":"0000200000002000",
        "closed_hats":"0010001000100010","open_hats_shaker":"0000001000000020",
        "low_percussion":"0000010000010000","high_percussion":"0001000000100000"
      }],"voices":[
        {"id":"core_drums","function":"Pulse anchor","interaction":"Leaves space at cadences","activity":0.8,"syncopation":0.4,"minimum_pitch":35,"maximum_pitch":81},
        {"id":"sub_bass","function":"Tonal gravity","interaction":"Supports harmonic rhythm","activity":0.7,"syncopation":0.2,"minimum_pitch":28,"maximum_pitch":48},
        {"id":"movement_bass","function":"Forward motion","interaction":"Answers the sub bass","activity":0.5,"syncopation":0.5,"minimum_pitch":36,"maximum_pitch":62},
        {"id":"harmonic_foundation","function":"Voice-led foundation","interaction":"Frames the lead","activity":0.6,"syncopation":0.1,"minimum_pitch":45,"maximum_pitch":76},
        {"id":"harmonic_pulse","function":"Rhythmic harmony","interaction":"Answers percussion","activity":0.5,"syncopation":0.6,"minimum_pitch":50,"maximum_pitch":84},
        {"id":"lead","function":"Carries the motif","interaction":"Alternates with countermelody","activity":0.6,"syncopation":0.4,"minimum_pitch":55,"maximum_pitch":92},
        {"id":"atmosphere","function":"Long-range depth","interaction":"Bridges sparse sections","activity":0.4,"syncopation":0.0,"minimum_pitch":42,"maximum_pitch":92}
      ],"sections":[
        {"name":"Prologue","function":"Introduce motif fragments","harmonic_direction":"Tonic ambiguity","motif_treatment":"Fragment","bars":8,"energy":0.2,"tension":0.2,"density":0.3,"motif_variant":0,"active_voices":["harmonic_foundation","lead","atmosphere"],"kick_state":"reduced","kick_continuity":"sectional","percussion_density":0.2,"rhythmic_syncopation":0.2,"swing":0.08,"rhythm_motif_id":"A","rhythm_mutations":[],"rhythm_gestures":[]},
        {"name":"Development","function":"Transform the theme","harmonic_direction":"Move away from tonic","motif_treatment":"Sequence","bars":16,"energy":0.7,"tension":0.8,"density":0.7,"motif_variant":2,"active_voices":["core_drums","sub_bass","harmonic_foundation","harmonic_pulse","lead"],"kick_state":"four_on_floor","kick_continuity":"required","percussion_density":0.7,"rhythmic_syncopation":0.5,"swing":0.1,"rhythm_motif_id":"A","rhythm_mutations":[{"bar_offset":6,"lane":"low_percussion","operation":"shift","step":5,"amount":1,"velocity":72,"purpose":"Answer the phrase"}],"rhythm_gestures":[{"bar_offset":7,"type":"drop_last_kick","beat":3,"intensity":0.6},{"bar_offset":15,"type":"double_kick","beat":3.75,"intensity":0.75}]},
        {"name":"Coda","function":"Resolve the argument","harmonic_direction":"Final tonic","motif_treatment":"Cadential recall","bars":8,"energy":0.3,"tension":0.1,"density":0.3,"motif_variant":0,"active_voices":["sub_bass","harmonic_foundation","lead","atmosphere"],"kick_state":"muted","kick_continuity":"sectional","percussion_density":0.2,"rhythmic_syncopation":0.1,"swing":0.05,"rhythm_motif_id":"A","rhythm_mutations":[],"rhythm_gestures":[]}
      ]
    })json");
    pulso::SongPlan parsedSongPlan;
    require(pulso::plugin::AiComposer::songPlanSchemaIsValid() &&
                pulso::plugin::AiComposer::parseSongPlanJson(songPlanExample, 64, 32, 120.0,
                                                              4.0, 77, parsedSongPlan, parseError),
            "Structured GPT song architecture must validate independently from MIDI rendering");
    require(parsedSongPlan.sections.size() == 3 && parsedSongPlan.voices.size() == 10 &&
                parsedSongPlan.sections[1].activeVoices.size() == 8 && parsedSongPlan.totalBars == 32 &&
                parsedSongPlan.sections.back().startBar == 24 && parsedSongPlan.key == "D minor" &&
                parsedSongPlan.sections[1].rhythm.kickState == pulso::KickState::FourOnFloor &&
                parsedSongPlan.rhythmMotifs.size() == 1 &&
                parsedSongPlan.sections[1].rhythm.mutations.size() == 1 &&
                parsedSongPlan.sections[1].rhythm.gestures.size() == 2,
            "Validated song architecture must preserve voices and contiguous section metadata");

    const auto orchestrationPlan = pulso::SongComposer::createLocalPlan(
        "Deep progressive long-form test", 120, 120.0, 4.0, 8841, 2, pulso::ScaleKind::Minor);
    pulso::GenerationContext orchestrationFoundation;
    orchestrationFoundation.role = pulso::Role::Ensemble;
    orchestrationFoundation.seed = orchestrationPlan.seed;
    const auto orchestration = pulso::SongComposer{}.render(orchestrationPlan,
                                                             orchestrationFoundation);
    const auto orchestrationFile = exportFolder.getNonexistentChildFile("orchestration", ".mid", false);
    exportOptions.channelFilter = 0;
    exportOptions.clipName = "PULSO Orchestration Test";
    require(pulso::plugin::writePatternToMidiFile(orchestration, orchestrationFile, exportOptions),
            "A dynamically orchestrated song must export as standard multitrack MIDI");
    juce::FileInputStream orchestrationInput(orchestrationFile);
    juce::MidiFile orchestrationMidi;
    require(orchestrationInput.openedOk() && orchestrationMidi.readFrom(orchestrationInput) &&
                orchestrationMidi.getNumTracks() == 16,
            "Full-song export must contain a conductor plus fifteen independently named voice tracks");
    require(!orchestration.controls.empty() &&
                orchestration.markers.size() == orchestrationPlan.sections.size(),
            "Long-form export must retain expressive CC data and structural section markers");

    const auto bassFile = exportFolder.getNonexistentChildFile("bass", ".mid", false);
    exportOptions.channelFilter = 1;
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
    for (auto block = 0; block < 2000; ++block) {
        playHead.playing = block % 97 != 0;
        if (block % 113 == 0) playHead.ppq = std::fmod(block * 0.137, 16.0);
        if (block % 31 == 0) processor.requestVariation();
        midi.clear();
        const auto started = std::chrono::steady_clock::now();
        processor.processBlock(audio, midi);
        longestCallback = std::max(longestCallback,
                                   std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - started));
        for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
            for (auto sample = 0; sample < audio.getNumSamples(); ++sample)
                require(std::isfinite(audio.getSample(channel, sample)),
                        "Stress transport must never produce NaN or infinity");
        require(peakMagnitude(audio) <= 1.0f, "Limiter must contain every stress-test block");
        advance(playHead, blockSize, sampleRate);
    }
    require(longestCallback < std::chrono::milliseconds(50),
            "The audio callback must not wait for asynchronous generation");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto tooltipCount = 0;
    for (auto index = 0; index < editor->getNumChildComponents(); ++index) {
        if (auto* tooltip = dynamic_cast<juce::TooltipClient*>(editor->getChildComponent(index))) {
            require(tooltip->getTooltip().isNotEmpty(),
                    "Every direct UX control must provide contextual help");
            ++tooltipCount;
        }
    }
    require(tooltipCount >= 22, "The complete visible interface must be covered by tooltips");
    editor.reset();

    processor.parameters.getParameter("previewWorld")->setValueNotifyingHost(1.0f);
    processor.toggleVoiceSolo(pulso::VoiceId::HarmonicPulse);
    processor.toggleVoiceMute(pulso::VoiceId::ClosedHats);
    juce::MemoryBlock savedState;
    processor.getStateInformation(savedState);
    pulso::plugin::PulsoAudioProcessor restored;
    restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    require(restored.currentCompositionSeed() == processor.currentCompositionSeed() &&
                restored.currentVariationIndex() == processor.currentVariationIndex(),
            "Composition DNA and lineage must survive a DAW project reload");
    require(restored.isVoiceSolo(pulso::VoiceId::HarmonicPulse) &&
                restored.isVoiceMuted(pulso::VoiceId::ClosedHats) &&
                !restored.isVoiceAudible(pulso::VoiceId::ClosedHats),
            "Per-voice solo and mute choices must survive an Ableton project reload");
    const auto restoredPatternSnapshot = restored.currentPattern();
    const auto originalPatternSnapshot = processor.currentPattern();
    require(restoredPatternSnapshot != nullptr && originalPatternSnapshot != nullptr &&
                restoredPatternSnapshot->notes == originalPatternSnapshot->notes,
            "The complete approved AI composition must survive a DAW project reload exactly");
    require(std::abs(restored.parameters.getRawParameterValue("space")->load()) < 0.0001f &&
                std::abs(restored.parameters.getRawParameterValue("groove")->load()) < 0.0001f,
            "Reloading a project must keep retired controls fixed at zero");
    require(std::abs(restored.parameters.getRawParameterValue("previewWorld")->load() - 8.0f) < 0.0001f,
            "The selected preview sound world must survive a DAW project reload");

    ensembleFile.deleteFile();
    bassFile.deleteFile();
    orchestrationFile.deleteFile();

    processor.releaseResources();
    processor.setPlayHead(nullptr);
    std::cout << "[PASS] Processor transport, panic, recovery and preview ceiling"
              << " | stress_max_callback_us=" << longestCallback.count() << '\n';
    return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] Processor: " << exception.what() << '\n';
        return 1;
    }
}
