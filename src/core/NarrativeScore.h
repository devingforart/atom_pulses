#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace pulso {

struct SongPlan;

struct NarrativeScoreReport {
    bool active{};
    std::size_t totalNotes{};
    std::size_t aiAuthoredNotes{};
    std::size_t thematicPlacements{};
    std::size_t recurringThematicPlacements{};
    std::size_t bassWindows{};
    std::size_t developedBassWindows{};
    double aiAuthoredNoteRatio{};
    double primaryVoiceCoverage{};
    double grooveAuthorshipCoverage{};
    double thematicRecallRatio{};
    double bassPhraseContinuity{};
    double harmonicDirection{};
    double rhythmicDevelopment{};
    double score{};
    std::vector<std::string> issues;
};

class NarrativeScoreGate final {
public:
    [[nodiscard]] static NarrativeScoreReport audit(const Pattern&, const SongPlan&);
    static void stamp(Pattern&, const NarrativeScoreReport&);
};

} // namespace pulso
