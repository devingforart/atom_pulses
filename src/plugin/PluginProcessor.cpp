#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PreviewSynth.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pulso::plugin {
namespace ids {
constexpr auto role = "role";
constexpr auto scale = "scale";
constexpr auto root = "root";
constexpr auto follow = "follow";
constexpr auto risk = "risk";
constexpr auto space = "space";
constexpr auto preview = "preview";
constexpr auto thru = "thru";
constexpr auto gain = "gain";
constexpr std::array generative{role, scale, root, follow, risk, space};
} // namespace ids

PulsoAudioProcessor::PulsoAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PULSO_STATE", createParameterLayout()) {
    initialisePreviewSynth(previewSynth);
    patternSnapshot.store(std::make_shared<Pattern>(), std::memory_order_release);
    for (const auto* parameterId : ids::generative) parameters.addParameterListener(parameterId, this);
}

PulsoAudioProcessor::~PulsoAudioProcessor() {
    for (const auto* parameterId : ids::generative) parameters.removeParameterListener(parameterId, this);
}

void PulsoAudioProcessor::parameterChanged(const juce::String&, float) { requestVariation(); }

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
    result.push_back(std::make_unique<Bool>(ids::preview, "Preview", true));
    result.push_back(std::make_unique<Bool>(ids::thru, "MIDI Thru", false));
    result.push_back(std::make_unique<Float>(ids::gain, "Output", juce::NormalisableRange<float>(-36.0f, 0.0f, 0.1f), -12.0f));
    return {result.begin(), result.end()};
}

void PulsoAudioProcessor::prepareToPlay(double sampleRate, int) {
    currentSampleRate = sampleRate;
    previewSynth.setCurrentPlaybackSampleRate(sampleRate);
    standaloneBeat = 0.0;
    generatedBar = std::numeric_limits<std::int64_t>::min();
}

void PulsoAudioProcessor::releaseResources() {}

bool PulsoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

PulsoAudioProcessor::Transport PulsoAudioProcessor::readTransport(int numSamples) {
    Transport transport;
    transport.bpm = 120.0;
    transport.beatsPerBar = 4.0;
    transport.isPlaying = false;

    if (auto* hostPlayHead = getPlayHead()) {
        transport.hostAvailable = true;
        if (const auto position = hostPlayHead->getPosition()) {
            transport.startBeat = position->getPpqPosition().orFallback(standaloneBeat);
            transport.bpm = position->getBpm().orFallback(120.0);
            transport.isPlaying = position->getIsPlaying();
            if (const auto signature = position->getTimeSignature())
                transport.beatsPerBar = static_cast<double>(signature->numerator) * 4.0 /
                                        static_cast<double>(signature->denominator);
        }
    }

    const auto blockBeats = static_cast<double>(numSamples) / currentSampleRate * transport.bpm / 60.0;
    if (!transport.isPlaying) {
        transport.startBeat = standaloneBeat;
        standaloneBeat += blockBeats;
    } else {
        standaloneBeat = transport.startBeat + blockBeats;
    }
    transport.endBeat = transport.startBeat + blockBeats;
    tempo.store(transport.bpm, std::memory_order_relaxed);
    playing.store(transport.isPlaying, std::memory_order_relaxed);
    return transport;
}

void PulsoAudioProcessor::collectInput(const juce::MidiBuffer& input, double blockStartBeat, double beatsPerBar) {
    for (const auto metadata : input) {
        const auto message = metadata.getMessage();
        if (message.isNoteOnOrOff() && message.getChannel() == 16 && message.getNoteNumber() == 127) {
            if (message.isNoteOn()) requestVariation();
            continue;
        }
        if (message.isNoteOn()) {
            heldNotes[static_cast<std::size_t>(message.getNoteNumber())] = true;
            const auto beatOffset = static_cast<double>(metadata.samplePosition) / currentSampleRate *
                                    tempo.load(std::memory_order_relaxed) / 60.0;
            recentSourceNotes.push_back({std::fmod(blockStartBeat + beatOffset, beatsPerBar),
                                         message.getNoteNumber(), message.getVelocity()});
        } else if (message.isNoteOff()) {
            heldNotes[static_cast<std::size_t>(message.getNoteNumber())] = false;
        } else if (message.isAllNotesOff()) {
            heldNotes.fill(false);
        }
    }
    if (recentSourceNotes.size() > 128)
        recentSourceNotes.erase(recentSourceNotes.begin(), recentSourceNotes.end() - 128);
}

GenerationContext PulsoAudioProcessor::buildContext(double beatsPerBar, std::uint64_t seed) const {
    GenerationContext context;
    context.role = static_cast<Role>(static_cast<int>(parameters.getRawParameterValue(ids::role)->load()));
    context.scale = static_cast<ScaleKind>(static_cast<int>(parameters.getRawParameterValue(ids::scale)->load()));
    context.rootPitchClass = static_cast<int>(parameters.getRawParameterValue(ids::root)->load());
    context.follow = parameters.getRawParameterValue(ids::follow)->load();
    context.risk = parameters.getRawParameterValue(ids::risk)->load();
    context.space = parameters.getRawParameterValue(ids::space)->load();
    context.beatsPerBar = beatsPerBar;
    context.seed = seed;
    context.sourceNotes = recentSourceNotes;

    std::set<int> chord;
    for (int pitch = 0; pitch < 128; ++pitch)
        if (heldNotes[static_cast<std::size_t>(pitch)]) chord.insert(pitch % 12);
    if (chord.empty()) {
        const auto third = context.scale == ScaleKind::Major || context.scale == ScaleKind::Mixolydian ? 4 : 3;
        context.chordPitchClasses = {context.rootPitchClass,
                                     positiveModulo(context.rootPitchClass + third, 12),
                                     positiveModulo(context.rootPitchClass + 7, 12)};
    } else {
        context.chordPitchClasses.assign(chord.begin(), chord.end());
    }
    return context;
}

void PulsoAudioProcessor::regenerate(double beatsPerBar) {
    const auto request = variationRequests.load(std::memory_order_relaxed);
    auto context = buildContext(beatsPerBar, request * 0x9e3779b97f4a7c15ULL);
    auto next = std::make_shared<Pattern>(generator.generate(context));
    patternSnapshot.store(std::move(next), std::memory_order_release);
    handledVariationRequests = request;
    recentSourceNotes.clear();
}

void PulsoAudioProcessor::schedulePattern(const Pattern& pattern, const Transport& transport,
                                          int numSamples, juce::MidiBuffer& output) const {
    if (pattern.notes.empty() || pattern.lengthBeats <= 0.0) return;
    const auto firstCycle = static_cast<std::int64_t>(std::floor(transport.startBeat / pattern.lengthBeats)) - 1;
    const auto lastCycle = static_cast<std::int64_t>(std::floor(transport.endBeat / pattern.lengthBeats)) + 1;
    const auto samplesPerBeat = currentSampleRate * 60.0 / transport.bpm;

    const auto addAtBeat = [&](const juce::MidiMessage& message, double absoluteBeat) {
        if (absoluteBeat < transport.startBeat || absoluteBeat >= transport.endBeat) return;
        const auto offset = static_cast<int>(std::floor((absoluteBeat - transport.startBeat) * samplesPerBeat));
        output.addEvent(message, std::clamp(offset, 0, numSamples - 1));
    };

    for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle) {
        const auto cycleStart = static_cast<double>(cycle) * pattern.lengthBeats;
        for (const auto& note : pattern.notes) {
            addAtBeat(juce::MidiMessage::noteOn(note.channel, note.pitch,
                                                static_cast<juce::uint8>(note.velocity)),
                      cycleStart + note.startBeat);
            addAtBeat(juce::MidiMessage::noteOff(note.channel, note.pitch),
                      cycleStart + note.endBeat());
        }
    }
}

void PulsoAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    const auto transport = readTransport(audio.getNumSamples());
    const auto inputMidi = midi;
    collectInput(inputMidi, transport.startBeat, transport.beatsPerBar);

    const auto currentBar = static_cast<std::int64_t>(std::floor(transport.startBeat / transport.beatsPerBar));
    const auto requests = variationRequests.load(std::memory_order_relaxed);
    if (currentBar != generatedBar || requests != handledVariationRequests) {
        regenerate(transport.beatsPerBar);
        generatedBar = currentBar;
    }

    juce::MidiBuffer generated;
    if (transport.isPlaying || !transport.hostAvailable)
        if (const auto pattern = patternSnapshot.load(std::memory_order_acquire))
            schedulePattern(*pattern, transport, audio.getNumSamples(), generated);

    midi.clear();
    if (parameters.getRawParameterValue(ids::thru)->load() > 0.5f) midi.addEvents(inputMidi, 0, -1, 0);
    midi.addEvents(generated, 0, -1, 0);

    if (parameters.getRawParameterValue(ids::preview)->load() > 0.5f) {
        previewSynth.renderNextBlock(audio, generated, 0, audio.getNumSamples());
        const auto gainDb = parameters.getRawParameterValue(ids::gain)->load();
        audio.applyGain(juce::Decibels::decibelsToGain(gainDb));
    }
}

void PulsoAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, destination);
}

void PulsoAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*xml));
    requestVariation();
}

juce::AudioProcessorEditor* PulsoAudioProcessor::createEditor() { return new PulsoAudioProcessorEditor(*this); }

} // namespace pulso::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pulso::plugin::PulsoAudioProcessor(); }
