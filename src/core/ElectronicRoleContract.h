#pragma once

#include "MusicTypes.h"

#include <cstddef>

namespace pulso {

struct InstrumentAssignment;
struct SongPlan;

// Shared semantic vocabulary for electronic roles. Composition, validation and
// publication must agree on these predicates; otherwise a transition can silently
// masquerade as an arpeggiator or a required motion lane can disappear between stages.
class ElectronicRoleContract final {
public:
    [[nodiscard]] static bool electronic(const SongPlan&) noexcept;
    [[nodiscard]] static bool transition(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static bool transition(const InstrumentPart&) noexcept;
    [[nodiscard]] static bool motionOwner(const InstrumentAssignment&) noexcept;
    [[nodiscard]] static bool motionOwner(const InstrumentPart&) noexcept;
    [[nodiscard]] static bool requiresMotionOwner(const SongPlan&);
    [[nodiscard]] static std::size_t motionOwnerCount(const SongPlan&) noexcept;
};

} // namespace pulso
