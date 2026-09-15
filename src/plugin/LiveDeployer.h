#pragma once

#include "core/MusicTypes.h"

#include <juce_core/juce_core.h>

namespace pulso::plugin {

struct LiveDeploymentOptions {
    enum class SoundMode : std::uint8_t { NeutralAudition = 0, MidiOnly };

    juce::String title;
    double bpm{120.0};
    int numerator{4};
    int denominator{4};
    bool aggregateDepartmentStems{};
    SoundMode soundMode{SoundMode::NeutralAudition};
};

[[nodiscard]] bool writeLiveDeploymentRequest(const Pattern&, const LiveDeploymentOptions&,
                                               juce::String& statusMessage,
                                               const juce::File& directoryOverride = {});
[[nodiscard]] juce::String readLiveDeploymentStatus();
[[nodiscard]] bool liveBridgeIsAvailable();
[[nodiscard]] juce::String readLiveNativeInventorySummary();
[[nodiscard]] juce::String readLiveNativeCapabilitiesSummary();
[[nodiscard]] juce::String readLiveDeploymentReport();
[[nodiscard]] juce::String readLiveAudibleExecutionFeedback();
[[nodiscard]] bool liveNativeInventoryIsReady();

} // namespace pulso::plugin
