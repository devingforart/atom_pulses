#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct CreativeAuthorityReport {
    bool active{};
    std::size_t foregroundFallbackNotesRemoved{};
    std::size_t movementBassFallbackNotesRemoved{};
    std::size_t ownedHarmonyProceduralNotesRemoved{};
    std::size_t ownedGrooveProceduralNotesRemoved{};
    double foregroundAiRatioBefore{1.0};
    double foregroundAiRatioAfter{1.0};
};

// Enforces authorship after every generative and continuity stage. It does not compose:
// it prevents the local safety engine from silently becoming the writer of GPT-owned music.
class CreativeAuthority final {
public:
    [[nodiscard]] static CreativeAuthorityReport enforce(Pattern&, const SongPlan&);
};

} // namespace pulso
