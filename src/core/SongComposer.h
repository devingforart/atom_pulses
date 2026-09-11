#pragma once

#include "Generator.h"
#include "AttentionDirector.h"
#include "ArrangementDensityPlanner.h"
#include "CreativeAuthority.h"
#include "ElectronicCompositionFabric.h"
#include "ElectronicProductionDirector.h"
#include "ElectronicSoundscape.h"
#include "HarmonyPlan.h"
#include "MusicalCritic.h"
#include "MusicalIdentityGate.h"
#include "NarrativeScore.h"
#include "OrchestrationScore.h"
#include "PerformanceExpression.h"
#include "PerformanceScore.h"
#include "ProductionPolish.h"
#include "RhythmPlan.h"
#include "TonalContract.h"
#include "TrackViability.h"
#include "VerticalHarmonyGate.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

struct PlannedVoice {
    VoiceId id{VoiceId::CoreDrums};
    std::string function;
    std::string interaction;
    double activity{0.5};
    double syncopation{0.5};
    int minimumPitch{};
    int maximumPitch{127};
    PerformanceProfile performance;
};

struct SongSection {
    std::string name;
    std::string function;
    std::string harmonicDirection;
    std::string motifTreatment;
    int startBar{};
    int bars{8};
    double energy{0.5};
    double tension{0.5};
    double density{0.5};
    int motifVariant{};
    int tonalCenterPitchClass{};
    std::string modeHint{"minor"};
    std::vector<HarmonicEvent> harmonicEvents;
    std::vector<VoiceId> activeVoices;
    SectionRhythmPlan rhythm;
};

// The form is not the story. Sections describe where events happen; this contract
// describes why one event must lead to the next and what the listener should hear
// as unresolved or fulfilled in the rendered MIDI.
enum class NarrativeStage {
    Premise,
    Question,
    Departure,
    Transformation,
    Climax,
    Resolution,
    Aftermath
};

[[nodiscard]] constexpr std::string_view narrativeStageKey(NarrativeStage stage) noexcept {
    switch (stage) {
        case NarrativeStage::Premise: return "premise";
        case NarrativeStage::Question: return "question";
        case NarrativeStage::Departure: return "departure";
        case NarrativeStage::Transformation: return "transformation";
        case NarrativeStage::Climax: return "climax";
        case NarrativeStage::Resolution: return "resolution";
        case NarrativeStage::Aftermath: return "aftermath";
    }
    return "transformation";
}

struct NarrativeAct {
    std::string sectionName;
    NarrativeStage stage{NarrativeStage::Premise};
    std::string cause;
    std::string consequence;
    std::string unresolvedElement;
    std::string resolutionTarget;
    double tensionTarget{0.5};
    double resolutionStrength{};
};

struct NarrativeSpine {
    bool authored{};
    std::string premise;
    std::string question;
    std::string harmonicDebt;
    std::string protagonistInstrumentId;
    std::string motifIdentity;
    std::string climaxConsequence;
    std::string resolution;
    std::vector<NarrativeAct> acts;
};

struct SongPlan {
    std::string title{"Untitled Song"};
    std::string key{"C minor"};
    std::string summary;
    int targetSeconds{210};
    int totalBars{64};
    double bpm{120.0};
    double beatsPerBar{4.0};
    int rootPitchClass{};
    ScaleKind scale{ScaleKind::Minor};
    RhythmLanguage rhythmLanguage;
    HarmonicLanguage harmonicLanguage;
    OrchestrationLanguage orchestrationLanguage;
    TimbrePalette timbrePalette;
    ProductionLanguage productionLanguage;
    std::string productionModeSource{"adaptive_inference"};
    std::uint64_t seed{1};
    std::vector<int> motifIntervals{0, 3, 5, 7, 3};
    std::vector<HarmonicChord> chordPalette;
    std::vector<RhythmMotif> rhythmMotifs;
    std::vector<PlannedVoice> voices;
    std::vector<InstrumentAssignment> instruments;
    ElectronicSoundscapePlan soundscape;
    NarrativeSpine narrativeSpine;
    // Explicit user constraint propagated independently of genre classification. A
    // percussion-free electronic piece must not be graded as a failed club track.
    bool percussionFreeIntent{};
    // True when the instrument list came from the structured AI score. In that case the
    // cast is authoritative per voice: missing ownership is critic feedback, not permission
    // to inject generic instruments behind the composer's back.
    bool instrumentCastAuthored{};
    std::vector<SongSection> sections;
    PerformanceScore performanceScore;
    std::size_t implicitVoicesPruned{};
    std::size_t implicitPerformanceNotesPruned{};
};

struct CompositionRenderReport {
    TonalRepairReport firstTonalPass;
    TonalRepairReport finalTonalPass;
    MusicalQualityReport musical;
    OrchestrationReport orchestration;
    std::size_t harmonicWindows{};
    std::size_t unintendedSilenceWindowsRepaired{};
    std::size_t sparseStructuralWindowsRepaired{};
    std::size_t structuralContinuityNotesCreated{};
    std::size_t extendedForegroundWindowsRepaired{};
    std::size_t foregroundContinuityNotesCreated{};
    std::size_t earlyRhythmNotesCreated{};
    std::size_t audibleDurationRepairs{};
    std::size_t inaudibleNotesRemoved{};
    double longestGlobalSilenceBefore{};
    double longestGlobalSilenceAfter{};
    ExpressionCompactionReport expression;
    ProductionAuditReport production;
    ElectronicProductionReport electronicProduction;
    MusicalIdentityReport musicalIdentity;
    NarrativeScoreReport narrative;
    VerticalHarmonyReport verticalHarmony;
    CreativeAuthorityReport creativeAuthority;
    ElectronicFabricReport electronicFabric;
    AttentionDirectionReport attention;
    ArrangementDensityReport arrangementDensity;
    ElectronicSoundscapeReport soundscape;
    TrackViabilityReport trackViability;

    [[nodiscard]] bool productionReady() const noexcept {
        return production.ready;
    }
};

class SongComposer final {
public:
    using ProgressCallback = std::function<void(std::size_t, std::size_t, const SongSection&)>;

    [[nodiscard]] static SongPlan createLocalPlan(const std::string& direction,
                                                  int targetSeconds, double bpm,
                                                  double beatsPerBar, std::uint64_t seed,
                                                  int rootPitchClass, ScaleKind scale);
    [[nodiscard]] static int phraseAlignedBars(int rawBars) noexcept;
    static void normalizePlan(SongPlan&);
    [[nodiscard]] Pattern render(const SongPlan&, const GenerationContext& foundation,
                                 const ProgressCallback& = {},
                                 CompositionRenderReport* report = nullptr) const;
};

} // namespace pulso
