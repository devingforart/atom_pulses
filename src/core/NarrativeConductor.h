#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct NarrativeConductorReport {
    std::size_t densityNotesRemoved{};
    std::size_t arpeggioNotesRemoved{};
    std::size_t melodicLeapsRevoiced{};
    std::size_t cadenceNotesRevoiced{};
    std::size_t peakPartsBefore{};
    std::size_t peakPartsAfter{};
    double arpeggioShareBefore{};
    double arpeggioShareAfter{};
    bool cadenceResolved{};
};

// Final, subtractive editorial pass. It never invents filler. It preserves the
// composer's themes while deciding who speaks, when an ostinato breathes, and how
// the narrative lands before the exact MIDI is audited and exported.
class NarrativeConductor final {
public:
    [[nodiscard]] static NarrativeConductorReport enforce(Pattern&, const SongPlan&);
};

} // namespace pulso
