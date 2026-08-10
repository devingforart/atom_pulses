#pragma once

#include "core/Generator.h"
#include "core/SongComposer.h"
#include "AiComposer.h"
#include "PreviewSynth.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace pulso::plugin {

class PulsoAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener {
public:
    PulsoAudioProcessor();
    ~PulsoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    enum class Layer : std::uint8_t { Harmony = 0, Melody, Bass, Drums };
    void setCreativeDirection(const juce::String&);
    void setTargetSongDurationSeconds(int) noexcept;
    void requestGenerateIdea() noexcept;
    void requestRegenerateUnlocked() noexcept;
    void requestNextIdea() noexcept;
    void requestUndo() noexcept;
    void requestVariation() noexcept { requestRegenerateUnlocked(); }
    void requestNewComposition() noexcept { requestNextIdea(); }
    void setLayerLocked(Layer, bool) noexcept;
    [[nodiscard]] bool isLayerLocked(Layer) const noexcept;
    [[nodiscard]] juce::String currentAiStatus() const;
    [[nodiscard]] juce::String currentIdeaTitle() const;
    [[nodiscard]] juce::String currentIdeaDescription() const;
    [[nodiscard]] juce::String currentCreativeDirection() const;
    [[nodiscard]] int targetSongDurationSeconds() const noexcept {
        return songDurationSeconds.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::shared_ptr<const SongPlan> currentSongPlan() const noexcept {
        return songPlanSnapshot.load(std::memory_order_acquire);
    }
    [[nodiscard]] float currentGenerationProgress() const noexcept {
        return generationProgress.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool aiAvailable() const { return AiComposer::hasApiKey(); }
    [[nodiscard]] bool isComposing() const noexcept {
        return generationInProgress.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::shared_ptr<const Pattern> currentPattern() const noexcept {
        return uiPatternSnapshot.load(std::memory_order_acquire);
    }
    [[nodiscard]] double currentTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
    [[nodiscard]] int currentTimeSignatureNumerator() const noexcept {
        return timeSignatureNumerator.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int currentTimeSignatureDenominator() const noexcept {
        return timeSignatureDenominator.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool hostIsPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
    [[nodiscard]] int currentPhraseBars() const noexcept;
    [[nodiscard]] std::uint64_t currentCompositionSeed() const noexcept {
        return compositionSeed.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t currentVariationIndex() const noexcept {
        return variationIndex.load(std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static constexpr std::size_t maxPhraseBars = 16;
    static constexpr std::size_t maxHarmonyNotes = 12;
    static constexpr std::size_t maxSourceNotes = 128;
    static constexpr std::size_t maxPatternNotes = 32768;
    static constexpr std::size_t requestQueueSize = 8;
    static constexpr std::size_t resultQueueSize = 4;

    struct Transport {
        double startBeat{};
        double endBeat{};
        double beatsPerBar{4.0};
        double bpm{120.0};
        bool isPlaying{};
        bool hostAvailable{};
    };

    struct HarmonySlot {
        std::array<int, maxHarmonyNotes> pitchClasses{};
        std::uint8_t size{};
    };

    struct GenerationRequest {
        Role role{Role::Bass};
        ScaleKind scale{ScaleKind::Minor};
        int rootPitchClass{};
        double beatsPerBar{4.0};
        double follow{0.65};
        double risk{0.30};
        double space{};
        double repetition{0.75};
        double complexity{0.45};
        double development{0.40};
        double groove{};
        double humanize{0.30};
        double cohesion{0.80};
        double energy{0.55};
        int bars{4};
        std::uint64_t seed{1};
        std::uint64_t variationIndex{};
        std::uint64_t evolutionStep{};
        std::uint64_t serial{};
        std::uint64_t epoch{};
        std::array<HarmonySlot, maxPhraseBars> harmony{};
        HarmonySlot heldChord{};
        std::array<SourceNote, maxSourceNotes> sourceNotes{};
        std::uint16_t sourceNoteCount{};
        std::uint8_t action{};
        std::uint8_t lockedLayers{};
        int targetSongSeconds{};
    };

    struct RealtimePattern {
        std::shared_ptr<const Pattern> pattern;
        double lengthBeats{4.0};
        std::uint64_t seed{1};
        std::uint64_t serial{};
        std::uint64_t epoch{};
    };

    Transport readTransport(int numSamples);
    void collectInput(const juce::MidiBuffer&, double blockStartBeat, double beatsPerBar);
    bool captureHarmonyForBar(std::int64_t absoluteBar, int phraseBars) noexcept;
    GenerationRequest makeGenerationRequest(double beatsPerBar) noexcept;
    static GenerationContext expandContext(const GenerationRequest&);
    static void addHarmonyLayer(Pattern&, const GenerationContext&);
    static void preserveLockedLayers(Pattern&, const Pattern&, std::uint8_t);

    bool pushGenerationRequest(const GenerationRequest&) noexcept;
    bool popGenerationRequest(GenerationRequest&) noexcept;
    bool pushGeneratedPattern(const RealtimePattern&) noexcept;
    bool popGeneratedPattern(RealtimePattern&) noexcept;
    void generationThreadMain(std::stop_token);
    bool consumeLatestPattern() noexcept;

    void schedulePattern(const RealtimePattern&, const Transport&, int numSamples,
                         juce::MidiBuffer&, bool retriggerOverlaps);
    void scheduleOverlappingPreviewNotes(const RealtimePattern&, const Transport&,
                                         juce::MidiBuffer&) const;
    void trackGeneratedMessage(const juce::MidiMessage&) noexcept;
    void sendGeneratedPanic(juce::MidiBuffer&, int sampleOffset);
    void silencePreview(bool allowTailOff) noexcept;
    void parameterChanged(const juce::String&, float) override;

    Generator generator;
    SongComposer songComposer;
    PreviewSynth previewSynth;
    juce::dsp::Limiter<float> previewLimiter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> previewGain;
    juce::MidiBuffer generatedMidi;
    juce::MidiBuffer previewMidi;
    juce::MidiBuffer thruMidi;

    std::array<bool, 128> heldNotes{};
    std::array<SourceNote, maxSourceNotes> recentSourceNotes{};
    std::size_t recentSourceCount{};
    std::size_t recentSourceWrite{};
    std::array<HarmonySlot, maxPhraseBars> chordTimeline{};
    std::array<std::array<std::uint8_t, 128>, 16> activeGeneratedNotes{};

    std::array<GenerationRequest, requestQueueSize> requestQueue{};
    std::atomic<std::size_t> requestWrite{};
    std::atomic<std::size_t> requestRead{};
    std::array<RealtimePattern, resultQueueSize> resultQueue{};
    std::atomic<std::size_t> resultWrite{};
    std::atomic<std::size_t> resultRead{};
    std::jthread generationThread;

    RealtimePattern activePattern{};
    std::atomic<std::shared_ptr<const Pattern>> retiredRealtimeSnapshot;
    std::atomic<std::shared_ptr<const Pattern>> uiPatternSnapshot;
    std::atomic<std::shared_ptr<const Pattern>> previousPatternSnapshot;
    std::atomic<std::shared_ptr<const SongPlan>> songPlanSnapshot;
    struct IdeaMetadata {
        juce::String title{"Local Idea"};
        juce::String key{"C minor"};
        juce::String description{"Deterministic local composition"};
        juce::String status{"LOCAL ENGINE READY"};
    };
    std::atomic<std::shared_ptr<const IdeaMetadata>> ideaMetadata;
    mutable std::mutex creativeDirectionMutex;
    juce::String creativeDirection;
    enum class IdeaAction : std::uint8_t { None, Generate, Regenerate, Next, Undo, Restore };
    std::atomic<IdeaAction> pendingIdeaAction{IdeaAction::None};
    std::atomic<bool> generationInProgress{};
    std::atomic<float> generationProgress{};
    std::atomic<int> songDurationSeconds{};
    std::atomic<std::uint8_t> lockedLayers{};
    std::atomic<std::uint64_t> compositionSeed{1};
    std::atomic<std::uint64_t> variationIndex{};
    std::atomic<std::uint64_t> generationRevision{1};
    std::atomic<std::uint64_t> processingEpoch{1};
    std::uint64_t submittedGenerationRevision{};
    std::uint64_t nextRequestSerial{};
    std::uint64_t evolutionStep{};
    std::int64_t observedBar{std::numeric_limits<std::int64_t>::min()};
    std::int64_t lastPhraseIndex{std::numeric_limits<std::int64_t>::min()};
    int observedPhraseBars{};
    double observedBeatsPerBar{};
    double standaloneBeat{};
    double previousTransportEnd{};
    double currentSampleRate{44100.0};
    bool hasSubmittedRequest{};
    bool pendingContextChange{true};
    bool wasPlaybackActive{};
    bool previewWasEnabled{true};
    std::atomic<double> tempo{120.0};
    std::atomic<int> timeSignatureNumerator{4};
    std::atomic<int> timeSignatureDenominator{4};
    std::atomic<bool> playing{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessor)
};

} // namespace pulso::plugin
