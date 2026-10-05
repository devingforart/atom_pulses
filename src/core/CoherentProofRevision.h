#pragma once

#include "CoherentProofGate.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pulso {

struct CoherentRevisionWindow {
    std::uint16_t partId{};
    double startBeat{};
    double endBeat{};
    double priority{};
};

// A contextual, symbolic critic. It never changes notes: only the AI may
// supply replacements, and the caller must validate the complete result.
class CoherentProofRevision final {
public:
    [[nodiscard]] static double issuePriority(const CoherentProofIssue&) noexcept;
    [[nodiscard]] static double risk(const CoherentProofGateReport&) noexcept;
    [[nodiscard]] static std::vector<CoherentRevisionWindow> selectWindows(
        const SongPlan&, const Pattern&, const CoherentProofGateReport&,
        std::size_t maximum = 2);
    [[nodiscard]] static bool accepts(const CoherentProofGateReport& before,
                                      const CoherentProofGateReport& after,
                                      std::size_t originalNotes,
                                      std::size_t revisedNotes) noexcept;
};

} // namespace pulso
