#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;
struct InstrumentAssignment;

struct ElectronicFabricReport {
    bool active{};
    std::size_t notesCreated{};
    std::size_t foundationNotesCreated{};
    std::size_t protagonistNotesCreated{};
    std::size_t arpeggioNotesCreated{};
    std::size_t dialogueNotesCreated{};
    std::size_t supportNotesCreated{};
    std::size_t publicationClosureNotesCreated{};
    std::size_t harmonicFloorBarsRepaired{};
    std::size_t protagonistWindowsRepaired{};
    std::size_t resolutionCodaNotesCreated{};
    std::size_t independentLines{};
    std::size_t meaningfulLines{};
    std::size_t protagonistPhraseWindows{};
    std::size_t arpeggioNoteCount{};
    std::size_t dialogueLines{};
    double harmonicFloorCoverage{};
    double medianHarmonicFloorLayers{};
    bool ready{true};
};

struct ThematicOwnershipReport {
    bool active{};
    std::size_t foregroundTracksBefore{};
    std::size_t foregroundTracksAfter{};
    std::size_t consolidatedTracks{};
    std::size_t notesReassigned{};
};

struct TimbralHandoffReport {
    bool active{};
    std::size_t contentLanes{};
    std::size_t timbralDestinations{};
    std::size_t phraseWindowsReassigned{};
    std::size_t notesReassigned{};
    std::size_t populatedDestinations{};
    bool exactCast{};
};

// Turns the AI's high-level harmonic, thematic and orchestration decisions into a
// complete electronic score. It is deliberately additive: emergency fallback can be
// removed without deleting this plan-derived structural fabric.
class ElectronicCompositionFabric final {
public:
    static void normalizePlan(SongPlan&);
    // Legacy non-AI routing only. Every AI-authored instrument owns its own
    // performance notes, including deliberately related relay instruments.
    [[nodiscard]] static bool rendererOwnedDestination(
        const SongPlan&, const InstrumentAssignment&) noexcept;
    [[nodiscard]] static ElectronicFabricReport materialize(Pattern&, const SongPlan&);
    // Measures foreground ownership. Legacy fallback may consolidate a redundant
    // thematic destination; AI-authored notes are never reassigned.
    [[nodiscard]] static ThematicOwnershipReport concentrateThematicOwnership(
        Pattern&, const SongPlan&);
    // Reports explicitly authored shared lanes and cast coverage. It never moves
    // or duplicates notes between AI-authored instruments after composition.
    [[nodiscard]] static TimbralHandoffReport realizeTimbralHandoffs(
        Pattern&, const SongPlan&);
    // Re-establishes the aggregate musical promises after tonal, vertical and
    // duration repair have altered the realized score. It only uses retained AI
    // lanes and material from the authored harmony/motif.
    [[nodiscard]] static ElectronicFabricReport convergePublication(Pattern&, const SongPlan&);
    // Recompute the contract from the exact notes that will be published. Rendering,
    // register repair and release shaping are allowed to alter the material after
    // materialize(), so publication must never trust the construction counters.
    [[nodiscard]] static ElectronicFabricReport audit(const Pattern&, const SongPlan&);
};

} // namespace pulso
