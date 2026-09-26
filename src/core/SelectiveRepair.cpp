#include "SelectiveRepair.h"

#include "ElectronicCompositionFabric.h"
#include "ElectronicRoleContract.h"
#include "PerformanceScore.h"
#include "Scale.h"
#include "TrackViability.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
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

std::optional<std::size_t> centralChordBedIndex(const SongPlan& plan) {
    std::optional<std::size_t> selected;
    auto best = -1.0;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& instrument = plan.instruments[index];
        if (instrument.sourceVoice != VoiceId::HarmonicFoundation) continue;
        auto score = instrument.prominence * 2.0 + instrument.activity;
        if (containsAny(instrument, {"primary_chord_bed"})) score += 100.0;
        if (containsAny(instrument, {"chord", "acorde", "pad", "colchon", "bed", "body"}))
            score += 8.0;
        const auto* definition = instrumentDefinition(instrument.instrumentId);
        if (definition != nullptr && definition->polyphonic) score += 4.0;
        if (!selected || score > best) {
            selected = index;
            best = score;
        }
    }
    return selected;
}

int pitchForClass(int pitchClass, int target, int minimum, int maximum) noexcept {
    auto best = std::clamp(target, minimum, maximum);
    auto distance = 1000;
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto candidate = std::abs(pitch - target);
        if (candidate < distance) {
            best = pitch;
            distance = candidate;
        }
    }
    return best;
}

void retainInstrument(PerformanceCell& cell, const std::string& instrumentId) {
    cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
        return note.instrumentId != instrumentId;
    }), cell.notes.end());
    cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
        return control.instrumentId != instrumentId;
    }), cell.controls.end());
    std::set<VoiceId> voices;
    for (const auto& note : cell.notes) voices.insert(note.voice);
    for (const auto& control : cell.controls) voices.insert(control.voice);
    cell.ownedVoices.assign(voices.begin(), voices.end());
}

void removeInstrument(PerformanceCell& cell, const std::string& instrumentId) {
    cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
        return note.instrumentId == instrumentId;
    }), cell.notes.end());
    cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
        return control.instrumentId == instrumentId;
    }), cell.controls.end());
    std::set<VoiceId> voices;
    for (const auto& note : cell.notes) voices.insert(note.voice);
    for (const auto& control : cell.controls) voices.insert(control.voice);
    cell.ownedVoices.assign(voices.begin(), voices.end());
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
        case PerformanceRepairOperation::AuthorCentralChordBed:
            return "author_central_chord_bed";
        case PerformanceRepairOperation::ShapeHarmonicBreath:
            return "shape_harmonic_breath";
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
    // A weak ending or an incomplete narrative spine is an editorial defect when the
    // complete score is otherwise coherent.  Treat it as terminal only when the
    // broader narrative evidence is also below the minimum usable floor.  The old
    // predicate discarded technically healthy, fully authored songs solely because
    // their final cadence was not conclusive enough; a failed optional rewrite then
    // left the user with no composition at all.
    const auto brokenNarrative = report.narrative.active && !report.narrative.creativeReady &&
        (report.narrative.score < 0.68 ||
         (!report.narrative.narrativeSpineReady && report.narrative.score < 0.74 &&
          report.narrative.resolutionScore < 0.25));
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
    std::vector<double> densityWindows;
    if (plan.beatsPerBar > 0.0 && pattern.lengthBeats > 0.0) {
        const auto window = plan.beatsPerBar * 4.0;
        for (auto start = 0.0; start < pattern.lengthBeats; start += window) {
            auto sampledTotal = 0.0;
            auto samples = 0;
            for (auto beat = start + plan.beatsPerBar * .5;
                 beat < std::min(pattern.lengthBeats, start + window);
                 beat += plan.beatsPerBar) {
                std::set<std::uint16_t> owners;
                for (const auto& note : pattern.notes)
                    if (note.partId != 0 && note.startBeat <= beat && note.endBeat() > beat)
                        owners.insert(note.partId);
                sampledTotal += static_cast<double>(owners.size());
                ++samples;
            }
            if (samples > 0) densityWindows.push_back(sampledTotal / samples);
        }
    }
    const auto densityRange = densityWindows.empty() ? 0.0 :
        *std::max_element(densityWindows.begin(), densityWindows.end()) -
        *std::min_element(densityWindows.begin(), densityWindows.end());
    const auto flatLongFormDensity = densityWindows.size() >= 8 && densityRange < 1.75;
    std::vector<std::size_t> allInstruments(plan.instruments.size());
    for (std::size_t index = 0; index < allInstruments.size(); ++index)
        allInstruments[index] = index;
    const auto performanceFindings = performanceDeficits(
        plan, plan.performanceScore, allInstruments);
    const auto hasBlockingMusicalEvidence = std::any_of(
        performanceFindings.begin(), performanceFindings.end(), [](const auto& finding) {
            return finding.missingCodaResolution || finding.duplicatedIndependentLine ||
                finding.missingSectionalEvolution || finding.missingNarrativePresence ||
                finding.missingThematicDevelopment || finding.missingMelodicSpeech ||
                finding.missingCentralChordBed || finding.missingChordBedBreath ||
                finding.missingChordBedNarrativeArc;
        });
    result.needed = !publicationReady(report) || hasBlockingMusicalEvidence ||
        flatLongFormDensity;
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
        if (finding.missingCentralChordBed)
            add(finding.instrumentIndex, 18.0,
                "author one unmistakable polyphonic central chord bed on this single MIDI lane");
        if (finding.missingChordBedBreath)
            add(finding.instrumentIndex, 11.0,
                "shape sectional withdrawal and re-entry in the central chord bed without removing harmonic continuity");
        if (finding.missingChordBedNarrativeArc)
            add(finding.instrumentIndex, 17.0,
                "extend the central chord bed through premise, development, climax and the audible final stage");
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
    std::map<std::uint16_t, double> soundingDurationByPart;
    for (const auto& note : pattern.notes)
        if (note.partId != 0) {
            ++renderedByPart[note.partId];
            soundingDurationByPart[note.partId] += note.durationBeats;
        }
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

    // Route critic findings to their actual musical owners.  Generic prominence
    // ranking used to send groove and cadence problems to pads, flute and cello,
    // making a costly repair incapable of addressing the reported defect.
    const auto grooveNeedsRepair = hasNarrativeIssue("groove_structure_not_ai_authored") ||
        hasNarrativeIssue("club_pulse_absent_too_long") ||
        hasNarrativeIssue("undeveloped_rhythm_narrative");
    if (grooveNeedsRepair) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            if (!isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Rhythm)) continue;
            const auto structuralOwner = instrument.sourceVoice == VoiceId::CoreDrums ||
                containsAny(instrument, {"kick", "structural_grid", "pulse owner"});
            add(index, structuralOwner ? 22.0 : 14.0,
                "repair AI-authored groove continuity and sectional rhythmic development");
        }
    }
    if (hasNarrativeIssue("fragmented_movement_bass") ||
        hasNarrativeIssue("low_end_narrative_absent_too_long")) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            if (!isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Bass)) continue;
            const auto movementOwner = instrument.sourceVoice == VoiceId::MovementBass ||
                containsAny(instrument, {"movement", "propulsion", "moving bass"});
            add(index, movementOwner ? 22.0 : 13.0,
                "repair the moving-bass phrase arc and low-end continuity");
        }
    }
    const auto endingNeedsRepair = hasNarrativeIssue("ending_does_not_repay_harmonic_debt") ||
        report.narrative.resolutionScore < 0.48 || !report.narrative.narrativeSpineReady;
    if (endingNeedsRepair) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            const auto protagonist = instrument.id == plan.narrativeSpine.protagonistInstrumentId ||
                instrument.sourceVoice == VoiceId::Lead ||
                containsAny(instrument, {"protagonist", "principal speaker"});
            const auto harmonicOwner = instrument.sourceVoice == VoiceId::HarmonicFoundation ||
                containsAny(instrument, {"primary_chord_bed", "chord bed", "harmonic floor"});
            if (protagonist)
                add(index, 24.0,
                    "resolve the protagonist at the final boundary and repay thematic debt");
            else if (harmonicOwner)
                add(index, 21.0,
                    "author an audible final harmonic cadence without changing the blueprint");
        }
    }
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
    if (flatLongFormDensity) {
        std::vector<std::pair<std::size_t, double>> sustainedHarmony;
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& instrument = plan.instruments[index];
            if (eventInstrument(plan, instrument) ||
                !(isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Harmony) ||
                  instrument.sourceVoice == VoiceId::Atmosphere)) continue;
            sustainedHarmony.emplace_back(index,
                soundingDurationByPart[static_cast<std::uint16_t>(index + 1)]);
        }
        std::stable_sort(sustainedHarmony.begin(), sustainedHarmony.end(),
            [](const auto& left, const auto& right) { return left.second > right.second; });
        for (std::size_t ordinal = 0;
             ordinal < std::min<std::size_t>(2, sustainedHarmony.size()); ++ordinal)
            add(sustainedHarmony[ordinal].first, 10.0 - ordinal,
                "create a perceptible AI-authored density curve through withdrawal, accumulation and consequential return");
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
            finding.missingMelodicSpeech || finding.missingCentralChordBed ||
            finding.missingChordBedBreath || finding.missingChordBedNarrativeArc ||
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
        const auto chordBed = centralChordBedIndex(plan);
        const auto localizedChordClosure = constraint.evidence.missingCodaResolution &&
            chordBed && *chordBed == constraint.evidence.instrumentIndex &&
            constraint.evidence.notes > 0;
        constraint.blocksPublication =
            (missingIdentity && (essential || identityExplicit)) ||
            (constraint.evidence.missingCodaResolution && !localizedChordClosure) ||
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
        if (constraint.evidence.missingCentralChordBed)
            constraint.operations.push_back(
                PerformanceRepairOperation::AuthorCentralChordBed);
        if (constraint.evidence.missingChordBedNarrativeArc)
            constraint.operations.push_back(
                PerformanceRepairOperation::ExtendCoverage);
        if (constraint.evidence.missingChordBedBreath)
            constraint.operations.push_back(
                PerformanceRepairOperation::ShapeHarmonicBreath);
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
    std::set<int> resolutionStablePitchClasses{positiveModulo(plan.rootPitchClass, 12)};
    auto explicitTonicEnding = false;
    auto intentionalOpenEnding = false;
    const auto resolutionWords = lower(plan.narrativeSpine.resolution);
    explicitTonicEnding = resolutionWords.find("tonic") != std::string::npos ||
        resolutionWords.find("root") != std::string::npos ||
        resolutionWords.find("home note") != std::string::npos ||
        resolutionWords.find("tonica") != std::string::npos;
    intentionalOpenEnding = resolutionWords.find("open") != std::string::npos ||
        resolutionWords.find("suspend") != std::string::npos ||
        resolutionWords.find("modal") != std::string::npos ||
        resolutionWords.find("abiert") != std::string::npos ||
        resolutionWords.find("suspendid") != std::string::npos;
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto target = lower(act.resolutionTarget);
        if (target.find("tonic") != std::string::npos ||
            target.find("root") != std::string::npos ||
            target.find("home note") != std::string::npos ||
            target.find("tonica") != std::string::npos)
            explicitTonicEnding = true;
        if (target.find("open") != std::string::npos ||
            target.find("suspend") != std::string::npos ||
            target.find("modal") != std::string::npos ||
            target.find("abiert") != std::string::npos ||
            target.find("suspendid") != std::string::npos)
            intentionalOpenEnding = true;
    }
    const HarmonicChord* terminalResolutionChord = nullptr;
    // Closure is audited against what the listener actually hears last. A declared
    // Resolution act may be followed by an aftermath; validating against the earlier
    // act allowed the protagonist to disappear before the absolute song ending.
    if (!plan.sections.empty()) {
        const auto& section = plan.sections.back();
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
                terminalResolutionChord = &*chord;
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
    const auto centralChordBed = centralChordBedIndex(plan);

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
            if (centralChordBed && *centralChordBed == index) {
                std::map<std::int64_t, std::set<int>> pitchesByAttack;
                for (const auto* note : ordered) {
                    const auto attack = static_cast<std::int64_t>(
                        std::llround(note->startBeat * 96.0));
                    pitchesByAttack[attack].insert(note->pitch);
                }
                deficit.polyphonicChordAttacks = static_cast<std::size_t>(std::count_if(
                    pitchesByAttack.begin(), pitchesByAttack.end(),
                    [](const auto& item) { return item.second.size() >= 3; }));
                deficit.minimumPolyphonicChordAttacks = std::clamp<std::size_t>(
                    plan.sections.size() * 2, 4, 12);
                if (deficit.polyphonicChordAttacks < deficit.minimumPolyphonicChordAttacks) {
                    deficit.missingCentralChordBed = true;
                    incomplete = true;
                }

                std::set<int> essentialStages;
                if (!plan.sections.empty()) {
                    essentialStages.insert(0);
                    essentialStages.insert(static_cast<int>(plan.sections.size() / 2));
                    essentialStages.insert(static_cast<int>(plan.sections.size() - 1));
                    for (const auto& act : plan.narrativeSpine.acts) {
                        if (act.stage != NarrativeStage::Transformation &&
                            act.stage != NarrativeStage::Climax &&
                            act.stage != NarrativeStage::Resolution) continue;
                        const auto found = std::find_if(plan.sections.begin(), plan.sections.end(),
                            [&](const auto& section) {
                                return section.name == act.sectionName;
                            });
                        if (found != plan.sections.end())
                            essentialStages.insert(static_cast<int>(
                                std::distance(plan.sections.begin(), found)));
                    }
                }
                deficit.minimumChordBedNarrativeStages = essentialStages.size();
                for (const auto stage : essentialStages)
                    if (sections[instrument.id].contains(stage))
                        ++deficit.chordBedNarrativeStages;
                if (deficit.chordBedNarrativeStages <
                    deficit.minimumChordBedNarrativeStages) {
                    deficit.missingChordBedNarrativeArc = true;
                    incomplete = true;
                }

                auto longestBreath = std::size_t{};
                auto currentBreath = std::size_t{};
                for (auto bar = 0; bar < std::max(1, plan.totalBars); ++bar) {
                    if (activeBars.contains(bar)) {
                        currentBreath = 0;
                    } else {
                        longestBreath = std::max(longestBreath, ++currentBreath);
                    }
                }
                deficit.longestChordBedBreathBars = longestBreath;
                const auto deliberatelyContinuous = containsAny(
                    instrument, {"continuous", "continuo", "constant", "drone"});
                if (plan.totalBars >= 64 && !deliberatelyContinuous && longestBreath < 2) {
                    deficit.missingChordBedBreath = true;
                    incomplete = true;
                }
                const auto tonicClosure = terminalResolutionChord != nullptr &&
                    (terminalResolutionChord->function == HarmonicFunction::Tonic ||
                     positiveModulo(terminalResolutionChord->rootPitchClass, 12) ==
                         positiveModulo(plan.rootPitchClass, 12));
                const auto stableOpenClosure = intentionalOpenEnding &&
                    terminalResolutionChord != nullptr &&
                    (terminalResolutionChord->function == HarmonicFunction::Modal ||
                     terminalResolutionChord->function == HarmonicFunction::Pedal ||
                     terminalResolutionChord->tension <= .45);
                // A long-form central bed must carry the harmonic argument to a
                // perceptible destination. Ending on an arbitrary loop chord is not
                // resolution; an open ending must be declared and genuinely stable.
                if (plan.totalBars >= 64 && terminalResolutionChord != nullptr &&
                    !tonicClosure && !stableOpenClosure) {
                    deficit.missingCodaResolution = true;
                    incomplete = true;
                }
            }
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
                        std::vector<const NoteEvent*> windowNotes;
                        for (const auto* note : notes)
                            if (note->startBeat >= start && note->startBeat < end)
                                windowNotes.push_back(note);
                        std::sort(windowNotes.begin(), windowNotes.end(),
                            [](const auto* left, const auto* right) {
                                if (left->startBeat != right->startBeat)
                                    return left->startBeat < right->startBeat;
                                return left->pitch < right->pitch;
                            });
                        auto connected = windowNotes.empty() ? std::size_t{} : std::size_t{1};
                        auto longestConnected = connected;
                        auto previousAttack = windowNotes.empty() ? 0.0 :
                            windowNotes.front()->startBeat;
                        for (std::size_t noteIndex = 1;
                             noteIndex < windowNotes.size(); ++noteIndex) {
                            const auto attack = windowNotes[noteIndex]->startBeat;
                            if (std::abs(attack - previousAttack) < .01) continue;
                            connected = attack - previousAttack <= plan.beatsPerBar * .75 + .001
                                ? connected + 1 : 1;
                            longestConnected = std::max(longestConnected, connected);
                            previousAttack = attack;
                        }
                        if (longestConnected >= 4) ++deficit.narrativePhraseWindows;
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
                // A line made almost entirely from chord-tone jumps is tonal but does
                // not speak. Conversely, an uninterrupted scalar walk is connective
                // but equally synthetic. Preserve characteristic leaps inside an
                // audibly singable amount of conjunct motion.
                if (deficit.melodicIntervals >= 8 &&
                    (deficit.melodicStepRatio < .25 || deficit.melodicStepRatio > .68)) {
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
        if (protagonist && !plan.sections.empty()) {
            const auto songEnd = plan.totalBars * plan.beatsPerBar;
            const auto codaStart = std::max(0.0, songEnd - plan.beatsPerBar * 8.0);
            const auto finalTwoBars = std::max(0.0, songEnd - plan.beatsPerBar * 2.0);
            std::vector<const NoteEvent*> coda;
            for (const auto* note : renderedByInstrument[instrument.id])
                if (note->startBeat >= codaStart && note->startBeat < songEnd)
                    coda.push_back(note);
            std::sort(coda.begin(), coda.end(), [](const auto* left, const auto* right) {
                return left->startBeat < right->startBeat;
            });
            auto connectedAttacks = coda.empty() ? std::size_t{} : std::size_t{1};
            auto longestConnected = connectedAttacks;
            auto previousAttack = coda.empty() ? 0.0 : coda.front()->startBeat;
            for (std::size_t noteIndex = 1; noteIndex < coda.size(); ++noteIndex) {
                const auto attack = coda[noteIndex]->startBeat;
                if (std::abs(attack - previousAttack) < .01) continue;
                connectedAttacks = attack - previousAttack <=
                    plan.beatsPerBar * .75 + .001 ? connectedAttacks + 1 : 1;
                longestConnected = std::max(longestConnected, connectedAttacks);
                previousAttack = attack;
            }
            const auto terminalPitchClass = coda.empty()
                ? -1 : positiveModulo(coda.back()->pitch, 12);
            const auto stableEnding = explicitTonicEnding
                ? terminalPitchClass == positiveModulo(plan.rootPitchClass, 12)
                : resolutionStablePitchClasses.contains(terminalPitchClass);
            const auto audibleBoundary = longestConnected >= 4 &&
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

    const auto protagonist = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& instrument) { return instrument.id == protagonistId; });
    if (protagonist == plan.instruments.end()) return false;
    const auto protagonistIndex = static_cast<std::size_t>(
        std::distance(plan.instruments.begin(), protagonist));
    const auto existingVerification = performanceDeficits(plan, score, {protagonistIndex});
    const auto alreadyResolved = std::none_of(existingVerification.begin(),
        existingVerification.end(), [&](const auto& finding) {
            return finding.instrumentId == protagonistId && finding.missingCodaResolution;
        });
    if (alreadyResolved) return false;

    // Always close in the section containing the absolute song boundary. The
    // narrative may name an earlier Resolution followed by an Aftermath, but that
    // does not make the earlier boundary audible as the end of the track.
    const auto resolutionSection = static_cast<int>(plan.sections.size() - 1);
    const auto& resolution = plan.sections[static_cast<std::size_t>(resolutionSection)];
    const auto sectionLength = resolution.bars * plan.beatsPerBar;
    if (sectionLength <= 0.0) return false;

    const PerformanceCell* source = nullptr;
    std::size_t bestNotes = 0;
    for (const auto& cell : score.cells) {
        std::vector<double> attacks;
        for (const auto& note : cell.notes)
            if (note.instrumentId == protagonistId)
                attacks.push_back(note.beat);
        const auto count = attacks.size();
        if (count < 4) continue;
        std::sort(attacks.begin(), attacks.end());
        attacks.erase(std::unique(attacks.begin(), attacks.end(),
            [](double left, double right) { return std::abs(left - right) < .01; }),
            attacks.end());
        auto connected = attacks.empty() ? std::size_t{} : std::size_t{1};
        auto longestConnected = connected;
        for (std::size_t attack = 1; attack < attacks.size(); ++attack) {
            connected = attacks[attack] - attacks[attack - 1] <=
                plan.beatsPerBar * .75 + .001 ? connected + 1 : 1;
            longestConnected = std::max(longestConnected, connected);
        }
        if (longestConnected < 4) continue;
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
    if (authored.size() < 4) return false;

    // Extract a real connected sentence. Sparse one-note cue markers are not a
    // melodic source and must never be compacted into an artificial protagonist.
    std::vector<std::size_t> attackStarts;
    attackStarts.push_back(0);
    for (std::size_t index = 1; index < authored.size(); ++index)
        if (std::abs(authored[index]->beat - authored[index - 1]->beat) >= .01)
            attackStarts.push_back(index);
    std::size_t bestAttackBegin{};
    std::size_t bestAttackEnd{};
    auto runBegin = std::size_t{};
    for (std::size_t attack = 1; attack <= attackStarts.size(); ++attack) {
        const auto continues = attack < attackStarts.size() &&
            authored[attackStarts[attack]]->beat -
                authored[attackStarts[attack - 1]]->beat <=
                    plan.beatsPerBar * .75 + .001;
        if (continues) continue;
        if (attack - runBegin >= 4 && attack - runBegin >=
                bestAttackEnd - bestAttackBegin) {
            bestAttackBegin = runBegin;
            bestAttackEnd = attack;
        }
        runBegin = attack;
    }
    if (bestAttackEnd - bestAttackBegin < 4) return false;

    // Build a bounded transformed return from up to twelve connected attacks.
    // Rebasing an existing fragment is necessary when its source cell is longer
    // than the resolution section; a placement of the full cell would be clipped
    // before its intended final attack and would falsely report success.
    const auto selectedAttackBegin = bestAttackEnd - bestAttackBegin > 12
        ? bestAttackEnd - 12 : bestAttackBegin;
    const auto fragmentBegin = attackStarts[selectedAttackBegin];
    const auto fragmentEnd = bestAttackEnd < attackStarts.size()
        ? attackStarts[bestAttackEnd] : authored.size();
    const auto firstBeat = authored[fragmentBegin]->beat;
    const auto lastBeat = authored[fragmentEnd - 1]->beat - firstBeat;
    const auto terminalTarget = std::max(0.0, sectionLength -
        std::max(0.5, plan.beatsPerBar * 0.5));
    const auto codaWindowStart = std::max(0.0, sectionLength -
        std::min(8, resolution.bars) * plan.beatsPerBar);
    const auto availableSpan = terminalTarget - codaWindowStart;
    auto timeScale = 1.0;
    if (lastBeat > availableSpan && lastBeat > .001) {
        // PerformanceScoreEngine normalizes strict-grid placement scales to this
        // discrete set. Select from it now and compute the onset from the selected
        // value; otherwise normalization can shorten a fractional scale (for example
        // .75 -> .5), pull the last authored attack out of the final two bars and make
        // a coda that just passed verification fail immediately afterwards.
        constexpr std::array legalScales{0.25, 0.5, 1.0, 2.0, 4.0};
        const auto maximumScale = availableSpan / lastBeat;
        const auto legal = std::find_if(legalScales.rbegin(), legalScales.rend(),
            [&](double candidate) { return candidate <= maximumScale + .0001; });
        if (legal == legalScales.rend()) return false;
        timeScale = *legal;
    }
    if (lastBeat * timeScale > terminalTarget - codaWindowStart + .001)
        return false;
    auto terminalPitchClass = positiveModulo(plan.rootPitchClass, 12);
    const auto resolutionWords = lower(plan.narrativeSpine.resolution);
    const auto explicitTonicEnding = resolutionWords.find("tonic") != std::string::npos ||
        resolutionWords.find("root") != std::string::npos ||
        resolutionWords.find("home note") != std::string::npos ||
        resolutionWords.find("tonica") != std::string::npos;
    if (!explicitTonicEnding && !resolution.harmonicEvents.empty()) {
        const auto terminalEvent = std::max_element(
            resolution.harmonicEvents.begin(), resolution.harmonicEvents.end(),
            [](const auto& left, const auto& right) {
                return std::tie(left.barOffset, left.beatOffset) <
                    std::tie(right.barOffset, right.beatOffset);
            });
        const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
            [&](const auto& candidate) { return candidate.id == terminalEvent->chordId; });
        if (chord != plan.chordPalette.end())
            terminalPitchClass = positiveModulo(chord->rootPitchClass, 12);
    }
    auto transpose = positiveModulo(
        terminalPitchClass - authored[fragmentEnd - 1]->pitch, 12);
    if (transpose > 6) transpose -= 12;

    PerformanceCell codaCell;
    codaCell.id = "authored_coda_" + source->id;
    codaCell.themeId = source->themeId;
    codaCell.narrativeFunction = "transformed_resolution";
    codaCell.ownedVoices = source->ownedVoices;
    for (auto index = fragmentBegin; index < fragmentEnd; ++index) {
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

bool SelectiveRepair::ensurePrimaryChordBedClosure(SongPlan& plan) {
    const auto bedIndex = centralChordBedIndex(plan);
    if (!bedIndex || *bedIndex >= plan.instruments.size() || plan.sections.empty())
        return false;
    const auto deficits = performanceDeficits(
        plan, plan.performanceScore, {*bedIndex});
    const auto needsClosure = std::any_of(deficits.begin(), deficits.end(),
        [](const auto& finding) { return finding.missingCodaResolution; });
    if (!needsClosure) return false;

    auto tonic = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
        [&](const auto& chord) {
            return chord.function == HarmonicFunction::Tonic &&
                positiveModulo(chord.rootPitchClass, 12) ==
                    positiveModulo(plan.rootPitchClass, 12);
        });
    if (tonic == plan.chordPalette.end())
        tonic = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
            [](const auto& chord) { return chord.function == HarmonicFunction::Tonic; });
    if (tonic == plan.chordPalette.end()) return false;

    const auto resolutionIndex = plan.sections.size() - 1;
    auto& resolution = plan.sections[resolutionIndex];
    const auto closureBars = std::min(4, std::max(2, resolution.bars));
    const auto sectionLength = resolution.bars * plan.beatsPerBar;
    const auto closureLength = std::min(sectionLength,
        closureBars * plan.beatsPerBar);
    const auto closureStart = std::max(0.0, sectionLength - closureLength);
    if (closureLength <= 0.0) return false;

    const auto originalPlan = plan;
    const auto& bed = plan.instruments[*bedIndex];
    auto& score = plan.performanceScore;

    // Isolate the bed from any multi-instrument cells. This lets the transaction
    // trim only its terminal placements while all unrelated MIDI and placements
    // remain exactly as authored.
    std::set<std::string> existingIds;
    for (const auto& cell : score.cells) existingIds.insert(cell.id);
    std::set<std::string> bedCellIds;
    std::vector<PerformanceCell> isolatedCells;
    std::vector<PerformancePlacement> isolatedPlacements;
    const auto initialCellCount = score.cells.size();
    const auto placementSnapshot = score.placements;
    for (std::size_t cellIndex = 0; cellIndex < initialCellCount; ++cellIndex) {
        auto& cell = score.cells[cellIndex];
        const auto hasBed = std::any_of(cell.notes.begin(), cell.notes.end(),
            [&](const auto& note) { return note.instrumentId == bed.id; });
        if (!hasBed) continue;
        const auto hasOther = std::any_of(cell.notes.begin(), cell.notes.end(),
            [&](const auto& note) {
                return !note.instrumentId.empty() && note.instrumentId != bed.id;
            });
        if (!hasOther) {
            bedCellIds.insert(cell.id);
            continue;
        }
        auto isolated = cell;
        auto suffix = std::string{"__terminal_bed"};
        auto candidate = cell.id + suffix;
        for (auto ordinal = 2; existingIds.contains(candidate); ++ordinal)
            candidate = cell.id + suffix + std::to_string(ordinal);
        isolated.id = candidate;
        existingIds.insert(candidate);
        retainInstrument(isolated, bed.id);
        removeInstrument(cell, bed.id);
        bedCellIds.insert(isolated.id);
        for (const auto& placement : placementSnapshot) {
            if (placement.cellId != cell.id) continue;
            auto copy = placement;
            copy.cellId = isolated.id;
            isolatedPlacements.push_back(std::move(copy));
        }
        isolatedCells.push_back(std::move(isolated));
    }
    score.cells.insert(score.cells.end(), isolatedCells.begin(), isolatedCells.end());
    score.placements.insert(score.placements.end(),
        isolatedPlacements.begin(), isolatedPlacements.end());

    std::map<std::string, double> lengths;
    for (const auto& cell : score.cells) lengths[cell.id] = cell.lengthBeats;
    std::vector<PerformancePlacement> preserved;
    preserved.reserve(score.placements.size() + 1);
    for (const auto& placement : score.placements) {
        if (placement.sectionIndex != static_cast<int>(resolutionIndex) ||
            !bedCellIds.contains(placement.cellId)) {
            preserved.push_back(placement);
            continue;
        }
        const auto cellLength = lengths[placement.cellId];
        const auto scale = std::max(.01, placement.timeScale);
        const auto fragmentEnd = placement.fragmentEnd < 0.0
            ? cellLength : placement.fragmentEnd;
        const auto iterationLength = cellLength * scale;
        for (auto repeat = 0; repeat < std::max(1, placement.repeats); ++repeat) {
            auto copy = placement;
            copy.repeats = 1;
            copy.startBeat = placement.startBeat + repeat * iterationLength;
            copy.fragmentEnd = fragmentEnd;
            const auto ownedStartInCell = placement.retrograde
                ? cellLength - fragmentEnd : placement.fragmentStart;
            const auto ownedEndInCell = placement.retrograde
                ? cellLength - placement.fragmentStart : fragmentEnd;
            const auto spanStart = copy.startBeat + ownedStartInCell * scale;
            const auto spanEnd = copy.startBeat + ownedEndInCell * scale;
            if (spanEnd <= closureStart + .0001) {
                preserved.push_back(std::move(copy));
                continue;
            }
            if (spanStart >= closureStart - .0001) continue;
            const auto clippedCellBeat = std::clamp(
                (closureStart - copy.startBeat) / scale, 0.0, cellLength);
            if (!placement.retrograde)
                copy.fragmentEnd = std::min(copy.fragmentEnd, clippedCellBeat);
            else
                copy.fragmentStart = std::max(copy.fragmentStart,
                    cellLength - clippedCellBeat);
            if (copy.fragmentEnd > copy.fragmentStart + .01)
                preserved.push_back(std::move(copy));
        }
    }
    score.placements = std::move(preserved);

    std::vector<int> pitchClasses = tonic->pitchClasses;
    pitchClasses.push_back(plan.rootPitchClass);
    const auto intervals = intervalsFor(plan.scale);
    if (intervals.size() >= 5) {
        pitchClasses.push_back(plan.rootPitchClass + intervals[2]);
        pitchClasses.push_back(plan.rootPitchClass + intervals[4]);
    }
    pitchClasses = normalizePitchClasses(pitchClasses);
    if (pitchClasses.size() < 3) {
        plan = originalPlan;
        return false;
    }
    std::stable_sort(pitchClasses.begin(), pitchClasses.end(), [&](int left, int right) {
        const auto leftRoot = positiveModulo(left, 12) ==
            positiveModulo(plan.rootPitchClass, 12);
        const auto rightRoot = positiveModulo(right, 12) ==
            positiveModulo(plan.rootPitchClass, 12);
        if (leftRoot != rightRoot) return leftRoot;
        return left < right;
    });
    if (pitchClasses.size() > 4) pitchClasses.resize(4);

    std::vector<int> voicing;
    auto target = std::clamp(bed.minimumPitch + 12,
        bed.minimumPitch, bed.maximumPitch);
    for (const auto pitchClass : pitchClasses) {
        auto pitch = pitchForClass(pitchClass, target,
            bed.minimumPitch, bed.maximumPitch);
        while (!voicing.empty() && pitch <= voicing.back() && pitch + 12 <= bed.maximumPitch)
            pitch += 12;
        if (!voicing.empty() && pitch <= voicing.back()) continue;
        voicing.push_back(pitch);
        target = pitch + 4;
    }
    if (voicing.size() < 3) {
        plan = originalPlan;
        return false;
    }

    auto closureId = std::string{"transactional_tonic_closure_"} + bed.id;
    for (auto ordinal = 2; existingIds.contains(closureId); ++ordinal)
        closureId = "transactional_tonic_closure_" + bed.id + std::to_string(ordinal);
    PerformanceCell closure;
    closure.id = closureId;
    closure.lengthBeats = closureLength;
    closure.ownedVoices = {bed.sourceVoice};
    closure.themeId = plan.narrativeSpine.motifIdentity.empty()
        ? "harmonic_resolution" : plan.narrativeSpine.motifIdentity;
    closure.narrativeFunction = "transactional_harmonic_closure";
    const auto secondAttack = closureLength * .5;
    for (std::size_t index = 0; index < voicing.size(); ++index) {
        closure.notes.push_back({0.0, std::max(.25, secondAttack - .0625),
            voicing[index], std::max(42, 62 - static_cast<int>(index) * 4),
            bed.sourceVoice, MetricIntent::StrictGrid, bed.id});
        auto terminalPitch = voicing[index];
        if (index + 1 == voicing.size() && terminalPitch + 12 <= bed.maximumPitch)
            terminalPitch += 12;
        closure.notes.push_back({secondAttack,
            std::max(.25, closureLength - secondAttack - .0625),
            terminalPitch, std::max(38, 58 - static_cast<int>(index) * 4),
            bed.sourceVoice, MetricIntent::StrictGrid, bed.id});
    }
    score.cells.push_back(std::move(closure));
    PerformancePlacement closurePlacement;
    closurePlacement.cellId = closureId;
    closurePlacement.sectionIndex = static_cast<int>(resolutionIndex);
    closurePlacement.startBeat = closureStart;
    closurePlacement.repeats = 1;
    closurePlacement.purpose = "bounded primary chord-bed tonic closure";
    closurePlacement.fragmentStart = 0.0;
    closurePlacement.fragmentEnd = closureLength;
    score.placements.push_back(std::move(closurePlacement));

    resolution.harmonicEvents.erase(std::remove_if(
        resolution.harmonicEvents.begin(), resolution.harmonicEvents.end(),
        [&](const auto& event) {
            return event.barOffset * plan.beatsPerBar + event.beatOffset >=
                closureStart - .0001;
        }), resolution.harmonicEvents.end());
    resolution.harmonicEvents.push_back({resolution.bars - closureBars, 0.0,
        tonic->id, 1.0, "transactional tonic resolution"});
    std::sort(resolution.harmonicEvents.begin(), resolution.harmonicEvents.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.barOffset, left.beatOffset) <
                std::tie(right.barOffset, right.beatOffset);
        });

    const auto verification = performanceDeficits(plan, score, {*bedIndex});
    if (std::any_of(verification.begin(), verification.end(),
            [](const auto& finding) { return finding.missingCodaResolution; })) {
        plan = originalPlan;
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
        deficit.missingThematicDevelopment || deficit.missingMelodicSpeech ||
        deficit.missingCentralChordBed || deficit.missingChordBedBreath ||
        deficit.missingChordBedNarrativeArc) return true;
    return deficit.minimumPhrases > 0 && deficit.phrases < deficit.minimumPhrases &&
        deficit.notes >= deficit.minimumNotes &&
        deficit.activeBars >= deficit.minimumActiveBars;
}

} // namespace pulso
