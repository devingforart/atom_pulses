#include "HarmonyPlan.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <string>

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

std::string_view tonalPolicyKey(TonalPolicy value) noexcept {
    switch (value) {
        case TonalPolicy::Consolidated: return "consolidated";
        case TonalPolicy::Expanded: return "expanded";
        case TonalPolicy::Free: return "free";
    }
    return "consolidated";
}

TonalPolicy tonalPolicyForDirection(std::string_view direction) {
    auto text = std::string(direction);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    constexpr std::array freeTerms{
        "atonal", "twelve-tone", "twelve tone", "12-tone", "serialism", "serialista",
        "free chromatic", "cromatismo libre", "deliberately dissonant", "deliberadamente disonante",
        "dissonant cluster", "cluster disonante", "polytonal", "politonal", "bitonal"
    };
    for (const auto* term : freeTerms)
        if (text.find(term) != std::string::npos) return TonalPolicy::Free;

    constexpr std::array expandedTerms{
        "modal interchange", "intercambio modal", "borrowed chord", "acorde prestado",
        "secondary dominant", "dominante secundaria", "chromatic harmony", "armonia cromatica",
        "chromatic mediant", "mediante cromatica", "brief modulation", "modulacion breve",
        "jazz harmony", "armonia jazz", "altered harmony", "armonia alterada"
    };
    for (const auto* term : expandedTerms)
        if (text.find(term) != std::string::npos) return TonalPolicy::Expanded;
    return TonalPolicy::Consolidated;
}

std::optional<HarmonicFunction> harmonicFunctionFromKey(std::string_view key) noexcept {
    return fromKey(key, functionValues);
}

std::optional<VoicingStrategy> voicingStrategyFromKey(std::string_view key) noexcept {
    return fromKey(key, voicingValues);
}

} // namespace pulso
