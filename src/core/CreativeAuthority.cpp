#include "CreativeAuthority.h"

#include "PerformanceScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>

namespace pulso {
namespace {

bool aiOrigin(NoteOrigin origin) noexcept {
    return origin == NoteOrigin::AiAuthored || origin == NoteOrigin::AiTransformed;
}

bool foregroundVoice(VoiceId voice) noexcept {
    return voice == VoiceId::Lead || voice == VoiceId::Countermelody;
}

VoiceId targetVoice(const PerformancePlacement& placement, VoiceId source) noexcept {
    const auto mapping = std::find_if(placement.voiceMap.begin(), placement.voiceMap.end(),
        [&](const auto& item) { return item.from == source; });
    return mapping == placement.voiceMap.end() ? source : mapping->to;
}

std::array<bool, static_cast<std::size_t>(VoiceId::Count)>
globallyOwnedVoices(const PerformanceScore& score) {
    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> result{};
    for (const auto& placement : score.placements) {
        const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
            [&](const auto& item) { return item.id == placement.cellId; });
        if (cell == score.cells.end()) continue;
        for (const auto source : cell->ownedVoices) {
            const auto target = targetVoice(placement, source);
            const auto index = static_cast<std::size_t>(target);
            if (index < result.size()) result[index] = true;
        }
    }
    return result;
}

double foregroundRatio(const Pattern& pattern) noexcept {
    auto total = std::size_t{};
    auto authored = std::size_t{};
    for (const auto& note : pattern.notes) {
        if (!foregroundVoice(note.voice)) continue;
        ++total;
        if (aiOrigin(note.origin)) ++authored;
    }
    return total == 0 ? 1.0 : static_cast<double>(authored) / static_cast<double>(total);
}

} // namespace

CreativeAuthorityReport CreativeAuthority::enforce(Pattern& pattern, const SongPlan& plan) {
    CreativeAuthorityReport report;
    report.active = !plan.performanceScore.empty() &&
        plan.productionModeSource != "local_fallback" &&
        plan.productionModeSource != "local_engine";
    if (!report.active) return report;

    report.foregroundAiRatioBefore = foregroundRatio(pattern);
    const auto owned = globallyOwnedVoices(plan.performanceScore);
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (aiOrigin(note.origin)) return false;

        // These voices express identity and narrative. In an AI score, missing material
        // means silence or an incomplete plan; it never grants authorship to fallback.
        if (foregroundVoice(note.voice)) {
            ++report.foregroundFallbackNotesRemoved;
            return true;
        }
        if (note.voice == VoiceId::MovementBass) {
            ++report.movementBassFallbackNotesRemoved;
            return true;
        }

        const auto index = static_cast<std::size_t>(note.voice);
        if (note.origin != NoteOrigin::Procedural || index >= owned.size() || !owned[index])
            return false;
        if (isVoiceInFamily(note.voice, VoiceFamily::Harmony)) {
            ++report.ownedHarmonyProceduralNotesRemoved;
            return true;
        }
        if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm)) {
            ++report.ownedGrooveProceduralNotesRemoved;
            return true;
        }
        return false;
    }), pattern.notes.end());
    report.foregroundAiRatioAfter = foregroundRatio(pattern);
    return report;
}

} // namespace pulso
