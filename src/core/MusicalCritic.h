#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct MusicalQualityReport {
    double overall{};
    double negativeSpace{};
    double variation{};
    double voiceIndependence{};
    double dynamicShape{};
    double registerClarity{};
    std::size_t repeatedBars{};
    std::size_t excessiveLeaps{};
    std::size_t overlapsRepaired{};
    std::size_t densityEventsRemoved{};
    std::size_t literalRhythmBarsVaried{};
};

class MusicalCritic final {
public:
    [[nodiscard]] static MusicalQualityReport review(const Pattern&, const SongPlan&);
    [[nodiscard]] static MusicalQualityReport reviewAndRefine(Pattern&, const SongPlan&);
};

} // namespace pulso
