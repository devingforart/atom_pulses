#pragma once

#include "SongComposer.h"

namespace pulso {

// Local A/B rendering path. The authored performance score is the sole source of
// MIDI notes and controls. All subsequent analysis is observational.
class SovereignScoreRenderer final {
public:
    [[nodiscard]] static Pattern render(const SongPlan&, const SongComposer::ProgressCallback&,
                                        CompositionRenderReport* report = nullptr);
};

} // namespace pulso
