#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <cstdint>

namespace pulso {

// The composition remains editable on a strict musical lattice. Expressive timing is
// calculated separately by the scheduler and never mutates the stored/exported pattern.
void quantizePatternTiming(Pattern&, int stepsPerBeat = 4);

[[nodiscard]] double performanceOffsetBeats(const NoteEvent&, std::uint64_t seed,
                                             std::size_t ordinal, double bpm) noexcept;

} // namespace pulso
