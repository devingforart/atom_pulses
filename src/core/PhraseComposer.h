#pragma once

#include "HarmonyEngine.h"
#include "MusicTypes.h"

#include <array>
#include <cstddef>

namespace pulso {

struct SongPlan;
struct SongSection;

struct PhrasePerformanceState {
    std::array<int, static_cast<std::size_t>(VoiceId::Count)> previousPitch{};
    std::array<std::size_t, static_cast<std::size_t>(VoiceId::Count)> motifCursor{};

    PhrasePerformanceState() noexcept;
};

class PhraseComposer final {
public:
    static void renderMelodicVoices(Pattern&, const SongPlan&, const SongSection&,
                                    const std::vector<BarDirection>&,
                                    const HarmonicTimeline&,
                                    int sectionBar, int chunkBars,
                                    PhrasePerformanceState&);

    static void renderBassVoices(Pattern&, const SongPlan&, const SongSection&,
                                 const std::vector<BarDirection>&,
                                 const HarmonicTimeline&,
                                 int sectionBar, int chunkBars,
                                 PhrasePerformanceState&);
};

} // namespace pulso
