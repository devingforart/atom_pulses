#pragma once

#include "MusicTypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {

struct SongPlan;

struct InstrumentDefinition {
    std::string_view id;
    std::string_view name;
    ScoreDepartment department;
    VoiceId preferredVoice;
    int minimumPitch;
    int maximumPitch;
    bool polyphonic;
    double weight;
};

struct InstrumentAssignment {
    std::string id;
    std::string instrumentId;
    std::string name;
    VoiceId sourceVoice{VoiceId::Unspecified};
    std::string role;
    int minimumPitch{};
    int maximumPitch{127};
    int octaveShift{};
    double activity{0.55};
    double prominence{0.50};
    double doubling{0.20};
    std::vector<std::string> activeSections;
    std::string orchestralFunction{"body"};
    std::string articulation{"natural"};
    int divisiVoices{1};
    std::string liveDevice{"auto"};
    std::string livePresetIntent{"balanced natural"};
    TimbreSignature timbre;
};

struct OrchestrationLanguage {
    std::string description{"Evolving chamber-to-tutti orchestration"};
    double ensembleScale{0.58};
    double timbralMotion{0.65};
    double foregroundRotation{0.78};
    double doublingRestraint{0.72};
    double registerSeparation{0.74};
    double chamberContrast{0.62};
    double tuttiRarity{0.82};
    double harmonicDepth{0.72};
    double counterpointActivity{0.58};
    double divisiDepth{0.55};
    double articulationContrast{0.62};
    double familyDialogue{0.68};
    double hybridProduction{0.42};
};

// One shared sound world is optimized before individual instruments are selected.
// Per-part preset intent is interpreted relative to this palette instead of as an
// unrelated search phrase, keeping a large ensemble sonically coherent.
struct TimbrePalette {
    std::string description{"coherent, dimensional and natural"};
    std::string material{"warm organic core with restrained electronic detail"};
    std::string space{"deep foreground-to-background stage"};
    double warmth{0.62};
    double brightness{0.48};
    double transientDefinition{0.58};
    double acousticElectronicBalance{0.58};
    double cohesion{0.82};
    double contrast{0.55};
    // Derived from concrete instrument names in the AI-authored palette. It is an internal
    // binding contract and does not expand the public prompt schema.
    std::vector<std::string> essentialInstrumentIds;
};

struct OrchestrationReport {
    std::size_t parts{};
    std::size_t rhythmParts{};
    std::size_t harmonyParts{};
    std::size_t melodyParts{};
    std::size_t notesAssigned{};
    std::size_t notesDoubled{};
    std::size_t foregroundChanges{};
    std::size_t chamberSections{};
    std::size_t tuttiSections{};
    std::size_t independentNotes{};
    std::size_t contrapuntalParts{};
    std::size_t divisiNotes{};
    std::size_t articulationChanges{};
    std::size_t registerRepairs{};
    std::size_t balanceRemovals{};
    std::size_t implicitVoicesPruned{};
    std::size_t implicitPerformanceNotesPruned{};
    double registerClarity{1.0};
    double familyBalance{1.0};
};

[[nodiscard]] std::span<const InstrumentDefinition> instrumentCatalog() noexcept;
[[nodiscard]] const InstrumentDefinition* instrumentDefinition(std::string_view id) noexcept;
[[nodiscard]] InstrumentSoundModel instrumentSoundModel(std::string_view id) noexcept;
[[nodiscard]] std::vector<InstrumentAssignment> defaultOrchestrationAssignments();

class OrchestrationScore final {
public:
    [[nodiscard]] static OrchestrationReport realize(Pattern&, const SongPlan&);
    // Continuity repair runs after realization and can create notes directly on a part.
    // Re-apply the published part/voice register without changing pitch class.
    [[nodiscard]] static std::size_t enforcePublishedRegisters(Pattern&);
    static void applyPartExpression(Pattern&, const SongPlan&, OrchestrationReport* = nullptr);
};

} // namespace pulso
