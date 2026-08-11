#pragma once

#include "MusicTypes.h"
#include "PhraseDirector.h"

#include <array>
#include <vector>

namespace pulso {

struct SongPlan;
struct SongSection;

struct HarmonicMoment {
    int degree{};
    int bassPitchClass{};
    std::vector<int> pitchClasses;
    std::array<int, 4> voicing{48, 55, 60, 64};
    int voiceCount{3};
    double tension{};
    bool cadence{};
};

struct HarmonyState {
    std::array<int, 4> previousVoicing{48, 55, 60, 64};
};

class HarmonyEngine final {
public:
    [[nodiscard]] static std::vector<HarmonicMoment> composeSection(
        const SongPlan&, const SongSection&, const std::vector<BarDirection>&, HarmonyState&);

    static void renderBar(Pattern&, const SongPlan&, const SongSection&,
                          const BarDirection&, const HarmonicMoment&,
                          int chunkBar, int remainingChunkBars);
};

} // namespace pulso
