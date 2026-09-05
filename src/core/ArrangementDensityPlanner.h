#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct ArrangementDensityTargets {
    std::size_t proposedParts{};
    std::size_t minimumPopulatedParts{};
    std::size_t minimumHarmonyParts{};
    std::size_t minimumMelodyParts{};
    std::size_t minimumTextureParts{};
    std::size_t maximumSimultaneousParts{};
    bool electronic{};
    bool percussionFree{};
};

struct ArrangementDensityReport {
    ArrangementDensityTargets targets;
    std::size_t populatedParts{};
    std::size_t harmonyParts{};
    std::size_t melodyParts{};
    std::size_t rhythmParts{};
    std::size_t textureParts{};
    std::size_t peakSimultaneousParts{};
    double independenceScore{1.0};
    double maximumPartNoteShare{};
    bool ready{};
};

// Converts a production-scale request into an explicit instrument cast, then audits
// the exact rendered MIDI. Musical density and track count remain separate: a large
// cast may rotate through the form without creating an overcrowded tutti.
class ArrangementDensityPlanner final {
public:
    [[nodiscard]] static ArrangementDensityTargets targetsFor(const SongPlan&);
    static void apply(SongPlan&);
    [[nodiscard]] static ArrangementDensityReport auditAndStamp(Pattern&, const SongPlan&);
};

} // namespace pulso
