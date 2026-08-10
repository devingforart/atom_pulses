#pragma once

#include "MusicTypes.h"

#include <vector>

namespace pulso {

struct BarDirection;
struct SongPlan;
struct SongSection;

struct RhythmValidationReport {
    int mandatoryKicksRestored{};
    int forbiddenKicksRemoved{};
    int duplicateHitsRemoved{};
};

class RhythmEngine final {
public:
    static void renderChunk(Pattern&, const SongPlan&, const SongSection&,
                            const std::vector<BarDirection>&, int sectionBar, int chunkBars);
    static void coordinateBassWithKick(Pattern&, const SongSection&, double beatsPerBar);
    [[nodiscard]] static RhythmValidationReport enforceContract(Pattern&, const SongPlan&);
};

} // namespace pulso
