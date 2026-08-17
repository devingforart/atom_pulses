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
    int semanticPitchRepairs{};
    int articulationDiversifications{};
};

class RhythmEngine final {
public:
    static void renderChunk(Pattern&, const SongPlan&, const SongSection&,
                            const std::vector<BarDirection>&, int sectionBar, int chunkBars);
    static void coordinateBassWithKick(Pattern&, const SongSection&, double beatsPerBar);
    [[nodiscard]] static RhythmValidationReport enforceContract(Pattern&, const SongPlan&);
    // Pitch-only semantic pass for rhythm notes created after the main rhythm render.
    // It never adds/removes attacks or changes their timing.
    [[nodiscard]] static RhythmValidationReport enforceSemanticArticulations(Pattern&, const SongPlan&);
};

} // namespace pulso
