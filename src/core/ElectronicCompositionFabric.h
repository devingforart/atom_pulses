#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

struct ElectronicFabricReport {
    bool active{};
    std::size_t notesCreated{};
    std::size_t foundationNotesCreated{};
    std::size_t protagonistNotesCreated{};
    std::size_t arpeggioNotesCreated{};
    std::size_t dialogueNotesCreated{};
    std::size_t supportNotesCreated{};
    std::size_t independentLines{};
    std::size_t meaningfulLines{};
    std::size_t protagonistPhraseWindows{};
    std::size_t arpeggioNoteCount{};
    std::size_t dialogueLines{};
    double harmonicFloorCoverage{};
    double medianHarmonicFloorLayers{};
    bool ready{true};
};

// Turns the AI's high-level harmonic, thematic and orchestration decisions into a
// complete electronic score. It is deliberately additive: emergency fallback can be
// removed without deleting this plan-derived structural fabric.
class ElectronicCompositionFabric final {
public:
    static void normalizePlan(SongPlan&);
    [[nodiscard]] static ElectronicFabricReport materialize(Pattern&, const SongPlan&);
};

} // namespace pulso
