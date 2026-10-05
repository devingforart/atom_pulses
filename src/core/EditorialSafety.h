#pragma once

#include "SongComposer.h"

namespace pulso {

// Separates MIDI integrity from subjective musical findings. The local AI
// studio may publish a technically valid score for audition while retaining
// every harmonic and narrative warning for targeted AI revision.
class EditorialSafety final {
public:
    [[nodiscard]] static bool technicallySafeAiScore(
        const Pattern&, const CompositionRenderReport&) noexcept;
};

} // namespace pulso
