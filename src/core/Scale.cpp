#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso {
namespace {
constexpr std::array major{0, 2, 4, 5, 7, 9, 11};
constexpr std::array minor{0, 2, 3, 5, 7, 8, 10};
constexpr std::array dorian{0, 2, 3, 5, 7, 9, 10};
constexpr std::array mixolydian{0, 2, 4, 5, 7, 9, 10};
constexpr std::array chromatic{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
} // namespace

std::span<const int> intervalsFor(ScaleKind kind) noexcept {
    switch (kind) {
    case ScaleKind::Major: return major;
    case ScaleKind::Minor: return minor;
    case ScaleKind::Dorian: return dorian;
    case ScaleKind::Mixolydian: return mixolydian;
    case ScaleKind::Chromatic: return chromatic;
    }
    return minor;
}

bool isPitchClassInScale(int pitchClass, int rootPitchClass, ScaleKind kind) noexcept {
    const auto relative = positiveModulo(pitchClass - rootPitchClass, 12);
    const auto intervals = intervalsFor(kind);
    return std::find(intervals.begin(), intervals.end(), relative) != intervals.end();
}

int nearestPitchInScale(int midiPitch, int rootPitchClass, ScaleKind kind) noexcept {
    for (int distance = 0; distance < 12; ++distance) {
        const auto down = midiPitch - distance;
        if (isPitchClassInScale(down, rootPitchClass, kind)) return down;
        const auto up = midiPitch + distance;
        if (isPitchClassInScale(up, rootPitchClass, kind)) return up;
    }
    return midiPitch;
}

int pitchClassToMidi(int pitchClass, int preferredOctave, int minimum, int maximum) noexcept {
    auto pitch = 12 * (preferredOctave + 1) + positiveModulo(pitchClass, 12);
    while (pitch < minimum) pitch += 12;
    while (pitch > maximum) pitch -= 12;
    return std::clamp(pitch, minimum, maximum);
}

std::vector<int> normalizePitchClasses(std::span<const int> pitchClasses) {
    std::vector<int> normalized;
    normalized.reserve(pitchClasses.size());
    for (const auto pitch : pitchClasses) normalized.push_back(positiveModulo(pitch, 12));
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

} // namespace pulso

