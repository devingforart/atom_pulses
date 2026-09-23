#include "SelectiveRepair.h"

#include "ElectronicCompositionFabric.h"
#include "ElectronicRoleContract.h"
#include "PerformanceScore.h"
#include "TrackViability.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
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

bool independentlyAuthored(const SongPlan& plan, const InstrumentAssignment& instrument) {
    if (ElectronicCompositionFabric::rendererOwnedDestination(plan, instrument)) return false;
    const auto relationship = lower(instrument.lineRelationship);
    return relationship.find("doubling") == std::string::npos &&
        relationship.find("handoff") == std::string::npos &&
        relationship.find("relay") == std::string::npos &&
        relationship.find("reinforcement") == std::string::npos;
}

using AttackSignature = std::pair<std::int64_t, int>;

std::set<AttackSignature> attackSignatures(const std::vector<const NoteEvent*>& notes) {
    std::set<AttackSignature> result;
    for (const auto* note : notes)
        result.emplace(static_cast<std::int64_t>(std::llround(note->startBeat * 96.0)),
                       note->pitch);
    return result;
}

double eventOverlap(const std::set<AttackSignature>& a,
                    const std::set<AttackSignature>& b) {
    if (std::min(a.size(), b.size()) < 12) return 0.0;
    std::size_t intersection{};
    const auto& smaller = a.size() <= b.size() ? a : b;
    const auto& larger = a.size() <= b.size() ? b : a;
    for (const auto& event : smaller)
        if (larger.contains(event)) ++intersection;
    return static_cast<double>(intersection) /
        static_cast<double>(std::max<std::size_t>(1, smaller.size()));
}

double preservationPriority(const SongPlan& plan, std::size_t index) {
    if (index >= plan.instruments.size()) return 0.0;
    const auto& instrument = plan.instruments[index];
    auto result = instrument.prominence;
    if (instrument.id == plan.narrativeSpine.protagonistInstrumentId) result += 4.0;
    if (ElectronicRoleContract::motionOwner(instrument)) result += 2.0;
    if (containsAny(instrument, {"primary_motion_owner", "principal", "protagonist"}))
        result += 1.0;
    return result;
}

std::map<std::size_t, std::pair<std::size_t, double>> duplicatedIndependentTargets(
    const SongPlan& plan,
    const std::map<std::string, std::vector<const NoteEvent*>>& notesByInstrument,
    const std::set<std::size_t>& candidates) {
    std::map<std::size_t, std::pair<std::size_t, double>> result;
    std::map<std::string, std::set<AttackSignature>> signatures;
    for (const auto& [instrumentId, notes] : notesByInstrument)
        signatures.emplace(instrumentId, attackSignatures(notes));
    for (std::size_t left = 0; left < plan.instruments.size(); ++left) {
        const auto& a = plan.instruments[left];
        if (!independentlyAuthored(plan, a)) continue;
        const auto aNotes = signatures.find(a.id);
        if (aNotes == signatures.end()) continue;
        for (auto right = left + 1; right < plan.instruments.size(); ++right) {
            const auto& b = plan.instruments[right];
            if (!independentlyAuthored(plan, b) ||
                (!a.contentLaneId.empty() && a.contentLaneId == b.contentLaneId)) continue;
            const auto bNotes = signatures.find(b.id);
            if (bNotes == signatures.end()) continue;
            const auto overlap = eventOverlap(aNotes->second, bNotes->second);
            // A harmonic arrangement is expected to share chord tones and attacks.
            // Only near-literal melodic identity is blocking: exact pitch and onset
            // across more than four fifths of the shorter performance. Duration is
            // intentionally not part of this signature: orchestral doubling often
            // changes articulation while preserving the same audible line. Duration
            // and contour remain part of the sectional-state fingerprint below.
            if (overlap < 0.82) continue;

            const auto leftCandidate = candidates.contains(left);
            const auto rightCandidate = candidates.contains(right);
            if (!leftCandidate && !rightCandidate) continue;
            // During incremental writing, rewrite the newly inspected owner even if
            // an older block has lower prominence: accepted blocks remain immutable.
            // A global inspection may choose the less structurally important owner.
            auto target = leftCandidate != rightCandidate
                ? (leftCandidate ? left : right)
                : (preservationPriority(plan, left) <= preservationPriority(plan, right)
                       ? left : right);
            const auto counterpart = target == left ? right : left;
            const auto existing = result.find(target);
            if (existing == result.end() || overlap > existing->second.second)
                result[target] = {counterpart, overlap};
        }
    }
    return result;
}

bool requiresSectionalEvolution(const InstrumentAssignment& instrument) {
    if (containsAny(instrument, {"sub", "root", "pedal", "drone", "floor",
                                 "foundation", "gravity"})) return false;
    return instrument.sourceVoice == VoiceId::HarmonicPulse ||
        instrument.sourceVoice == VoiceId::HarmonicUpper ||
        instrument.sourceVoice == VoiceId::MovementBass ||
        containsAny(instrument, {"arp", "sequence", "pulse", "ostinato", "inner",
                                 "suspension", "counterpoint", "chord body"});
}

std::size_t minimumSectionalStates(const SongPlan& plan,
                                   const InstrumentAssignment& instrument) {
    const auto protagonist = instrument.id == plan.narrativeSpine.protagonistInstrumentId;
    const auto principalMotion = ElectronicRoleContract::motionOwner(instrument) &&
        containsAny(instrument, {"primary_motion_owner", "principal"});
    const auto activeAcrossLongForm = plan.totalBars >= 160;
    if (protagonist || principalMotion) return activeAcrossLongForm ? 6 : 5;
    if (instrument.sourceVoice == VoiceId::HarmonicPulse ||
        containsAny(instrument, {"arp", "sequence", "ostinato"}))
        return activeAcrossLongForm ? 5 : 4;
    return activeAcrossLongForm ? 4 : 3;
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
        case PerformanceRepairOperation::DevelopSectionalEvolution:
            return "develop_sectional_evolution";
        case PerformanceRepairOperation::DevelopNarrativePresence:
            return "develop_narrative_presence";
        case PerformanceRepairOperation::TransformThematicReturns:
            return "transform_thematic_returns";
        case PerformanceRepairOperation::ShapeMelodicSpeech:
            return "shape_melodic_speech";
        case PerformanceRepairOperation::SeparateIndependentLine:
            return "separate_independent_line";
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
    std::vector<std::size_t> allInstruments(plan.instruments.size());
    for (std::size_t index = 0; index < allInstruments.size(); ++index)
        allInstruments[index] = index;
    const auto performanceFindings = performanceDeficits(
        plan, plan.performanceScore, allInstruments);
    const auto hasBlockingMusicalEvidence = std::any_of(
        performanceFindings.begin(), performanceFindings.end(), [](const auto& finding) {
            return finding.missingCodaResolution || finding.duplicatedIndependentLine ||
                finding.missingSectionalEvolution || finding.missingNarrativePresence ||
                finding.missingThematicDevelopment || finding.missingMelodicSpeech;
        });
    result.needed = !publicationReady(report) || hasBlockingMusicalEvidence;
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

    for (const auto& finding : performanceFindings) {
        if (finding.duplicatedIndependentLine)
            add(finding.instrumentIndex, 14.0 + finding.duplicateEventOverlap * 4.0,
                "rewrite a cloned independent line with complementary onset grammar and contour");
        if (finding.missingSectionalEvolution)
            add(finding.instrumentIndex, 12.0,
                "replace literal long-form repetition with sectional motif development");
        if (finding.missingCodaResolution)
            add(finding.instrumentIndex, 16.0,
                "complete the protagonist coda at the audible final boundary");
        if (finding.missingNarrativePresence)
            add(finding.instrumentIndex, 15.0,
                "author the protagonist in the missing long-form phrase windows");
        if (finding.missingThematicDevelopment)
            add(finding.instrumentIndex, 14.0,
                "replace literal thematic copies with audible transformed returns");
        if (finding.missingMelodicSpeech)
            add(finding.instrumentIndex, 13.0,
                "rewrite disconnected leaps or scalar filler as singable phrase contour");
    }

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
            finding.duplicatedIndependentLine || finding.missingSectionalEvolution ||
            finding.missingNarrativePresence || finding.missingThematicDevelopment ||
            finding.missingMelodicSpeech ||
            (finding.minimumSections > 0 && finding.sections < finding.minimumSections) ||
            !TrackViability::marginalActiveBarAcceptance(
                finding.notes, finding.activeBars, finding.phrases, contract);
    }), findings.end());
    return findings;
}

EnsembleContinuityReport SelectiveRepair::ensembleContinuity(
    const SongPlan& plan, const PerformanceScore& score) {
    EnsembleContinuityReport report;
    if (plan.sections.empty() || plan.instruments.empty() || score.empty() ||
        plan.beatsPerBar <= 0.0) return report;

    Pattern rendered;
    rendered.lengthBeats = plan.totalBars * plan.beatsPerBar;
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
            rendered.notes.push_back(std::move(note));
        }
    }
    if (rendered.notes.empty()) return report;

    const auto sectionAt = [&](double beat) -> const SongSection* {
        const auto bar = static_cast<int>(std::floor(beat / plan.beatsPerBar));
        const auto found = std::find_if(plan.sections.begin(), plan.sections.end(),
            [&](const auto& section) {
                return bar >= section.startBar && bar < section.startBar + section.bars;
            });
        return found == plan.sections.end() ? nullptr : &*found;
    };
    const auto intentionalSilence = [](const SongSection& section) {
        const auto text = lower(section.function + " " + section.harmonicDirection + " " +
                                section.motifTreatment);
        return text.find("full silence") != std::string::npos ||
            text.find("complete silence") != std::string::npos ||
            text.find("silencio total") != std::string::npos;
    };

    // Measure silence only inside audible form spans. A silence explicitly authored
    // as a complete-silence section is a valid dramatic event, whereas an accidental
    // gap inside an audible section is a publication defect. Contiguous audible
    // sections are merged so a hole across their boundary cannot evade the check.
    std::vector<std::pair<double, double>> audibleSpans;
    for (const auto& section : plan.sections) {
        if (intentionalSilence(section)) continue;
        const auto start = section.startBar * plan.beatsPerBar;
        const auto end = std::min(rendered.lengthBeats,
            (section.startBar + section.bars) * plan.beatsPerBar);
        if (end <= start) continue;
        if (!audibleSpans.empty() && start <= audibleSpans.back().second + .001)
            audibleSpans.back().second = std::max(audibleSpans.back().second, end);
        else
            audibleSpans.emplace_back(start, end);
    }
    auto chronological = rendered.notes;
    std::sort(chronological.begin(), chronological.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    for (const auto& [spanStart, spanEnd] : audibleSpans) {
        auto soundingUntil = spanStart;
        for (const auto& note : chronological) {
            if (note.endBeat() <= spanStart || note.startBeat >= spanEnd) continue;
            const auto noteStart = std::max(spanStart, note.startBeat);
            report.longestGlobalSilenceBeats = std::max(
                report.longestGlobalSilenceBeats, noteStart - soundingUntil);
            soundingUntil = std::max(soundingUntil, std::min(spanEnd, note.endBeat()));
        }
        report.longestGlobalSilenceBeats = std::max(
            report.longestGlobalSilenceBeats, spanEnd - soundingUntil);
    }

    const auto windowBeats = plan.beatsPerBar * 4.0;
    auto consecutiveSilentWindows = std::size_t{};
    for (auto start = 0.0; start < rendered.lengthBeats; start += windowBeats) {
        const auto end = std::min(rendered.lengthBeats, start + windowBeats);
        const auto* section = sectionAt((start + end) * .5);
        if (section != nullptr && intentionalSilence(*section)) {
            ++report.intentionalBreathWindows;
            continue;
        }
        ++report.evaluatedWindows;
        std::set<std::uint16_t> audibleOwners;
        std::set<std::uint16_t> nonRhythmOwners;
        std::set<std::uint16_t> harmonicOwners;
        for (const auto& note : rendered.notes) {
            if (note.startBeat >= end || note.endBeat() <= start) continue;
            if (note.partId != 0) audibleOwners.insert(note.partId);
            if (note.partId == 0 || note.partId > plan.instruments.size()) continue;
            const auto& instrument = plan.instruments[note.partId - 1];
            if (!isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Rhythm) &&
                instrument.sourceVoice != VoiceId::Transitions)
                nonRhythmOwners.insert(note.partId);
            if (isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Harmony) ||
                instrument.sourceVoice == VoiceId::Atmosphere)
                harmonicOwners.insert(note.partId);
        }
        if (audibleOwners.empty()) {
            ++report.silentWindows;
            ++consecutiveSilentWindows;
            report.maximumConsecutiveSilentWindows = std::max(
                report.maximumConsecutiveSilentWindows, consecutiveSilentWindows);
        } else {
            consecutiveSilentWindows = 0;
        }
        const auto density = section == nullptr ? .5 : section->density;
        const auto minimumOwners = density >= .68 ? std::size_t{3} :
            density >= .30 ? std::size_t{2} : std::size_t{1};
        if (nonRhythmOwners.size() < minimumOwners) ++report.underfilledWindows;
        if (harmonicOwners.size() >= 2) ++report.twoLayerHarmonicWindows;
    }

    const auto denominator = static_cast<double>(std::max<std::size_t>(1, report.evaluatedWindows));
    report.audibleCoverage = 1.0 - static_cast<double>(report.underfilledWindows) / denominator;
    report.harmonicFloorCoverage =
        static_cast<double>(report.twoLayerHarmonicWindows) / denominator;
    const auto electronic = plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    const auto floorTarget = plan.percussionFreeIntent && electronic ? .70 : electronic ? .58 : .45;
    // One isolated four-bar negative-space window is valid long-form phrasing. What
    // must fail is repeated/adjacent silence or a material percentage of the form.
    // The half-bar margin accounts for release-to-attack phrasing around the window.
    const auto silentWindowBudget = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(report.evaluatedWindows * .04)));
    const auto longestAllowed = plan.beatsPerBar * (electronic ? 4.5 : 6.0);
    report.ready = report.evaluatedWindows > 0 &&
        report.silentWindows <= silentWindowBudget &&
        report.maximumConsecutiveSilentWindows <= 1 &&
        report.audibleCoverage >= .82 && report.harmonicFloorCoverage >= floorTarget &&
        report.longestGlobalSilenceBeats <= longestAllowed + .001;
    return report;
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
    (void) explicitCastCommitment;
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
        const auto identityExplicit = constraint.evidence.instrumentIndex < plan.instruments.size() &&
            plan.instruments[constraint.evidence.instrumentIndex].explicitPromptIdentity;
        constraint.authority = missingIdentity && identityExplicit
            ? ConstraintAuthority::ExplicitPromptCommitment
            : ConstraintAuthority::MusicalObjective;
        // Count targets govern the size of the requested architecture, not the names
        // GPT invents to satisfy it. Only a concrete user-named identity, the
        // protagonist, or the sole motion owner can make an empty lane block the song.
        constraint.blocksPublication =
            (missingIdentity && (essential || identityExplicit)) ||
            constraint.evidence.missingCodaResolution ||
            constraint.evidence.duplicatedIndependentLine;
        if (missingIdentity)
            constraint.operations.push_back(
                PerformanceRepairOperation::SupplyMissingIdentity);
        if (constraint.evidence.activeBars < constraint.evidence.minimumActiveBars ||
            constraint.evidence.sections < constraint.evidence.minimumSections)
            constraint.operations.push_back(
                PerformanceRepairOperation::ExtendCoverage);
        if (constraint.evidence.notes < constraint.evidence.minimumNotes ||
            constraint.evidence.phrases < constraint.evidence.minimumPhrases)
            constraint.operations.push_back(
                PerformanceRepairOperation::DevelopPhrase);
        if (constraint.evidence.missingSectionalEvolution)
            constraint.operations.push_back(
                PerformanceRepairOperation::DevelopSectionalEvolution);
        if (constraint.evidence.missingNarrativePresence)
            constraint.operations.push_back(
                PerformanceRepairOperation::DevelopNarrativePresence);
        if (constraint.evidence.missingThematicDevelopment)
            constraint.operations.push_back(
                PerformanceRepairOperation::TransformThematicReturns);
        if (constraint.evidence.missingMelodicSpeech)
            constraint.operations.push_back(
                PerformanceRepairOperation::ShapeMelodicSpeech);
        if (constraint.evidence.duplicatedIndependentLine)
            constraint.operations.push_back(
                PerformanceRepairOperation::SeparateIndependentLine);
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

std::vector<std::size_t> SelectiveRepair::consolidatableDuplicateTargets(
    const SongPlan& plan, const std::vector<PerformanceConstraint>& constraints,
    bool explicitCastCommitment) {
    (void) explicitCastCommitment;

    const auto motionOwners = static_cast<std::size_t>(std::count_if(
        plan.instruments.begin(), plan.instruments.end(),
        [](const auto& instrument) { return ElectronicRoleContract::motionOwner(instrument); }));
    const auto bassOwners = static_cast<std::size_t>(std::count_if(
        plan.instruments.begin(), plan.instruments.end(), [](const auto& instrument) {
            return isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Bass);
        }));

    std::set<std::string> selectedIds;
    std::vector<std::size_t> selected;
    for (const auto& constraint : constraints) {
        const auto& evidence = constraint.evidence;
        if (!evidence.duplicatedIndependentLine || evidence.notes == 0 ||
            evidence.instrumentIndex >= plan.instruments.size() ||
            evidence.duplicatedWithInstrumentId.empty()) continue;
        const auto& instrument = plan.instruments[evidence.instrumentIndex];
        const auto counterpart = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& candidate) {
                return candidate.id == evidence.duplicatedWithInstrumentId;
            });
        if (counterpart == plan.instruments.end() ||
            selectedIds.contains(counterpart->id) ||
            instrument.id == plan.narrativeSpine.protagonistInstrumentId ||
            instrument.explicitPromptIdentity) continue;
        const auto soleMotionOwner = ElectronicRoleContract::motionOwner(instrument) &&
            ElectronicRoleContract::requiresMotionOwner(plan) && motionOwners <= 1;
        const auto soleBassOwner = isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Bass) &&
            bassOwners <= 1;
        if (soleMotionOwner || soleBassOwner) continue;
        selectedIds.insert(instrument.id);
        selected.push_back(evidence.instrumentIndex);
    }
    return selected;
}

std::vector<PerformanceCoverageDeficit> SelectiveRepair::performanceDeficitsImpl(
    const SongPlan& plan, const PerformanceScore& score,
    const std::vector<std::size_t>& candidates,
    bool allowMarginalBarAcceptance) {
    std::map<std::string, std::size_t> noteCounts;
    std::map<std::string, std::set<int>> sections;
    std::map<std::string, std::set<std::string>> instrumentsByCell;
    std::map<std::string, std::set<std::string>> themesByInstrument;
    std::map<std::string, std::map<std::string, std::size_t>> placementStates;
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
    const std::set<std::size_t> candidateSet(candidates.begin(), candidates.end());
    const auto duplicatedTargets = duplicatedIndependentTargets(
        plan, renderedByInstrument, candidateSet);
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
        const auto state = placement.cellId + "|t=" + std::to_string(placement.timeScale) +
            "|r=" + std::to_string(placement.retrograde) +
            "|i=" + std::to_string(placement.invertContour) +
            "|fs=" + std::to_string(placement.fragmentStart) +
            "|fe=" + std::to_string(placement.fragmentEnd);
        for (const auto& id : found->second) {
            sections[id].insert(placement.sectionIndex);
            placementStates[id][state] += static_cast<std::size_t>(std::max(1, placement.repeats));
        }
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
        if (const auto duplicate = duplicatedTargets.find(index);
            duplicate != duplicatedTargets.end()) {
            deficit.duplicatedIndependentLine = true;
            deficit.duplicateEventOverlap = duplicate->second.second;
            if (duplicate->second.first < plan.instruments.size())
                deficit.duplicatedWithInstrumentId =
                    plan.instruments[duplicate->second.first].id;
        }
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
            if (protagonist) {
                for (const auto& section : plan.sections) {
                    const auto active = instrument.activeSections.empty() ||
                        std::find(instrument.activeSections.begin(), instrument.activeSections.end(),
                                  section.name) != instrument.activeSections.end();
                    if (!active) continue;
                    for (auto localBar = 0; localBar < section.bars; localBar += 8) {
                        ++deficit.minimumNarrativePhraseWindows;
                        const auto start = (section.startBar + localBar) * plan.beatsPerBar;
                        const auto end = std::min(
                            (section.startBar + section.bars) * plan.beatsPerBar,
                            start + plan.beatsPerBar * 8.0);
                        const auto attacks = std::count_if(notes.begin(), notes.end(),
                            [&](const auto* note) {
                                return note->startBeat >= start && note->startBeat < end;
                            });
                        if (attacks >= 3) ++deficit.narrativePhraseWindows;
                    }
                }
                // A protagonist is a dramatic speaker, not an always-on density
                // layer. Forty percent of eligible long-form windows is enough when
                // section coverage, thematic development and the coda are validated
                // independently. This preserves anticipation and negative space.
                deficit.minimumNarrativePhraseWindows = std::min(
                    deficit.minimumNarrativePhraseWindows,
                    std::max<std::size_t>(3, static_cast<std::size_t>(std::ceil(
                        static_cast<double>(deficit.minimumNarrativePhraseWindows) * .40))));
                if (deficit.narrativePhraseWindows < deficit.minimumNarrativePhraseWindows) {
                    deficit.missingNarrativePresence = true;
                    incomplete = true;
                }
            }

            if ((protagonist || answer || motion) && placementStates.contains(instrument.id)) {
                const auto& states = placementStates.at(instrument.id);
                const auto total = std::accumulate(states.begin(), states.end(), std::size_t{},
                    [](auto sum, const auto& item) { return sum + item.second; });
                const auto dominant = std::accumulate(states.begin(), states.end(), std::size_t{},
                    [](auto largest, const auto& item) { return std::max(largest, item.second); });
                deficit.literalPlacementRatio = static_cast<double>(dominant) /
                    static_cast<double>(std::max<std::size_t>(1, total));
                if (total >= 4 && (states.size() < 2 || deficit.literalPlacementRatio > .70)) {
                    deficit.missingThematicDevelopment = true;
                    incomplete = true;
                }
            }
            if ((protagonist || answer) && ordered.size() >= 9) {
                for (std::size_t noteIndex = 1; noteIndex < ordered.size(); ++noteIndex) {
                    const auto* previous = ordered[noteIndex - 1];
                    const auto* current = ordered[noteIndex];
                    if (current->startBeat - previous->startBeat > plan.beatsPerBar * .75)
                        continue;
                    ++deficit.melodicIntervals;
                    const auto distance = std::abs(current->pitch - previous->pitch);
                    if (distance >= 1 && distance <= 2) deficit.melodicStepRatio += 1.0;
                }
                if (deficit.melodicIntervals > 0)
                    deficit.melodicStepRatio /= static_cast<double>(deficit.melodicIntervals);
                if (deficit.melodicIntervals >= 8 &&
                    (deficit.melodicStepRatio < .15 || deficit.melodicStepRatio > .75)) {
                    deficit.missingMelodicSpeech = true;
                    incomplete = true;
                }
            }
            // Long-form orchestration is sectional, not a global note quota. An
            // instrument assigned to several scenes must develop its responsibility
            // in more than one of them; otherwise twenty declared tracks can collapse
            // into six audible lanes while still passing raw note/bar minimums.
            auto intendedSections = instrument.activeSections.empty()
                ? plan.sections.size() : std::size_t{};
            if (!instrument.activeSections.empty()) {
                for (const auto& section : plan.sections)
                    if (std::find(instrument.activeSections.begin(), instrument.activeSections.end(),
                                  section.name) != instrument.activeSections.end())
                        ++intendedSections;
            }
            deficit.minimumSections = rareEvent ? std::size_t{1} :
                std::min(intendedSections,
                    plan.totalBars >= 96 ? std::size_t{3} : std::size_t{2});
            const auto coverageIncomplete = allowMarginalBarAcceptance
                ? !TrackViability::acceptsCoverage(
                    deficit.notes, deficit.activeBars, deficit.phrases, contract)
                : deficit.notes < deficit.minimumNotes ||
                    deficit.activeBars < deficit.minimumActiveBars ||
                    deficit.phrases < deficit.minimumPhrases;
            incomplete = incomplete || coverageIncomplete ||
                deficit.sections < deficit.minimumSections;

            // Persistent motion, inner voices and chordal bodies must change their
            // onset/interval/duration grammar across the form. Harmonic transposition
            // alone is deliberately normalized away by distinctBarPatternCount.
            if (!rareEvent && requiresSectionalEvolution(instrument) &&
                activeBars.size() >= std::max<std::size_t>(32, plan.totalBars / 3) &&
                deficit.sections >= 3) {
                // Eight-bar phrase fingerprints normalize onset origin and pitch
                // origin, so literal transposition does not masquerade as development.
                // The requirement remains musical and bounded: 3-6 states across
                // the whole form, never a linear quota per active bar.
                deficit.sectionalStates = distinctPhraseCount(ordered, plan.beatsPerBar);
                deficit.minimumSectionalStates = minimumSectionalStates(plan, instrument);
                if (deficit.sectionalStates < deficit.minimumSectionalStates) {
                    deficit.missingSectionalEvolution = true;
                    incomplete = true;
                }
            }
        } else {
            incomplete = deficit.notes < deficit.minimumNotes ||
                deficit.sections < deficit.minimumSections;
        }
        if (deficit.duplicatedIndependentLine) incomplete = true;
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

bool SelectiveRepair::ensureAuthoredProtagonistCoda(
    const SongPlan& plan, PerformanceScore& score, std::string_view preferredCellId) {
    const auto& protagonistId = plan.narrativeSpine.protagonistInstrumentId;
    if (protagonistId.empty() || plan.sections.empty()) return false;

    auto resolutionSection = static_cast<int>(plan.sections.size() - 1);
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto found = std::find_if(plan.sections.begin(), plan.sections.end(),
            [&](const auto& section) { return section.name == act.sectionName; });
        if (found != plan.sections.end())
            resolutionSection = static_cast<int>(std::distance(plan.sections.begin(), found));
    }
    const auto& resolution = plan.sections[static_cast<std::size_t>(resolutionSection)];
    const auto sectionLength = resolution.bars * plan.beatsPerBar;
    if (sectionLength <= 0.0) return false;

    const PerformanceCell* source = nullptr;
    std::size_t bestNotes = 0;
    for (const auto& cell : score.cells) {
        const auto count = static_cast<std::size_t>(std::count_if(
            cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                return note.instrumentId == protagonistId;
            }));
        if (count < 3) continue;
        const auto preferred = !preferredCellId.empty() && cell.id == preferredCellId;
        const auto currentPreferred = source != nullptr &&
            !preferredCellId.empty() && source->id == preferredCellId;
        if (source == nullptr || (preferred && !currentPreferred) ||
            (preferred == currentPreferred && count > bestNotes)) {
            source = &cell;
            bestNotes = count;
        }
    }
    if (source == nullptr) return false;

    std::vector<const AuthoredNote*> authored;
    for (const auto& note : source->notes)
        if (note.instrumentId == protagonistId) authored.push_back(&note);
    std::sort(authored.begin(), authored.end(), [](const auto* left, const auto* right) {
        return std::tie(left->beat, left->pitch) < std::tie(right->beat, right->pitch);
    });
    if (authored.size() < 3) return false;

    // Build a bounded transformed return from the final three authored attacks.
    // Rebasing an existing fragment is necessary when its source cell is longer
    // than the resolution section; a placement of the full cell would be clipped
    // before its intended final attack and would falsely report success.
    const auto fragmentCount = std::min<std::size_t>(3, authored.size());
    const auto fragmentBegin = authored.size() - fragmentCount;
    const auto firstBeat = authored[fragmentBegin]->beat;
    const auto lastBeat = authored.back()->beat - firstBeat;
    const auto terminalTarget = std::max(0.0, sectionLength -
        std::max(0.5, plan.beatsPerBar * 0.5));
    const auto codaWindowStart = std::max(0.0, sectionLength -
        std::min(8, resolution.bars) * plan.beatsPerBar);
    auto timeScale = 1.0;
    if (lastBeat > terminalTarget - codaWindowStart) timeScale = .5;
    if (lastBeat * timeScale > terminalTarget - codaWindowStart + .001)
        return false;
    auto transpose = positiveModulo(plan.rootPitchClass - authored.back()->pitch, 12);
    if (transpose > 6) transpose -= 12;

    PerformanceCell codaCell;
    codaCell.id = "authored_coda_" + source->id;
    codaCell.themeId = source->themeId;
    codaCell.narrativeFunction = "transformed_resolution";
    codaCell.ownedVoices = source->ownedVoices;
    for (auto index = fragmentBegin; index < authored.size(); ++index) {
        auto note = *authored[index];
        note.beat -= firstBeat;
        codaCell.notes.push_back(std::move(note));
    }
    codaCell.lengthBeats = std::max(.25,
        codaCell.notes.back().beat + codaCell.notes.back().durationBeats);

    // Replace a previous tentative coda atomically, then verify the exact same
    // rendered contract used by publication. Success may only be reported after
    // the independent audit observes a stable terminal attack.
    const auto originalScore = score;
    score.placements.erase(std::remove_if(score.placements.begin(), score.placements.end(),
        [&](const auto& placement) { return placement.cellId == codaCell.id; }),
        score.placements.end());
    score.cells.erase(std::remove_if(score.cells.begin(), score.cells.end(),
        [&](const auto& cell) { return cell.id == codaCell.id; }), score.cells.end());
    score.cells.push_back(std::move(codaCell));

    PerformancePlacement coda;
    coda.cellId = score.cells.back().id;
    coda.sectionIndex = resolutionSection;
    coda.startBeat = terminalTarget - lastBeat * timeScale;
    coda.repeats = 1;
    coda.transpose = transpose;
    coda.velocityScale = .88;
    coda.timeScale = timeScale;
    coda.purpose = "authored protagonist coda";
    coda.fragmentStart = 0.0;
    coda.fragmentEnd = score.cells.back().lengthBeats;
    score.placements.push_back(std::move(coda));

    const auto protagonist = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& instrument) { return instrument.id == protagonistId; });
    if (protagonist == plan.instruments.end()) {
        score = originalScore;
        return false;
    }
    const auto protagonistIndex = static_cast<std::size_t>(
        std::distance(plan.instruments.begin(), protagonist));
    const auto verification = performanceDeficits(plan, score, {protagonistIndex});
    const auto unresolved = std::find_if(verification.begin(), verification.end(),
        [&](const auto& finding) {
            return finding.instrumentId == protagonistId && finding.missingCodaResolution;
        });
    if (unresolved != verification.end()) {
        score = originalScore;
        return false;
    }
    return true;
}

bool SelectiveRepair::requiresReplacement(
    const PerformanceCoverageDeficit& deficit) noexcept {
    // Notes can repair missing quantity and placements can extend active coverage.
    // They cannot create a phrase break inside material that already fills its
    // required horizon; that target must be replaced so silence can be authored.
    if (deficit.duplicatedIndependentLine || deficit.missingSectionalEvolution ||
        deficit.missingThematicDevelopment || deficit.missingMelodicSpeech) return true;
    return deficit.minimumPhrases > 0 && deficit.phrases < deficit.minimumPhrases &&
        deficit.notes >= deficit.minimumNotes &&
        deficit.activeBars >= deficit.minimumActiveBars;
}

} // namespace pulso
