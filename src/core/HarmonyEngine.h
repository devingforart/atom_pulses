#pragma once

#include "HarmonyPlan.h"
#include "MusicTypes.h"
#include "PhraseDirector.h"

#include <array>
#include <vector>

namespace pulso {

struct SongPlan;
struct SongSection;

struct HarmonicMoment {
    std::string chordId;
    std::string label;
    int rootPitchClass{};
    int bassPitchClass{};
    std::vector<int> pitchClasses;
    std::array<int, 4> voicing{48, 55, 60, 64};
    int voiceCount{3};
    double beatOffset{};
    double durationBeats{4.0};
    double tension{};
    double emphasis{0.5};
    HarmonicFunction function{HarmonicFunction::Tonic};
    VoicingStrategy voicingStrategy{VoicingStrategy::Mixed};
    bool cadence{};
};

using HarmonicTimeline = std::vector<std::vector<HarmonicMoment>>;

struct HarmonyState {
    std::array<int, 4> previousVoicing{48, 55, 60, 64};
};

class HarmonyEngine final {
public:
    [[nodiscard]] static HarmonicTimeline composeSection(
        const SongPlan&, const SongSection&, const std::vector<BarDirection>&, HarmonyState&);

    static void renderBar(Pattern&, const SongPlan&, const SongSection&,
                          const BarDirection&, const std::vector<HarmonicMoment>&,
                          int chunkBar, int remainingChunkBars);
};

} // namespace pulso
