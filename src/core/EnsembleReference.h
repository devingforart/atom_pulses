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
};

} // namespace pulso
