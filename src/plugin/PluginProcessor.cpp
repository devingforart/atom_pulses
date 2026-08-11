#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "core/PerformanceTiming.h"
#include "core/Scale.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>

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
constexpr auto performance = "performance";
constexpr auto language = "language";
constexpr auto previewWorld = "previewWorld";
constexpr auto previewDrumKit = "previewDrumKit";
constexpr auto previewBassTone = "previewBassTone";
constexpr auto previewHarmonyTone = "previewHarmonyTone";
constexpr auto previewMelodyTone = "previewMelodyTone";
constexpr std::array voiceTimbreIds{
    "previewVoice00", "previewVoice01", "previewVoice02", "previewVoice03", "previewVoice04",
    "previewVoice05", "previewVoice06", "previewVoice07", "previewVoice08", "previewVoice09",
    "previewVoice10", "previewVoice11", "previewVoice12", "previewVoice13", "previewVoice14"
};
constexpr std::array voiceOctaveIds{
    "previewOctave00", "previewOctave01", "previewOctave02", "previewOctave03", "previewOctave04",
    "previewOctave05", "previewOctave06", "previewOctave07", "previewOctave08", "previewOctave09",
    "previewOctave10", "previewOctave11", "previewOctave12", "previewOctave13", "previewOctave14"
};
constexpr std::array voiceLevelIds{
    "previewLevel00", "previewLevel01", "previewLevel02", "previewLevel03", "previewLevel04",
    "previewLevel05", "previewLevel06", "previewLevel07", "previewLevel08", "previewLevel09",
    "previewLevel10", "previewLevel11", "previewLevel12", "previewLevel13", "previewLevel14"
};
constexpr auto thru = "thru";
constexpr auto gain = "gain";
constexpr std::array generative{role, scale, root, follow, risk, space, repetition,
                                complexity, development, groove, humanize, cohesion,
                                energy, phraseBars, mode};
constexpr std::array phraseLengths{1, 2, 4, 8, 16};

int previewWorldFromDirection(juce::String direction) {
    direction = direction.toLowerCase();
    const auto has = [&direction](std::initializer_list<const char*> words) {
        return std::any_of(words.begin(), words.end(), [&direction](const auto* word) {
            return direction.contains(word);
        });
    };
    if (has({"organic", "tribal", "acoustic", "earthy", "hand percussion"})) return 1;
    if (has({"analog", "analogue", "vintage", "warm", "tape", "retro"})) return 2;
    if (has({"dub", "echo", "spacious", "space", "delay", "reggae"})) return 3;
    if (has({"minimal", "sparse", "restrained", "clean", "micro"})) return 4;
    if (has({"hypnotic", "nocturnal", "trance", "meditative", "rolling"})) return 5;
    if (has({"cinematic", "epic", "orchestral", "soundtrack", "dramatic"})) return 6;
    if (has({"dark", "club", "peak", "warehouse", "hard", "driving"})) return 7;
    return 0;
}

constexpr std::array previewWorldNames{"DEEP PROGRESSIVE", "ORGANIC MOTION", "ANALOG WARMTH",
                                       "DUB SPACE", "MINIMAL PULSE", "HYPNOTIC NIGHT",
                                       "CINEMATIC ARC", "DARK CLUB"};

juce::StringArray timbreChoices(VoiceId voice) {
    switch (voice) {
        case VoiceId::CoreDrums: return {"Follow Kit", "808 Deep Kick", "909 House Kick", "Modern Club Kick", "Organic Kick"};
        case VoiceId::SnareClap: return {"Follow Kit", "808 Body Snare", "909 Noise Snare", "Modern Tight Clap", "Organic Skin Snare"};
        case VoiceId::ClosedHats: return {"Follow Kit", "808 Metallic Hat", "909 Air Hat", "Modern Tight Hat", "Organic Shaker"};
        case VoiceId::OpenHatsShaker: return {"Follow Kit", "808 Open Hat", "909 Open Hat", "Modern Ride Hat", "Organic Shaker"};
        case VoiceId::LowPercussion: return {"Follow Kit", "808 Toms", "909 Toms", "Modern Low Perc", "Organic Congas"};
        case VoiceId::HighPercussion: return {"Follow Kit", "808 Cowbell", "909 Rim", "Modern Metallic", "Organic Claves"};
        case VoiceId::SubBass: return {"Follow Bass", "Deep Sub", "Warm Analog", "Rolling Reese", "Acid Pluck"};
        case VoiceId::MovementBass: return {"Follow Bass", "Deep Sub", "Warm Analog", "Rolling Reese", "Acid Pluck"};
        case VoiceId::HarmonicFoundation: return {"Follow Harmony", "Deep Pad", "Warm Poly", "House Organ", "Glass"};
        case VoiceId::HarmonicPulse: return {"Follow Harmony", "Deep Pad", "Warm Poly", "House Organ", "Glass"};
        case VoiceId::HarmonicUpper: return {"Follow Harmony", "Deep Pad", "Warm Poly", "House Organ", "Glass"};
        case VoiceId::Atmosphere: return {"Follow Harmony", "Deep Pad", "Warm Poly", "House Organ", "Glass"};
        case VoiceId::Lead: return {"Follow Melody", "Warm Mono", "Soft Pluck", "Air", "Bell"};
        case VoiceId::Countermelody: return {"Follow Melody", "Warm Mono", "Soft Pluck", "Air", "Bell"};
        case VoiceId::Transitions: return {"Follow World", "Noise Sweep", "Deep Impact", "Tonal Riser", "Dub Hit"};
        case VoiceId::Count:
        case VoiceId::Unspecified: return {"Follow Family"};
    }
    return {"Follow Family"};
}

int auditionNote(VoiceId voice) noexcept {
    switch (voice) {
        case VoiceId::CoreDrums: return 36;
        case VoiceId::LowPercussion: return 45;
        case VoiceId::HighPercussion: return 75;
        case VoiceId::SubBass: return 43;
        case VoiceId::MovementBass: return 48;
        case VoiceId::HarmonicFoundation: return 60;
        case VoiceId::HarmonicPulse: return 67;
        case VoiceId::HarmonicUpper: return 76;
        case VoiceId::Lead: return 69;
        case VoiceId::Countermelody: return 64;
        case VoiceId::Atmosphere: return 60;
        case VoiceId::Transitions: return 48;
        case VoiceId::SnareClap: return 38;
        case VoiceId::ClosedHats: return 42;
        case VoiceId::OpenHatsShaker: return 46;
        case VoiceId::Count:
        case VoiceId::Unspecified: return 60;
    }
    return 60;
}

juce::String songPlanToJson(const SongPlan& plan) {
    auto* jsonRoot = new juce::DynamicObject();
    jsonRoot->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    jsonRoot->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    jsonRoot->setProperty("summary", juce::String::fromUTF8(plan.summary.c_str()));
    jsonRoot->setProperty("root_pitch_class", plan.rootPitchClass);
    jsonRoot->setProperty("mode", plan.scale == ScaleKind::Major ? "major" :
                              plan.scale == ScaleKind::Dorian ? "dorian" :
                              plan.scale == ScaleKind::Mixolydian ? "mixolydian" : "minor");
    auto* rhythmLanguage = new juce::DynamicObject();
    rhythmLanguage->setProperty("description", juce::String::fromUTF8(plan.rhythmLanguage.description.c_str()));
    rhythmLanguage->setProperty("pulse_stability", plan.rhythmLanguage.pulseStability);
    rhythmLanguage->setProperty("backbeat_gravity", plan.rhythmLanguage.backbeatGravity);
    rhythmLanguage->setProperty("syncopation", plan.rhythmLanguage.syncopation);
    rhythmLanguage->setProperty("ghost_density", plan.rhythmLanguage.ghostDensity);
    rhythmLanguage->setProperty("velocity_contrast", plan.rhythmLanguage.velocityContrast);
    rhythmLanguage->setProperty("timing_freedom", plan.rhythmLanguage.timingFreedom);
    rhythmLanguage->setProperty("orchestration_motion", plan.rhythmLanguage.orchestrationMotion);
    rhythmLanguage->setProperty("silence_bias", plan.rhythmLanguage.silenceBias);
    rhythmLanguage->setProperty("call_response", plan.rhythmLanguage.callResponse);
    jsonRoot->setProperty("rhythm_language", juce::var(rhythmLanguage));
    juce::Array<juce::var> motif;
    for (const auto value : plan.motifIntervals) motif.add(value);
    jsonRoot->setProperty("motif_intervals", motif);
    juce::Array<juce::var> chords;
    for (const auto value : plan.chordDegrees) chords.add(value);
    jsonRoot->setProperty("chord_degrees", chords);
    juce::Array<juce::var> rhythmMotifs;
    for (const auto& motifPattern : plan.rhythmMotifs) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", juce::String::fromUTF8(motifPattern.id.c_str()));
        item->setProperty("bars", motifPattern.bars);
        item->setProperty("steps_per_bar", motifPattern.stepsPerBar);
        item->setProperty("kick", juce::String::fromUTF8(motifPattern.kick.c_str()));
        item->setProperty("snare_clap", juce::String::fromUTF8(motifPattern.snareClap.c_str()));
        item->setProperty("closed_hats", juce::String::fromUTF8(motifPattern.closedHats.c_str()));
        item->setProperty("open_hats_shaker", juce::String::fromUTF8(motifPattern.openHatsShaker.c_str()));
        item->setProperty("low_percussion", juce::String::fromUTF8(motifPattern.lowPercussion.c_str()));
        item->setProperty("high_percussion", juce::String::fromUTF8(motifPattern.highPercussion.c_str()));
        juce::Array<juce::var> ornaments;
        for (const auto& ornament : motifPattern.ornaments) {
            auto* ornamentObject = new juce::DynamicObject();
            ornamentObject->setProperty("step", ornament.step);
            ornamentObject->setProperty("instrument", juce::String(rhythmInstrumentKey(ornament.instrument).data()));
            ornamentObject->setProperty("velocity", ornament.velocity);
            ornamentObject->setProperty("duration_steps", ornament.durationSteps);
            ornaments.add(juce::var(ornamentObject));
        }
        item->setProperty("ornaments", ornaments);
        rhythmMotifs.add(juce::var(item));
    }
    jsonRoot->setProperty("rhythm_motifs", rhythmMotifs);
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
        item->setProperty("performance_intent", juce::String::fromUTF8(voice.performance.intent.c_str()));
        item->setProperty("articulation", juce::String(articulationStyleKey(voice.performance.articulation).data()));
        item->setProperty("dynamic_contour", juce::String(dynamicContourKey(voice.performance.dynamics).data()));
        item->setProperty("vibrato", juce::String(vibratoStyleKey(voice.performance.vibrato).data()));
        item->setProperty("pitch_gesture", juce::String(pitchGestureKey(voice.performance.pitchGesture).data()));
        item->setProperty("expression_depth", voice.performance.expressionDepth);
        item->setProperty("brightness", voice.performance.brightness);
        item->setProperty("humanization", voice.performance.humanization);
        item->setProperty("sustain_pedal", voice.performance.sustainPedal);
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
        item->setProperty("kick_state", juce::String(kickStateKey(section.rhythm.kickState).data()));
        item->setProperty("kick_continuity", juce::String(kickContinuityKey(section.rhythm.continuity).data()));
        item->setProperty("percussion_density", section.rhythm.percussionDensity);
        item->setProperty("rhythmic_syncopation", section.rhythm.syncopation);
        item->setProperty("swing", section.rhythm.swing);
        item->setProperty("rhythm_motif_id", juce::String::fromUTF8(section.rhythm.motifId.c_str()));
        juce::Array<juce::var> mutations;
        for (const auto& mutation : section.rhythm.mutations) {
            auto* mutationObject = new juce::DynamicObject();
            mutationObject->setProperty("bar_offset", mutation.barOffset);
            mutationObject->setProperty("lane", juce::String(rhythmLaneKey(mutation.lane).data()));
            mutationObject->setProperty("operation", juce::String(rhythmMutationKey(mutation.kind).data()));
            mutationObject->setProperty("step", mutation.step);
            mutationObject->setProperty("amount", mutation.amount);
            mutationObject->setProperty("velocity", mutation.velocity);
            mutationObject->setProperty("purpose", juce::String::fromUTF8(mutation.purpose.c_str()));
            mutations.add(juce::var(mutationObject));
        }
        item->setProperty("rhythm_mutations", mutations);
        juce::Array<juce::var> gestures;
        for (const auto& gesture : section.rhythm.gestures) {
            auto* gestureObject = new juce::DynamicObject();
            gestureObject->setProperty("bar_offset", gesture.barOffset);
            gestureObject->setProperty("type", juce::String(rhythmGestureKey(gesture.kind).data()));
            gestureObject->setProperty("beat", gesture.beat);
            gestureObject->setProperty("intensity", gesture.intensity);
            gestures.add(juce::var(gestureObject));
        }
        item->setProperty("rhythm_gestures", gestures);
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
    languageParameter = parameters.getRawParameterValue(ids::language);
    for (std::size_t index = 0; index < voiceTimbreParameters.size(); ++index) {
        voiceTimbreParameters[index] = parameters.getRawParameterValue(ids::voiceTimbreIds[index]);
        voiceOctaveParameters[index] = parameters.getRawParameterValue(ids::voiceOctaveIds[index]);
        voiceLevelParameters[index] = parameters.getRawParameterValue(ids::voiceLevelIds[index]);
    }
    for (const auto* parameterId : ids::generative) parameters.addParameterListener(parameterId, this);
    generationThread = std::jthread([this](const std::stop_token token) { generationThreadMain(token); });
}

PulsoAudioProcessor::~PulsoAudioProcessor() {
    cancelGeneration();
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
    automaticPreviewWorld.store(ids::previewWorldFromDirection(creativeDirection), std::memory_order_relaxed);
}

juce::String PulsoAudioProcessor::currentPreviewWorldName() const {
    const auto selected = static_cast<int>(parameters.getRawParameterValue(ids::previewWorld)->load());
    const auto resolved = selected == 0 ? automaticPreviewWorld.load(std::memory_order_relaxed) : selected - 1;
    return ids::previewWorldNames[static_cast<std::size_t>(std::clamp(resolved, 0, 7))];
}

void PulsoAudioProcessor::setTargetSongDurationSeconds(int seconds) noexcept {
    songDurationSeconds.store(seconds <= 0 ? 0 : std::clamp(seconds, 30, 1800),
                              std::memory_order_relaxed);
}

void PulsoAudioProcessor::requestGenerateIdea() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationPreviousSeed.store(compositionSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationPreviousVariation.store(variationIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationCancelRequested.store(false, std::memory_order_release);
    generationProgress.store(0.0f, std::memory_order_relaxed);
    compositionSeed.fetch_add(1, std::memory_order_relaxed);
    variationIndex.store(0, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Generate, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestRegenerateUnlocked() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationPreviousSeed.store(compositionSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationPreviousVariation.store(variationIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationCancelRequested.store(false, std::memory_order_release);
    generationProgress.store(0.0f, std::memory_order_relaxed);
    variationIndex.fetch_add(1, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Regenerate, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestNextIdea() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationPreviousSeed.store(compositionSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationPreviousVariation.store(variationIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationCancelRequested.store(false, std::memory_order_release);
    generationProgress.store(0.0f, std::memory_order_relaxed);
    compositionSeed.fetch_add(1, std::memory_order_relaxed);
    variationIndex.store(0, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Next, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::requestUndo() noexcept {
    if (generationInProgress.exchange(true, std::memory_order_acq_rel)) return;
    generationPreviousSeed.store(compositionSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationPreviousVariation.store(variationIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
    generationCancelRequested.store(false, std::memory_order_release);
    generationProgress.store(0.0f, std::memory_order_relaxed);
    pendingIdeaAction.store(IdeaAction::Undo, std::memory_order_release);
    generationRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::cancelGeneration() noexcept {
    if (!generationInProgress.load(std::memory_order_acquire) &&
        !activeGenerationCancellation.load(std::memory_order_acquire)) return;
    generationCancelRequested.store(true, std::memory_order_release);
    if (const auto cancellation = activeGenerationCancellation.load(std::memory_order_acquire)) {
        cancellation->request_stop();
    }
    if (const auto current = ideaMetadata.load(std::memory_order_acquire)) {
        auto cancelling = std::make_shared<IdeaMetadata>(*current);
        cancelling->status = "CANCELLING - KEEPING CURRENT IDEA";
        cancelling->description = "Stopping network and composition work safely.";
        ideaMetadata.store(std::move(cancelling), std::memory_order_release);
    }
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

void PulsoAudioProcessor::toggleVoiceSolo(VoiceId voice) noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= static_cast<std::size_t>(VoiceId::Count) || index >= 32) return;
    soloVoices.fetch_xor(1u << index, std::memory_order_acq_rel);
    auditionRevision.fetch_add(1, std::memory_order_release);
}

void PulsoAudioProcessor::toggleVoiceMute(VoiceId voice) noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= static_cast<std::size_t>(VoiceId::Count) || index >= 32) return;
    mutedVoices.fetch_xor(1u << index, std::memory_order_acq_rel);
    auditionRevision.fetch_add(1, std::memory_order_release);
}

bool PulsoAudioProcessor::isVoiceSolo(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    return index < 32 && (soloVoices.load(std::memory_order_relaxed) & (1u << index)) != 0;
}

bool PulsoAudioProcessor::isVoiceMuted(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    return index < 32 && (mutedVoices.load(std::memory_order_relaxed) & (1u << index)) != 0;
}

bool PulsoAudioProcessor::isVoiceAudible(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= 32) return false;
    const auto bit = 1u << index;
    const auto solo = soloVoices.load(std::memory_order_relaxed);
    const auto muted = mutedVoices.load(std::memory_order_relaxed);
    return (muted & bit) == 0 && (solo == 0 || (solo & bit) != 0);
}

juce::StringArray PulsoAudioProcessor::voicePreviewTimbreChoices(VoiceId voice) {
    return ids::timbreChoices(voice);
}

juce::String PulsoAudioProcessor::voicePreviewTimbreParameterId(VoiceId voice) {
    const auto index = static_cast<std::size_t>(voice);
    return index < ids::voiceTimbreIds.size() ? ids::voiceTimbreIds[index] : juce::String{};
}

juce::String PulsoAudioProcessor::voicePreviewLevelParameterId(VoiceId voice) {
    const auto index = static_cast<std::size_t>(voice);
    return index < ids::voiceLevelIds.size() ? ids::voiceLevelIds[index] : juce::String{};
}

int PulsoAudioProcessor::voicePreviewTimbre(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= voiceTimbreParameters.size() || voiceTimbreParameters[index] == nullptr) return 0;
    return std::clamp(static_cast<int>(voiceTimbreParameters[index]->load(std::memory_order_relaxed)), 0, 4);
}

juce::String PulsoAudioProcessor::voicePreviewTimbreName(VoiceId voice) const {
    const auto choices = voicePreviewTimbreChoices(voice);
    const auto selected = std::clamp(voicePreviewTimbre(voice), 0, choices.size() - 1);
    if (selected > 0) return choices[selected];
    const auto family = voiceDefinition(voice).family;
    if (family == VoiceFamily::Rhythm) {
        const auto kit = std::clamp(static_cast<int>(parameters.getRawParameterValue(ids::previewDrumKit)->load()), 0, 3);
        return "Auto: " + choices[std::min(kit + 1, choices.size() - 1)];
    }
    if (family == VoiceFamily::Bass) {
        const auto tone = std::clamp(static_cast<int>(parameters.getRawParameterValue(ids::previewBassTone)->load()), 0, 3);
        return "Auto: " + choices[std::min(tone + 1, choices.size() - 1)];
    }
    if (family == VoiceFamily::Harmony || voice == VoiceId::Atmosphere) {
        const auto tone = std::clamp(static_cast<int>(parameters.getRawParameterValue(ids::previewHarmonyTone)->load()), 0, 3);
        return "Auto: " + choices[std::min(tone + 1, choices.size() - 1)];
    }
    if (family == VoiceFamily::Melodic) {
        const auto tone = std::clamp(static_cast<int>(parameters.getRawParameterValue(ids::previewMelodyTone)->load()), 0, 3);
        return "Auto: " + choices[std::min(tone + 1, choices.size() - 1)];
    }
    return "Auto: " + currentPreviewWorldName();
}

void PulsoAudioProcessor::setVoicePreviewTimbre(VoiceId voice, int selection) {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= ids::voiceTimbreIds.size()) return;
    auto* parameter = parameters.getParameter(ids::voiceTimbreIds[index]);
    if (parameter == nullptr) return;
    const auto clamped = std::clamp(selection, 0, 4);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(clamped)));
    parameter->endChangeGesture();
}

int PulsoAudioProcessor::voicePreviewOctave(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= voiceOctaveParameters.size() || voiceOctaveParameters[index] == nullptr) return 0;
    const auto choice = std::clamp(static_cast<int>(voiceOctaveParameters[index]->load(std::memory_order_relaxed)), 0, 2);
    return (choice - 1) * 12;
}

void PulsoAudioProcessor::setVoicePreviewOctave(VoiceId voice, int semitones) {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= ids::voiceOctaveIds.size()) return;
    auto* parameter = parameters.getParameter(ids::voiceOctaveIds[index]);
    if (parameter == nullptr) return;
    const auto choice = semitones <= -6 ? 0 : semitones >= 6 ? 2 : 1;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(choice)));
    parameter->endChangeGesture();
}

void PulsoAudioProcessor::auditionVoicePreview(VoiceId voice) noexcept {
    const auto index = static_cast<int>(voice);
    if (index >= 0 && index < static_cast<int>(VoiceId::Count))
        pendingPreviewAudition.store(index, std::memory_order_release);
}

float PulsoAudioProcessor::voicePreviewLevelDb(VoiceId voice) const noexcept {
    const auto index = static_cast<std::size_t>(voice);
    if (index >= voiceLevelParameters.size() || voiceLevelParameters[index] == nullptr) return 0.0f;
    return voiceLevelParameters[index]->load(std::memory_order_relaxed);
}

juce::String PulsoAudioProcessor::currentAiStatus() const {
    if (const auto metadata = ideaMetadata.load(std::memory_order_acquire)) return metadata->status;
    return {};
}

UiLanguage PulsoAudioProcessor::uiLanguage() const noexcept {
    return languageParameter != nullptr && languageParameter->load(std::memory_order_relaxed) >= 0.5f
        ? UiLanguage::Spanish : UiLanguage::English;
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
    result.push_back(std::make_unique<Bool>(ids::performance, "Human Performance", false));
    result.push_back(std::make_unique<Choice>(ids::language, "Interface Language",
                                              juce::StringArray{"English", "Español"}, 1));
    result.push_back(std::make_unique<Choice>(ids::previewWorld, "Preview Sound World",
                                              juce::StringArray{"Auto", "Deep Progressive", "Organic Motion",
                                                  "Analog Warmth", "Dub Space", "Minimal Pulse",
                                                  "Hypnotic Night", "Cinematic Arc", "Dark Club"}, 0));
    result.push_back(std::make_unique<Choice>(ids::previewDrumKit, "Preview Drum Kit",
                                              juce::StringArray{"808 Deep", "909 House", "Modern Club", "Organic"}, 1));
    result.push_back(std::make_unique<Choice>(ids::previewBassTone, "Preview Bass Instrument",
                                              juce::StringArray{"Deep Sub", "Warm Analog", "Rolling Reese", "Acid Pluck"}, 1));
    result.push_back(std::make_unique<Choice>(ids::previewHarmonyTone, "Preview Harmony Instrument",
                                              juce::StringArray{"Deep Pad", "Warm Poly", "House Organ", "Glass"}, 0));
    result.push_back(std::make_unique<Choice>(ids::previewMelodyTone, "Preview Melody Instrument",
                                              juce::StringArray{"Warm Mono", "Soft Pluck", "Air", "Bell"}, 0));
    for (std::size_t index = 0; index < ids::voiceTimbreIds.size(); ++index) {
        const auto voice = static_cast<VoiceId>(index);
        result.push_back(std::make_unique<Choice>(
            ids::voiceTimbreIds[index],
            "Preview " + juce::String(voiceDefinition(voice).name.data()),
            ids::timbreChoices(voice), 0));
        result.push_back(std::make_unique<Choice>(
            ids::voiceOctaveIds[index],
            "Preview Octave " + juce::String(voiceDefinition(voice).name.data()),
            juce::StringArray{"-12", "Original", "+12"}, 1));
        result.push_back(std::make_unique<Float>(
            ids::voiceLevelIds[index],
            "Preview Level " + juce::String(voiceDefinition(voice).name.data()),
            juce::NormalisableRange<float>(-36.0f, 6.0f, 0.1f), 0.0f));
    }
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
    pendingPreviewAudition.store(-1, std::memory_order_relaxed);
    activePreviewAudition = -1;
    previewAuditionSamplesRemaining = 0;
    activePattern = {};
    recentSourceCount = 0;
    recentSourceWrite = 0;
    standaloneBeat = 0.0;
    transportBeat.store(0.0, std::memory_order_relaxed);
    hostTransportAvailable.store(false, std::memory_order_relaxed);
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
    transportBeat.store(transport.startBeat, std::memory_order_relaxed);
    hostTransportAvailable.store(transport.hostAvailable, std::memory_order_relaxed);
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
    context.humanize = 0.0;
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
                                     pitch, 67 + static_cast<int>(index) * 3, 3,
                                     VoiceId::HarmonicFoundation});
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
        generated.expressions.erase(std::remove_if(generated.expressions.begin(), generated.expressions.end(),
                                                    [&](const auto& expression) {
                                                        return familyForVoice(expression.voice, expression.channel) == family;
                                                    }), generated.expressions.end());
        for (const auto& expression : previous.expressions)
            if (familyForVoice(expression.voice, expression.channel) == family &&
                expression.beat < generated.lengthBeats)
                generated.expressions.push_back(expression);
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
        const auto cancellableAction = action != IdeaAction::None && action != IdeaAction::Restore;
        auto operationCancellation = std::make_shared<std::stop_source>();
        const auto metadataBeforeOperation = ideaMetadata.load(std::memory_order_acquire);
        if (token.stop_requested()) operationCancellation->request_stop();
        if (cancellableAction)
            activeGenerationCancellation.store(operationCancellation, std::memory_order_release);
        if (cancellableAction && generationCancelRequested.load(std::memory_order_acquire)) {
            compositionSeed.store(generationPreviousSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
            variationIndex.store(generationPreviousVariation.load(std::memory_order_relaxed), std::memory_order_relaxed);
            if (metadataBeforeOperation) {
                auto cancelled = std::make_shared<IdeaMetadata>(*metadataBeforeOperation);
                cancelled->status = "CANCELLED - CURRENT IDEA KEPT";
                cancelled->description = "The request was cancelled before composition began.";
                ideaMetadata.store(std::move(cancelled), std::memory_order_release);
            }
            activeGenerationCancellation.store(nullptr, std::memory_order_release);
            generationInProgress.store(false, std::memory_order_release);
            generationProgress.store(0.0f, std::memory_order_relaxed);
            continue;
        }
        const auto operationToken = operationCancellation->get_token();
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
                        currentTempo(), newest.beatsPerBar, newest.seed, operationToken, aiError,
                        [this, metadata](AiSongStage stage) {
                            auto progressMetadata = std::make_shared<IdeaMetadata>(*metadata);
                            if (stage == AiSongStage::Architecture) {
                                generationProgress.store(0.08f, std::memory_order_relaxed);
                                progressMetadata->status = "GPT ARCHITECTURE - DRAFTING";
                                progressMetadata->description = "Writing form, harmony, orchestration and rhythmic DNA.";
                            } else {
                                generationProgress.store(0.46f, std::memory_order_relaxed);
                                progressMetadata->status = "GPT CRITIC - OPTIONAL REVISION";
                                progressMetadata->description = "Auditing motif lineage, breathing, contrast and kick-bass interlock.";
                            }
                            ideaMetadata.store(std::move(progressMetadata), std::memory_order_release);
                        });
                    usedAiPlan = !plan.sections.empty();
                }
                if (operationToken.stop_requested() || token.stop_requested() ||
                    generationCancelRequested.load(std::memory_order_acquire)) {
                    compositionSeed.store(generationPreviousSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    variationIndex.store(generationPreviousVariation.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    auto cancelled = metadataBeforeOperation
                        ? std::make_shared<IdeaMetadata>(*metadataBeforeOperation)
                        : std::make_shared<IdeaMetadata>();
                    cancelled->description = "The request was cancelled. The previous composition was kept unchanged.";
                    cancelled->status = "CANCELLED - CURRENT IDEA KEPT";
                    ideaMetadata.store(std::move(cancelled), std::memory_order_release);
                    activeGenerationCancellation.store(nullptr, std::memory_order_release);
                    generationInProgress.store(false, std::memory_order_release);
                    generationProgress.store(0.0f, std::memory_order_relaxed);
                    continue;
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
                                              current.get(), newest.lockedLayers, operationToken, aiError);
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
            PerformanceExpression::applyIdeaDefaults(generated, currentTempo(), newest.beatsPerBar);
            if (explicitIdeaRequest)
                songPlanSnapshot.store(std::make_shared<SongPlan>(), std::memory_order_release);
            }
            if (current && !current->notes.empty())
                preserveLockedLayers(generated, *current, newest.lockedLayers);
        }

        if (cancellableAction && (operationToken.stop_requested() || token.stop_requested() ||
            generationCancelRequested.load(std::memory_order_acquire))) {
            compositionSeed.store(generationPreviousSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
            variationIndex.store(generationPreviousVariation.load(std::memory_order_relaxed), std::memory_order_relaxed);
            auto cancelled = metadataBeforeOperation
                ? std::make_shared<IdeaMetadata>(*metadataBeforeOperation)
                : std::make_shared<IdeaMetadata>();
            cancelled->description = "The request was cancelled. The previous composition was kept unchanged.";
            cancelled->status = "CANCELLED - CURRENT IDEA KEPT";
            ideaMetadata.store(std::move(cancelled), std::memory_order_release);
            activeGenerationCancellation.store(nullptr, std::memory_order_release);
            generationInProgress.store(false, std::memory_order_release);
            generationProgress.store(0.0f, std::memory_order_relaxed);
            continue;
        }

        quantizePatternTiming(generated, 4);

        RealtimePattern realtime;
        auto playbackPattern = std::make_shared<Pattern>();
        const auto playbackNoteCount = std::min(generated.notes.size(),
                                                static_cast<std::size_t>(maxPatternNotes));
        playbackPattern->notes.assign(generated.notes.begin(),
                                      generated.notes.begin() + static_cast<std::ptrdiff_t>(playbackNoteCount));
        playbackPattern->controls = generated.controls;
        playbackPattern->expressions = generated.expressions;
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
        if (cancellableAction)
            activeGenerationCancellation.store(nullptr, std::memory_order_release);
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
        output.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 64, 0), sampleOffset);
        output.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 1, 0), sampleOffset);
        output.addEvent(juce::MidiMessage::pitchWheel(channel + 1, 8192), sampleOffset);
        output.addEvent(juce::MidiMessage::channelPressureChange(channel + 1, 0), sampleOffset);
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
    const auto soloMask = soloVoices.load(std::memory_order_relaxed);
    const auto muteMask = mutedVoices.load(std::memory_order_relaxed);
    const auto performanceEnabled = parameters.getRawParameterValue(ids::performance)->load() > 0.5f;
    const auto audible = [soloMask, muteMask](VoiceId voice, int channel) {
        const auto resolved = voice == VoiceId::Unspecified ? inferVoiceFromChannel(channel) : voice;
        const auto index = static_cast<std::size_t>(resolved);
        if (index >= 32) return false;
        const auto bit = 1u << index;
        return (muteMask & bit) == 0 && (soloMask == 0 || (soloMask & bit) != 0);
    };

    const auto addAtBeat = [&](const juce::MidiMessage& message, double absoluteBeat) {
        if (absoluteBeat < transport.startBeat || absoluteBeat >= transport.endBeat) return;
        const auto offset = static_cast<int>(std::floor((absoluteBeat - transport.startBeat) * samplesPerBeat));
        output.addEvent(message, std::clamp(offset, 0, numSamples - 1));
        trackGeneratedMessage(message);
    };

    if (retriggerOverlaps) {
        auto localBeat = std::fmod(transport.startBeat, pattern.lengthBeats);
        if (localBeat < 0.0) localBeat += pattern.lengthBeats;
        std::array<std::array<const ControlEvent*, 128>, 16> latestControls{};
        std::array<const ExpressionEvent*, 16> latestPitch{};
        std::array<const ExpressionEvent*, 16> latestPressure{};
        for (const auto& control : pattern.pattern->controls) {
            if (control.beat > localBeat || !audible(control.voice, control.channel)) continue;
            latestControls[static_cast<std::size_t>(std::clamp(control.channel, 1, 16) - 1)]
                          [static_cast<std::size_t>(std::clamp(control.controller, 0, 127))] = &control;
        }
        for (const auto& expression : pattern.pattern->expressions) {
            if (expression.beat > localBeat || !audible(expression.voice, expression.channel)) continue;
            const auto channel = static_cast<std::size_t>(std::clamp(expression.channel, 1, 16) - 1);
            if (expression.type == ExpressionEventType::PitchBend) latestPitch[channel] = &expression;
            else if (expression.type == ExpressionEventType::ChannelPressure) latestPressure[channel] = &expression;
        }
        for (std::size_t channel = 0; channel < latestControls.size(); ++channel) {
            for (const auto* control : latestControls[channel])
                if (control != nullptr)
                    output.addEvent(juce::MidiMessage::controllerEvent(static_cast<int>(channel + 1),
                        control->controller, control->value), 0);
            if (latestPitch[channel] != nullptr)
                output.addEvent(juce::MidiMessage::pitchWheel(static_cast<int>(channel + 1),
                    latestPitch[channel]->value), 0);
            if (latestPressure[channel] != nullptr)
                output.addEvent(juce::MidiMessage::channelPressureChange(static_cast<int>(channel + 1),
                    latestPressure[channel]->value), 0);
        }
    }

    for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle) {
        const auto cycleStart = static_cast<double>(cycle) * pattern.lengthBeats;
        for (std::size_t noteIndex = 0; noteIndex < pattern.pattern->notes.size(); ++noteIndex) {
            const auto& note = pattern.pattern->notes[noteIndex];
            if (!audible(note.voice, note.channel)) continue;
            const auto expressive = performanceEnabled
                ? performanceOffsetBeats(note, pattern.seed, noteIndex, transport.bpm) : 0.0;
            const auto noteStart = cycleStart + note.startBeat + expressive;
            const auto noteEnd = cycleStart + note.endBeat() + expressive;
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
        for (const auto& control : pattern.pattern->controls) {
            if (!audible(control.voice, control.channel)) continue;
            addAtBeat(juce::MidiMessage::controllerEvent(
                          std::clamp(control.channel, 1, 16),
                          std::clamp(control.controller, 0, 127),
                          std::clamp(control.value, 0, 127)),
                      cycleStart + control.beat);
        }
        for (const auto& expression : pattern.pattern->expressions) {
            if (!audible(expression.voice, expression.channel)) continue;
            const auto channel = std::clamp(expression.channel, 1, 16);
            switch (expression.type) {
                case ExpressionEventType::PitchBend:
                    addAtBeat(juce::MidiMessage::pitchWheel(channel,
                                  std::clamp(expression.value, 0, 16383)),
                              cycleStart + expression.beat);
                    break;
                case ExpressionEventType::ChannelPressure:
                    addAtBeat(juce::MidiMessage::channelPressureChange(channel,
                                  std::clamp(expression.value, 0, 127)),
                              cycleStart + expression.beat);
                    break;
                case ExpressionEventType::PolyAftertouch:
                    addAtBeat(juce::MidiMessage::aftertouchChange(channel,
                                  std::clamp(expression.note, 0, 127),
                                  std::clamp(expression.value, 0, 127)),
                              cycleStart + expression.beat);
                    break;
            }
        }
    }
}

void PulsoAudioProcessor::scheduleOverlappingPreviewNotes(const RealtimePattern& pattern,
                                                          const Transport& transport,
                                                          juce::MidiBuffer& output) const {
    if (!pattern.pattern || pattern.pattern->notes.empty() || pattern.lengthBeats <= 0.0) return;
    const auto cycle = static_cast<std::int64_t>(std::floor(transport.startBeat / pattern.lengthBeats));
    const auto performanceEnabled = parameters.getRawParameterValue(ids::performance)->load() > 0.5f;
    for (auto candidateCycle = cycle - 1; candidateCycle <= cycle; ++candidateCycle) {
        const auto cycleStart = static_cast<double>(candidateCycle) * pattern.lengthBeats;
        for (std::size_t noteIndex = 0; noteIndex < pattern.pattern->notes.size(); ++noteIndex) {
            const auto& note = pattern.pattern->notes[noteIndex];
            if (!isVoiceAudible(note.voice == VoiceId::Unspecified
                                    ? inferVoiceFromChannel(note.channel) : note.voice)) continue;
            const auto expressive = performanceEnabled
                ? performanceOffsetBeats(note, pattern.seed, noteIndex, transport.bpm) : 0.0;
            const auto start = cycleStart + note.startBeat + expressive;
            if (start < transport.startBeat && cycleStart + note.endBeat() + expressive > transport.startBeat)
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
    const auto currentAuditionRevision = auditionRevision.load(std::memory_order_acquire);
    const auto auditionChanged = currentAuditionRevision != submittedAuditionRevision;
    submittedAuditionRevision = currentAuditionRevision;
    const auto playbackActive = transport.isPlaying || !transport.hostAvailable;
    const auto blockBeats = transport.endBeat - transport.startBeat;
    const auto discontinuityTolerance = std::max(0.01, blockBeats * 1.5);
    const auto transportDiscontinuity = wasPlaybackActive && playbackActive &&
                                        std::abs(transport.startBeat - previousTransportEnd) >
                                            discontinuityTolerance;
    const auto transportStarted = !wasPlaybackActive && playbackActive;
    const auto transportStopped = wasPlaybackActive && !playbackActive;
    if (transportStopped || transportDiscontinuity || patternChanged || auditionChanged)
        sendGeneratedPanic(generatedMidi, 0);

    const auto retrigger = transportStarted || transportDiscontinuity || patternChanged || auditionChanged;
    if (playbackActive)
        schedulePattern(activePattern, transport, audio.getNumSamples(), generatedMidi, retrigger);

    const auto thruEnabled = parameters.getRawParameterValue(ids::thru)->load() > 0.5f;
    midi.clear();
    if (thruEnabled) midi.addEvents(thruMidi, 0, -1, 0);
    midi.addEvents(generatedMidi, 0, -1, 0);

    const auto previewEnabled = parameters.getRawParameterValue(ids::preview)->load() > 0.5f;
    const auto selectedPreviewWorld = static_cast<int>(parameters.getRawParameterValue(ids::previewWorld)->load());
    previewSynth.setSoundWorld(selectedPreviewWorld == 0
        ? automaticPreviewWorld.load(std::memory_order_relaxed) : selectedPreviewWorld - 1);
    previewSynth.setDrumKit(static_cast<int>(parameters.getRawParameterValue(ids::previewDrumKit)->load()));
    previewSynth.setBassTone(static_cast<int>(parameters.getRawParameterValue(ids::previewBassTone)->load()));
    previewSynth.setHarmonyTone(static_cast<int>(parameters.getRawParameterValue(ids::previewHarmonyTone)->load()));
    previewSynth.setMelodyTone(static_cast<int>(parameters.getRawParameterValue(ids::previewMelodyTone)->load()));
    for (std::size_t index = 0; index < voiceTimbreParameters.size(); ++index) {
        const auto* parameter = voiceTimbreParameters[index];
        previewSynth.setVoiceTimbre(static_cast<VoiceId>(index),
                                    parameter != nullptr ? static_cast<int>(parameter->load(std::memory_order_relaxed)) : 0);
        const auto* octave = voiceOctaveParameters[index];
        const auto octaveChoice = octave != nullptr ? std::clamp(static_cast<int>(octave->load(std::memory_order_relaxed)), 0, 2) : 1;
        previewSynth.setVoiceTranspose(static_cast<VoiceId>(index), (octaveChoice - 1) * 12);
        const auto* level = voiceLevelParameters[index];
        previewSynth.setVoiceLevelDb(static_cast<VoiceId>(index),
                                     level != nullptr ? level->load(std::memory_order_relaxed) : 0.0f);
    }
    if (!previewEnabled && previewWasEnabled) silencePreview(true);
    if (previewEnabled) {
        previewMidi.addEvents(generatedMidi, 0, -1, 0);
        if (!previewWasEnabled && playbackActive && !retrigger)
            scheduleOverlappingPreviewNotes(activePattern, transport, previewMidi);
    }

    if (const auto requested = pendingPreviewAudition.exchange(-1, std::memory_order_acq_rel); requested >= 0) {
        if (activePreviewAudition >= 0 && previewAuditionChannel != 10)
            previewMidi.addEvent(juce::MidiMessage::noteOff(previewAuditionChannel, previewAuditionNote), 0);
        const auto voice = static_cast<VoiceId>(requested);
        previewAuditionChannel = voiceDefinition(voice).midiChannel;
        previewAuditionNote = ids::auditionNote(voice);
        previewMidi.addEvent(juce::MidiMessage::noteOn(previewAuditionChannel, previewAuditionNote,
                                                       static_cast<juce::uint8>(108)), 0);
        activePreviewAudition = requested;
        previewAuditionSamplesRemaining = previewAuditionChannel == 10
            ? 0 : static_cast<int>(currentSampleRate * 0.65);
    }
    if (activePreviewAudition >= 0 && previewAuditionChannel != 10) {
        if (previewAuditionSamplesRemaining < audio.getNumSamples()) {
            previewMidi.addEvent(juce::MidiMessage::noteOff(previewAuditionChannel, previewAuditionNote),
                                 std::max(0, previewAuditionSamplesRemaining));
            activePreviewAudition = -1;
            previewAuditionSamplesRemaining = 0;
        } else {
            previewAuditionSamplesRemaining -= audio.getNumSamples();
        }
    } else if (previewAuditionChannel == 10) {
        activePreviewAudition = -1;
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
    state.setProperty("soloVoices", static_cast<juce::int64>(soloVoices.load(std::memory_order_relaxed)), nullptr);
    state.setProperty("mutedVoices", static_cast<juce::int64>(mutedVoices.load(std::memory_order_relaxed)), nullptr);
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
        composition.writeInt(3); // Binary composition state version.
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
        composition.writeInt(static_cast<int>(pattern->expressions.size()));
        for (const auto& expression : pattern->expressions) {
            composition.writeDouble(expression.beat);
            composition.writeInt(static_cast<int>(expression.type));
            composition.writeInt(expression.value);
            composition.writeInt(expression.note);
            composition.writeInt(expression.channel);
            composition.writeInt(static_cast<int>(expression.voice));
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
            constexpr auto validVoiceMask = (1u << static_cast<std::size_t>(VoiceId::Count)) - 1u;
            soloVoices.store(static_cast<std::uint32_t>(static_cast<juce::int64>(
                                 state.getProperty("soloVoices", 0))) & validVoiceMask,
                             std::memory_order_relaxed);
            mutedVoices.store(static_cast<std::uint32_t>(static_cast<juce::int64>(
                                  state.getProperty("mutedVoices", 0))) & validVoiceMask,
                              std::memory_order_relaxed);
            auditionRevision.fetch_add(1, std::memory_order_release);
            songDurationSeconds.store(std::clamp(static_cast<int>(
                                          state.getProperty("songDurationSeconds", 0)), 0, 1800),
                                      std::memory_order_relaxed);
            {
                const std::scoped_lock lock(creativeDirectionMutex);
                creativeDirection = state.getProperty("creativeDirection", {}).toString().substring(0, 600);
                automaticPreviewWorld.store(ids::previewWorldFromDirection(creativeDirection),
                                            std::memory_order_relaxed);
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
                if ((version != 1 && version != 2 && version != 3) || !std::isfinite(restoredPattern->lengthBeats) ||
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
                    if (version >= 3) {
                        const auto expressionCount = composition.readInt();
                        if (expressionCount >= 0 && expressionCount <= 65536) {
                            for (auto index = 0; index < expressionCount; ++index) {
                                ExpressionEvent expression;
                                expression.beat = composition.readDouble();
                                const auto type = composition.readInt();
                                expression.type = type >= 0 && type <= static_cast<int>(ExpressionEventType::PolyAftertouch)
                                    ? static_cast<ExpressionEventType>(type) : ExpressionEventType::PitchBend;
                                expression.value = composition.readInt();
                                expression.note = composition.readInt();
                                expression.channel = composition.readInt();
                                const auto voice = composition.readInt();
                                expression.voice = voice >= 0 && voice < static_cast<int>(VoiceId::Count)
                                    ? static_cast<VoiceId>(voice) : VoiceId::Unspecified;
                                const auto maximum = expression.type == ExpressionEventType::PitchBend ? 16383 : 127;
                                if (std::isfinite(expression.beat) && expression.beat >= 0.0 &&
                                    expression.beat < restoredPattern->lengthBeats &&
                                    expression.value >= 0 && expression.value <= maximum &&
                                    expression.note >= -1 && expression.note <= 127 &&
                                    expression.channel >= 1 && expression.channel <= 16)
                                    restoredPattern->expressions.push_back(expression);
                            }
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
                    quantizePatternTiming(*restoredPattern, 4);
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
