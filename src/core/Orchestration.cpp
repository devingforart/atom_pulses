#include "Orchestration.h"

namespace pulso {

std::optional<VoiceId> voiceIdFromKey(std::string_view key) noexcept {
    for (const auto& definition : voiceDefinitions)
        if (definition.key == key) return definition.id;
    return std::nullopt;
}

VoiceId inferVoiceFromChannel(int channel) noexcept {
    if (channel == 1) return VoiceId::SubBass;
    if (channel == 2) return VoiceId::Lead;
    if (channel == 3) return VoiceId::HarmonicFoundation;
    if (channel == 4) return VoiceId::HarmonicPulse;
    if (channel == 5) return VoiceId::HarmonicUpper;
    if (channel == 6) return VoiceId::MovementBass;
    if (channel == 7) return VoiceId::Countermelody;
    if (channel == 8) return VoiceId::Atmosphere;
    if (channel == 9) return VoiceId::Transitions;
    if (channel == 10) return VoiceId::CoreDrums;
    return VoiceId::Lead;
}

} // namespace pulso
