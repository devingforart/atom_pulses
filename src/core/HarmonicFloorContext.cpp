#include "HarmonicFloorContext.h"

#include "SongComposer.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>

namespace pulso {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool containsBreathMarker(std::string_view value) noexcept {
    constexpr std::string_view markers[] = {
        "intro", "opening", "break", "breakdown", "bajada", "interlude",
        "suspend", "aftermath", "outro", "coda", "withdraw"
    };
    return std::any_of(std::begin(markers), std::end(markers), [&](const auto marker) {
        return value.find(marker) != std::string_view::npos;
    });
}

const SongSection* sectionAt(const SongPlan& plan, double beat) noexcept {
    const SongSection* result = plan.sections.empty() ? nullptr : &plan.sections.front();
    for (const auto& section : plan.sections) {
        if (beat + .001 < section.startBar * plan.beatsPerBar) break;
        result = &section;
    }
    return result;
}

} // namespace

std::size_t HarmonicFloorContext::requiredLayers(const SongPlan& plan, double beat) noexcept {
    const auto* section = sectionAt(plan, beat);
    if (section == nullptr) return 1;
    const auto identity = lower(section->name + " " + section->function + " " +
                                section->motifTreatment);
    const auto intensity = section->energy * .58 + section->density * .42;
    const auto authoredAftermath = std::any_of(plan.narrativeSpine.acts.begin(),
        plan.narrativeSpine.acts.end(), [&](const auto& act) {
            return act.sectionName == section->name && act.stage == NarrativeStage::Aftermath;
        });
    return containsBreathMarker(identity) || authoredAftermath || intensity < .44 ? 1U : 2U;
}

} // namespace pulso
