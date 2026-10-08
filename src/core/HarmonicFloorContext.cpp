#include "HarmonicFloorContext.h"

#include "SongComposer.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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

bool HarmonicFloorContext::sustainedFloorOwner(
    const InstrumentAssignment& part) noexcept {
    if (part.sourceVoice != VoiceId::HarmonicFoundation &&
        part.sourceVoice != VoiceId::HarmonicUpper &&
        part.sourceVoice != VoiceId::Atmosphere) return false;
    const auto identity = lower(part.id + " " + part.name + " " + part.role +
                                " " + part.instrumentId);
    // A tonal sub or an arpeggiator can support the harmony, but neither is
    // the second sustained chord body promised by an electronic cast.
    for (const auto token : {"sub", "bass", "kick", "arpeggio", "arp", "sequence"})
        if (identity.find(token) != std::string::npos) return false;
    // Timbre is not a musical responsibility: a granular pad used for a reply
    // is still a reply, not a sustained chord bed. Require the declared cast
    // function before expensive performance writing begins.
    if (part.lineRelationship == "call_response" ||
        part.lineRelationship == "relay" ||
        part.lineRelationship == "timbral_handoff") return false;
    return part.orchestralFunction == "foundation" ||
        part.orchestralFunction == "body";
}

double HarmonicFloorContext::plannedCoverage(const SongPlan& plan) noexcept {
    if (plan.totalBars <= 0 || plan.beatsPerBar <= 0.0) return 0.0;
    std::vector<const InstrumentAssignment*> floors;
    for (const auto& instrument : plan.instruments)
        if (sustainedFloorOwner(instrument)) floors.push_back(&instrument);
    // The audible fabric auditor intentionally evaluates at most four bodies.
    if (floors.size() > 4) floors.resize(4);
    auto covered = 0;
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto beat = static_cast<double>(bar) * plan.beatsPerBar;
        const auto* section = sectionAt(plan, beat);
        auto active = std::size_t{};
        for (const auto* owner : floors)
            if (section == nullptr || owner->activeSections.empty() ||
                std::find(owner->activeSections.begin(), owner->activeSections.end(),
                          section->name) != owner->activeSections.end()) ++active;
        if (active >= requiredLayers(plan, beat)) ++covered;
    }
    return static_cast<double>(covered) / static_cast<double>(plan.totalBars);
}

} // namespace pulso
