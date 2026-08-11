#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <cstdint>

namespace pulso {

// Onsets remain editable on a strict musical lattice. Releases use a finer grid so
// articulation, legato and phrase breathing survive MIDI export.
void quantizePatternTiming(Pattern&, int onsetStepsPerBeat = 4,
                           int releaseStepsPerBeat = 16);

[[nodiscard]] double performanceOffsetBeats(const NoteEvent&, std::uint64_t seed,
                                             std::size_t ordinal, double bpm) noexcept;

} // namespace pulso
