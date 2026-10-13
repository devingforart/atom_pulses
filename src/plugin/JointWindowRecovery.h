#pragma once

#include "core/TonalContract.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace pulso::plugin {

// Select the voices that must be reconsidered as one musical transaction.
// A collision belongs to both sounding voices: freezing either one before the
// composer has reviewed the pair can trap a window in repeated one-part fixes.
// For a wide conflict graph, rewrite the entire unaccepted window together.
[[nodiscard]] inline std::vector<std::size_t> jointWindowRepairOwners(
    const std::vector<std::size_t>& stageOwners,
    const std::vector<TonalIssue>& issues,
    double windowStart, double windowEnd,
    std::optional<std::size_t> missingOwner = std::nullopt) {
    std::vector<std::size_t> implicated;
    const auto includePart = [&](std::uint16_t part) {
        if (part == 0) return;
        const auto owner = static_cast<std::size_t>(part - 1);
        if (std::find(stageOwners.begin(), stageOwners.end(), owner) != stageOwners.end() &&
            std::find(implicated.begin(), implicated.end(), owner) == implicated.end())
            implicated.push_back(owner);
    };
    if (missingOwner &&
        std::find(stageOwners.begin(), stageOwners.end(), *missingOwner) != stageOwners.end())
        implicated.push_back(*missingOwner);
    for (const auto& issue : issues) {
        if (issue.beat < windowStart - .001 || issue.beat >= windowEnd - .001) continue;
        if (issue.kind == "harsh_overlap" && issue.overlapBeats >= .5 - .001) {
            includePart(issue.partId);
            includePart(issue.otherPartId);
        } else if (issue.kind == "unsupported_chromatic" ||
                   issue.kind == "strong_non_chord") {
            includePart(issue.partId);
        }
    }
    if (implicated.empty() || implicated.size() > 6) return stageOwners;
    std::vector<std::size_t> ordered;
    for (const auto owner : stageOwners)
        if (std::find(implicated.begin(), implicated.end(), owner) != implicated.end())
            ordered.push_back(owner);
    return ordered;
}

} // namespace pulso::plugin
