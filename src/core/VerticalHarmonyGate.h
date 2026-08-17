#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct VerticalHarmonyReport {
    std::size_t collisionsBefore{};
    std::size_t collisionsAfter{};
    std::size_t supportNotesDucked{};
    std::size_t continuationFragmentsCreated{};
    double score{1.0};
};

// Reviews the notes that will actually sound together after orchestration. Tonality alone
// cannot detect a low major seventh/minor ninth or tritone between two legal scale tones.
// Short gaps in sustained support preserve both pitches and behave like arrangement-aware
// sidechain phrasing instead of rewriting the harmony.
class VerticalHarmonyGate final {
public:
    [[nodiscard]] static VerticalHarmonyReport enforce(Pattern&);
    [[nodiscard]] static std::size_t audit(const Pattern&);
};

} // namespace pulso
