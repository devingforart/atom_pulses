#pragma once

#include "core/SongComposer.h"

#include <juce_core/juce_core.h>

namespace pulso::plugin {

class OperationalJournal final {
public:
    [[nodiscard]] static juce::File logFile();
    static void write(const juce::String& level, const juce::String& stage,
                      const juce::String& message);
    [[nodiscard]] static juce::File writeRejectedAudit(
        const SongPlan&, const CompositionRenderReport&, const juce::String& reason,
        std::size_t repairPasses, const juce::String& rejectionStage = "audition");
};

} // namespace pulso::plugin
