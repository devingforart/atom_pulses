#pragma once

#include "MusicTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pulso {

enum class MetricIntent : std::uint8_t { StrictGrid = 0, Tuplet, DeliberateDisplacement };

// Compact, reusable note-level authorship. GPT writes cells and places them across
// the form; PULSO validates and schedules them without recomposing their phrasing.
struct AuthoredNote {
    double beat{};
    double durationBeats{0.25};
    int pitch{60};
    int velocity{96};
    VoiceId voice{VoiceId::Unspecified};
    MetricIntent metricIntent{MetricIntent::StrictGrid};
};

struct AuthoredControl {
    double beat{};
    int controller{11};
    int value{100};
    VoiceId voice{VoiceId::Unspecified};
};

struct PerformanceCell {
    std::string id;
    double lengthBeats{4.0};
    std::vector<VoiceId> ownedVoices;
    std::vector<AuthoredNote> notes;
    std::vector<AuthoredControl> controls;
};

struct VoiceRemap {
    VoiceId from{VoiceId::Unspecified};
    VoiceId to{VoiceId::Unspecified};
};

struct PerformancePlacement {
    std::string cellId;
    int sectionIndex{};
    double startBeat{}; // Relative to the section.
    int repeats{1};
    int transpose{};
    double velocityScale{1.0};
    double timeScale{1.0};
    std::string purpose;
    std::vector<VoiceRemap> voiceMap;
    bool retrograde{};
    bool invertContour{};
    int inversionAxis{60};
    double fragmentStart{};
    double fragmentEnd{-1.0};
    MetricIntent metricIntent{MetricIntent::StrictGrid};
};

[[nodiscard]] std::string_view metricIntentKey(MetricIntent) noexcept;
[[nodiscard]] MetricIntent metricIntentFromKey(std::string_view) noexcept;

struct PerformanceScore {
    std::vector<PerformanceCell> cells;
    std::vector<PerformancePlacement> placements;

    [[nodiscard]] bool empty() const noexcept { return cells.empty() || placements.empty(); }
};

struct PerformanceScoreReport {
    std::size_t cellsAccepted{};
    std::size_t cellsRejected{};
    std::size_t notesAccepted{};
    std::size_t notesRejected{};
    std::size_t exactDuplicateCells{};
    double novelty{1.0};
};

class PerformanceScoreEngine final {
public:
    static PerformanceScoreReport normalize(PerformanceScore&, std::size_t sectionCount,
                                            const std::vector<double>& sectionLengths);
    [[nodiscard]] static std::array<bool, static_cast<std::size_t>(VoiceId::Count)>
        ownedVoicesForSection(const PerformanceScore&, int sectionIndex) noexcept;
    static void replaceChunk(Pattern&, const PerformanceScore&, int sectionIndex,
                             double chunkStartInSection, double chunkLength);
    [[nodiscard]] static std::uint64_t fingerprint(const PerformanceCell&) noexcept;
};

} // namespace pulso
