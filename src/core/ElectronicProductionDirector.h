#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pulso {

struct SongPlan;

enum class ProductionDomain : std::uint8_t {
    Adaptive = 0,
    ClubElectronic,
    Hybrid,
    Orchestral
};

// This is a production grammar, not a genre preset. GPT describes how the record should
// behave across the dance floor, frequency spectrum and arrangement without selecting a
// closed house/techno template.
struct ProductionLanguage {
    ProductionDomain domain{ProductionDomain::Adaptive};
    std::string description{"Adaptive production direction"};
    double electronicIntent{};
    double clubFocus{};
    double lowEndInterlock{0.62};
    double grooveEvolution{0.58};
    double hookEconomy{0.66};
    double automationMotion{0.56};
    double djUtility{0.42};
    double spectralRestraint{0.64};
    double orchestralAllowance{0.35};
};

struct ElectronicProductionReport {
    bool active{};
    std::size_t lowEndCollisionsBefore{};
    std::size_t lowEndCollisionsAfter{};
    std::size_t bassAttacksMoved{};
    std::size_t bassReleasesTrimmed{};
    std::size_t phraseBreathsCreated{};
    std::size_t rhythmNotesEvolved{};
    std::size_t phraseVariationsCreated{};
    std::size_t bassPhraseDevelopmentsCreated{};
    std::size_t bassNotesDeveloped{};
    std::size_t kickPhraseDevelopmentsCreated{};
    std::size_t macroKickAnchorBarsCreated{};
    std::size_t maximumKicklessBarsBefore{};
    std::size_t maximumKicklessBarsAfter{};
    std::size_t latePercussionArticulationRepairs{};
    std::size_t kickOrnamentsRemoved{};
    std::size_t harmonicBreathsCreated{};
    std::size_t supportNotesRotated{};
    std::size_t thematicRecallWindowsCreated{};
    std::size_t thematicRecallNotesCreated{};
    std::size_t sparseStructuralWindowsRepaired{};
    std::size_t continuityNotesCreated{};
    std::size_t foregroundNotesRemoved{};
    std::size_t automationEventsAdded{};
    std::size_t earlyRhythmNotesCreated{};
    std::size_t literalRhythmBars{};
    std::size_t maximumRhythmRun{};
    std::size_t competingForegroundBars{};
    std::size_t maximumHarmonicRun{};
    std::size_t peakActiveVoices{};
    std::size_t thematicWindows{};
    std::size_t recurringThematicWindows{};
    std::size_t percussionArticulations{};
    std::size_t percussionNotes{};
    std::size_t materializedEssentialInstruments{};
    std::size_t expectedEssentialInstruments{};
    double kickOrnamentRatio{};
    double thematicRecurrenceRatio{};
    double intentionMatch{1.0};
    double musicalIdentityScore{1.0};
    double grooveRecallRatio{1.0};
    double responseLineageRatio{1.0};
    std::size_t transitionNotesRemoved{};
    std::size_t proceduralScalarNotesRemoved{};
    std::size_t publicationKickBarsRepaired{};
    std::size_t lowEndContinuityBarsRepaired{};
    std::size_t maximumLowEndGapBarsBefore{};
    std::size_t maximumLowEndGapBarsAfter{};
    double score{1.0};
};

class ElectronicProductionDirector final {
public:
    [[nodiscard]] static ProductionLanguage infer(std::string_view direction);
    static void normalizePlan(SongPlan&);
    [[nodiscard]] static ElectronicProductionReport shapePerformance(Pattern&, const SongPlan&);
    // Final audible repair runs after part realization. It preserves authored phrases and
    // only restores club pulse/low-end safety or removes procedural scalar filler.
    [[nodiscard]] static ElectronicProductionReport finalizePublication(Pattern&, const SongPlan&);
    [[nodiscard]] static ElectronicProductionReport audit(const Pattern&, const SongPlan&);
    static void stamp(Pattern&, const ElectronicProductionReport&);
};

} // namespace pulso
