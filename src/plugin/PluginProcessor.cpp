#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "core/Scale.h"
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
constexpr auto groove = "groove";
constexpr auto humanize = "humanize";
constexpr auto cohesion = "cohesion";
constexpr auto energy = "energy";
constexpr auto phraseBars = "phraseBars";
constexpr auto mode = "mode";
constexpr auto preview = "preview";
constexpr auto thru = "thru";
constexpr auto gain = "gain";
constexpr std::array generative{role, scale, root, follow, risk, space, repetition,
                                complexity, development, groove, humanize, cohesion,
                                energy, phraseBars, mode};
constexpr std::array phraseLengths{1, 2, 4, 8, 16};

juce::String songPlanToJson(const SongPlan& plan) {
    auto* jsonRoot = new juce::DynamicObject();
    jsonRoot->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    jsonRoot->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    jsonRoot->setProperty("summary", juce::String::fromUTF8(plan.summary.c_str()));
    jsonRoot->setProperty("root_pitch_class", plan.rootPitchClass);
    jsonRoot->setProperty("mode", plan.scale == ScaleKind::Major ? "major" :
                              plan.scale == ScaleKind::Dorian ? "dorian" :
                              plan.scale == ScaleKind::Mixolydian ? "mixolydian" : "minor");
    juce::Array<juce::var> motif;
    for (const auto value : plan.motifIntervals) motif.add(value);
    jsonRoot->setProperty("motif_intervals", motif);
    juce::Array<juce::var> chords;
    for (const auto value : plan.chordDegrees) chords.add(value);
    jsonRoot->setProperty("chord_degrees", chords);
    juce::Array<juce::var> voices;
    for (const auto& voice : plan.voices) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", juce::String(voiceDefinition(voice.id).key.data()));
        item->setProperty("function", juce::String::fromUTF8(voice.function.c_str()));
        item->setProperty("interaction", juce::String::fromUTF8(voice.interaction.c_str()));
        item->setProperty("activity", voice.activity);
        item->setProperty("syncopation", voice.syncopation);
        item->setProperty("minimum_pitch", voice.minimumPitch);
        item->setProperty("maximum_pitch", voice.maximumPitch);
        voices.add(juce::var(item));
    }
    jsonRoot->setProperty("voices", voices);
    juce::Array<juce::var> sections;
    for (const auto& section : plan.sections) {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", juce::String::fromUTF8(section.name.c_str()));
        item->setProperty("function", juce::String::fromUTF8(section.function.c_str()));
        item->setProperty("harmonic_direction", juce::String::fromUTF8(section.harmonicDirection.c_str()));
        item->setProperty("motif_treatment", juce::String::fromUTF8(section.motifTreatment.c_str()));
        item->setProperty("bars", section.bars);
        item->setProperty("energy", section.energy);
        item->setProperty("tension", section.tension);
        item->setProperty("density", section.density);
        item->setProperty("motif_variant", section.motifVariant);
        juce::Array<juce::var> activeVoices;
        for (const auto voice : section.activeVoices)
            activeVoices.add(juce::String(voiceDefinition(voice).key.data()));
        item->setProperty("active_voices", activeVoices);
        sections.add(juce::var(item));
    }
    jsonRoot->setProperty("sections", sections);
    return juce::JSON::toString(juce::var(jsonRoot), true);
}
} // namespace ids

PulsoAudioProcessor::PulsoAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PULSO_STATE", createParameterLayout()) {
    uiPatternSnapshot.store(std::make_shared<Pattern>(), std::memory_order_release);
    previousPatternSnapshot.store(std::make_shared<Pattern>(), std::memory_order_release);
    songPlanSnapshot.store(std::make_shared<SongPlan>(), std::memory_order_release);
    retiredRealtimeSnapshot.store(nullptr, std::memory_order_release);
    ideaMetadata.store(std::make_shared<IdeaMetadata>(), std::memory_order_release);
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

void PulsoAudioProcessor::setCreativeDirection(const juce::String& direction) {
    const std::scoped_lock lock(creativeDirectionMutex);
    creativeDirection = direction.substring(0, 600);
}

void PulsoAudioProcessor::setTargetSongDurationSeconds(int seconds) noexcept {
    songDurationSeconds.store(seconds <= 0 ? 0 : std::clamp(seconds, 30, 1800),
                              std::memory_order_relaxed);
}

void PulsoAudioProcessor::requestGenerateIdea() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationProgress.store(0.0f, std::memory_order_relaxed);
    compositionSeed.fetch_add(1, std::memory_order_relaxed);
    variationIndex.store(0, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Generate, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestRegenerateUnlocked() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationProgress.store(0.0f, std::memory_order_relaxed);
    variationIndex.fetch_add(1, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Regenerate, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestNextIdea() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationProgress.store(0.0f, std::memory_order_relaxed);
    compositionSeed.fetch_add(1, std::memory_order_relaxed);
    variationIndex.store(0, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Next, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestUndo() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationProgress.store(0.0f, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Undo, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::setLayerLocked(Layer layer, bool shouldLock) noexcept {
    const auto bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(layer));
    if (shouldLock)
        lockedLayers.fetch_or(bit, std::memory_order_relaxed);
    else
        lockedLayers.fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_relaxed);
}

bool PulsoAudioProcessor::isLayerLocked(Layer layer) const noexcept {
    const auto bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(layer));
    return (lockedLayers.load(std::memory_order_relaxed) & bit) != 0;
}

juce::String PulsoAudioProcessor::currentAiStatus() const {
    if (const auto metadata = ideaMetadata.load(std::memory_order_acquire)) return metadata->status;
    return {};
}

juce::String PulsoAudioProcessor::currentIdeaTitle() const {
    if (const auto metadata = ideaMetadata.load(std::memory_order_acquire))
        return metadata->title + "  ·  " + metadata->key;
    return {};
}

juce::String PulsoAudioProcessor::currentIdeaDescription() const {
    if (const auto metadata = ideaMetadata.load(std::memory_order_acquire)) return metadata->description;
    return {};
}

juce::String PulsoAudioProcessor::currentCreativeDirection() const {
    const std::scoped_lock lock(creativeDirectionMutex);
    return creativeDirection;
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
                                              juce::StringArray{"Bass", "Percussion", "Countermelody", "Ensemble"}, 3));
    result.push_back(std::make_unique<Choice>(ids::scale, "Scale",
                                              juce::StringArray{"Major", "Minor", "Dorian", "Mixolydian", "Chromatic"}, 1));
    result.push_back(std::make_unique<Int>(ids::root, "Root", 0, 11, 0));
    result.push_back(std::make_unique<Float>(ids::follow, "Follow", 0.0f, 1.0f, 0.65f));
    result.push_back(std::make_unique<Float>(ids::risk, "Risk", 0.0f, 1.0f, 0.30f));
    result.push_back(std::make_unique<Float>(ids::space, "Space (Legacy)", 0.0f, 1.0f, 0.0f));
    result.push_back(std::make_unique<Float>(ids::repetition, "Repetition", 0.0f, 1.0f, 0.78f));
    result.push_back(std::make_unique<Float>(ids::complexity, "Complexity", 0.0f, 1.0f, 0.45f));
    result.push_back(std::make_unique<Float>(ids::development, "Development", 0.0f, 1.0f, 0.45f));
    result.push_back(std::make_unique<Float>(ids::groove, "Groove (Legacy)", 0.0f, 1.0f, 0.0f));
    result.push_back(std::make_unique<Float>(ids::humanize, "Humanize", 0.0f, 1.0f, 0.32f));
    result.push_back(std::make_unique<Float>(ids::cohesion, "Cohesion", 0.0f, 1.0f, 0.82f));
    result.push_back(std::make_unique<Float>(ids::energy, "Energy", 0.0f, 1.0f, 0.56f));
    result.push_back(std::make_unique<Choice>(ids::phraseBars, "Phrase Length",
                                               juce::StringArray{"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"}, 3));
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
            if (const auto signature = position->getTimeSignature()) {
                transport.beatsPerBar = std::max(1.0, static_cast<double>(signature->numerator) * 4.0 /
                                                       static_cast<double>(signature->denominator));
                timeSignatureNumerator.store(signature->numerator, std::memory_order_relaxed);
                timeSignatureDenominator.store(signature->denominator, std::memory_order_relaxed);
            }
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
    request.space = 0.0;
    request.repetition = parameters.getRawParameterValue(ids::repetition)->load();
    request.complexity = parameters.getRawParameterValue(ids::complexity)->load();
    request.development = parameters.getRawParameterValue(ids::development)->load();
    request.groove = 0.0;
    request.humanize = parameters.getRawParameterValue(ids::humanize)->load();
    request.cohesion = parameters.getRawParameterValue(ids::cohesion)->load();
    request.energy = parameters.getRawParameterValue(ids::energy)->load();
    request.bars = currentPhraseBars();
    request.beatsPerBar = beatsPerBar;
    request.seed = compositionSeed.load(std::memory_order_relaxed) * 0x9e3779b97f4a7c15ULL;
    request.variationIndex = variationIndex.load(std::memory_order_relaxed);
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
    request.action = static_cast<std::uint8_t>(pendingIdeaAction.load(std::memory_order_acquire));
    request.lockedLayers = lockedLayers.load(std::memory_order_relaxed);
    request.targetSongSeconds = songDurationSeconds.load(std::memory_order_relaxed);
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
    context.groove = request.groove;
    context.humanize = request.humanize;
    context.cohesion = request.cohesion;
    context.energy = request.energy;
    context.bars = request.bars;
    context.seed = request.seed;
    context.variationIndex = request.variationIndex;
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

void PulsoAudioProcessor::addHarmonyLayer(Pattern& pattern, const GenerationContext& context) {
    const auto bars = std::max(1, context.bars);
    for (auto bar = 0; bar < bars; ++bar) {
        auto chord = bar < static_cast<int>(context.harmonyByBar.size())
                       ? context.harmonyByBar[static_cast<std::size_t>(bar)]
                       : context.chordPitchClasses;
        if (chord.empty()) chord = {context.rootPitchClass, positiveModulo(context.rootPitchClass + 3, 12),
                                    positiveModulo(context.rootPitchClass + 7, 12)};
        const auto start = bar * context.beatsPerBar;
        auto previous = 47;
        for (std::size_t index = 0; index < chord.size() && index < 4; ++index) {
            auto pitch = pitchClassToMidi(positiveModulo(chord[index], 12), 4, 48, 72);
            while (pitch <= previous && pitch + 12 <= 72) pitch += 12;
            previous = pitch;
            pattern.notes.push_back({start, std::max(0.25, context.beatsPerBar - 0.08),
                                     pitch, 67 + static_cast<int>(index) * 3, 3});
        }
    }
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.channel < b.channel;
    });
}

void PulsoAudioProcessor::preserveLockedLayers(Pattern& generated, const Pattern& previous,
                                                std::uint8_t mask) {
    constexpr std::array families{VoiceFamily::Harmony, VoiceFamily::Melodic,
                                  VoiceFamily::Bass, VoiceFamily::Rhythm};
    const auto familyForVoice = [](VoiceId voice, int channel) {
        const auto resolved = voice == VoiceId::Unspecified ? inferVoiceFromChannel(channel) : voice;
        auto family = voiceDefinition(resolved).family;
        if (family == VoiceFamily::Texture) family = VoiceFamily::Harmony;
        return family;
    };
    for (std::size_t layer = 0; layer < families.size(); ++layer) {
        if ((mask & (1u << layer)) == 0) continue;
        const auto family = families[layer];
        generated.notes.erase(std::remove_if(generated.notes.begin(), generated.notes.end(),
                                              [&](const auto& note) {
                                                  return familyForVoice(note.voice, note.channel) == family;
                                              }),
                              generated.notes.end());
        for (const auto& note : previous.notes)
            if (familyForVoice(note.voice, note.channel) == family &&
                note.startBeat < generated.lengthBeats)
                generated.notes.push_back(note);
        generated.controls.erase(std::remove_if(generated.controls.begin(), generated.controls.end(),
                                                 [&](const auto& control) {
                                                     return familyForVoice(control.voice, control.channel) == family;
                                                 }), generated.controls.end());
        for (const auto& control : previous.controls)
            if (familyForVoice(control.voice, control.channel) == family &&
                control.beat < generated.lengthBeats)
                generated.controls.push_back(control);
    }
    std::sort(generated.notes.begin(), generated.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        return a.channel < b.channel;
    });
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
        retiredRealtimeSnapshot.exchange(nullptr, std::memory_order_acq_rel);
        GenerationRequest request;
        GenerationRequest newest;
        auto found = false;
        auto newestExplicitAction = static_cast<std::uint8_t>(IdeaAction::None);
        while (popGenerationRequest(request)) {
            newest = request;
            if (request.action != static_cast<std::uint8_t>(IdeaAction::None))
                newestExplicitAction = request.action;
            found = true;
        }
        if (!found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (newestExplicitAction != static_cast<std::uint8_t>(IdeaAction::None))
            newest.action = newestExplicitAction;
        const auto action = static_cast<IdeaAction>(newest.action);
        const auto current = uiPatternSnapshot.load(std::memory_order_acquire);
        const auto previous = previousPatternSnapshot.load(std::memory_order_acquire);
        Pattern generated;
        auto metadata = std::make_shared<IdeaMetadata>();

        if ((action == IdeaAction::Undo && previous && !previous->notes.empty()) ||
            (action == IdeaAction::Restore && current && !current->notes.empty())) {
            generated = action == IdeaAction::Undo ? *previous : *current;
            if (action == IdeaAction::Undo) {
                metadata->title = "Previous Idea";
                metadata->key = "Restored";
                metadata->description = "Undo restored the complete previous composition.";
                metadata->status = "UNDO RESTORED";
            } else if (const auto restoredMetadata = ideaMetadata.load(std::memory_order_acquire)) {
                *metadata = *restoredMetadata;
                metadata->status = "PROJECT IDEA RESTORED";
            }
        } else {
            const auto context = expandContext(newest);
            const auto explicitIdeaRequest = action == IdeaAction::Generate ||
                                             action == IdeaAction::Regenerate ||
                                             action == IdeaAction::Next;
            const auto isSongRequest = explicitIdeaRequest && newest.targetSongSeconds > 0;
            juce::String songDirection;
            {
                const std::scoped_lock lock(creativeDirectionMutex);
                songDirection = creativeDirection;
            }
            if (isSongRequest) {
                const auto totalBars = std::clamp(static_cast<int>(std::lround(
                    newest.targetSongSeconds * currentTempo() / 60.0 / newest.beatsPerBar)), 8, 512);
                auto plan = SongPlan{};
                auto reusedPlan = false;
                if (action == IdeaAction::Regenerate) {
                    if (const auto existingPlan = songPlanSnapshot.load(std::memory_order_acquire);
                        existingPlan && !existingPlan->sections.empty() && existingPlan->totalBars == totalBars) {
                        plan = *existingPlan;
                        reusedPlan = true;
                    }
                }

                juce::String aiError;
                auto usedAiPlan = false;
                if (!reusedPlan && AiComposer::hasApiKey()) {
                    auto thinking = std::make_shared<IdeaMetadata>(*metadata);
                    thinking->status = "GPT ARCHITECTING FULL SONG...";
                    thinking->description = "Designing form, thematic DNA, harmonic narrative and dramatic curve.";
                    ideaMetadata.store(thinking, std::memory_order_release);
                    generationProgress.store(0.06f, std::memory_order_relaxed);
                    plan = AiComposer::planSong(songDirection, newest.targetSongSeconds, totalBars,
                                                currentTempo(), newest.beatsPerBar, newest.seed,
                                                token, aiError);
                    usedAiPlan = !plan.sections.empty();
                }
                if (plan.sections.empty()) {
                    plan = SongComposer::createLocalPlan(songDirection.toStdString(),
                        newest.targetSongSeconds, currentTempo(), newest.beatsPerBar,
                        newest.seed, context.rootPitchClass, context.scale);
                }
                plan.seed = newest.seed;
                plan.targetSeconds = newest.targetSongSeconds;
                SongComposer::normalizePlan(plan);
                songPlanSnapshot.store(std::make_shared<SongPlan>(plan), std::memory_order_release);
                generationProgress.store(0.14f, std::memory_order_relaxed);

                metadata->title = juce::String::fromUTF8(plan.title.c_str());
                metadata->key = juce::String::fromUTF8(plan.key.c_str());
                metadata->description = juce::String::fromUTF8(plan.summary.c_str());
                auto songContext = context;
                songContext.variationIndex = newest.variationIndex;
                generated = songComposer.render(plan, songContext,
                    [this, metadata](std::size_t completed, std::size_t total,
                                     const SongSection& section) {
                        const auto fraction = total == 0 ? 1.0f
                            : static_cast<float>(completed) / static_cast<float>(total);
                        generationProgress.store(0.14f + fraction * 0.84f,
                                                 std::memory_order_relaxed);
                        auto progressMetadata = std::make_shared<IdeaMetadata>(*metadata);
                        progressMetadata->status = "RENDERING " + juce::String(completed) + "/" +
                            juce::String(total) + " - " +
                            juce::String::fromUTF8(section.name.c_str()).toUpperCase();
                        ideaMetadata.store(progressMetadata, std::memory_order_release);
                    });
                generated.seed = newest.seed;
                metadata->status = usedAiPlan ? "GPT SONG PLAN - VALIDATED - FULL SONG"
                    : reusedPlan ? "SONG RECOMPOSED - STRUCTURE PRESERVED"
                    : aiError.isNotEmpty() ? "LOCAL SONG FALLBACK - GPT UNAVAILABLE"
                                           : "LOCAL LONG-FORM ENGINE";
                if (aiError.isNotEmpty())
                    metadata->description += " Local rendering remained available because: " + aiError;
            } else {
            auto usedAI = false;
            juce::String aiError;
            if (explicitIdeaRequest && AiComposer::hasApiKey()) {
                metadata->status = "GPT COMPOSING…";
                ideaMetadata.store(std::make_shared<IdeaMetadata>(*metadata),
                                   std::memory_order_release);
                juce::String direction;
                {
                    const std::scoped_lock lock(creativeDirectionMutex);
                    direction = creativeDirection;
                }
                auto ai = AiComposer::compose(direction, newest.bars, currentTempo(),
                                              current.get(), newest.lockedLayers, token, aiError);
                if (!ai.pattern.notes.empty()) {
                    generated = std::move(ai.pattern);
                    generated.seed = newest.seed;
                    metadata->title = ai.title;
                    metadata->key = ai.key;
                    metadata->description = ai.summary;
                    metadata->status = "GPT-5.6 TERRA · VALIDATED";
                    usedAI = true;
                }
            }
            if (!usedAI) {
                generated = generator.generate(context);
                addHarmonyLayer(generated, context);
                metadata->title = explicitIdeaRequest ? "New Local Idea" : "Local Idea";
                metadata->key = juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
                                    [std::clamp(context.rootPitchClass, 0, 11)] + " " +
                                (context.scale == ScaleKind::Major ? "major" : "minor");
                metadata->description = aiError.isNotEmpty()
                    ? "GPT unavailable: " + aiError + ". Generated safely with the local composition engine."
                    : "Coherent deterministic composition generated locally. Add OPENAI_API_KEY and restart the host for GPT.";
                metadata->status = aiError.isNotEmpty() ? "LOCAL FALLBACK · GPT UNAVAILABLE" : "LOCAL ENGINE";
            }
            if (explicitIdeaRequest)
                songPlanSnapshot.store(std::make_shared<SongPlan>(), std::memory_order_release);
            }
            if (current && !current->notes.empty())
                preserveLockedLayers(generated, *current, newest.lockedLayers);
        }

        RealtimePattern realtime;
        auto playbackPattern = std::make_shared<Pattern>();
        const auto playbackNoteCount = std::min(generated.notes.size(),
                                                static_cast<std::size_t>(maxPatternNotes));
        playbackPattern->notes.assign(generated.notes.begin(),
                                      generated.notes.begin() + static_cast<std::ptrdiff_t>(playbackNoteCount));
        playbackPattern->controls = generated.controls;
        playbackPattern->markers = generated.markers;
        playbackPattern->lengthBeats = generated.lengthBeats;
        playbackPattern->seed = generated.seed;
        realtime.pattern = std::move(playbackPattern);
        realtime.lengthBeats = generated.lengthBeats;
        realtime.seed = generated.seed;
        realtime.serial = newest.serial;
        realtime.epoch = newest.epoch;

        if (action == IdeaAction::Undo) {
            previousPatternSnapshot.store(current ? current : std::make_shared<Pattern>(),
                                          std::memory_order_release);
        } else if (current && !current->notes.empty()) {
            previousPatternSnapshot.store(current, std::memory_order_release);
        }
        uiPatternSnapshot.store(std::make_shared<Pattern>(generated), std::memory_order_release);
        ideaMetadata.store(metadata, std::memory_order_release);
        while (!token.stop_requested() && !pushGeneratedPattern(realtime))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (action != IdeaAction::None)
            generationInProgress.store(false, std::memory_order_release);
        generationProgress.store(action == IdeaAction::None ? 0.0f : 1.0f,
                                 std::memory_order_relaxed);
    }
}

bool PulsoAudioProcessor::consumeLatestPattern() noexcept {
    RealtimePattern incoming;
    auto changed = false;
    const auto epoch = processingEpoch.load(std::memory_order_acquire);
    while (popGeneratedPattern(incoming)) {
        if (incoming.epoch != epoch || incoming.serial <= activePattern.serial) continue;
        retiredRealtimeSnapshot.store(activePattern.pattern, std::memory_order_release);
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
    if (!pattern.pattern || pattern.pattern->notes.empty() ||
        pattern.lengthBeats <= 0.0 || numSamples <= 0) return;
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
        for (const auto& note : pattern.pattern->notes) {
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
        for (const auto& control : pattern.pattern->controls)
            addAtBeat(juce::MidiMessage::controllerEvent(
                          std::clamp(control.channel, 1, 16),
                          std::clamp(control.controller, 0, 127),
                          std::clamp(control.value, 0, 127)),
                      cycleStart + control.beat);
    }
}

void PulsoAudioProcessor::scheduleOverlappingPreviewNotes(const RealtimePattern& pattern,
                                                          const Transport& transport,
                                                          juce::MidiBuffer& output) const {
    if (!pattern.pattern || pattern.pattern->notes.empty() || pattern.lengthBeats <= 0.0) return;
    const auto cycle = static_cast<std::int64_t>(std::floor(transport.startBeat / pattern.lengthBeats));
    for (auto candidateCycle = cycle - 1; candidateCycle <= cycle; ++candidateCycle) {
        const auto cycleStart = static_cast<double>(candidateCycle) * pattern.lengthBeats;
        for (const auto& note : pattern.pattern->notes) {
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
            if (request.action != static_cast<std::uint8_t>(IdeaAction::None)) {
                auto expected = static_cast<IdeaAction>(request.action);
                pendingIdeaAction.compare_exchange_strong(expected, IdeaAction::None,
                                                          std::memory_order_acq_rel);
            }
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
    if (const auto existing = state.getChildWithName("COMPOSITION"); existing.isValid())
        state.removeChild(existing, nullptr);
    state.setProperty("compositionSeed", static_cast<juce::int64>(compositionSeed.load(std::memory_order_relaxed)), nullptr);
    state.setProperty("variationIndex", static_cast<juce::int64>(variationIndex.load(std::memory_order_relaxed)), nullptr);
    state.setProperty("lockedLayers", static_cast<int>(lockedLayers.load(std::memory_order_relaxed)), nullptr);
    state.setProperty("songDurationSeconds", songDurationSeconds.load(std::memory_order_relaxed), nullptr);
    {
        const std::scoped_lock lock(creativeDirectionMutex);
        state.setProperty("creativeDirection", creativeDirection, nullptr);
    }
    if (const auto plan = songPlanSnapshot.load(std::memory_order_acquire);
        plan && !plan->sections.empty()) {
        state.setProperty("songPlanJson", ids::songPlanToJson(*plan), nullptr);
        state.setProperty("songBars", plan->totalBars, nullptr);
        state.setProperty("songBeatsPerBar", plan->beatsPerBar, nullptr);
    }
    if (const auto pattern = uiPatternSnapshot.load(std::memory_order_acquire);
        pattern && !pattern->notes.empty()) {
        juce::MemoryOutputStream composition;
        composition.writeInt(2); // Binary composition state version.
        composition.writeDouble(pattern->lengthBeats);
        composition.writeInt64(static_cast<juce::int64>(pattern->seed));
        composition.writeInt(static_cast<int>(pattern->notes.size()));
        for (const auto& note : pattern->notes) {
            composition.writeDouble(note.startBeat);
            composition.writeDouble(note.durationBeats);
            composition.writeInt(note.pitch);
            composition.writeInt(note.velocity);
            composition.writeInt(note.channel);
            composition.writeInt(static_cast<int>(note.voice));
        }
        composition.writeInt(static_cast<int>(pattern->controls.size()));
        for (const auto& control : pattern->controls) {
            composition.writeDouble(control.beat);
            composition.writeInt(control.controller);
            composition.writeInt(control.value);
            composition.writeInt(control.channel);
            composition.writeInt(static_cast<int>(control.voice));
        }
        composition.writeInt(static_cast<int>(pattern->markers.size()));
        for (const auto& marker : pattern->markers) {
            composition.writeDouble(marker.beat);
            composition.writeString(juce::String::fromUTF8(marker.name.c_str()));
        }
        state.setProperty("compositionData", composition.getMemoryBlock().toBase64Encoding(), nullptr);
        if (const auto metadata = ideaMetadata.load(std::memory_order_acquire)) {
            state.setProperty("ideaTitle", metadata->title, nullptr);
            state.setProperty("ideaKey", metadata->key, nullptr);
            state.setProperty("ideaDescription", metadata->description, nullptr);
            state.setProperty("ideaStatus", metadata->status, nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destination);
}

void PulsoAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(parameters.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xml);
            const auto legacySeed = state.getProperty("variationSeed", 1);
            compositionSeed.store(static_cast<std::uint64_t>(static_cast<juce::int64>(
                                      state.getProperty("compositionSeed", legacySeed))),
                                  std::memory_order_relaxed);
            variationIndex.store(static_cast<std::uint64_t>(static_cast<juce::int64>(
                                     state.getProperty("variationIndex", 0))),
                                 std::memory_order_relaxed);
            lockedLayers.store(static_cast<std::uint8_t>(static_cast<int>(
                                   state.getProperty("lockedLayers", 0))),
                               std::memory_order_relaxed);
            songDurationSeconds.store(std::clamp(static_cast<int>(
                                          state.getProperty("songDurationSeconds", 0)), 0, 1800),
                                      std::memory_order_relaxed);
            {
                const std::scoped_lock lock(creativeDirectionMutex);
                creativeDirection = state.getProperty("creativeDirection", {}).toString().substring(0, 600);
            }
            juce::MemoryBlock compositionData;
            if (compositionData.fromBase64Encoding(
                    state.getProperty("compositionData", {}).toString())) {
                juce::MemoryInputStream composition(compositionData, false);
                const auto version = composition.readInt();
                auto restoredPattern = std::make_shared<Pattern>();
                restoredPattern->lengthBeats = composition.readDouble();
                restoredPattern->seed = static_cast<std::uint64_t>(composition.readInt64());
                const auto noteCount = composition.readInt();
                if ((version != 1 && version != 2) || !std::isfinite(restoredPattern->lengthBeats) ||
                    restoredPattern->lengthBeats < 1.0 || noteCount < 0 ||
                    noteCount > static_cast<int>(maxPatternNotes))
                    restoredPattern->notes.clear();
                else for (auto index = 0; index < noteCount; ++index) {
                    NoteEvent note;
                    note.startBeat = composition.readDouble();
                    note.durationBeats = composition.readDouble();
                    note.pitch = composition.readInt();
                    note.velocity = composition.readInt();
                    note.channel = composition.readInt();
                    if (version >= 2) {
                        const auto voice = composition.readInt();
                        note.voice = voice >= 0 && voice < static_cast<int>(VoiceId::Count)
                            ? static_cast<VoiceId>(voice) : VoiceId::Unspecified;
                    }
                    if (std::isfinite(note.startBeat) && std::isfinite(note.durationBeats) &&
                        note.startBeat >= 0.0 && note.startBeat < restoredPattern->lengthBeats &&
                        note.durationBeats > 0.0 && note.pitch >= 0 && note.pitch <= 127 &&
                        note.velocity >= 1 && note.velocity <= 127 &&
                        note.channel >= 1 && note.channel <= 16)
                        restoredPattern->notes.push_back(note);
                }
                if (version >= 2 && !restoredPattern->notes.empty()) {
                    const auto controlCount = composition.readInt();
                    if (controlCount >= 0 && controlCount <= 65536) {
                        for (auto index = 0; index < controlCount; ++index) {
                            ControlEvent control;
                            control.beat = composition.readDouble();
                            control.controller = composition.readInt();
                            control.value = composition.readInt();
                            control.channel = composition.readInt();
                            const auto voice = composition.readInt();
                            control.voice = voice >= 0 && voice < static_cast<int>(VoiceId::Count)
                                ? static_cast<VoiceId>(voice) : VoiceId::Unspecified;
                            if (std::isfinite(control.beat) && control.beat >= 0.0 &&
                                control.beat < restoredPattern->lengthBeats &&
                                control.controller >= 0 && control.controller <= 127 &&
                                control.value >= 0 && control.value <= 127 &&
                                control.channel >= 1 && control.channel <= 16)
                                restoredPattern->controls.push_back(control);
                        }
                    }
                    const auto markerCount = composition.readInt();
                    if (markerCount >= 0 && markerCount <= 512) {
                        for (auto index = 0; index < markerCount; ++index) {
                            MarkerEvent marker;
                            marker.beat = composition.readDouble();
                            marker.name = composition.readString().substring(0, 96).toStdString();
                            if (std::isfinite(marker.beat) && marker.beat >= 0.0 &&
                                marker.beat < restoredPattern->lengthBeats)
                                restoredPattern->markers.push_back(std::move(marker));
                        }
                    }
                }
                if (!restoredPattern->notes.empty()) {
                    auto metadata = std::make_shared<IdeaMetadata>();
                    metadata->title = state.getProperty("ideaTitle", "Restored Idea").toString();
                    metadata->key = state.getProperty("ideaKey", "Restored key").toString();
                    metadata->description = state.getProperty("ideaDescription", {}).toString();
                    metadata->status = "PROJECT IDEA RESTORED";
                    uiPatternSnapshot.store(restoredPattern, std::memory_order_release);
                    ideaMetadata.store(metadata, std::memory_order_release);
                    pendingIdeaAction.store(IdeaAction::Restore, std::memory_order_release);
                }
            }
            const auto savedPlanJson = state.getProperty("songPlanJson", {}).toString();
            if (savedPlanJson.isNotEmpty()) {
                SongPlan restoredPlan;
                juce::String planError;
                const auto savedBars = static_cast<int>(state.getProperty("songBars", 0));
                const auto savedBeatsPerBar = static_cast<double>(
                    state.getProperty("songBeatsPerBar", 4.0));
                if (AiComposer::parseSongPlanJson(savedPlanJson,
                        songDurationSeconds.load(std::memory_order_relaxed), savedBars,
                        currentTempo(), savedBeatsPerBar,
                        compositionSeed.load(std::memory_order_relaxed), restoredPlan, planError))
                    songPlanSnapshot.store(std::make_shared<SongPlan>(std::move(restoredPlan)),
                                           std::memory_order_release);
            }
            if (const auto legacyComposition = state.getChildWithName("COMPOSITION");
                legacyComposition.isValid())
                state.removeChild(legacyComposition, nullptr);
            parameters.replaceState(state);
            // These IDs remain only so older Ableton projects load cleanly.
            // Their values are retired and always normalised to zero.
            if (auto* retiredSpace = parameters.getParameter(ids::space))
                retiredSpace->setValueNotifyingHost(0.0f);
            if (auto* retiredGroove = parameters.getParameter(ids::groove))
                retiredGroove->setValueNotifyingHost(0.0f);
        }
    }
    generationRevision.fetch_add(1, std::memory_order_release);
}

juce::AudioProcessorEditor* PulsoAudioProcessor::createEditor() { return new PulsoAudioProcessorEditor(*this); }

} // namespace pulso::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pulso::plugin::PulsoAudioProcessor(); }
