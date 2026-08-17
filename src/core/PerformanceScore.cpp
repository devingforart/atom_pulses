#include "PerformanceScore.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace pulso {
namespace {

const PerformanceCell* findCell(const PerformanceScore& score, const std::string& id) noexcept {
    const auto found = std::find_if(score.cells.begin(), score.cells.end(), [&](const auto& cell) {
        return cell.id == id;
    });
    return found == score.cells.end() ? nullptr : &*found;
}

bool validVoice(VoiceId voice) noexcept {
    return static_cast<std::size_t>(voice) < static_cast<std::size_t>(VoiceId::Count);
}

VoiceId remappedVoice(const PerformancePlacement& placement, VoiceId source) noexcept {
    const auto found = std::find_if(placement.voiceMap.begin(), placement.voiceMap.end(),
        [&](const auto& mapping) { return mapping.from == source; });
    return found == placement.voiceMap.end() ? source : found->to;
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint32_t narrativeIdFor(const PerformanceCell& cell) noexcept {
    auto hash = std::uint32_t{2166136261u};
    const auto& text = cell.themeId.empty() ? cell.id : cell.themeId;
    for (const auto character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

} // namespace

std::string_view metricIntentKey(MetricIntent value) noexcept {
    switch (value) {
        case MetricIntent::StrictGrid: return "strict_grid";
        case MetricIntent::Tuplet: return "tuplet";
        case MetricIntent::DeliberateDisplacement: return "deliberate_displacement";
    }
    return "strict_grid";
}

MetricIntent metricIntentFromKey(std::string_view value) noexcept {
    if (value == "tuplet") return MetricIntent::Tuplet;
    if (value == "deliberate_displacement") return MetricIntent::DeliberateDisplacement;
    return MetricIntent::StrictGrid;
}

std::uint64_t PerformanceScoreEngine::fingerprint(const PerformanceCell& cell) noexcept {
    auto hash = std::uint64_t{0xcbf29ce484222325ULL};
    hash = mix(hash, static_cast<std::uint64_t>(std::llround(cell.lengthBeats * 96.0)));
    for (const auto& note : cell.notes) {
        hash = mix(hash, static_cast<std::uint64_t>(note.voice));
        hash = mix(hash, static_cast<std::uint64_t>(std::llround(note.beat * 96.0)));
        hash = mix(hash, static_cast<std::uint64_t>(std::llround(note.durationBeats * 96.0)));
        hash = mix(hash, static_cast<std::uint64_t>(note.pitch));
        hash = mix(hash, static_cast<std::uint64_t>(note.velocity / 4));
    }
    return hash;
}

PerformanceScoreReport PerformanceScoreEngine::normalize(
    PerformanceScore& score, std::size_t sectionCount, const std::vector<double>& sectionLengths) {
    PerformanceScoreReport report;
    if (score.cells.size() > 64) score.cells.resize(64);
    std::set<std::string> ids;
    std::set<std::string> themes;
    std::set<std::uint64_t> fingerprints;
    score.cells.erase(std::remove_if(score.cells.begin(), score.cells.end(), [&](auto& cell) {
        if (cell.id.empty() || !ids.insert(cell.id).second || !std::isfinite(cell.lengthBeats)) {
            ++report.cellsRejected;
            return true;
        }
        cell.lengthBeats = std::clamp(cell.lengthBeats, 0.25, 64.0);
        if (cell.themeId.empty()) cell.themeId = cell.id;
        if (cell.themeId.size() > 80) cell.themeId.resize(80);
        if (cell.narrativeFunction.empty()) cell.narrativeFunction = "support";
        if (cell.narrativeFunction.size() > 40) cell.narrativeFunction.resize(40);
        themes.insert(cell.themeId);
        if (cell.narrativeFunction != "support") ++report.narrativeCells;
        std::array<bool, static_cast<std::size_t>(VoiceId::Count)> owned{};
        cell.ownedVoices.erase(std::remove_if(cell.ownedVoices.begin(), cell.ownedVoices.end(), [&](VoiceId voice) {
            if (!validVoice(voice) || owned[static_cast<std::size_t>(voice)]) return true;
            owned[static_cast<std::size_t>(voice)] = true;
            return false;
        }), cell.ownedVoices.end());
        if (cell.notes.size() > 768) cell.notes.resize(768);
        cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](auto& note) {
            if (!validVoice(note.voice) || !std::isfinite(note.beat) ||
                !std::isfinite(note.durationBeats) || note.beat < 0.0 || note.beat >= cell.lengthBeats) {
                ++report.notesRejected;
                return true;
            }
            if (!owned[static_cast<std::size_t>(note.voice)]) {
                cell.ownedVoices.push_back(note.voice);
                owned[static_cast<std::size_t>(note.voice)] = true;
            }
            const auto& definition = voiceDefinition(note.voice);
            note.pitch = std::clamp(note.pitch, isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ? 0 : definition.minimumPitch,
                                    isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ? 127 : definition.maximumPitch);
            note.velocity = std::clamp(note.velocity, 1, 127);
            if (note.metricIntent == MetricIntent::StrictGrid)
                note.beat = std::round(note.beat * 4.0) / 4.0;
            else if (note.metricIntent == MetricIntent::Tuplet)
                note.beat = std::round(note.beat * 12.0) / 12.0;
            note.durationBeats = std::clamp(note.durationBeats, 0.01, cell.lengthBeats - note.beat);
            ++report.notesAccepted;
            return false;
        }), cell.notes.end());
        std::sort(cell.notes.begin(), cell.notes.end(), [](const auto& left, const auto& right) {
            return std::tie(left.beat, left.voice, left.pitch) < std::tie(right.beat, right.voice, right.pitch);
        });
        cell.notes.erase(std::unique(cell.notes.begin(), cell.notes.end(), [](const auto& left, const auto& right) {
            return std::abs(left.beat - right.beat) < 0.0001 && left.voice == right.voice && left.pitch == right.pitch;
        }), cell.notes.end());
        if (cell.controls.size() > 384) cell.controls.resize(384);
        cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](auto& control) {
            if (!validVoice(control.voice) || !std::isfinite(control.beat) ||
                control.beat < 0.0 || control.beat >= cell.lengthBeats) return true;
            control.controller = std::clamp(control.controller, 0, 127);
            control.value = std::clamp(control.value, 0, 127);
            return false;
        }), cell.controls.end());
        const auto cellFingerprint = fingerprint(cell);
        if (!fingerprints.insert(cellFingerprint).second) ++report.exactDuplicateCells;
        ++report.cellsAccepted;
        return cell.ownedVoices.empty();
    }), score.cells.end());

    if (score.placements.size() > 512) score.placements.resize(512);
    score.placements.erase(std::remove_if(score.placements.begin(), score.placements.end(), [&](auto& placement) {
        if (placement.sectionIndex < 0 || static_cast<std::size_t>(placement.sectionIndex) >= sectionCount ||
            findCell(score, placement.cellId) == nullptr || !std::isfinite(placement.startBeat) ||
            !std::isfinite(placement.velocityScale) || !std::isfinite(placement.timeScale)) return true;
        const auto* cell = findCell(score, placement.cellId);
        const auto sectionLength = placement.sectionIndex < static_cast<int>(sectionLengths.size())
            ? sectionLengths[static_cast<std::size_t>(placement.sectionIndex)] : 0.0;
        placement.startBeat = std::clamp(placement.startBeat, 0.0, std::max(0.0, sectionLength - 0.01));
        placement.repeats = std::clamp(placement.repeats, 1, 128);
        placement.transpose = std::clamp(placement.transpose, -24, 24);
        placement.velocityScale = std::clamp(placement.velocityScale, 0.25, 1.5);
        placement.timeScale = std::clamp(placement.timeScale, 0.25, 4.0);
        if (placement.metricIntent == MetricIntent::StrictGrid) {
            constexpr std::array legalScales{0.5, 1.0, 2.0, 4.0};
            placement.timeScale = *std::min_element(legalScales.begin(), legalScales.end(),
                [&](double a, double b) { return std::abs(a - placement.timeScale) <
                                                std::abs(b - placement.timeScale); });
            placement.startBeat = std::round(placement.startBeat * 4.0) / 4.0;
        } else if (placement.metricIntent == MetricIntent::Tuplet) {
            placement.startBeat = std::round(placement.startBeat * 12.0) / 12.0;
        }
        if (placement.purpose.size() > 180) placement.purpose.resize(180);
        std::array<bool, static_cast<std::size_t>(VoiceId::Count)> remapped{};
        placement.voiceMap.erase(std::remove_if(placement.voiceMap.begin(), placement.voiceMap.end(),
            [&](const auto& mapping) {
                if (!validVoice(mapping.from) || !validVoice(mapping.to) || mapping.from == mapping.to ||
                    cell == nullptr || std::find(cell->ownedVoices.begin(), cell->ownedVoices.end(), mapping.from) ==
                                       cell->ownedVoices.end())
                    return true;
                const auto index = static_cast<std::size_t>(mapping.from);
                if (remapped[index]) return true;
                remapped[index] = true;
                return false;
            }), placement.voiceMap.end());
        placement.inversionAxis = std::clamp(placement.inversionAxis, 0, 127);
        const auto cellLength = cell == nullptr ? 0.25 : cell->lengthBeats;
        if (!std::isfinite(placement.fragmentStart)) placement.fragmentStart = 0.0;
        placement.fragmentStart = std::clamp(placement.fragmentStart, 0.0,
                                             std::max(0.0, cellLength - 0.01));
        if (!std::isfinite(placement.fragmentEnd) || placement.fragmentEnd < 0.0)
            placement.fragmentEnd = cellLength;
        placement.fragmentEnd = std::clamp(placement.fragmentEnd,
            placement.fragmentStart + 0.01, cellLength);
        return false;
    }), score.placements.end());
    report.novelty = report.cellsAccepted == 0 ? 1.0
        : 1.0 - static_cast<double>(report.exactDuplicateCells) / static_cast<double>(report.cellsAccepted);
    report.namedThemes = themes.size();
    return report;
}

std::array<bool, static_cast<std::size_t>(VoiceId::Count)>
PerformanceScoreEngine::ownedVoicesForSection(const PerformanceScore& score, int sectionIndex) noexcept {
    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> result{};
    for (const auto& placement : score.placements) {
        if (placement.sectionIndex != sectionIndex) continue;
        if (const auto* cell = findCell(score, placement.cellId))
            for (const auto voice : cell->ownedVoices)
                if (const auto target = remappedVoice(placement, voice); validVoice(target))
                    result[static_cast<std::size_t>(target)] = true;
    }
    return result;
}

void PerformanceScoreEngine::replaceChunk(Pattern& chunk, const PerformanceScore& score,
                                          int sectionIndex, double chunkStartInSection,
                                          double chunkLength) {
    struct OwnershipSpan {
        double start{};
        double end{};
        std::array<bool, static_cast<std::size_t>(VoiceId::Count)> voices{};
    };
    std::vector<OwnershipSpan> spans;
    const auto chunkEnd = chunkStartInSection + chunkLength;
    for (const auto& placement : score.placements) {
        if (placement.sectionIndex != sectionIndex) continue;
        const auto* cell = findCell(score, placement.cellId);
        if (cell == nullptr) continue;
        const auto iterationLength = cell->lengthBeats * placement.timeScale;
        for (auto repeat = 0; repeat < placement.repeats; ++repeat) {
            const auto origin = placement.startBeat + repeat * iterationLength;
            const auto ownedStartInCell = placement.retrograde
                ? cell->lengthBeats - placement.fragmentEnd : placement.fragmentStart;
            const auto ownedEndInCell = placement.retrograde
                ? cell->lengthBeats - placement.fragmentStart : placement.fragmentEnd;
            const auto ownedStart = origin + ownedStartInCell * placement.timeScale;
            const auto ownedEnd = origin + ownedEndInCell * placement.timeScale;
            if (ownedStart >= chunkEnd || ownedEnd <= chunkStartInSection) continue;
            OwnershipSpan span{std::max(ownedStart, chunkStartInSection),
                               std::min(ownedEnd, chunkEnd), {}};
            for (const auto voice : cell->ownedVoices) {
                const auto target = remappedVoice(placement, voice);
                if (validVoice(target)) span.voices[static_cast<std::size_t>(target)] = true;
            }
            spans.push_back(span);
        }
    }
    const auto ownsAt = [&](VoiceId voice, double start, double end) {
        const auto index = static_cast<std::size_t>(voice);
        if (index >= static_cast<std::size_t>(VoiceId::Count)) return false;
        return std::any_of(spans.begin(), spans.end(), [&](const auto& span) {
            return span.voices[index] && start < span.end - 0.0001 && end > span.start + 0.0001;
        });
    };
    chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [&](const auto& note) {
        const auto start = chunkStartInSection + note.startBeat;
        return ownsAt(note.voice, start, start + std::max(0.01, note.durationBeats));
    }), chunk.notes.end());
    chunk.controls.erase(std::remove_if(chunk.controls.begin(), chunk.controls.end(), [&](const auto& control) {
        const auto beat = chunkStartInSection + control.beat;
        return ownsAt(control.voice, beat, beat + 0.0002);
    }), chunk.controls.end());

    for (const auto& placement : score.placements) {
        if (placement.sectionIndex != sectionIndex) continue;
        const auto* cell = findCell(score, placement.cellId);
        if (cell == nullptr) continue;
        const auto iterationLength = cell->lengthBeats * placement.timeScale;
        for (auto repeat = 0; repeat < placement.repeats; ++repeat) {
            const auto origin = placement.startBeat + repeat * iterationLength;
            if (origin >= chunkEnd || origin + iterationLength <= chunkStartInSection) continue;
            for (const auto& authored : cell->notes) {
                if (authored.beat < placement.fragmentStart || authored.beat >= placement.fragmentEnd)
                    continue;
                const auto transformedBeat = placement.retrograde
                    ? std::max(0.0, cell->lengthBeats - authored.beat - authored.durationBeats)
                    : authored.beat;
                const auto sectionBeat = origin + transformedBeat * placement.timeScale;
                if (sectionBeat < chunkStartInSection || sectionBeat >= chunkEnd) continue;
                const auto voice = remappedVoice(placement, authored.voice);
                if (!validVoice(voice)) continue;
                const auto& definition = voiceDefinition(voice);
                const auto rhythmic = isVoiceInFamily(voice, VoiceFamily::Rhythm);
                const auto transformedPitch = placement.invertContour
                    ? placement.inversionAxis * 2 - authored.pitch : authored.pitch;
                const auto pitch = rhythmic ? authored.pitch : std::clamp(transformedPitch + placement.transpose,
                    definition.minimumPitch, definition.maximumPitch);
                chunk.notes.push_back({sectionBeat - chunkStartInSection,
                    std::max(0.01, authored.durationBeats * placement.timeScale), pitch,
                    std::clamp(static_cast<int>(std::lround(authored.velocity * placement.velocityScale)), 1, 127),
                    definition.midiChannel, voice, 0,
                    authored.metricIntent != MetricIntent::StrictGrid ||
                    placement.metricIntent != MetricIntent::StrictGrid,
                    placement.retrograde || placement.invertContour || placement.transpose != 0 ||
                        std::abs(placement.timeScale - 1.0) > 0.001 ||
                        placement.fragmentStart > 0.001 ||
                        placement.fragmentEnd < cell->lengthBeats - 0.001 || !placement.voiceMap.empty()
                        ? NoteOrigin::AiTransformed : NoteOrigin::AiAuthored,
                    narrativeIdFor(*cell)});
            }
            for (const auto& authored : cell->controls) {
                if (authored.beat < placement.fragmentStart || authored.beat >= placement.fragmentEnd)
                    continue;
                const auto transformedBeat = placement.retrograde
                    ? std::max(0.0, cell->lengthBeats - authored.beat) : authored.beat;
                const auto sectionBeat = origin + transformedBeat * placement.timeScale;
                if (sectionBeat < chunkStartInSection || sectionBeat >= chunkEnd) continue;
                const auto voice = remappedVoice(placement, authored.voice);
                if (!validVoice(voice)) continue;
                chunk.controls.push_back({sectionBeat - chunkStartInSection, authored.controller,
                    authored.value, voiceDefinition(voice).midiChannel, voice, 0, true});
            }
        }
    }
}

} // namespace pulso
