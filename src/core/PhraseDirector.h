#pragma once

#include "Orchestration.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pulso {

struct SongPlan;
struct SongSection;

enum class Participation : std::uint8_t { Silent = 0, Foreground, Response, Support, Texture, Accent };

struct VoiceDirection {
    Participation participation{Participation::Silent};
    int maximumOnsets{};
    double entryBeat{};
    double exitBeat{4.0};
    double expression{0.5};
};

struct BarDirection {
    int localBar{};
    int harmonicHoldBars{1};
    int harmonicStep{};
    bool harmonicAttack{true};
    bool breath{};
    bool fullBreath{};
    bool arrival{};
    VoiceId foreground{VoiceId::Unspecified};
    VoiceId response{VoiceId::Unspecified};
    std::array<VoiceDirection, static_cast<std::size_t>(VoiceId::Count)> voices{};

    [[nodiscard]] const VoiceDirection& forVoice(VoiceId voice) const noexcept;
};

class PhraseDirector final {
public:
    [[nodiscard]] static std::vector<BarDirection> create(const SongPlan&, const SongSection&);
};

} // namespace pulso
