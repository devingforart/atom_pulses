#pragma once

#include <cstddef>

namespace pulso {

struct SongPlan;
struct InstrumentAssignment;

// Section-aware requirement for the sustained harmonic bed. Formal breath is part of
// the composition and must not be mistaken for missing orchestration.
class HarmonicFloorContext final {
public:
    [[nodiscard]] static std::size_t requiredLayers(const SongPlan&, double beat) noexcept;
    [[nodiscard]] static bool sustainedFloorOwner(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static double plannedCoverage(const SongPlan&) noexcept;
};

} // namespace pulso
