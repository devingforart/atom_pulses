#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct SongPlan;

// Post-orchestration musical identity is evaluated on the exact parts and attacks that
// will be exported.  The gate preserves the AI's material, but repairs contradictions
// between a declared role and its realised MIDI (for example, a transition part playing
// continuous hand percussion).
struct MusicalIdentityReport {
    bool active{};
    std::size_t groovePhrasePairs{};
    std::size_t grooveRecallPhrases{};
    std::size_t grooveNotesReplaced{};
    std::size_t groovePhraseDevelopments{};
    std::size_t grooveDevelopmentNotes{};
    std::size_t responsePhrases{};
    std::size_t responseParts{};
    std::size_t derivedResponsePhrases{};
    std::size_t responseNotesRetuned{};
    std::size_t transitionNotesBefore{};
    std::size_t transitionNotesAfter{};
    std::size_t transitionNotesRemoved{};
    std::size_t transitionBoundaries{};
    std::size_t percussionDurationsAuthored{};
    double grooveRecallRatio{1.0};
    double responseLineageRatio{1.0};
    double transitionRestraint{1.0};
    double score{1.0};
};

class MusicalIdentityGate final {
public:
    [[nodiscard]] static MusicalIdentityReport enforce(Pattern&, const SongPlan&);
};

} // namespace pulso
