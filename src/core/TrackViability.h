#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace pulso {

struct SongPlan;
struct InstrumentAssignment;

enum class TrackFunction {
    Rhythm,
    Bass,
    HarmonicFloor,
    HarmonicVoice,
    Pulse,
    Protagonist,
    Dialogue,
    Environment,
    Transition,
    OneShot
};

struct TrackViabilityContract {
    TrackFunction function{TrackFunction::HarmonicVoice};
    std::size_t minimumNotes{6};
    std::size_t minimumActiveBars{4};
    std::size_t minimumPhrases{2};
    bool eventException{};
};

struct TrackViabilityReport {
    bool active{};
    std::size_t declaredTracks{};
    std::size_t populatedBefore{};
    std::size_t meaningfulBefore{};
    std::size_t developedTracks{};
    std::size_t notesCreated{};
    std::size_t mergedTracks{};
    std::size_t prunedTracks{};
    std::size_t retainedTracks{};
    std::size_t viableTracks{};
    std::size_t tokenTracks{};
    std::size_t eventTracks{};
    double viabilityRatio{1.0};
    double retentionRatio{1.0};
    double score{1.0};
    bool ready{true};
    std::vector<std::string> issues;
};

// A DAW track is published only when its MIDI performs a complete role. Sparse events
// are valid for impacts and transitions; beds, basses, pulses and speakers need phrases
// and sectional presence. AI seeds may be developed from the authored motif/harmony.
// Technical filler without an authored seed is merged or removed.
class TrackViability final {
public:
    // A one-bar rounding margin is acceptable only when note quantity and phrase
    // development already satisfy the authored contract. This does not create MIDI
    // or excuse a genuinely sparse performance.
    [[nodiscard]] static bool marginalActiveBarAcceptance(
        std::size_t notes, std::size_t activeBars, std::size_t phrases,
        const TrackViabilityContract&) noexcept;
    [[nodiscard]] static bool acceptsCoverage(
        std::size_t notes, std::size_t activeBars, std::size_t phrases,
        const TrackViabilityContract&) noexcept;
    [[nodiscard]] static TrackViabilityContract contractFor(
        const InstrumentPart&, const SongPlan&);
    [[nodiscard]] static TrackViabilityContract contractFor(
        const InstrumentAssignment&, const SongPlan&);
    [[nodiscard]] static TrackViabilityReport enforce(Pattern&, SongPlan&);
    // Terminal publication invariant: after all pitch/duration collision repair,
    // remove any lane that was made incomplete. This pass never authors new notes.
    [[nodiscard]] static TrackViabilityReport compactIncomplete(Pattern&, SongPlan&);
    [[nodiscard]] static TrackViabilityReport audit(const Pattern&, const SongPlan&);
    static void stamp(Pattern&, const TrackViabilityReport&);
};

} // namespace pulso
