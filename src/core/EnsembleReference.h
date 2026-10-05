#pragma once

#include "PerformanceScore.h"

#include <cstddef>
#include <set>
#include <string>

namespace pulso {

struct SongPlan;

// A bounded, read-only view of already accepted MIDI. The writer of a new lane
// needs the actual attacks it is answering, not only the prose blueprint.
class EnsembleReference final {
public:
    [[nodiscard]] static std::string summarize(const SongPlan& plan,
                                               const PerformanceScore& accepted,
                                               const std::set<std::string>& excludedInstrumentIds,
                                               std::size_t maximumEvents = 240);
    // Complete simultaneous voicings of the accepted harmonic spine, grouped by
    // attack and release. Unlike sampled landmarks, no chord tone is silently
    // omitted from a reported attack. The event cap bounds long-form prompts.
    [[nodiscard]] static std::string harmonicLedger(
        const SongPlan& plan, const PerformanceScore& accepted,
        const std::set<std::string>& excludedInstrumentIds,
        std::size_t maximumGroups = 320);
};

} // namespace pulso
