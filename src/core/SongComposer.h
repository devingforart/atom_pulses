#pragma once

#include "Generator.h"
#include "HarmonyPlan.h"
#include "MusicalCritic.h"
#include "OrchestrationScore.h"
#include "PerformanceExpression.h"
#include "RhythmPlan.h"
#include "TonalContract.h"

#include <functional>
#include <string>
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
    std::uint64_t seed{1};
    std::vector<int> motifIntervals{0, 3, 5, 7, 3};
    std::vector<HarmonicChord> chordPalette;
    std::vector<RhythmMotif> rhythmMotifs;
    std::vector<PlannedVoice> voices;
    std::vector<InstrumentAssignment> instruments;
    std::vector<SongSection> sections;
};

struct CompositionRenderReport {
    TonalRepairReport firstTonalPass;
    TonalRepairReport finalTonalPass;
    MusicalQualityReport musical;
    OrchestrationReport orchestration;
    std::size_t harmonicWindows{};

    [[nodiscard]] bool productionReady() const noexcept {
        return finalTonalPass.after.productionReady() && orchestration.registerClarity >= 0.70 &&
               orchestration.familyBalance >= 0.45;
    }
};

class SongComposer final {
public:
    using ProgressCallback = std::function<void(std::size_t, std::size_t, const SongSection&)>;

    [[nodiscard]] static SongPlan createLocalPlan(const std::string& direction,
                                                  int targetSeconds, double bpm,
                                                  double beatsPerBar, std::uint64_t seed,
                                                  int rootPitchClass, ScaleKind scale);
    static void normalizePlan(SongPlan&);
    [[nodiscard]] Pattern render(const SongPlan&, const GenerationContext& foundation,
                                 const ProgressCallback& = {},
                                 CompositionRenderReport* report = nullptr) const;
};

} // namespace pulso
