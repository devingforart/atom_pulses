#pragma once

#include "SongComposer.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

// A bounded editorial diagnosis for an already-authored AI score.  It identifies the
// smallest set of instrument performances that can repair audible quality without
// reopening the song's form, harmony, cast or unaffected MIDI.
struct SelectiveRepairPlan {
    bool needed{};
    double deficit{};
    std::vector<std::size_t> instrumentIndices;
    std::vector<std::string> issues;
};

// Exact evidence used by incremental AI writing and by the publication contract.
// Keeping this as a shared value prevents prompts and validators from silently
// applying different note, active-bar or phrase requirements.
struct PerformanceCoverageDeficit {
    std::size_t instrumentIndex{};
    std::string instrumentId;
    std::size_t notes{};
    std::size_t minimumNotes{};
    std::size_t authoredNotes{};
    std::size_t minimumAuthoredNotes{};
    std::size_t activeBars{};
    std::size_t minimumActiveBars{};
    std::size_t phrases{};
    std::size_t minimumPhrases{};
    std::size_t sections{};
    std::size_t minimumSections{};
    std::size_t sectionalStates{};
    std::size_t minimumSectionalStates{};
    std::size_t narrativePhraseWindows{};
    std::size_t minimumNarrativePhraseWindows{};
    std::vector<int> missingNarrativeWindowStartBars;
    double literalPlacementRatio{};
    double melodicStepRatio{};
    std::size_t melodicIntervals{};
    std::size_t polyphonicChordAttacks{};
    std::size_t minimumPolyphonicChordAttacks{};
    std::size_t longestChordBedBreathBars{};
    std::size_t chordBedNarrativeStages{};
    std::size_t minimumChordBedNarrativeStages{};
    double duplicateEventOverlap{};
    std::string duplicatedWithInstrumentId;
    bool missingCodaResolution{};
    bool missingThematicRelationship{};
    bool missingAuthoredDevelopment{};
    bool duplicatedIndependentLine{};
    bool missingSectionalEvolution{};
    bool missingNarrativePresence{};
    bool missingThematicDevelopment{};
    bool missingMelodicSpeech{};
    bool missingCentralChordBed{};
    bool missingChordBedBreath{};
    bool missingChordBedNarrativeArc{};
};

enum class ConstraintAuthority {
    TechnicalInvariant,
    ExplicitPromptCommitment,
    MusicalObjective
};

enum class PerformanceRepairOperation {
    SupplyMissingIdentity,
    ExtendCoverage,
    DevelopPhrase,
    DevelopSectionalEvolution,
    DevelopNarrativePresence,
    TransformThematicReturns,
    ShapeMelodicSpeech,
    AuthorCentralChordBed,
    ShapeHarmonicBreath,
    SeparateIndependentLine,
    ResolveNarrative,
    EstablishThematicRelationship
};

struct PerformanceConstraint {
    PerformanceCoverageDeficit evidence;
    ConstraintAuthority authority{ConstraintAuthority::MusicalObjective};
    std::vector<PerformanceRepairOperation> operations;
    bool blocksPublication{};
};

struct EnsembleContinuityReport {
    bool ready{};
    std::size_t evaluatedWindows{};
    std::size_t intentionalBreathWindows{};
    std::size_t silentWindows{};
    std::size_t maximumConsecutiveSilentWindows{};
    std::size_t underfilledWindows{};
    std::size_t twoLayerHarmonicWindows{};
    double audibleCoverage{};
    double harmonicFloorCoverage{};
    double longestGlobalSilenceBeats{};
};

// Three structural checkpoints for the primary polyphonic chord bed. This
// protects a developing harmonic floor without imposing constant notes.
struct ChordBedFormCoverage {
    bool opening{};
    bool development{};
    bool closing{};
    [[nodiscard]] bool ready() const noexcept {
        return opening && development && closing;
    }
};

// Exact pair/window evidence for AI-only editorial repair. Each harsh interval
// remains a pair event; it is not mistaken for an independent bad MIDI note.
struct TonalConflictGroup {
    std::size_t firstInstrument{};
    std::size_t secondInstrument{};
    int bar{};
    std::size_t events{};
    double overlapBeats{};
    double longestOverlapBeats{};
    int exampleFirstPitch{};
    int exampleSecondPitch{};
};

// An actual rendered chord attack selected from measured conflicts. Beats are
// section-relative so a compact AI voicing reply cannot edit another passage.
struct ChordVoicingTarget {
    int sectionIndex{};
    double sectionBeat{};
    double durationBeats{};
    std::vector<int> pitches;
    std::size_t conflictEvents{};
};

struct ChordVoicingPatch {
    int sectionIndex{};
    double sectionBeat{};
    double durationBeats{};
    std::vector<int> pitches;
};

[[nodiscard]] std::string_view constraintAuthorityKey(ConstraintAuthority) noexcept;
[[nodiscard]] std::string_view performanceRepairOperationKey(
    PerformanceRepairOperation) noexcept;

class SelectiveRepair final {
public:
    [[nodiscard]] static bool publicationReady(const CompositionRenderReport&) noexcept;
    // Terminal means structurally unusable, not merely in need of editorial polish.
    // A complete high-scoring checkpoint with an inconclusive ending remains
    // publishable when a bounded optional repair cannot improve it.
    [[nodiscard]] static bool criticalFailure(const CompositionRenderReport&) noexcept;
    // A strict fallback for a technically and musically healthy score whose only
    // remaining findings are optional editorial refinements. This never overrides
    // a critical production, narrative, soundscape or track failure.
    [[nodiscard]] static bool safeWithEditorialObservations(
        const CompositionRenderReport&) noexcept;
    [[nodiscard]] static double deficit(const CompositionRenderReport&) noexcept;
    // Keeps an AI-authored editorial rewrite that measurably reduces tonal collisions
    // without worsening hard MIDI invariants, even before the complete score is ready.
    // This is a checkpoint decision, never permission to publish an unsafe song.
    [[nodiscard]] static bool improvedTonalCheckpoint(
        const CompositionRenderReport& before,
        const CompositionRenderReport& after,
        double aiAuthoredNoteRatio) noexcept;
    // Accepts one isolated AI-authored editorial rewrite when it measurably
    // improves creative/track evidence without worsening any hard MIDI or
    // tonal invariant. This is intentionally evaluated per instrument so one
    // unsafe rewrite cannot discard unrelated valid repairs.
    [[nodiscard]] static bool improvedCreativeCheckpoint(
        const CompositionRenderReport& before,
        const CompositionRenderReport& after,
        double aiAuthoredNoteRatio) noexcept;
    // A whole-score rewrite must not trade its narrative arc for a lower
    // dissonance counter. Called for every transactional editorial candidate.
    [[nodiscard]] static bool preservesNarrative(
        const CompositionRenderReport& before,
        const CompositionRenderReport& after) noexcept;
    [[nodiscard]] static std::vector<TonalConflictGroup> tonalConflictGroups(
        const SongPlan&, const TonalAuditReport&);
    // Sustained close intervals inside one low chord-bed voicing cannot be
    // repaired by writing later instruments. Diagnose them at the block boundary.
    [[nodiscard]] static std::vector<TonalIssue> sustainedLowChordBedSeconds(
        const SongPlan&, const TonalAuditReport&);
    [[nodiscard]] static std::vector<ChordVoicingTarget> chordVoicingTargets(
        const SongPlan&, const Pattern&, const TonalAuditReport&,
        std::size_t instrumentIndex, std::size_t maximumTargets = 24);
    [[nodiscard]] static bool applyChordVoicingPatches(
        const SongPlan&, const PerformanceScore&, std::size_t instrumentIndex,
        const std::vector<ChordVoicingPatch>&, PerformanceScore& output,
        std::string& error);
    [[nodiscard]] static bool preservesUntouchedMidi(
        const SongPlan&, const Pattern& before, const Pattern& after,
        std::size_t instrumentIndex, const std::vector<ChordVoicingPatch>&);
    [[nodiscard]] static bool editoriallyAcceptable(
        const CompositionRenderReport& before, const CompositionRenderReport& after,
        std::size_t completedRepairs) noexcept;
    [[nodiscard]] static SelectiveRepairPlan diagnose(
        const SongPlan&, const Pattern&, const CompositionRenderReport&,
        std::size_t maximumTargets = 6);
    [[nodiscard]] static std::vector<PerformanceCoverageDeficit> performanceDeficits(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates);
    [[nodiscard]] static std::vector<PerformanceCoverageDeficit> marginalBarAcceptances(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates);
    [[nodiscard]] static std::vector<PerformanceConstraint> performanceConstraints(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates,
        bool explicitCastCommitment);
    [[nodiscard]] static std::vector<PerformanceConstraint> classifyPerformanceDeficits(
        const SongPlan&, std::vector<PerformanceCoverageDeficit>,
        bool explicitCastCommitment);
    [[nodiscard]] static std::vector<std::size_t> blockingTargets(
        const std::vector<PerformanceConstraint>&);
    [[nodiscard]] static std::vector<std::size_t> editorialTargets(
        const std::vector<PerformanceConstraint>&);
    // After one bounded AI rewrite, an unrequested literal clone can be removed
    // without losing unique music. Explicit casts and essential identities remain.
    [[nodiscard]] static std::vector<std::size_t> consolidatableDuplicateTargets(
        const SongPlan&, const std::vector<PerformanceConstraint>&,
        bool explicitCastCommitment);
    [[nodiscard]] static bool requiresReplacement(
        const PerformanceCoverageDeficit&) noexcept;
    // One missing conjunct interval in an otherwise complete protagonist is an
    // editorial observation until the full ensemble can be auditioned. It must
    // not trigger repeated whole-track rewrites during protagonist-first writing.
    [[nodiscard]] static bool deferableMarginalMelodicSpeech(
        const SongPlan&, const PerformanceCoverageDeficit&) noexcept;
    // Local AI-only studio: a fully authored protagonist may carry editorial
    // narrative/speech findings into the complete-score audition. An empty,
    // incomplete or structurally unsafe protagonist never qualifies.
    [[nodiscard]] static bool deferableLocalProtagonistEditorial(
        const SongPlan&, const PerformanceCoverageDeficit&) noexcept;
    // Target missing dramatic acts even when the total number of phrase windows
    // already exceeds its minimum. Never request notes in inactive sections.
    [[nodiscard]] static std::vector<int> focusedProtagonistWindowTargets(
        const SongPlan&, const PerformanceCoverageDeficit&);
    // A null `after` means every measured deficit was resolved. This is a
    // successful focused repair, not an invalid validator response.
    [[nodiscard]] static bool acceptsFocusedProtagonistCompletion(
        const PerformanceCoverageDeficit& before,
        const PerformanceCoverageDeficit* after,
        std::span<const int> requestedWindowStartBars,
        bool codaOnly,
        std::size_t realizableNotesBefore,
        std::size_t realizableNotesAfter) noexcept;
    [[nodiscard]] static bool focusedProtagonistAdditionInScope(
        const SongPlan&, const PerformanceScore& addition,
        std::size_t instrumentIndex,
        std::span<const int> requestedWindowStartBars,
        bool codaOnly);
    [[nodiscard]] static std::optional<std::size_t> centralChordBedOwner(
        const SongPlan&);
    [[nodiscard]] static ChordBedFormCoverage chordBedFormCoverage(
        const SongPlan&, const PerformanceScore&, std::size_t instrumentIndex);
    [[nodiscard]] static long long measuredTonalDebt(
        const CompositionRenderReport&) noexcept;
    // A substantial protagonist whose only outstanding obligation is a modest
    // active-bar shortfall may continue to the complete-score audition if a
    // focused additive recovery fails. Missing identity or coda never qualify;
    // melodic speech has the separate one-interval exception above.
    [[nodiscard]] static bool deferableProtagonistCoverage(
        const SongPlan&, const PerformanceCoverageDeficit&) noexcept;
    [[nodiscard]] static std::vector<std::size_t> incompleteTargets(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates);
    [[nodiscard]] static EnsembleContinuityReport ensembleContinuity(
        const SongPlan&, const PerformanceScore&);
    // Reuses an already-authored protagonist cell at the audible resolution
    // boundary. Only placement transforms are added; no MIDI notes are invented.
    [[nodiscard]] static bool ensureAuthoredProtagonistCoda(
        const SongPlan&, PerformanceScore&,
        std::string_view preferredCellId = {});
    // Repairs only the terminal two-to-four bars of the primary chord bed using
    // the AI-authored tonic palette. Earlier placements and every other instrument
    // remain byte-for-byte unchanged.
    [[nodiscard]] static bool ensurePrimaryChordBedClosure(SongPlan&);

private:
    [[nodiscard]] static std::vector<PerformanceCoverageDeficit> performanceDeficitsImpl(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates,
        bool allowMarginalBarAcceptance);
};

} // namespace pulso
