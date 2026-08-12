#include "HarmonyEngine.h"

#include "SongComposer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace pulso {
namespace {

int nearest(int pitchClass, int target, int minimum, int maximum) noexcept {
    auto result = std::clamp(target, minimum, maximum);
    auto best = std::numeric_limits<int>::max();
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto distance = std::abs(pitch - target);
        if (distance < best) { best = distance; result = pitch; }
    }
    return result;
}

const HarmonicChord& chordFor(const SongPlan& plan, const std::string& id) {
    const auto found = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                                    [&](const auto& chord) { return chord.id == id; });
    return found == plan.chordPalette.end() ? plan.chordPalette.front() : *found;
}

std::array<int, 4> leadVoices(const HarmonicChord& chord,
                              const std::array<int, 4>& previous,
                              double smoothness) noexcept {
    std::array<int, 4> result{};
    const auto voiceCount = std::clamp(chord.voicing == VoicingStrategy::Shell ? 3
                                       : static_cast<int>(chord.pitchClasses.size()), 2, 4);
    for (auto voice = 0; voice < voiceCount; ++voice) {
        const auto minimum = 43 + voice * 6;
        const auto maximum = 69 + voice * 6;
        auto bestPitch = minimum;
        auto bestCost = std::numeric_limits<double>::max();
        for (const auto pitchClass : chord.pitchClasses) {
            for (const auto octaveShift : {-12, 0, 12}) {
                const auto candidate = nearest(pitchClass,
                    previous[static_cast<std::size_t>(voice)] + octaveShift, minimum, maximum);
                auto cost = std::abs(candidate - previous[static_cast<std::size_t>(voice)]) *
                            (1.0 + smoothness * 5.0);
                if (voice > 0 && candidate <= result[static_cast<std::size_t>(voice - 1)]) cost += 120.0;
                if (voice > 0) {
                    const auto gap = candidate - result[static_cast<std::size_t>(voice - 1)];
                    if (chord.voicing == VoicingStrategy::Cluster && gap > 5) cost += 28.0;
                    if ((chord.voicing == VoicingStrategy::Open ||
                         chord.voicing == VoicingStrategy::Drop2) && gap < 7) cost += 18.0;
                    if (gap > 16) cost += 30.0;
                }
                for (auto previousVoice = 0; previousVoice < voice; ++previousVoice) {
                    const auto previousPitch = result[static_cast<std::size_t>(previousVoice)];
                    const auto interval = positiveModulo(std::abs(candidate - previousPitch), 12);
                    const auto duplicateClass = interval == 0;
                    const auto semitone = interval == 1 || interval == 11;
                    const auto tritone = interval == 6;
                    if (duplicateClass) cost += chord.voicing == VoicingStrategy::Shell ? 8.0 : 55.0;
                    if (semitone) {
                        const auto intentionalCluster = chord.voicing == VoicingStrategy::Cluster &&
                                                        candidate >= 55 && previousPitch >= 55;
                        cost += intentionalCluster ? 2.0 : 180.0;
                    }
                    if (tritone) {
                        const auto intentionalColour = (chord.voicing == VoicingStrategy::Quartal ||
                                                        chord.voicing == VoicingStrategy::Cluster) &&
                                                       chord.tension >= 0.68 && candidate >= 55 &&
                                                       previousPitch >= 55;
                        cost += intentionalColour ? 5.0 : 90.0;
                    }
                }
                if (chord.voicing == VoicingStrategy::Cluster && candidate < 55) cost += 140.0;
                if (positiveModulo(candidate, 12) == chord.bassPitchClass && voice > 1) cost += 5.0;
                if (cost < bestCost) { bestCost = cost; bestPitch = candidate; }
            }
        }
        if (voice > 0)
            while (bestPitch <= result[static_cast<std::size_t>(voice - 1)] && bestPitch + 12 <= maximum)
                bestPitch += 12;
        result[static_cast<std::size_t>(voice)] = bestPitch;
    }
    if (chord.voicing == VoicingStrategy::Drop2 && voiceCount == 4) {
        result[1] = nearest(positiveModulo(result[1], 12), result[1] - 12, 40, 64);
        std::sort(result.begin(), result.begin() + voiceCount);
    }
    for (auto voice = voiceCount; voice < 4; ++voice)
        result[static_cast<std::size_t>(voice)] = result[static_cast<std::size_t>(voiceCount - 1)];
    return result;
}

HarmonicMoment makeMoment(const SongPlan& plan, const HarmonicChord& chord,
                          const HarmonicEvent& event, const BarDirection& direction,
                          HarmonyState& state) {
    HarmonicMoment moment;
    moment.chordId = chord.id;
    moment.label = chord.label;
    moment.rootPitchClass = chord.rootPitchClass;
    moment.bassPitchClass = chord.bassPitchClass;
    moment.pitchClasses = chord.pitchClasses;
    moment.voiceCount = std::clamp(chord.voicing == VoicingStrategy::Shell ? 3
                                   : static_cast<int>(chord.pitchClasses.size()), 2, 4);
    moment.beatOffset = event.beatOffset;
    moment.tension = std::clamp(chord.tension * 0.72 + direction.intensity * 0.28, 0.0, 1.0);
    moment.emphasis = event.emphasis;
    moment.function = chord.function;
    moment.voicingStrategy = chord.voicing;
    moment.cadence = direction.motifTransformation == MotifTransformation::Cadence ||
                     (direction.arrival && (chord.function == HarmonicFunction::Tonic ||
                                            chord.function == HarmonicFunction::Dominant));
    moment.voicing = leadVoices(chord, state.previousVoicing,
                                plan.harmonicLanguage.voiceLeadingSmoothness);
    state.previousVoicing = moment.voicing;
    return moment;
}

int velocity(double base, double energy, double accent = 0.0) noexcept {
    return std::clamp(static_cast<int>(std::lround(base + energy * 28.0 + accent)), 1, 127);
}

} // namespace

HarmonicTimeline HarmonyEngine::composeSection(const SongPlan& plan, const SongSection& section,
                                                const std::vector<BarDirection>& directions,
                                                HarmonyState& state) {
    HarmonicTimeline result(static_cast<std::size_t>(section.bars));
    auto currentChordId = section.harmonicEvents.empty() ? plan.chordPalette.front().id
                                                         : section.harmonicEvents.front().chordId;
    for (auto bar = 0; bar < section.bars; ++bar) {
        auto& moments = result[static_cast<std::size_t>(bar)];
        std::vector<HarmonicEvent> events;
        std::copy_if(section.harmonicEvents.begin(), section.harmonicEvents.end(),
                     std::back_inserter(events), [bar](const auto& event) { return event.barOffset == bar; });
        std::sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
            return left.beatOffset < right.beatOffset;
        });
        if (events.empty() || events.front().beatOffset > 0.001)
            events.insert(events.begin(), HarmonicEvent{bar, 0.0, currentChordId, 0.35, "Carry harmonic memory"});
        for (std::size_t index = 0; index < events.size(); ++index) {
            currentChordId = events[index].chordId;
            auto moment = makeMoment(plan, chordFor(plan, currentChordId), events[index],
                                     directions[static_cast<std::size_t>(bar)], state);
            const auto end = index + 1 < events.size() ? events[index + 1].beatOffset : plan.beatsPerBar;
            moment.durationBeats = std::max(0.08, end - moment.beatOffset);
            moments.push_back(std::move(moment));
        }
    }
    return result;
}

void HarmonyEngine::renderBar(Pattern& chunk, const SongPlan& plan, const SongSection& section,
                              const BarDirection& direction,
                              const std::vector<HarmonicMoment>& moments,
                              int chunkBar, int remainingChunkBars) {
    if (moments.empty()) return;
    const auto barStart = chunkBar * plan.beatsPerBar;
    const auto foundationBudget = direction.forVoice(VoiceId::HarmonicFoundation).maximumOnsets;
    if (foundationBudget > 0) {
        auto remaining = foundationBudget;
        for (std::size_t index = 0; index < moments.size() && remaining > 0; ++index) {
            const auto& harmony = moments[index];
            if (index == 0 && !direction.harmonicAttack && moments.size() == 1) continue;
            const auto voices = std::min({harmony.voiceCount, remaining, 4});
            const auto duration = std::max(0.10, std::min(harmony.durationBeats - 0.04,
                remainingChunkBars * plan.beatsPerBar - harmony.beatOffset - 0.03));
            for (auto voice = 0; voice < voices; ++voice)
                chunk.notes.push_back({barStart + harmony.beatOffset, duration,
                    harmony.voicing[static_cast<std::size_t>(voice)],
                    velocity(46.0, section.energy, harmony.emphasis * 9.0 - voice * 1.5),
                    voiceDefinition(VoiceId::HarmonicFoundation).midiChannel,
                    VoiceId::HarmonicFoundation});
            remaining -= voices;
        }
    }

    const auto& harmony = moments.front();
    const auto pulseBudget = direction.forVoice(VoiceId::HarmonicPulse).maximumOnsets;
    if (pulseBudget > 0) {
        const auto pulseCount = std::clamp(pulseBudget / 2, 1, 4);
        for (auto pulse = 0; pulse < pulseCount; ++pulse) {
            const auto onset = std::min((1.5 + pulse * 2.0) * plan.beatsPerBar / 8.0,
                                        plan.beatsPerBar - 0.18);
            const auto& activeMoment = *std::prev(std::upper_bound(moments.begin(), moments.end(), onset,
                [](double beat, const auto& moment) { return beat < moment.beatOffset; }));
            for (auto chordVoice = 0; chordVoice < 2; ++chordVoice) {
                const auto source = activeMoment.voicing[static_cast<std::size_t>(
                    positiveModulo(chordVoice + direction.phraseIndex, activeMoment.voiceCount))];
                chunk.notes.push_back({barStart + onset, plan.beatsPerBar / 8.0 * 0.58,
                    nearest(positiveModulo(source, 12), source + 12, 50, 84),
                    velocity(53.0, section.energy, activeMoment.emphasis * 6.0),
                    voiceDefinition(VoiceId::HarmonicPulse).midiChannel, VoiceId::HarmonicPulse});
            }
        }
    }

    if (direction.forVoice(VoiceId::HarmonicUpper).maximumOnsets > 0) {
        const auto& colourMoment = moments.back();
        const auto colour = colourMoment.pitchClasses.back();
        const auto start = barStart + std::max(colourMoment.beatOffset, plan.beatsPerBar * 0.125);
        chunk.notes.push_back({start,
            std::max(0.10, std::min(colourMoment.durationBeats * (direction.arrival ? 0.90 : 1.0),
                                   remainingChunkBars * plan.beatsPerBar - (start - barStart) - 0.03)),
            nearest(colour, 79, 67, 96), velocity(38.0, section.energy, colourMoment.tension * 8.0),
            voiceDefinition(VoiceId::HarmonicUpper).midiChannel, VoiceId::HarmonicUpper});
    }

    if (direction.forVoice(VoiceId::Atmosphere).maximumOnsets > 0) {
        const auto pitchClass = harmony.function == HarmonicFunction::Pedal
            ? harmony.bassPitchClass : harmony.rootPitchClass;
        chunk.notes.push_back({barStart, std::max(0.15, std::min(
            direction.harmonicHoldBars * plan.beatsPerBar - 0.06,
            remainingChunkBars * plan.beatsPerBar - 0.03)), nearest(pitchClass, 55, 43, 72),
            velocity(29.0, section.energy), voiceDefinition(VoiceId::Atmosphere).midiChannel,
            VoiceId::Atmosphere});
    }
}

} // namespace pulso
