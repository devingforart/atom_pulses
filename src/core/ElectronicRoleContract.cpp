#include "ElectronicRoleContract.h"

#include "SongComposer.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

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

bool motionIdentity(std::string text, VoiceId voice) {
    text = lower(std::move(text));
    return voice == VoiceId::HarmonicPulse ||
        containsAny(text, {"hypnotic_arp", "fm_sequence", "acid_line", "arpegg",
                           "sequence", "sequenc", "ostinato", "orbit", "pulse",
                           "recurrence", "motor hipnot", "movimiento hipnot"});
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

bool ElectronicRoleContract::motionOwner(const InstrumentAssignment& part) noexcept {
    if (transition(part) || isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm)) return false;
    return motionIdentity(part.id + " " + part.instrumentId + " " + part.name + " " +
        part.role + " " + part.orchestralFunction + " " + part.articulation,
        part.sourceVoice);
}

bool ElectronicRoleContract::motionOwner(const InstrumentPart& part) noexcept {
    if (transition(part) || isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm)) return false;
    return motionIdentity(part.catalogId + " " + part.name + " " + part.role + " " +
        part.orchestralFunction + " " + part.articulation,
        part.sourceVoice);
}

bool ElectronicRoleContract::requiresMotionOwner(const SongPlan& plan) {
    return electronic(plan) && plan.percussionFreeIntent && !staticWork(plan);
}

std::size_t ElectronicRoleContract::motionOwnerCount(const SongPlan& plan) noexcept {
    return static_cast<std::size_t>(std::count_if(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& instrument) { return motionOwner(instrument); }));
}

} // namespace pulso
