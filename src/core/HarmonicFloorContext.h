#pragma once

#include <cstddef>
#include <vector>

namespace pulso {

struct SongPlan;
struct InstrumentAssignment;
struct Pattern;

struct HarmonicPhraseGap {
    std::size_t ownerIndex{};
    std::size_t sectionIndex{};
    int firstBar{}; // Relative to the section.
    int bars{};
    int sustainedBarsBefore{};
};

// Section-aware requirement for the sustained harmonic bed. Formal breath is part of
// the composition and must not be mistaken for missing orchestration.
class HarmonicFloorContext final {
public:
    [[nodiscard]] static std::size_t requiredLayers(const SongPlan&, double beat) noexcept;
    [[nodiscard]] static bool sustainedFloorOwner(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static double plannedCoverage(const SongPlan&) noexcept;
    // A sparse, explicitly active chord body against continuing harmonic
    // accompaniment is a candidate for AI review, never an instruction to
    // synthesize notes. Intentional whole-window withdrawals are excluded.
    [[nodiscard]] static std::vector<HarmonicPhraseGap> phraseGaps(
        const SongPlan&, const Pattern&, std::size_t limit = 4);
};

} // namespace pulso
