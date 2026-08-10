#pragma once

#include "core/Generator.h"
#include "PreviewSynth.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
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

    void requestVariation() noexcept {
        variationSeed.fetch_add(1, std::memory_order_relaxed);
        generationRevision.fetch_add(1, std::memory_order_release);
    }
    [[nodiscard]] std::shared_ptr<const Pattern> currentPattern() const noexcept {
        return uiPatternSnapshot.load(std::memory_order_acquire);
    }
    [[nodiscard]] double currentTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
    [[nodiscard]] bool hostIsPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
    [[nodiscard]] int currentPhraseBars() const noexcept;

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static constexpr std::size_t maxPhraseBars = 16;
    static constexpr std::size_t maxHarmonyNotes = 12;
    static constexpr std::size_t maxSourceNotes = 128;
    static constexpr std::size_t maxPatternNotes = 2048;
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
        double space{0.35};
        double repetition{0.75};
        double complexity{0.45};
        double development{0.40};
        int bars{4};
        std::uint64_t seed{1};
        std::uint64_t evolutionStep{};
        std::uint64_t serial{};
        std::uint64_t epoch{};
        std::array<HarmonySlot, maxPhraseBars> harmony{};
        HarmonySlot heldChord{};
        std::array<SourceNote, maxSourceNotes> sourceNotes{};
        std::uint16_t sourceNoteCount{};
    };

    struct RealtimePattern {
        std::array<NoteEvent, maxPatternNotes> notes{};
        std::uint16_t noteCount{};
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
    std::atomic<std::shared_ptr<const Pattern>> uiPatternSnapshot;
    std::atomic<std::uint64_t> variationSeed{1};
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
    std::atomic<bool> playing{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsoAudioProcessor)
};

} // namespace pulso::plugin
