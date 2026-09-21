#include "SelectiveRepair.h"

#include "ElectronicCompositionFabric.h"
#include "ElectronicRoleContract.h"
#include "PerformanceScore.h"
#include "TrackViability.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

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

std::size_t distinctPhraseCount(const std::vector<const NoteEvent*>& notes,
                                double beatsPerBar) {
    if (notes.empty()) return 0;
    const auto width = std::max(1.0, beatsPerBar) * 8.0;
    std::map<int, std::vector<const NoteEvent*>> windows;
    for (const auto* note : notes)
        windows[static_cast<int>(std::floor(note->startBeat / width))].push_back(note);
    std::set<std::uint64_t> fingerprints;
    for (auto& [window, phrase] : windows) {
        (void) window;
        if (phrase.size() < 3) continue;
        std::sort(phrase.begin(), phrase.end(), [](const auto* left, const auto* right) {
            return std::tie(left->startBeat, left->pitch, left->durationBeats) <
                   std::tie(right->startBeat, right->pitch, right->durationBeats);
        });
        const auto origin = phrase.front()->startBeat;
        const auto pitchOrigin = phrase.front()->pitch;
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto* note : phrase) {
            const auto onset = static_cast<std::uint64_t>(std::llround((note->startBeat - origin) * 8.0));
            const auto duration = static_cast<std::uint64_t>(std::llround(note->durationBeats * 8.0));
            const auto contour = static_cast<std::uint64_t>(std::clamp(note->pitch - pitchOrigin, -48, 48) + 48);
            hash ^= onset + contour * 131 + duration * 17;
            hash *= 1099511628211ULL;
        }
        fingerprints.insert(hash);
    }
    return std::max<std::size_t>(1, fingerprints.size());
}

} // namespace

std::string_view constraintAuthorityKey(ConstraintAuthority authority) noexcept {
    switch (authority) {
        case ConstraintAuthority::TechnicalInvariant: return "technical_invariant";
        case ConstraintAuthority::ExplicitPromptCommitment: return "explicit_prompt_commitment";
        case ConstraintAuthority::MusicalObjective: return "musical_objective";
    }
    return "musical_objective";
}

std::string_view performanceRepairOperationKey(
    PerformanceRepairOperation operation) noexcept {
    switch (operation) {
        case PerformanceRepairOperation::SupplyMissingIdentity: return "supply_missing_identity";
        case PerformanceRepairOperation::ExtendCoverage: return "extend_coverage";
        case PerformanceRepairOperation::DevelopPhrase: return "develop_phrase";
        case PerformanceRepairOperation::ResolveNarrative: return "resolve_narrative";
        case PerformanceRepairOperation::EstablishThematicRelationship:
            return "establish_thematic_relationship";
    }
    return "develop_phrase";
}

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

std::vector<PerformanceCoverageDeficit> SelectiveRepair::performanceDeficits(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates) {
    return performanceDeficitsImpl(plan, score, candidates, true);
}

std::vector<PerformanceCoverageDeficit> SelectiveRepair::marginalBarAcceptances(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates) {
    auto findings = performanceDeficitsImpl(plan, score, candidates, false);
    findings.erase(std::remove_if(findings.begin(), findings.end(), [](const auto& finding) {
        TrackViabilityContract contract;
        contract.minimumNotes = finding.minimumNotes;
        contract.minimumActiveBars = finding.minimumActiveBars;
        contract.minimumPhrases = finding.minimumPhrases;
        return finding.missingCodaResolution || finding.missingThematicRelationship ||
            !TrackViability::marginalActiveBarAcceptance(
                finding.notes, finding.activeBars, finding.phrases, contract);
    }), findings.end());
    return findings;
}

std::vector<PerformanceConstraint> SelectiveRepair::performanceConstraints(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates,
    bool explicitCastCommitment) {
    return classifyPerformanceDeficits(
        plan, performanceDeficits(plan, score, candidates), explicitCastCommitment);
}

std::vector<PerformanceConstraint> SelectiveRepair::classifyPerformanceDeficits(
    const SongPlan& plan, std::vector<PerformanceCoverageDeficit> deficits,
    bool explicitCastCommitment) {
    std::vector<PerformanceConstraint> result;
    // A cast may declare several pulse/sequence identities.  Only the motion
    // owner that actually carries notes is structurally essential; an empty
    // secondary pulse is a replaceable colour lane, not a reason to discard the
    // whole composition. This is especially important for unrequested casts,
    // where GPT can over-specify a soundscape and underwrite one identity.
    const auto populatedMotionOwner = [&](std::size_t excluded) {
        const auto alternatives = std::count_if(plan.instruments.begin(), plan.instruments.end(),
            [](const auto& instrument) { return ElectronicRoleContract::motionOwner(instrument); });
        if (alternatives > 1) return true;
        return std::any_of(deficits.begin(), deficits.end(), [&](const auto& candidate) {
            return candidate.instrumentIndex != excluded && candidate.notes > 0 &&
                candidate.instrumentIndex < plan.instruments.size() &&
                ElectronicRoleContract::motionOwner(plan.instruments[candidate.instrumentIndex]);
        });
    };
    for (auto& deficit : deficits) {
        if (plan.percussionFreeIntent && deficit.instrumentIndex < plan.instruments.size()) {
            const auto& instrument = plan.instruments[deficit.instrumentIndex];
            const auto* definition = instrumentDefinition(instrument.instrumentId);
            if ((definition != nullptr && definition->department == ScoreDepartment::Rhythm) ||
                isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Rhythm))
                continue;
        }
        PerformanceConstraint constraint;
        constraint.evidence = std::move(deficit);
        const auto motionEssential = constraint.evidence.instrumentIndex < plan.instruments.size() &&
            ElectronicRoleContract::motionOwner(plan.instruments[constraint.evidence.instrumentIndex]) &&
            !populatedMotionOwner(constraint.evidence.instrumentIndex);
        const auto essential = constraint.evidence.instrumentId ==
                plan.narrativeSpine.protagonistInstrumentId ||
            motionEssential;
        const auto missingIdentity = constraint.evidence.notes == 0;
        constraint.authority = missingIdentity && explicitCastCommitment
            ? ConstraintAuthority::ExplicitPromptCommitment
            : ConstraintAuthority::MusicalObjective;
        // A protagonist or declared motion owner with no MIDI is structurally
        // absent and remains hard. In a production-scale cast, however, the exact
        // track count is an orchestration commitment, not a promise that every
        // colour lane must receive an independent GPT cell: renderer-owned and
        // optional low/support identities may be completed or retired locally.
        // Treating every empty colour lane as blocking caused otherwise complete
        // 40-track scores to reject after bounded recovery exhausted its budget.
        const auto productionScaleCast = plan.instruments.size() >= 32;
        constraint.blocksPublication = missingIdentity &&
            (essential || (explicitCastCommitment && !productionScaleCast));
        if (missingIdentity)
            constraint.operations.push_back(
                PerformanceRepairOperation::SupplyMissingIdentity);
        if (constraint.evidence.activeBars < constraint.evidence.minimumActiveBars)
            constraint.operations.push_back(
                PerformanceRepairOperation::ExtendCoverage);
        if (constraint.evidence.notes < constraint.evidence.minimumNotes ||
            constraint.evidence.phrases < constraint.evidence.minimumPhrases)
            constraint.operations.push_back(
                PerformanceRepairOperation::DevelopPhrase);
        if (constraint.evidence.missingCodaResolution)
            constraint.operations.push_back(
                PerformanceRepairOperation::ResolveNarrative);
        if (constraint.evidence.missingThematicRelationship)
            constraint.operations.push_back(
                PerformanceRepairOperation::EstablishThematicRelationship);
        result.push_back(std::move(constraint));
    }
    return result;
}

std::vector<std::size_t> SelectiveRepair::blockingTargets(
    const std::vector<PerformanceConstraint>& constraints) {
    std::vector<std::size_t> result;
    for (const auto& constraint : constraints)
        if (constraint.blocksPublication)
            result.push_back(constraint.evidence.instrumentIndex);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::size_t> SelectiveRepair::editorialTargets(
    const std::vector<PerformanceConstraint>& constraints) {
    std::vector<std::size_t> result;
    for (const auto& constraint : constraints)
        if (!constraint.blocksPublication)
            result.push_back(constraint.evidence.instrumentIndex);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<PerformanceCoverageDeficit> SelectiveRepair::performanceDeficitsImpl(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates,
    bool allowMarginalBarAcceptance) {
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

    Pattern renderedScore;
    renderedScore.lengthBeats = plan.totalBars * plan.beatsPerBar;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        Pattern chunk;
        chunk.lengthBeats = sectionBeats;
        PerformanceScoreEngine::replaceChunk(chunk, score, static_cast<int>(sectionIndex),
                                              0.0, sectionBeats, plan.instruments);
        const auto offset = section.startBar * plan.beatsPerBar;
        for (auto note : chunk.notes) {
            note.startBeat += offset;
            renderedScore.notes.push_back(std::move(note));
        }
    }
    std::map<std::string, std::vector<const NoteEvent*>> renderedByInstrument;
    for (const auto& note : renderedScore.notes)
        if (note.partId > 0 && note.partId <= plan.instruments.size())
            renderedByInstrument[plan.instruments[note.partId - 1].id].push_back(&note);
    auto resolutionSection = plan.sections.empty() ? -1 : static_cast<int>(plan.sections.size() - 1);
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto found = std::find_if(plan.sections.begin(), plan.sections.end(),
            [&](const auto& section) { return section.name == act.sectionName; });
        if (found != plan.sections.end())
            resolutionSection = static_cast<int>(std::distance(plan.sections.begin(), found));
    }
    std::set<int> resolutionStablePitchClasses{positiveModulo(plan.rootPitchClass, 12)};
    auto explicitTonicEnding = false;
    const auto resolutionWords = lower(plan.narrativeSpine.resolution);
    explicitTonicEnding = resolutionWords.find("tonic") != std::string::npos ||
        resolutionWords.find("root") != std::string::npos ||
        resolutionWords.find("home note") != std::string::npos ||
        resolutionWords.find("tonica") != std::string::npos;
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto target = lower(act.resolutionTarget);
        if (target.find("tonic") != std::string::npos ||
            target.find("root") != std::string::npos ||
            target.find("home note") != std::string::npos ||
            target.find("tonica") != std::string::npos)
            explicitTonicEnding = true;
    }
    if (resolutionSection >= 0 &&
        static_cast<std::size_t>(resolutionSection) < plan.sections.size()) {
        const auto& section = plan.sections[static_cast<std::size_t>(resolutionSection)];
        const HarmonicEvent* terminalEvent = nullptr;
        for (const auto& event : section.harmonicEvents)
            if (terminalEvent == nullptr ||
                std::tie(event.barOffset, event.beatOffset) >
                    std::tie(terminalEvent->barOffset, terminalEvent->beatOffset))
                terminalEvent = &event;
        if (terminalEvent != nullptr) {
            const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                [&](const auto& candidate) { return candidate.id == terminalEvent->chordId; });
            if (chord != plan.chordPalette.end()) {
                resolutionStablePitchClasses.insert(positiveModulo(chord->rootPitchClass, 12));
                resolutionStablePitchClasses.insert(positiveModulo(chord->bassPitchClass, 12));
                for (const auto pitchClass : chord->pitchClasses)
                    resolutionStablePitchClasses.insert(positiveModulo(pitchClass, 12));
            }
        }
    }
    for (const auto& placement : score.placements) {
        const auto found = instrumentsByCell.find(placement.cellId);
        if (found == instrumentsByCell.end()) continue;
        for (const auto& id : found->second) sections[id].insert(placement.sectionIndex);
    }

    const auto protagonistThemes = themesByInstrument[plan.narrativeSpine.protagonistInstrumentId];

    std::vector<PerformanceCoverageDeficit> deficits;
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
        const auto motion = ElectronicRoleContract::motionOwner(instrument);
        // A shared timbral destination is realized from its content owner after the
        // complete score has been assembled. Asking GPT for the full phrase again
        // wastes tokens on a clone. The content lane itself must still be authored;
        // only its additional orchestration destinations are exempt in this phase.
        // The owner itself remains in candidates and therefore carries any missing-
        // material failure. Testing the destination before its render pass is a false
        // direct-identity failure and used to discard complete AI scores.
        if (ElectronicCompositionFabric::rendererOwnedDestination(plan, instrument)) continue;
        const auto minimumNotes = rareEvent ? std::size_t{1} : protagonist || motion
            ? std::size_t{6} : answer ? std::size_t{4} : std::size_t{3};
        const auto minimumSections = rareEvent || plan.sections.size() < 4
            ? std::size_t{1} : std::size_t{2};
        PerformanceCoverageDeficit deficit;
        deficit.instrumentIndex = index;
        deficit.instrumentId = instrument.id;
        deficit.notes = noteCounts[instrument.id];
        deficit.minimumNotes = minimumNotes;
        deficit.sections = sections[instrument.id].size();
        deficit.minimumSections = minimumSections;
        auto incomplete = false;
        if (plan.instrumentCastAuthored) {
            const auto contract = TrackViability::contractFor(instrument, plan);
            const auto& notes = renderedByInstrument[instrument.id];
            std::set<int> activeBars;
            auto phrases = notes.empty() ? std::size_t{} : std::size_t{1};
            std::vector<const NoteEvent*> ordered(notes.begin(), notes.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
                return left->startBeat < right->startBeat;
            });
            auto soundingUntil = ordered.empty() ? 0.0 : ordered.front()->endBeat();
            for (std::size_t noteIndex = 0; noteIndex < ordered.size(); ++noteIndex) {
                const auto* note = ordered[noteIndex];
                const auto first = static_cast<int>(std::floor(note->startBeat / plan.beatsPerBar));
                const auto last = static_cast<int>(std::floor(
                    std::max(note->startBeat, note->endBeat() - .001) / plan.beatsPerBar));
                for (auto bar = first; bar <= last; ++bar) activeBars.insert(bar);
                if (noteIndex > 0 && note->startBeat - soundingUntil >= plan.beatsPerBar * .75)
                    ++phrases;
                soundingUntil = std::max(soundingUntil, note->endBeat());
            }
            phrases = std::max(phrases, distinctPhraseCount(ordered, plan.beatsPerBar));
            deficit.notes = notes.size();
            deficit.minimumNotes = contract.minimumNotes;
            deficit.activeBars = activeBars.size();
            deficit.minimumActiveBars = contract.minimumActiveBars;
            deficit.phrases = phrases;
            deficit.minimumPhrases = contract.minimumPhrases;
            // An AI-authored cast uses the same function-aware contract advertised to
            // the model and used at publication. In particular, rhythm articulations
            // are judged by their dedicated rhythm pipeline, so the older generic
            // three-note/two-section heuristic must not override that contract.
            deficit.minimumSections = 0;
            incomplete = allowMarginalBarAcceptance
                ? !TrackViability::acceptsCoverage(
                    deficit.notes, deficit.activeBars, deficit.phrases, contract)
                : deficit.notes < deficit.minimumNotes ||
                    deficit.activeBars < deficit.minimumActiveBars ||
                    deficit.phrases < deficit.minimumPhrases;
        } else {
            incomplete = deficit.notes < deficit.minimumNotes ||
                deficit.sections < deficit.minimumSections;
        }
        // The model—not the renderer—must complete the protagonist's transformed
        // answer at the audible boundary. A phrase merely somewhere in the final act
        // still leaves several bars of narrative vacuum.
        if (protagonist && resolutionSection >= 0) {
            const auto& resolution = plan.sections[static_cast<std::size_t>(resolutionSection)];
            const auto resolutionEnd = (resolution.startBar + resolution.bars) * plan.beatsPerBar;
            const auto codaStart = resolutionEnd - std::min(8, resolution.bars) * plan.beatsPerBar;
            const auto finalTwoBars = resolutionEnd - std::min(2, resolution.bars) * plan.beatsPerBar;
            std::vector<const NoteEvent*> coda;
            for (const auto* note : renderedByInstrument[instrument.id])
                if (note->startBeat >= codaStart && note->startBeat < resolutionEnd)
                    coda.push_back(note);
            std::sort(coda.begin(), coda.end(), [](const auto* left, const auto* right) {
                return left->startBeat < right->startBeat;
            });
            const auto terminalPitchClass = coda.empty()
                ? -1 : positiveModulo(coda.back()->pitch, 12);
            const auto stableEnding = explicitTonicEnding
                ? terminalPitchClass == positiveModulo(plan.rootPitchClass, 12)
                : resolutionStablePitchClasses.contains(terminalPitchClass);
            const auto audibleBoundary = coda.size() >= 3 &&
                coda.back()->startBeat >= finalTwoBars && stableEnding;
            if (!audibleBoundary) {
                deficit.missingCodaResolution = true;
                incomplete = true;
            }
        }
        // A response belongs to the same thematic family, but its distinct cell and
        // rhythm remain free. Labels alone cannot fabricate kinship.
        if (answer && !protagonistThemes.empty()) {
            const auto& answerThemes = themesByInstrument[instrument.id];
            const auto related = std::any_of(answerThemes.begin(), answerThemes.end(),
                [&](const auto& theme) { return protagonistThemes.contains(theme); });
            if (!related) {
                deficit.missingThematicRelationship = true;
                incomplete = true;
            }
        }
        if (incomplete)
            deficits.push_back(std::move(deficit));
    }
    return deficits;
}

std::vector<std::size_t> SelectiveRepair::incompleteTargets(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates) {
    const auto deficits = performanceDeficits(plan, score, candidates);
    std::vector<std::size_t> missing;
    missing.reserve(deficits.size());
    for (const auto& deficit : deficits) missing.push_back(deficit.instrumentIndex);
    return missing;
}

bool SelectiveRepair::requiresReplacement(
    const PerformanceCoverageDeficit& deficit) noexcept {
    // Notes can repair missing quantity and placements can extend active coverage.
    // They cannot create a phrase break inside material that already fills its
    // required horizon; that target must be replaced so silence can be authored.
    return deficit.minimumPhrases > 0 && deficit.phrases < deficit.minimumPhrases &&
        deficit.notes >= deficit.minimumNotes &&
        deficit.activeBars >= deficit.minimumActiveBars;
}

} // namespace pulso
