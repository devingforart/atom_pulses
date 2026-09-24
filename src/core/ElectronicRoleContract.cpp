#include "ElectronicRoleContract.h"

#include "SongComposer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <set>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool containsAny(std::string_view text,
                 std::initializer_list<std::string_view> tokens) noexcept {
    return std::any_of(tokens.begin(), tokens.end(), [&](const auto token) {
        return text.find(token) != std::string_view::npos;
    });
}

bool staticWork(const SongPlan& plan) {
    const auto text = lower(plan.summary + " " + plan.productionLanguage.description + " " +
        plan.harmonicLanguage.description + " " + plan.soundscape.scene + " " +
        plan.soundscape.spatialNarrative);
    return containsAny(text, {"drone-only", "drone only", "static work", "static piece",
                              "without pulse", "without motion", "sin pulso", "sin movimiento",
                              "obra estatica", "pieza estatica"});
}

constexpr std::string_view primaryMotionMarker{"primary_motion_owner"};
constexpr std::string_view supportingMotionMarker{"supporting_motion"};

bool motionIdentity(std::string text, VoiceId voice) {
    text = lower(std::move(text));
    return voice == VoiceId::HarmonicPulse ||
        containsAny(text, {"hypnotic_arp", "fm_sequence", "acid_line", "arpegg",
                           "sequence", "sequenc", "ostinato", "orbit", "pulse",
                           "recurrence", "motor hipnot", "movimiento hipnot",
                           primaryMotionMarker, supportingMotionMarker});
}

bool explicitPrimaryMotion(std::string text) {
    return lower(std::move(text)).find(primaryMotionMarker) != std::string::npos;
}

void eraseMarker(std::string& text, std::string_view marker) {
    for (auto position = text.find(marker); position != std::string::npos;
         position = text.find(marker)) text.erase(position, marker.size());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    for (auto position = text.find("  "); position != std::string::npos;
         position = text.find("  ")) text.erase(position, 1);
}

void setMotionMarker(InstrumentAssignment& part, std::string_view marker) {
    eraseMarker(part.role, primaryMotionMarker);
    eraseMarker(part.role, supportingMotionMarker);
    if (!part.role.empty()) part.role += " ";
    part.role += marker;
}

double primaryScore(const InstrumentAssignment& part, std::string_view protagonistId) {
    auto score = 0.0;
    if (explicitPrimaryMotion(part.role)) score += 1000.0;
    if (part.sourceVoice == VoiceId::HarmonicPulse) score += 100.0;
    const auto identity = lower(part.instrumentId + " " + part.name + " " + part.role + " " +
                                part.orchestralFunction + " " + part.articulation);
    if (containsAny(identity, {"hypnotic_arp", "fm_sequence", "arpegg", "sequence"})) score += 40.0;
    if (containsAny(identity, {"pulse", "ostinato", "orbit", "recurrence"})) score += 25.0;
    if (part.lineRelationship == "independent") score += 8.0;
    score += std::clamp(part.prominence, 0.0, 1.0) * 5.0;
    if (!protagonistId.empty() && part.id == protagonistId) score -= 15.0;
    return score;
}

} // namespace

bool ElectronicRoleContract::electronic(const SongPlan& plan) noexcept {
    return plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
}

bool ElectronicRoleContract::transition(const InstrumentAssignment& part) noexcept {
    return part.sourceVoice == VoiceId::Transitions || part.orchestralFunction == "transition";
}

bool ElectronicRoleContract::transition(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::Transitions || part.orchestralFunction == "transition";
}

bool ElectronicRoleContract::motionCandidate(const InstrumentAssignment& part) noexcept {
    if (transition(part) || isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm)) return false;
    return motionIdentity(part.id + " " + part.instrumentId + " " + part.name + " " +
        part.role + " " + part.orchestralFunction + " " + part.articulation,
        part.sourceVoice);
}

bool ElectronicRoleContract::motionCandidate(const InstrumentPart& part) noexcept {
    if (transition(part) || isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm)) return false;
    return motionIdentity(part.catalogId + " " + part.name + " " + part.role + " " +
        part.orchestralFunction + " " + part.articulation,
        part.sourceVoice);
}

bool ElectronicRoleContract::motionOwner(const InstrumentAssignment& part) noexcept {
    return motionCandidate(part) && explicitPrimaryMotion(part.role);
}

bool ElectronicRoleContract::motionOwner(const InstrumentPart& part) noexcept {
    return motionCandidate(part) && explicitPrimaryMotion(part.role);
}

std::optional<std::size_t> ElectronicRoleContract::electPrimaryMotionOwner(
        std::span<InstrumentAssignment> instruments,
        std::string_view protagonistInstrumentId) {
    std::vector<std::size_t> candidates;
    candidates.reserve(instruments.size());
    for (std::size_t index = 0; index < instruments.size(); ++index)
        if (motionCandidate(instruments[index])) candidates.push_back(index);
    const auto authoredCandidateCount = candidates.size();

    // A valid pitched cast can always acquire a recurrence conductor locally. Prefer
    // an authored candidate, but never spend another model call merely to classify it.
    if (candidates.empty()) {
        for (std::size_t index = 0; index < instruments.size(); ++index) {
            const auto& part = instruments[index];
            if (!transition(part) && !isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm))
                candidates.push_back(index);
        }
    }
    if (candidates.empty()) return std::nullopt;

    auto primary = candidates.front();
    auto best = -std::numeric_limits<double>::infinity();
    for (const auto index : candidates) {
        const auto score = primaryScore(instruments[index], protagonistInstrumentId);
        if (score > best) {
            best = score;
            primary = index;
        }
    }
    for (const auto index : candidates) {
        if (index == primary) setMotionMarker(instruments[index], primaryMotionMarker);
        else if (authoredCandidateCount > 0)
            setMotionMarker(instruments[index], supportingMotionMarker);
    }
    return primary;
}

AuthoredMotionOwnerReconciliation
ElectronicRoleContract::reconcileAuthoredPrimaryMotionOwner(SongPlan& plan) {
    AuthoredMotionOwnerReconciliation result;
    if (!requiresMotionOwner(plan) || plan.performanceScore.empty()) return result;

    std::map<std::string, std::size_t> placedNotes;
    std::map<std::string, std::set<int>> activeSections;
    for (const auto& placement : plan.performanceScore.placements) {
        const auto cell = std::find_if(plan.performanceScore.cells.begin(),
            plan.performanceScore.cells.end(), [&](const auto& candidate) {
                return candidate.id == placement.cellId;
        });
        if (cell == plan.performanceScore.cells.end()) continue;
        const auto repeats = static_cast<std::size_t>(std::max(1, placement.repeats));
        const auto fragmentStart = std::clamp(
            placement.fragmentStart, 0.0, cell->lengthBeats);
        const auto fragmentEnd = placement.fragmentEnd < 0.0
            ? cell->lengthBeats
            : std::clamp(placement.fragmentEnd, fragmentStart, cell->lengthBeats);
        for (const auto& note : cell->notes) {
            if (note.instrumentId.empty() || note.beat >= fragmentEnd ||
                note.beat + note.durationBeats <= fragmentStart) continue;
            placedNotes[note.instrumentId] += repeats;
            activeSections[note.instrumentId].insert(placement.sectionIndex);
        }
    }

    std::optional<std::size_t> currentOwner;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        if (!motionOwner(plan.instruments[index])) continue;
        if (!currentOwner ||
            placedNotes[plan.instruments[index].id] >
                placedNotes[plan.instruments[*currentOwner].id])
            currentOwner = index;
    }
    if (!currentOwner) return result;
    const auto& provisional = plan.instruments[*currentOwner];
    if (placedNotes[provisional.id] > 0 || provisional.explicitPromptIdentity)
        return result;

    std::optional<std::size_t> elected;
    auto best = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& candidate = plan.instruments[index];
        const auto notes = placedNotes[candidate.id];
        if (notes == 0 || !motionCandidate(candidate)) continue;
        auto score = std::log1p(static_cast<double>(notes)) * 20.0;
        score += static_cast<double>(activeSections[candidate.id].size()) * 8.0;
        if (candidate.sourceVoice == VoiceId::HarmonicPulse) score += 12.0;
        if (candidate.sourceVoice == VoiceId::MovementBass) score += 7.0;
        if (candidate.id == plan.narrativeSpine.protagonistInstrumentId) score -= 15.0;
        score += std::clamp(candidate.prominence, 0.0, 1.0) * 4.0;
        if (!elected || score > best) {
            elected = index;
            best = score;
        }
    }
    if (!elected || *elected == *currentOwner) return result;

    result.previousOwnerId = provisional.id;
    result.electedOwnerId = plan.instruments[*elected].id;
    result.electedAuthoredNotes = placedNotes[result.electedOwnerId];
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        if (!motionCandidate(plan.instruments[index])) continue;
        setMotionMarker(plan.instruments[index],
            index == *elected ? primaryMotionMarker : supportingMotionMarker);
    }
    result.changed = true;
    return result;
}

bool ElectronicRoleContract::requiresMotionOwner(const SongPlan& plan) {
    return electronic(plan) && plan.percussionFreeIntent && !staticWork(plan);
}

std::size_t ElectronicRoleContract::motionOwnerCount(const SongPlan& plan) noexcept {
    return static_cast<std::size_t>(std::count_if(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& instrument) { return motionOwner(instrument); }));
}

} // namespace pulso
