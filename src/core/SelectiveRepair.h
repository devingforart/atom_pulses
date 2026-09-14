#pragma once

#include "SongComposer.h"

#include <cstddef>
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
    std::size_t activeBars{};
    std::size_t minimumActiveBars{};
    std::size_t phrases{};
    std::size_t minimumPhrases{};
    std::size_t sections{};
    std::size_t minimumSections{};
    bool missingCodaResolution{};
    bool missingThematicRelationship{};
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
    ResolveNarrative,
    EstablishThematicRelationship
};

struct PerformanceConstraint {
    PerformanceCoverageDeficit evidence;
    ConstraintAuthority authority{ConstraintAuthority::MusicalObjective};
    std::vector<PerformanceRepairOperation> operations;
    bool blocksPublication{};
};

[[nodiscard]] std::string_view constraintAuthorityKey(ConstraintAuthority) noexcept;
[[nodiscard]] std::string_view performanceRepairOperationKey(
    PerformanceRepairOperation) noexcept;

class SelectiveRepair final {
public:
    [[nodiscard]] static bool publicationReady(const CompositionRenderReport&) noexcept;
    [[nodiscard]] static bool criticalFailure(const CompositionRenderReport&) noexcept;
    // A strict fallback for a technically and musically healthy score whose only
    // remaining findings are optional editorial refinements. This never overrides
    // a critical production, narrative, soundscape or track failure.
    [[nodiscard]] static bool safeWithEditorialObservations(
        const CompositionRenderReport&) noexcept;
    [[nodiscard]] static double deficit(const CompositionRenderReport&) noexcept;
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
    [[nodiscard]] static bool requiresReplacement(
        const PerformanceCoverageDeficit&) noexcept;
    [[nodiscard]] static std::vector<std::size_t> incompleteTargets(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates);

private:
    [[nodiscard]] static std::vector<PerformanceCoverageDeficit> performanceDeficitsImpl(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates,
        bool allowMarginalBarAcceptance);
};

} // namespace pulso
