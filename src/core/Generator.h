#pragma once

#include "MusicTypes.h"

namespace pulso {

class Generator final {
public:
    [[nodiscard]] Pattern generate(const GenerationContext& context) const;

private:
    [[nodiscard]] Pattern generateBass(const GenerationContext& context) const;
    [[nodiscard]] Pattern generatePercussion(const GenerationContext& context) const;
    [[nodiscard]] Pattern generateCountermelody(const GenerationContext& context) const;
};

} // namespace pulso

