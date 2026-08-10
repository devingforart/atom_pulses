#pragma once

#include "MusicTypes.h"

namespace pulso {

class Generator final {
public:
    [[nodiscard]] Pattern generate(const GenerationContext& context) const;
};

} // namespace pulso
