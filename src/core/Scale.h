#pragma once

#include "MusicTypes.h"

#include <span>
#include <vector>

namespace pulso {

[[nodiscard]] std::span<const int> intervalsFor(ScaleKind kind) noexcept;
[[nodiscard]] bool isPitchClassInScale(int pitchClass, int rootPitchClass, ScaleKind kind) noexcept;
[[nodiscard]] int nearestPitchInScale(int midiPitch, int rootPitchClass, ScaleKind kind) noexcept;
[[nodiscard]] int pitchClassToMidi(int pitchClass, int preferredOctave, int minimum, int maximum) noexcept;
[[nodiscard]] std::vector<int> normalizePitchClasses(std::span<const int> pitchClasses);

} // namespace pulso

