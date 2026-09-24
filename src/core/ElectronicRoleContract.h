#pragma once

#include "MusicTypes.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pulso {

struct InstrumentAssignment;
struct SongPlan;

struct AuthoredMotionOwnerReconciliation {
    bool changed{};
    std::string previousOwnerId;
    std::string electedOwnerId;
    std::size_t electedAuthoredNotes{};
};

// Shared semantic vocabulary for electronic roles. Composition, validation and
// publication must agree on these predicates; otherwise a transition can silently
// masquerade as an arpeggiator or a required motion lane can disappear between stages.
class ElectronicRoleContract final {
public:
    [[nodiscard]] static bool electronic(const SongPlan&) noexcept;
    [[nodiscard]] static bool transition(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static bool transition(const InstrumentPart&) noexcept;
    [[nodiscard]] static bool motionCandidate(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static bool motionCandidate(const InstrumentPart&) noexcept;
    [[nodiscard]] static bool motionOwner(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static bool motionOwner(const InstrumentPart&) noexcept;
    // Elects one recurrence conductor without deleting the other motion-capable parts.
    // Every other candidate is explicitly retained as supporting motion.
    [[nodiscard]] static std::optional<std::size_t> electPrimaryMotionOwner(
        std::span<InstrumentAssignment> instruments,
        std::string_view protagonistInstrumentId = {});
    // Once all GPT blocks have been assembled, the MIDI is authoritative about
    // which compatible lane actually performs the recurrence function. If the
    // provisional owner is empty and was not explicitly requested by the user,
    // transfer only the role marker to the strongest populated candidate. No MIDI
    // is copied, generated or removed here.
    [[nodiscard]] static AuthoredMotionOwnerReconciliation
        reconcileAuthoredPrimaryMotionOwner(SongPlan&);
    [[nodiscard]] static bool requiresMotionOwner(const SongPlan&);
    [[nodiscard]] static std::size_t motionOwnerCount(const SongPlan&) noexcept;
};

} // namespace pulso
