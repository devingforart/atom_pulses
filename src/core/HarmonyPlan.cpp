#include "HarmonyPlan.h"

#include <array>

namespace pulso {
namespace {

template <typename Value, std::size_t Size>
std::optional<Value> fromKey(std::string_view key,
                             const std::array<std::pair<std::string_view, Value>, Size>& values) noexcept {
    for (const auto& [candidate, value] : values)
        if (candidate == key) return value;
    return std::nullopt;
}

constexpr std::array functionValues{
    std::pair{std::string_view{"tonic"}, HarmonicFunction::Tonic},
    std::pair{std::string_view{"predominant"}, HarmonicFunction::Predominant},
    std::pair{std::string_view{"dominant"}, HarmonicFunction::Dominant},
    std::pair{std::string_view{"modal"}, HarmonicFunction::Modal},
    std::pair{std::string_view{"chromatic"}, HarmonicFunction::Chromatic},
    std::pair{std::string_view{"pedal"}, HarmonicFunction::Pedal},
    std::pair{std::string_view{"transitional"}, HarmonicFunction::Transitional},
    std::pair{std::string_view{"colour"}, HarmonicFunction::Colour}
};

constexpr std::array voicingValues{
    std::pair{std::string_view{"close"}, VoicingStrategy::Close},
    std::pair{std::string_view{"open"}, VoicingStrategy::Open},
    std::pair{std::string_view{"drop_2"}, VoicingStrategy::Drop2},
    std::pair{std::string_view{"quartal"}, VoicingStrategy::Quartal},
    std::pair{std::string_view{"cluster"}, VoicingStrategy::Cluster},
    std::pair{std::string_view{"shell"}, VoicingStrategy::Shell},
    std::pair{std::string_view{"mixed"}, VoicingStrategy::Mixed}
};

} // namespace

std::string_view harmonicFunctionKey(HarmonicFunction value) noexcept {
    for (const auto& [key, candidate] : functionValues) if (candidate == value) return key;
    return "colour";
}

std::string_view voicingStrategyKey(VoicingStrategy value) noexcept {
    for (const auto& [key, candidate] : voicingValues) if (candidate == value) return key;
    return "mixed";
}

std::optional<HarmonicFunction> harmonicFunctionFromKey(std::string_view key) noexcept {
    return fromKey(key, functionValues);
}

std::optional<VoicingStrategy> voicingStrategyFromKey(std::string_view key) noexcept {
    return fromKey(key, voicingValues);
}

} // namespace pulso
