#pragma once

#include "SongComposer.h"

#include <cstddef>
#include <string>
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
    [[nodiscard]] static std::vector<std::size_t> incompleteTargets(
        const SongPlan&, const PerformanceScore&,
        const std::vector<std::size_t>& candidates);
};

} // namespace pulso
