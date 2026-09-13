#include "SelectiveRepair.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace pulso {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const InstrumentAssignment& instrument,
                 std::initializer_list<std::string_view> words) {
    const auto text = lower(instrument.id + " " + instrument.instrumentId + " " +
                            instrument.name + " " + instrument.role + " " +
                            instrument.orchestralFunction);
    return std::any_of(words.begin(), words.end(), [&](const auto word) {
        return text.find(word) != std::string::npos;
    });
}

const SoundscapeLayerPlan* layerFor(const SongPlan& plan, const std::string& id) {
    const auto found = std::find_if(plan.soundscape.layers.begin(), plan.soundscape.layers.end(),
        [&](const auto& layer) { return layer.instrumentId == id; });
    return found == plan.soundscape.layers.end() ? nullptr : &*found;
}

bool eventInstrument(const SongPlan& plan, const InstrumentAssignment& instrument) {
    const auto* layer = layerFor(plan, instrument.id);
    return instrument.sourceVoice == VoiceId::Transitions ||
        instrument.orchestralFunction == "transition" ||
        (layer != nullptr && (layer->kind == SoundscapeLayerKind::Transition ||
                              layer->kind == SoundscapeLayerKind::OneShot));
}

} // namespace

bool SelectiveRepair::publicationReady(const CompositionRenderReport& report) noexcept {
    return report.production.ready && report.narrative.creativeReady &&
        (!report.soundscape.active || report.soundscape.ready) &&
        (!report.trackViability.active || report.trackViability.ready);
}

bool SelectiveRepair::criticalFailure(const CompositionRenderReport& report) noexcept {
    if (!report.production.ready) return true;
    const auto brokenNarrative = report.narrative.active && !report.narrative.creativeReady &&
        (report.narrative.score < 0.68 ||
         (!report.narrative.narrativeSpineReady && report.narrative.resolutionScore < 0.48));
    const auto brokenSoundscape = report.soundscape.active && !report.soundscape.ready &&
        (report.soundscape.score < 0.70 || report.soundscape.meaningfulCoverage < 0.65);
    const auto brokenTracks = report.trackViability.active && !report.trackViability.ready &&
        (report.trackViability.score < 0.72 || report.trackViability.viabilityRatio < 0.70);
    return brokenNarrative || brokenSoundscape || brokenTracks;
}

bool SelectiveRepair::safeWithEditorialObservations(
    const CompositionRenderReport& report) noexcept {
    if (criticalFailure(report) || !report.production.ready) return false;
    const auto narrativeSafe = !report.narrative.active ||
        (report.narrative.creativeReady && report.narrative.score >= 0.80 &&
         report.narrative.resolutionScore >= 0.60 &&
         report.narrative.densityControl >= 0.72);
    const auto soundscapeSafe = !report.soundscape.active || report.soundscape.ready ||
        (report.soundscape.score >= 0.82 && report.soundscape.meaningfulCoverage >= 0.72 &&
         report.soundscape.underdevelopedVoices == 0 &&
         report.soundscape.underdevelopedEnvironments == 0 &&
         report.soundscape.staticLayerRuns == 0);
    const auto tracksSafe = !report.trackViability.active || report.trackViability.ready ||
        (report.trackViability.score >= 0.92 && report.trackViability.viabilityRatio >= 0.90 &&
         report.trackViability.tokenTracks == 0);
    return narrativeSafe && soundscapeSafe && tracksSafe && deficit(report) <= 1.0;
}

double SelectiveRepair::deficit(const CompositionRenderReport& report) noexcept {
    auto value = report.production.ready ? 0.0 : 5.0;
    if (report.narrative.active && !report.narrative.creativeReady)
        value += 0.8 + std::max(0.0, 0.82 - report.narrative.score) * 3.0 +
            std::min<std::size_t>(report.narrative.issues.size(), 6) * 0.12;
    if (report.soundscape.active && !report.soundscape.ready)
        value += 0.6 + std::max(0.0, 0.90 - report.soundscape.score) * 2.0 +
            std::min<std::size_t>(report.soundscape.underdevelopedVoices +
                report.soundscape.underdevelopedEnvironments +
                report.soundscape.missingTransitionEvents + report.soundscape.staticLayerRuns, 8) * 0.14;
    if (report.trackViability.active && !report.trackViability.ready)
        value += 0.6 + std::max(0.0, 1.0 - report.trackViability.score) * 2.0 +
            std::min<std::size_t>(report.trackViability.tokenTracks, 6) * 0.18;
    value += std::max(0.0, 0.72 - report.narrative.resolutionScore) * 2.0;
    value += std::max(0.0, 0.84 - report.narrative.densityControl) * 1.5;
    return value;
}

bool SelectiveRepair::editoriallyAcceptable(
    const CompositionRenderReport& before, const CompositionRenderReport& after,
    std::size_t completedRepairs) noexcept {
    if (completedRepairs == 0 || criticalFailure(after)) return false;
    const auto materiallyImproved = deficit(after) + 0.18 < deficit(before);
    const auto narrativeFloor = !after.narrative.active || after.narrative.score >= 0.76;
    const auto soundscapeFloor = !after.soundscape.active || after.soundscape.score >= 0.78;
    const auto viabilityFloor = !after.trackViability.active ||
        (after.trackViability.score >= 0.82 && after.trackViability.viabilityRatio >= 0.78);
    return materiallyImproved && narrativeFloor && soundscapeFloor && viabilityFloor;
}

SelectiveRepairPlan SelectiveRepair::diagnose(
    const SongPlan& plan, const Pattern& pattern, const CompositionRenderReport& report,
    std::size_t maximumTargets) {
    SelectiveRepairPlan result;
    result.needed = !publicationReady(report);
    result.deficit = deficit(report);
    if (!result.needed || plan.instruments.empty() || maximumTargets == 0) return result;

    std::map<std::string, std::size_t> authoredNotes;
    std::map<std::string, std::set<int>> authoredSections;
    std::map<std::string, std::set<std::string>> ownersByCell;
    for (const auto& cell : plan.performanceScore.cells) {
        for (const auto& note : cell.notes) {
            if (note.instrumentId.empty()) continue;
            ++authoredNotes[note.instrumentId];
            ownersByCell[cell.id].insert(note.instrumentId);
        }
    }
    for (const auto& placement : plan.performanceScore.placements) {
        const auto owners = ownersByCell.find(placement.cellId);
        if (owners == ownersByCell.end()) continue;
        for (const auto& owner : owners->second)
            authoredSections[owner].insert(placement.sectionIndex);
    }

    std::map<std::size_t, double> priority;
    const auto add = [&](std::size_t index, double points, std::string issue) {
        if (index >= plan.instruments.size()) return;
        priority[index] += points;
        if (std::find(result.issues.begin(), result.issues.end(), issue) == result.issues.end())
            result.issues.push_back(std::move(issue));
    };

    // Token and underwritten tracks are the cheapest, most deterministic repairs.
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& instrument = plan.instruments[index];
        if (eventInstrument(plan, instrument)) continue;
        const auto* layer = layerFor(plan, instrument.id);
        const auto minimumNotes = layer == nullptr
            ? std::size_t{6}
            : std::max<std::size_t>(6, static_cast<std::size_t>(
                  std::max(1, layer->minimumPhrases)) * 3);
        const auto minimumSections = plan.sections.size() < 4 ? std::size_t{1} : std::size_t{2};
        if (authoredNotes[instrument.id] < minimumNotes ||
            authoredSections[instrument.id].size() < minimumSections)
            add(index, 8.0 + (1.0 - instrument.prominence),
                "complete underwritten musical lines with real phrases and sectional development");
    }

    // One pulse lane may be hypnotic, but it must not become the entire composition.
    std::map<std::uint16_t, std::size_t> renderedByPart;
    for (const auto& note : pattern.notes)
        if (note.partId != 0) ++renderedByPart[note.partId];
    const auto totalNotes = std::max<std::size_t>(1, pattern.notes.size());
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& instrument = plan.instruments[index];
        const auto pulse = instrument.sourceVoice == VoiceId::HarmonicPulse ||
            containsAny(instrument, {"arp", "sequence", "pulse", "ostinato"});
        if (!pulse) continue;
        const auto partId = static_cast<std::uint16_t>(index + 1);
        const auto share = static_cast<double>(renderedByPart[partId]) /
                           static_cast<double>(totalNotes);
        if (share > 0.20)
            add(index, 7.0 + share * 10.0,
                "reduce single-arpeggio dominance through rests, variation and hand-offs");
    }

    const auto hasNarrativeIssue = [&](std::string_view issue) {
        return std::find(report.narrative.issues.begin(), report.narrative.issues.end(), issue) !=
               report.narrative.issues.end();
    };
    if (report.narrative.resolutionScore < 0.72 || !report.narrative.narrativeSpineReady) {
        const auto finalName = plan.sections.empty() ? std::string{} : plan.sections.back().name;
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            const auto speaks = instrument.sourceVoice == VoiceId::Lead ||
                instrument.sourceVoice == VoiceId::Countermelody ||
                instrument.sourceVoice == VoiceId::HarmonicUpper ||
                instrument.sourceVoice == VoiceId::HarmonicFoundation;
            const auto finalActive = finalName.empty() || instrument.activeSections.empty() ||
                std::find(instrument.activeSections.begin(), instrument.activeSections.end(), finalName) !=
                    instrument.activeSections.end();
            if (speaks && finalActive)
                add(index, 5.0 + instrument.prominence,
                    "make the ending repay harmonic and thematic debt without adding density");
        }
    }
    if (hasNarrativeIssue("overcrowded_arrangement") ||
        report.narrative.densityControl < 0.84 || report.soundscape.staticLayerRuns > 0) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            if (eventInstrument(plan, instrument)) continue;
            const auto partId = static_cast<std::uint16_t>(index + 1);
            const auto share = static_cast<double>(renderedByPart[partId]) /
                               static_cast<double>(totalNotes);
            if (share > 0.07 || containsAny(instrument, {"arp", "sequence", "pulse"}))
                add(index, 3.0 + share * 8.0 + (1.0 - instrument.prominence),
                    "restore breath and evolving orchestration in overcrowded or static regions");
        }
    }

    std::vector<std::pair<std::size_t, double>> ranked(priority.begin(), priority.end());
    std::stable_sort(ranked.begin(), ranked.end(), [&](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second > right.second;
        return plan.instruments[left.first].prominence > plan.instruments[right.first].prominence;
    });
    for (const auto& [index, score] : ranked) {
        (void) score;
        if (result.instrumentIndices.size() == maximumTargets) break;
        result.instrumentIndices.push_back(index);
    }

    // A critic finding with no exact culprit must still have an editorial owner.
    if (result.instrumentIndices.empty()) {
        std::vector<std::size_t> fallback(plan.instruments.size());
        for (std::size_t index = 0; index < fallback.size(); ++index) fallback[index] = index;
        std::stable_sort(fallback.begin(), fallback.end(), [&](auto left, auto right) {
            return plan.instruments[left].prominence > plan.instruments[right].prominence;
        });
        for (const auto index : fallback) {
            if (eventInstrument(plan, plan.instruments[index])) continue;
            result.instrumentIndices.push_back(index);
            if (result.instrumentIndices.size() == std::min<std::size_t>(3, maximumTargets)) break;
        }
        result.issues.push_back("repair the audible creative-gate findings while preserving the blueprint");
    }
    return result;
}

std::vector<std::size_t> SelectiveRepair::incompleteTargets(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates) {
    std::map<std::string, std::size_t> noteCounts;
    std::map<std::string, std::set<int>> sections;
    std::map<std::string, std::set<std::string>> instrumentsByCell;
    std::map<std::string, std::set<std::string>> themesByInstrument;
    for (const auto& cell : score.cells) {
        for (const auto& note : cell.notes) {
            if (note.instrumentId.empty()) continue;
            ++noteCounts[note.instrumentId];
            instrumentsByCell[cell.id].insert(note.instrumentId);
            if (!cell.themeId.empty()) themesByInstrument[note.instrumentId].insert(cell.themeId);
        }
    }
    auto resolutionSection = plan.sections.empty() ? -1 : static_cast<int>(plan.sections.size() - 1);
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto found = std::find_if(plan.sections.begin(), plan.sections.end(),
            [&](const auto& section) { return section.name == act.sectionName; });
        if (found != plan.sections.end())
            resolutionSection = static_cast<int>(std::distance(plan.sections.begin(), found));
    }
    std::set<std::string> codaOwners;
    for (const auto& placement : score.placements) {
        const auto found = instrumentsByCell.find(placement.cellId);
        if (found == instrumentsByCell.end()) continue;
        for (const auto& id : found->second) {
            sections[id].insert(placement.sectionIndex);
            if (placement.sectionIndex == resolutionSection && resolutionSection >= 0) {
                const auto sectionBeats = plan.sections[static_cast<std::size_t>(resolutionSection)].bars *
                    plan.beatsPerBar;
                const auto codaThreshold = std::max(0.0, sectionBeats - plan.beatsPerBar * 16.0);
                if (placement.startBeat >= codaThreshold) codaOwners.insert(id);
            }
        }
    }

    const auto protagonistThemes = themesByInstrument[plan.narrativeSpine.protagonistInstrumentId];

    std::vector<std::size_t> missing;
    for (const auto index : candidates) {
        if (index >= plan.instruments.size()) continue;
        const auto& instrument = plan.instruments[index];
        const auto layer = std::find_if(plan.soundscape.layers.begin(), plan.soundscape.layers.end(),
            [&](const auto& candidate) { return candidate.instrumentId == instrument.id; });
        const auto eventLayer = layer != plan.soundscape.layers.end() &&
            (layer->kind == SoundscapeLayerKind::Transition ||
             layer->kind == SoundscapeLayerKind::OneShot);
        const auto rareEvent = eventLayer || instrument.sourceVoice == VoiceId::Transitions ||
            instrument.orchestralFunction == "transition";
        const auto protagonist = instrument.id == plan.narrativeSpine.protagonistInstrumentId;
        const auto answer = instrument.lineRelationship == "call_response";
        const auto motion = instrument.sourceVoice == VoiceId::HarmonicPulse ||
            containsAny(instrument, {"arp", "sequence", "pulse", "ostinato", "orbit"});
        const auto minimumNotes = rareEvent ? std::size_t{1} : protagonist || motion
            ? std::size_t{6} : answer ? std::size_t{4} : std::size_t{3};
        const auto minimumSections = rareEvent || plan.sections.size() < 4
            ? std::size_t{1} : std::size_t{2};
        auto incomplete = noteCounts[instrument.id] < minimumNotes ||
            sections[instrument.id].size() < minimumSections;
        // The model—not the renderer—must bring the protagonist back in the coda.
        if (protagonist && !codaOwners.contains(instrument.id)) incomplete = true;
        // A response belongs to the same thematic family, but its distinct cell and
        // rhythm remain free. Labels alone cannot fabricate kinship.
        if (answer && !protagonistThemes.empty()) {
            const auto& answerThemes = themesByInstrument[instrument.id];
            const auto related = std::any_of(answerThemes.begin(), answerThemes.end(),
                [&](const auto& theme) { return protagonistThemes.contains(theme); });
            if (!related) incomplete = true;
        }
        if (incomplete)
            missing.push_back(index);
    }
    return missing;
}

} // namespace pulso
