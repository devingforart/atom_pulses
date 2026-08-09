#include "TestSupport.h"

#include "core/Scale.h"

#include <array>

using namespace pulso;

void runScaleTests() {
    require(isPitchClassInScale(0, 0, ScaleKind::Major), "C must be in C major");
    require(isPitchClassInScale(11, 0, ScaleKind::Major), "B must be in C major");
    require(!isPitchClassInScale(1, 0, ScaleKind::Major), "C# must not be in C major");
    require(isPitchClassInScale(7, 9, ScaleKind::Minor), "G must be in A minor");
    require(nearestPitchInScale(61, 0, ScaleKind::Major) == 60,
            "C# should quantize down to C with a downward tie-break");
    require(pitchClassToMidi(0, 2, 28, 52) == 36, "C in bass octave should be MIDI 36");
    const std::array raw{12, -1, 0, 11, 24};
    const auto normalized = normalizePitchClasses(raw);
    require(normalized.size() == 2 && normalized[0] == 0 && normalized[1] == 11,
            "Pitch classes must normalize, sort and deduplicate");
}
