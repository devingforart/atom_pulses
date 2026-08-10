#pragma once

#include "MusicTypes.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pulso {

struct TonalRepairReport {
    int outOfScaleRepaired{};
    int strongBeatRepaired{};
    int unsupportedChromaticRepaired{};
    int verticalCollisionsRepaired{};
    int harmonicOverlapsTrimmed{};
    int intentionalChromaticNotes{};
};

[[nodiscard]] std::string canonicalKeyName(int rootPitchClass, ScaleKind);
[[nodiscard]] std::optional<std::pair<int, ScaleKind>> parseKeyName(std::string_view);
void canonicalizeMotif(std::vector<int>& intervals, ScaleKind);

[[nodiscard]] TonalRepairReport repairTonalContract(
    Pattern&, int rootPitchClass, ScaleKind, double beatsPerBar,
    std::span<const std::vector<int>> harmonyByBar,
    double maximumChromaticRatio = 0.035);

} // namespace pulso
