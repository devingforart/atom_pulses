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
    // A Club premise may establish its tonal identity with bass/arp before the
    // chord beds enter. This is an authored introduction, not missing pads.
    // Audibility and the whole-work density/voice contracts remain independent.
    if (plan.compositionBehavior == CompositionBehavior::Club && section->startBar == 0 &&
        std::any_of(plan.narrativeSpine.acts.begin(), plan.narrativeSpine.acts.end(),
            [&](const auto& act) { return act.sectionName == section->name && act.stage == NarrativeStage::Premise; }))
        return 0;
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

std::vector<HarmonicPhraseGap> HarmonicFloorContext::phraseGaps(
    const SongPlan& plan, const Pattern& rendered, std::size_t limit) {
    std::vector<HarmonicPhraseGap> candidates;
    if (plan.beatsPerBar <= 0.0 || limit == 0) return candidates;
    for (std::size_t ownerIndex = 0; ownerIndex < plan.instruments.size(); ++ownerIndex) {
        const auto& owner = plan.instruments[ownerIndex];
        if (owner.role.find("primary_chord_bed") == std::string::npos ||
            (owner.sourceVoice != VoiceId::HarmonicFoundation &&
             owner.sourceVoice != VoiceId::HarmonicUpper)) continue;
        for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
            const auto& section = plan.sections[sectionIndex];
            if (section.bars < 4 ||
                (!owner.activeSections.empty() &&
                 std::find(owner.activeSections.begin(), owner.activeSections.end(),
                           section.name) == owner.activeSections.end())) continue;
            const auto identity = lower(section.name + " " + section.function + " " +
                                        section.motifTreatment);
            if (containsBreathMarker(identity)) continue;
            auto best = HarmonicPhraseGap{};
            for (int firstBar = 0; firstBar + 4 <= section.bars; firstBar += 4) {
                int sustained = 0, supported = 0;
                for (int bar = 0; bar < 4; ++bar) {
                    const auto start = (section.startBar + firstBar + bar) * plan.beatsPerBar;
                    const auto end = start + plan.beatsPerBar;
                    bool ownerSounds = false, accompanimentSounds = false;
                    for (const auto& note : rendered.notes) {
                        const auto overlap = std::min(note.endBeat(), end) -
                            std::max(note.startBeat, start);
                        if (overlap <= .001 || note.partId == 0) continue;
                        if (note.partId == ownerIndex + 1 &&
                            overlap + .001 >= std::min(2.0, plan.beatsPerBar * .5))
                            ownerSounds = true;
                        else if (note.partId != ownerIndex + 1 &&
                                 note.partId <= plan.instruments.size()) {
                            const auto voice = plan.instruments[note.partId - 1].sourceVoice;
                            if (voice == VoiceId::HarmonicFoundation ||
                                voice == VoiceId::HarmonicUpper ||
                                voice == VoiceId::HarmonicPulse)
                                accompanimentSounds = true;
                        }
                    }
                    sustained += ownerSounds ? 1 : 0;
                    supported += accompanimentSounds ? 1 : 0;
                }
                // A whole-window withdrawal may be intentional. A chord bed
                // that states one chord and abandons three changes while the
                // pulse continues is the specific incomplete musical promise.
                if (sustained != 1 || supported < 3) continue;
                if (best.bars == 0 || firstBar < best.firstBar)
                    best = {ownerIndex, sectionIndex, firstBar, 4, sustained};
            }
            if (best.bars != 0) candidates.push_back(best);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [&](const auto& a, const auto& b) {
            return (plan.sections[a.sectionIndex].startBar + a.firstBar) <
                   (plan.sections[b.sectionIndex].startBar + b.firstBar);
        });
    if (candidates.size() > limit) candidates.resize(limit);
    return candidates;
}

} // namespace pulso
