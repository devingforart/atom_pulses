#include "PluginProcessor.h"

#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace pulso::plugin {
namespace ids {
constexpr auto role = "role";
constexpr auto scale = "scale";
constexpr auto root = "root";
constexpr auto follow = "follow";
constexpr auto risk = "risk";
constexpr auto space = "space";
constexpr auto repetition = "repetition";
constexpr auto complexity = "complexity";
constexpr auto development = "development";
constexpr auto phraseBars = "phraseBars";
constexpr auto mode = "mode";
constexpr auto preview = "preview";
constexpr auto thru = "thru";
constexpr auto gain = "gain";
constexpr std::array generative{role, scale, root, follow, risk, space, repetition,
                                complexity, development, phraseBars, mode};
constexpr std::array phraseLengths{1, 2, 4, 8, 16};
} // namespace ids

PulsoAudioProcessor::PulsoAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PULSO_STATE", createParameterLayout()) {
    uiPatternSnapshot.store(std::make_shared<Pattern>(), std::memory_order_release);
    for (const auto* parameterId : ids::generative) parameters.addParameterListener(parameterId, this);
    generationThread = std::jthread([this](const std::stop_token token) { generationThreadMain(token); });
}

PulsoAudioProcessor::~PulsoAudioProcessor() {
    generationThread.request_stop();
    if (generationThread.joinable()) generationThread.join();
    for (const auto* parameterId : ids::generative) parameters.removeParameterListener(parameterId, this);
}

const juce::String PulsoAudioProcessor::getName() const { return "PULSO"; }

void PulsoAudioProcessor::parameterChanged(const juce::String&, float) {
    generationRevision.fetch_add(1, std::memory_order_release);
}

int PulsoAudioProcessor::currentPhraseBars() const noexcept {
    const auto index = std::clamp(static_cast<int>(parameters.getRawParameterValue(ids::phraseBars)->load()),
                                  0, static_cast<int>(ids::phraseLengths.size()) - 1);
    return ids::phraseLengths[static_cast<std::size_t>(index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout PulsoAudioProcessor::createParameterLayout() {
    using Choice = juce::AudioParameterChoice;
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;
    using Int = juce::AudioParameterInt;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(std::make_unique<Choice>(ids::role, "Role",
                                              juce::StringArray{"Bass", "Percussion", "Countermelody"}, 0));
    result.push_back(std::make_unique<Choice>(ids::scale, "Scale",
                                              juce::StringArray{"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"}, 1));
    result.push_back(std::make_unique<Int>(ids::root, "Root", 0, 11, 0));
    result.push_back(std::make_unique<Float>(ids::follow, "Follow", 0.0f, 1.0f, 0.65f));
    result.push_back(std::make_unique<Float>(ids::risk, "Risk", 0.0f, 1.0f, 0.30f));
    result.push_back(std::make_unique<Float>(ids::space, "Space", 0.0f, 1.0f, 0.35f));
    result.push_back(std::make_unique<Float>(ids::repetition, "Repetition", 0.0f, 1.0f, 0.78f));
    result.push_back(std::make_unique<Float>(ids::complexity, "Complexity", 0.0f, 1.0f, 0.45f));
    result.push_back(std::make_unique<Float>(ids::development, "Development", 0.0f, 1.0f, 0.45f));
    result.push_back(std::make_unique<Choice>(ids::phraseBars, "Phrase Length",
                                              juce::StringArray{"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 2));
    result.push_back(std::make_unique<Choice>(ids::mode, "Phrase Mode",
                                              juce::StringArray{"Loop", "Evolve"}, 0));
    result.push_back(std::make_unique<Bool>(ids::preview, "Preview", true));
    result.push_back(std::make_unique<Bool>(ids::thru, "MIDI Thru", false));
    result.push_back(std::make_unique<Float>(ids::gain, "Output",
                                             juce::NormalisableRange<float>(-36.0f, 0.0f, 0.1f), -12.0f));
    return {result.begin(), result.end()};
}

void PulsoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = std::max(1.0, sampleRate);
    previewSynth.prepare(currentSampleRate);

    juce::dsp::ProcessSpec spec{currentSampleRate,
                                static_cast<juce::uint32>(std::max(1, samplesPerBlock)),
                                static_cast<juce::uint32>(std::max(1, getTotalNumOutputChannels()))};
    previewLimiter.prepare(spec);
    previewLimiter.reset();
    previewLimiter.setThreshold(-0.5f);
    previewLimiter.setRelease(60.0f);
    previewGain.reset(currentSampleRate, 0.025);
    previewGain.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(parameters.getRawParameterValue(ids::gain)->load()));

    generatedMidi.ensureSize(65536);
    previewMidi.ensureSize(65536);
    thruMidi.ensureSize(65536);
    activeGeneratedNotes = {};
    activePattern = {};
    recentSourceCount = 0;
    recentSourceWrite = 0;
    standaloneBeat = 0.0;
    previousTransportEnd = 0.0;
    observedBar = std::numeric_limits<std::int64_t>::min();
    lastPhraseIndex = std::numeric_limits<std::int64_t>::min();
    observedPhraseBars = 0;
    observedBeatsPerBar = 0.0;
    submittedGenerationRevision = 0;
    evolutionStep = 0;
    hasSubmittedRequest = false;
    pendingContextChange = true;
    wasPlaybackActive = false;
    previewWasEnabled = parameters.getRawParameterValue(ids::preview)->load() > 0.5f;
    processingEpoch.fetch_add(1, std::memory_order_acq_rel);
}

void PulsoAudioProcessor::releaseResources() { silencePreview(false); }

bool PulsoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

PulsoAudioProcessor::Transport PulsoAudioProcessor::readTransport(int numSamples) {
    Transport transport;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;

    if (auto* hostPlayHead = getPlayHead()) {
        if (const auto position = hostPlayHead->getPosition()) {
            transport.hostAvailable = true;
            transport.startBeat = position->getPpqPosition().orFallback(standaloneBeat);
            transport.bpm = std::max(1.0, position->getBpm().orFallback(120.0));
            transport.isPlaying = position->getIsPlaying();
            if (const auto signature = position->getTimeSignature())
                transport.beatsPerBar = std::max(1.0, static_cast<double>(signature->numerator) * 4.0 /
                                                       static_cast<double>(signature->denominator));
        }
    }

    const auto blockBeats = static_cast<double>(numSamples) / currentSampleRate * transport.bpm / 60.0;
    if (!transport.hostAvailable) {
        transport.startBeat = standaloneBeat;
        standaloneBeat += blockBeats;
    } else if (transport.isPlaying) {
        standaloneBeat = transport.startBeat + blockBeats;
    }
    transport.endBeat = transport.startBeat + blockBeats;
    tempo.store(transport.bpm, std::memory_order_relaxed);
    playing.store(transport.isPlaying, std::memory_order_relaxed);
    return transport;
}

void PulsoAudioProcessor::collectInput(const juce::MidiBuffer& input, double blockStartBeat,
                                       double beatsPerBar) {
    thruMidi.clear();
    for (const auto metadata : input) {
        const auto message = metadata.getMessage();
        const auto isVariationCommand = message.isNoteOnOrOff() && message.getChannel() == 16 &&
                                        message.getNoteNumber() == 127;
        if (isVariationCommand) {
            if (message.isNoteOn()) requestVariation();
            continue;
        }
        thruMidi.addEvent(message, metadata.samplePosition);

        if (message.isNoteOn()) {
            heldNotes[static_cast<std::size_t>(message.getNoteNumber())] = true;
            const auto beatOffset = static_cast<double>(metadata.samplePosition) / currentSampleRate *
                                    tempo.load(std::memory_order_relaxed) / 60.0;
            auto localBeat = std::fmod(blockStartBeat + beatOffset, beatsPerBar);
            if (localBeat < 0.0) localBeat += beatsPerBar;
            recentSourceNotes[recentSourceWrite] = {localBeat, message.getNoteNumber(), message.getVelocity()};
            recentSourceWrite = (recentSourceWrite + 1) % maxSourceNotes;
            recentSourceCount = std::min(recentSourceCount + 1, maxSourceNotes);
        } else if (message.isNoteOff()) {
            heldNotes[static_cast<std::size_t>(message.getNoteNumber())] = false;
        } else if (message.isAllNotesOff()) {
            heldNotes.fill(false);
        }
    }
}

bool PulsoAudioProcessor::captureHarmonyForBar(std::int64_t absoluteBar, int phraseBars) noexcept {
    HarmonySlot chord;
    for (auto pitch = 0; pitch < 128 && chord.size < maxHarmonyNotes; ++pitch) {
        if (!heldNotes[static_cast<std::size_t>(pitch)]) continue;
        const auto pitchClass = pitch % 12;
        const auto duplicate = std::find(chord.pitchClasses.begin(),
                                         chord.pitchClasses.begin() + chord.size, pitchClass) !=
                               chord.pitchClasses.begin() + chord.size;
        if (!duplicate) chord.pitchClasses[chord.size++] = pitchClass;
    }
    if (chord.size == 0) return false;

    const auto slot = static_cast<std::size_t>(positiveModulo(static_cast<int>(absoluteBar), phraseBars));
    const auto& previous = chordTimeline[slot];
    if (previous.size == chord.size &&
        std::equal(chord.pitchClasses.begin(), chord.pitchClasses.begin() + chord.size,
                   previous.pitchClasses.begin()))
        return false;
    chordTimeline[slot] = chord;
    return true;
}

PulsoAudioProcessor::GenerationRequest PulsoAudioProcessor::makeGenerationRequest(double beatsPerBar) noexcept {
    GenerationRequest request;
    request.role = static_cast<Role>(static_cast<int>(parameters.getRawParameterValue(ids::role)->load()));
    request.scale = static_cast<ScaleKind>(static_cast<int>(parameters.getRawParameterValue(ids::scale)->load()));
    request.rootPitchClass = static_cast<int>(parameters.getRawParameterValue(ids::root)->load());
    request.follow = parameters.getRawParameterValue(ids::follow)->load();
    request.risk = parameters.getRawParameterValue(ids::risk)->load();
    request.space = parameters.getRawParameterValue(ids::space)->load();
    request.repetition = parameters.getRawParameterValue(ids::repetition)->load();
    request.complexity = parameters.getRawParameterValue(ids::complexity)->load();
    request.development = parameters.getRawParameterValue(ids::development)->load();
    request.bars = currentPhraseBars();
    request.beatsPerBar = beatsPerBar;
    request.seed = variationSeed.load(std::memory_order_relaxed) * 0x9e3779b97f4a7c15ULL;
    request.evolutionStep = evolutionStep;
    request.serial = ++nextRequestSerial;
    request.epoch = processingEpoch.load(std::memory_order_acquire);
    request.harmony = chordTimeline;

    for (auto pitch = 0; pitch < 128 && request.heldChord.size < maxHarmonyNotes; ++pitch) {
        if (!heldNotes[static_cast<std::size_t>(pitch)]) continue;
        const auto pitchClass = pitch % 12;
        const auto duplicate = std::find(request.heldChord.pitchClasses.begin(),
                                         request.heldChord.pitchClasses.begin() + request.heldChord.size,
                                         pitchClass) != request.heldChord.pitchClasses.begin() + request.heldChord.size;
        if (!duplicate) request.heldChord.pitchClasses[request.heldChord.size++] = pitchClass;
    }

    request.sourceNoteCount = static_cast<std::uint16_t>(recentSourceCount);
    const auto oldest = recentSourceCount == maxSourceNotes ? recentSourceWrite : 0;
    for (std::size_t index = 0; index < recentSourceCount; ++index)
        request.sourceNotes[index] = recentSourceNotes[(oldest + index) % maxSourceNotes];
    return request;
}

GenerationContext PulsoAudioProcessor::expandContext(const GenerationRequest& request) {
    GenerationContext context;
    context.role = request.role;
    context.scale = request.scale;
    context.rootPitchClass = request.rootPitchClass;
    context.beatsPerBar = request.beatsPerBar;
    context.follow = request.follow;
    context.risk = request.risk;
    context.space = request.space;
    context.repetition = request.repetition;
    context.complexity = request.complexity;
    context.development = request.development;
    context.bars = request.bars;
    context.seed = request.seed;
    context.evolutionStep = request.evolutionStep;

    context.harmonyByBar.reserve(static_cast<std::size_t>(request.bars));
    for (auto bar = 0; bar < request.bars; ++bar) {
        const auto& slot = request.harmony[static_cast<std::size_t>(bar)];
        context.harmonyByBar.emplace_back(slot.pitchClasses.begin(), slot.pitchClasses.begin() + slot.size);
    }
    context.sourceNotes.assign(request.sourceNotes.begin(),
                               request.sourceNotes.begin() + request.sourceNoteCount);
    if (request.heldChord.size > 0) {
        context.chordPitchClasses.assign(request.heldChord.pitchClasses.begin(),
                                         request.heldChord.pitchClasses.begin() + request.heldChord.size);
    } else {
        const auto third = request.scale == ScaleKind::Minor || request.scale == ScaleKind::Dorian ? 3 : 4;
        context.chordPitchClasses = {request.rootPitchClass,
                                     positiveModulo(request.rootPitchClass + third, 12),
                                     positiveModulo(request.rootPitchClass + 7, 12)};
    }
    return context;
}

bool PulsoAudioProcessor::pushGenerationRequest(const GenerationRequest& request) noexcept {
    const auto write = requestWrite.load(std::memory_order_relaxed);
    const auto next = (write + 1) % requestQueueSize;
    if (next == requestRead.load(std::memory_order_acquire)) return false;
    requestQueue[write] = request;
    requestWrite.store(next, std::memory_order_release);
    return true;
}

bool PulsoAudioProcessor::popGenerationRequest(GenerationRequest& request) noexcept {
    const auto read = requestRead.load(std::memory_order_relaxed);
    if (read == requestWrite.load(std::memory_order_acquire)) return false;
    request = requestQueue[read];
    requestRead.store((read + 1) % requestQueueSize, std::memory_order_release);
    return true;
}

bool PulsoAudioProcessor::pushGeneratedPattern(const RealtimePattern& pattern) noexcept {
    const auto write = resultWrite.load(std::memory_order_relaxed);
    const auto next = (write + 1) % resultQueueSize;
    if (next == resultRead.load(std::memory_order_acquire)) return false;
    resultQueue[write] = pattern;
    resultWrite.store(next, std::memory_order_release);
    return true;
}

bool PulsoAudioProcessor::popGeneratedPattern(RealtimePattern& pattern) noexcept {
    const auto read = resultRead.load(std::memory_order_relaxed);
    if (read == resultWrite.load(std::memory_order_acquire)) return false;
    pattern = resultQueue[read];
    resultRead.store((read + 1) % resultQueueSize, std::memory_order_release);
    return true;
}

void PulsoAudioProcessor::generationThreadMain(const std::stop_token token) {
    while (!token.stop_requested()) {
        GenerationRequest request;
        GenerationRequest newest;
        auto found = false;
        while (popGenerationRequest(request)) {
            newest = request;
            found = true;
        }
        if (!found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        auto generated = generator.generate(expandContext(newest));
        RealtimePattern realtime;
        realtime.noteCount = static_cast<std::uint16_t>(
            std::min(generated.notes.size(), static_cast<std::size_t>(maxPatternNotes)));
        std::copy_n(generated.notes.begin(), realtime.noteCount, realtime.notes.begin());
        realtime.lengthBeats = generated.lengthBeats;
        realtime.seed = generated.seed;
        realtime.serial = newest.serial;
        realtime.epoch = newest.epoch;

        uiPatternSnapshot.store(std::make_shared<Pattern>(generated), std::memory_order_release);
        while (!token.stop_requested() && !pushGeneratedPattern(realtime))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool PulsoAudioProcessor::consumeLatestPattern() noexcept {
    RealtimePattern incoming;
    auto changed = false;
    const auto epoch = processingEpoch.load(std::memory_order_acquire);
    while (popGeneratedPattern(incoming)) {
        if (incoming.epoch != epoch || incoming.serial <= activePattern.serial) continue;
        activePattern = incoming;
        changed = true;
    }
    return changed;
}

void PulsoAudioProcessor::trackGeneratedMessage(const juce::MidiMessage& message) noexcept {
    if (!message.isNoteOnOrOff()) return;
    const auto channel = static_cast<std::size_t>(std::clamp(message.getChannel(), 1, 16) - 1);
    const auto note = static_cast<std::size_t>(std::clamp(message.getNoteNumber(), 0, 127));
    auto& count = activeGeneratedNotes[channel][note];
    if (message.isNoteOn()) {
        if (count < std::numeric_limits<std::uint8_t>::max()) ++count;
    } else if (count > 0) {
        --count;
    }
}

void PulsoAudioProcessor::sendGeneratedPanic(juce::MidiBuffer& output, int sampleOffset) {
    for (auto channel = 0; channel < 16; ++channel) {
        auto hadNotes = false;
        for (auto note = 0; note < 128; ++note) {
            auto& count = activeGeneratedNotes[static_cast<std::size_t>(channel)][static_cast<std::size_t>(note)];
            if (count == 0) continue;
            output.addEvent(juce::MidiMessage::noteOff(channel + 1, note), sampleOffset);
            count = 0;
            hadNotes = true;
        }
        if (hadNotes || channel == 0 || channel == 9)
            output.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 123, 0), sampleOffset);
    }
}

void PulsoAudioProcessor::schedulePattern(const RealtimePattern& pattern, const Transport& transport,
                                          int numSamples, juce::MidiBuffer& output,
                                          bool retriggerOverlaps) {
    if (pattern.noteCount == 0 || pattern.lengthBeats <= 0.0 || numSamples <= 0) return;
    const auto firstCycle = static_cast<std::int64_t>(std::floor(transport.startBeat / pattern.lengthBeats)) - 1;
    const auto lastCycle = static_cast<std::int64_t>(std::floor(transport.endBeat / pattern.lengthBeats)) + 1;
    const auto samplesPerBeat = currentSampleRate * 60.0 / transport.bpm;

    const auto addAtBeat = [&](const juce::MidiMessage& message, double absoluteBeat) {
        if (absoluteBeat < transport.startBeat || absoluteBeat >= transport.endBeat) return;
        const auto offset = static_cast<int>(std::floor((absoluteBeat - transport.startBeat) * samplesPerBeat));
        output.addEvent(message, std::clamp(offset, 0, numSamples - 1));
        trackGeneratedMessage(message);
    };

    for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle) {
        const auto cycleStart = static_cast<double>(cycle) * pattern.lengthBeats;
        for (std::size_t index = 0; index < pattern.noteCount; ++index) {
            const auto& note = pattern.notes[index];
            const auto noteStart = cycleStart + note.startBeat;
            const auto noteEnd = cycleStart + note.endBeat();
            if (retriggerOverlaps && noteStart < transport.startBeat && noteEnd > transport.startBeat) {
                auto on = juce::MidiMessage::noteOn(note.channel, note.pitch,
                                                    static_cast<juce::uint8>(note.velocity));
                output.addEvent(on, 0);
                trackGeneratedMessage(on);
            }
            addAtBeat(juce::MidiMessage::noteOn(note.channel, note.pitch,
                                                static_cast<juce::uint8>(note.velocity)), noteStart);
            addAtBeat(juce::MidiMessage::noteOff(note.channel, note.pitch), noteEnd);
        }
    }
}

void PulsoAudioProcessor::scheduleOverlappingPreviewNotes(const RealtimePattern& pattern,
                                                          const Transport& transport,
                                                          juce::MidiBuffer& output) const {
    if (pattern.noteCount == 0 || pattern.lengthBeats <= 0.0) return;
    const auto cycle = static_cast<std::int64_t>(std::floor(transport.startBeat / pattern.lengthBeats));
    for (auto candidateCycle = cycle - 1; candidateCycle <= cycle; ++candidateCycle) {
        const auto cycleStart = static_cast<double>(candidateCycle) * pattern.lengthBeats;
        for (std::size_t index = 0; index < pattern.noteCount; ++index) {
            const auto& note = pattern.notes[index];
            const auto start = cycleStart + note.startBeat;
            if (start < transport.startBeat && cycleStart + note.endBeat() > transport.startBeat)
                output.addEvent(juce::MidiMessage::noteOn(note.channel, note.pitch,
                                                          static_cast<juce::uint8>(note.velocity)), 0);
        }
    }
}

void PulsoAudioProcessor::silencePreview(bool allowTailOff) noexcept {
    for (auto channel = 1; channel <= 16; ++channel)
        previewSynth.allNotesOff(channel, allowTailOff);
}

void PulsoAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    generatedMidi.clear();
    previewMidi.clear();

    const auto transport = readTransport(audio.getNumSamples());
    collectInput(midi, transport.startBeat, transport.beatsPerBar);

    const auto currentBar = static_cast<std::int64_t>(std::floor(transport.startBeat / transport.beatsPerBar));
    const auto phraseBars = currentPhraseBars();
    const auto barChanged = currentBar != observedBar;
    const auto harmonyChanged = barChanged && captureHarmonyForBar(currentBar, phraseBars);
    const auto phraseIndex = static_cast<std::int64_t>(std::floor(static_cast<double>(currentBar) / phraseBars));
    const auto evolveMode = parameters.getRawParameterValue(ids::mode)->load() > 0.5f;
    auto evolutionChanged = false;
    if (barChanged && lastPhraseIndex != std::numeric_limits<std::int64_t>::min() &&
        phraseIndex != lastPhraseIndex && evolveMode) {
        ++evolutionStep;
        evolutionChanged = true;
    }

    const auto meterChanged = std::abs(observedBeatsPerBar - transport.beatsPerBar) > 0.001;
    const auto lengthChanged = observedPhraseBars != phraseBars;
    if (harmonyChanged || evolutionChanged || meterChanged || lengthChanged || !hasSubmittedRequest)
        pendingContextChange = true;
    observedBar = currentBar;
    lastPhraseIndex = phraseIndex;
    observedPhraseBars = phraseBars;
    observedBeatsPerBar = transport.beatsPerBar;

    const auto revision = generationRevision.load(std::memory_order_acquire);
    if (pendingContextChange || revision != submittedGenerationRevision) {
        const auto request = makeGenerationRequest(transport.beatsPerBar);
        if (pushGenerationRequest(request)) {
            submittedGenerationRevision = revision;
            hasSubmittedRequest = true;
            pendingContextChange = false;
        }
    }

    const auto patternChanged = consumeLatestPattern();
    const auto playbackActive = transport.isPlaying || !transport.hostAvailable;
    const auto blockBeats = transport.endBeat - transport.startBeat;
    const auto discontinuityTolerance = std::max(0.01, blockBeats * 1.5);
    const auto transportDiscontinuity = wasPlaybackActive && playbackActive &&
                                        std::abs(transport.startBeat - previousTransportEnd) >
                                            discontinuityTolerance;
    const auto transportStarted = !wasPlaybackActive && playbackActive;
    const auto transportStopped = wasPlaybackActive && !playbackActive;
    if (transportStopped || transportDiscontinuity || patternChanged)
        sendGeneratedPanic(generatedMidi, 0);

    const auto retrigger = transportStarted || transportDiscontinuity || patternChanged;
    if (playbackActive)
        schedulePattern(activePattern, transport, audio.getNumSamples(), generatedMidi, retrigger);

    const auto thruEnabled = parameters.getRawParameterValue(ids::thru)->load() > 0.5f;
    midi.clear();
    if (thruEnabled) midi.addEvents(thruMidi, 0, -1, 0);
    midi.addEvents(generatedMidi, 0, -1, 0);

    const auto previewEnabled = parameters.getRawParameterValue(ids::preview)->load() > 0.5f;
    if (!previewEnabled && previewWasEnabled) silencePreview(true);
    if (previewEnabled) {
        previewMidi.addEvents(generatedMidi, 0, -1, 0);
        if (!previewWasEnabled && playbackActive && !retrigger)
            scheduleOverlappingPreviewNotes(activePattern, transport, previewMidi);
    }
    previewSynth.renderNextBlock(audio, previewMidi, 0, audio.getNumSamples());

    const auto targetGain = juce::Decibels::decibelsToGain(parameters.getRawParameterValue(ids::gain)->load());
    previewGain.setTargetValue(targetGain);
    if (audio.getNumSamples() > 0) {
        const auto startGain = previewGain.getCurrentValue();
        const auto endGain = previewGain.skip(audio.getNumSamples());
        audio.applyGainRamp(0, audio.getNumSamples(), startGain, endGain);
        juce::dsp::AudioBlock<float> block(audio);
        juce::dsp::ProcessContextReplacing<float> context(block);
        previewLimiter.process(context);
    }

    previewWasEnabled = previewEnabled;
    wasPlaybackActive = playbackActive;
    previousTransportEnd = transport.endBeat;
}

void PulsoAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    auto state = parameters.copyState();
    state.setProperty("variationSeed", static_cast<juce::int64>(variationSeed.load(std::memory_order_relaxed)), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destination);
}

void PulsoAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(parameters.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xml);
            variationSeed.store(static_cast<std::uint64_t>(static_cast<juce::int64>(
                                    state.getProperty("variationSeed", 1))), std::memory_order_relaxed);
            parameters.replaceState(state);
        }
    }
    generationRevision.fetch_add(1, std::memory_order_release);
}

juce::AudioProcessorEditor* PulsoAudioProcessor::createEditor() { return new PulsoAudioProcessorEditor(*this); }

} // namespace pulso::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pulso::plugin::PulsoAudioProcessor(); }
