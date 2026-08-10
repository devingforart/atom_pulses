#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pulso {

enum class VoiceFamily : std::uint8_t { Rhythm = 0, Bass, Harmony, Melodic, Texture };

enum class VoiceId : std::uint8_t {
    CoreDrums = 0,
    LowPercussion,
    HighPercussion,
    SubBass,
    MovementBass,
    HarmonicFoundation,
    HarmonicPulse,
    HarmonicUpper,
    Lead,
    Countermelody,
    Atmosphere,
    Transitions,
    Count,
    Unspecified = 255
};

struct VoiceDefinition {
    VoiceId id;
    std::string_view key;
    std::string_view name;
    VoiceFamily family;
    int midiChannel;
    int minimumPitch;
    int maximumPitch;
};

inline constexpr std::array voiceDefinitions{
    VoiceDefinition{VoiceId::CoreDrums, "core_drums", "Core Drums", VoiceFamily::Rhythm, 10, 35, 81},
    VoiceDefinition{VoiceId::LowPercussion, "low_percussion", "Low Percussion", VoiceFamily::Rhythm, 10, 35, 60},
    VoiceDefinition{VoiceId::HighPercussion, "high_percussion", "High Percussion", VoiceFamily::Rhythm, 10, 42, 81},
    VoiceDefinition{VoiceId::SubBass, "sub_bass", "Sub Bass", VoiceFamily::Bass, 1, 28, 48},
    VoiceDefinition{VoiceId::MovementBass, "movement_bass", "Movement Bass", VoiceFamily::Bass, 6, 36, 62},
    VoiceDefinition{VoiceId::HarmonicFoundation, "harmonic_foundation", "Harmonic Foundation", VoiceFamily::Harmony, 3, 45, 76},
    VoiceDefinition{VoiceId::HarmonicPulse, "harmonic_pulse", "Harmonic Pulse", VoiceFamily::Harmony, 4, 50, 84},
    VoiceDefinition{VoiceId::HarmonicUpper, "harmonic_upper", "Harmonic Upper", VoiceFamily::Harmony, 5, 60, 96},
    VoiceDefinition{VoiceId::Lead, "lead", "Lead", VoiceFamily::Melodic, 2, 55, 92},
    VoiceDefinition{VoiceId::Countermelody, "countermelody", "Countermelody", VoiceFamily::Melodic, 7, 48, 86},
    VoiceDefinition{VoiceId::Atmosphere, "atmosphere", "Atmosphere", VoiceFamily::Texture, 8, 42, 92},
    VoiceDefinition{VoiceId::Transitions, "transitions", "Transitions / FX", VoiceFamily::Texture, 9, 36, 108}
};

[[nodiscard]] constexpr const VoiceDefinition& voiceDefinition(VoiceId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return voiceDefinitions[index < voiceDefinitions.size() ? index : 0];
}

[[nodiscard]] std::optional<VoiceId> voiceIdFromKey(std::string_view) noexcept;
[[nodiscard]] VoiceId inferVoiceFromChannel(int channel) noexcept;
[[nodiscard]] constexpr bool isVoiceInFamily(VoiceId id, VoiceFamily family) noexcept {
    return id != VoiceId::Unspecified && voiceDefinition(id).family == family;
}

} // namespace pulso
