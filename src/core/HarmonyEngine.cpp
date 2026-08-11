#include "HarmonyEngine.h"

#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

namespace pulso {
namespace {

constexpr std::array minorSemitones{0, 2, 3, 5, 7, 8, 10};
constexpr std::array majorSemitones{0, 2, 4, 5, 7, 9, 11};
constexpr std::array dorianSemitones{0, 2, 3, 5, 7, 9, 10};
constexpr std::array mixolydianSemitones{0, 2, 4, 5, 7, 9, 10};

int degreePitchClass(const SongPlan& plan, int degree) noexcept {
    const auto index = static_cast<std::size_t>(positiveModulo(degree, 7));
    const auto semitone = plan.scale == ScaleKind::Major ? majorSemitones[index]
        : plan.scale == ScaleKind::Dorian ? dorianSemitones[index]
        : plan.scale == ScaleKind::Mixolydian ? mixolydianSemitones[index]
                                              : minorSemitones[index];
    return positiveModulo(plan.rootPitchClass + semitone, 12);
}

int nearest(int pitchClass, int target, int minimum, int maximum) noexcept {
    auto result = std::clamp(target, minimum, maximum);
    auto best = std::numeric_limits<int>::max();
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != pitchClass) continue;
        const auto distance = std::abs(pitch - target);
        if (distance < best) {
            best = distance;
            result = pitch;
        }
    }
    return result;
}

std::array<int, 4> leadVoices(const HarmonicMoment& moment,
                              const std::array<int, 4>& previous) noexcept {
    std::array<int, 4> result{};
    for (auto voice = 0; voice < moment.voiceCount; ++voice) {
        const auto minimum = 45 + voice * 5;
        const auto maximum = 70 + voice * 5;
        auto bestPitch = minimum;
        auto bestCost = std::numeric_limits<int>::max();
        for (const auto pitchClass : moment.pitchClasses) {
            const auto candidate = nearest(pitchClass, previous[static_cast<std::size_t>(voice)],
                                           minimum, maximum);
            auto cost = std::abs(candidate - previous[static_cast<std::size_t>(voice)]) * 3;
            if (voice > 0 && candidate <= result[static_cast<std::size_t>(voice - 1)]) cost += 80;
            if (voice > 0 && candidate - result[static_cast<std::size_t>(voice - 1)] > 12) cost += 18;
            if (pitchClass == moment.bassPitchClass && voice > 1) cost += 5;
            if (cost < bestCost) {
                bestCost = cost;
                bestPitch = candidate;
            }
        }
        if (voice > 0)
            while (bestPitch <= result[static_cast<std::size_t>(voice - 1)] && bestPitch + 12 <= maximum)
                bestPitch += 12;
        result[static_cast<std::size_t>(voice)] = bestPitch;
    }
    for (auto voice = moment.voiceCount; voice < 4; ++voice)
        result[static_cast<std::size_t>(voice)] = result[static_cast<std::size_t>(moment.voiceCount - 1)];
    return result;
}

int velocity(double base, double energy, double accent = 0.0) noexcept {
    return std::clamp(static_cast<int>(std::lround(base + energy * 28.0 + accent)), 1, 127);
}

bool contains(std::string text, const char* needle) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text.find(needle) != std::string::npos;
}

} // namespace

std::vector<HarmonicMoment> HarmonyEngine::composeSection(
    const SongPlan& plan, const SongSection& section,
    const std::vector<BarDirection>& directions, HarmonyState& state) {
    std::vector<HarmonicMoment> result;
    result.reserve(static_cast<std::size_t>(section.bars));
    for (auto bar = 0; bar < section.bars; ++bar) {
        const auto& direction = directions[static_cast<std::size_t>(bar)];
        auto progressionIndex = direction.harmonicStep + section.motifVariant;
        if (bar + 1 == section.bars && direction.arrival) progressionIndex = 0;
        HarmonicMoment moment;
        moment.degree = plan.chordDegrees[static_cast<std::size_t>(progressionIndex) %
                                          plan.chordDegrees.size()];
        const auto harmonicIntent = section.harmonicDirection + " " + section.function;
        if (direction.arrival && (contains(harmonicIntent, "tonic") ||
                                  contains(harmonicIntent, "resolve") ||
                                  contains(harmonicIntent, "home")))
            moment.degree = 0;
        moment.bassPitchClass = degreePitchClass(plan, moment.degree);
        moment.pitchClasses = {degreePitchClass(plan, moment.degree),
                               degreePitchClass(plan, moment.degree + 2),
                               degreePitchClass(plan, moment.degree + 4)};
        moment.tension = std::clamp(section.tension * 0.72 + direction.phrasePosition * 0.28,
                                    0.0, 1.0);
        moment.cadence = direction.motifTransformation == MotifTransformation::Cadence;
        if (moment.tension > 0.38 || direction.phraseFunction == PhraseFunction::Develop) {
            moment.pitchClasses.push_back(degreePitchClass(plan, moment.degree + 6));
            moment.voiceCount = 4;
        }
        if (moment.tension > 0.76 && !moment.cadence)
            moment.pitchClasses.push_back(degreePitchClass(plan, moment.degree + 8));
        if (!moment.cadence && section.density < 0.46 &&
            (contains(harmonicIntent, "pedal") || contains(harmonicIntent, "ambigu")))
            moment.bassPitchClass = plan.rootPitchClass;
        moment.voicing = leadVoices(moment, state.previousVoicing);
        state.previousVoicing = moment.voicing;
        result.push_back(std::move(moment));
    }
    return result;
}

void HarmonyEngine::renderBar(Pattern& chunk, const SongPlan& plan,
                              const SongSection& section, const BarDirection& direction,
                              const HarmonicMoment& harmony, int chunkBar,
                              int remainingChunkBars) {
    const auto barStart = chunkBar * plan.beatsPerBar;
    const auto foundationBudget = direction.forVoice(VoiceId::HarmonicFoundation).maximumOnsets;
    if (foundationBudget > 0) {
        const auto voices = std::min(harmony.voiceCount, foundationBudget);
        const auto duration = std::max(0.12, std::min(
            direction.harmonicHoldBars * plan.beatsPerBar - 0.08,
            remainingChunkBars * plan.beatsPerBar - 0.03));
        for (auto voice = 0; voice < voices; ++voice)
            chunk.notes.push_back({barStart, duration,
                harmony.voicing[static_cast<std::size_t>(voice)],
                velocity(48.0, section.energy, voice == 0 ? 5.0 : -voice * 1.5),
                voiceDefinition(VoiceId::HarmonicFoundation).midiChannel,
                VoiceId::HarmonicFoundation});
    }

    const auto pulseBudget = direction.forVoice(VoiceId::HarmonicPulse).maximumOnsets;
    if (pulseBudget > 0) {
        const auto subdivision = plan.beatsPerBar / 8.0;
        const auto seedStep = positiveModulo(direction.phraseIndex * 3 + direction.barInPhrase * 5, 7);
        const auto pulseCount = std::clamp(pulseBudget / 2, 1, 4);
        for (auto pulse = 0; pulse < pulseCount; ++pulse) {
            auto step = positiveModulo(seedStep + pulse * 3, 8);
            if (step == 0) step = 1;
            const auto onset = std::min((step + 0.5) * subdivision, plan.beatsPerBar - 0.18);
            for (auto chordVoice = 0; chordVoice < 2; ++chordVoice) {
                const auto source = harmony.voicing[static_cast<std::size_t>(
                    positiveModulo(chordVoice + direction.phraseIndex, harmony.voiceCount))];
                chunk.notes.push_back({barStart + onset, subdivision * 0.58,
                    nearest(positiveModulo(source, 12), source + 12, 50, 84),
                    velocity(54.0, section.energy, pulse == pulseCount - 1 ? 4.0 : 0.0),
                    voiceDefinition(VoiceId::HarmonicPulse).midiChannel,
                    VoiceId::HarmonicPulse});
            }
        }
    }

    if (direction.forVoice(VoiceId::HarmonicUpper).maximumOnsets > 0) {
        const auto colour = harmony.pitchClasses.back();
        const auto start = barStart + plan.beatsPerBar *
            (direction.phraseFunction == PhraseFunction::Answer ? 0.25 : 0.125);
        chunk.notes.push_back({start,
            std::min(plan.beatsPerBar * (direction.arrival ? 0.92 : 1.45),
                     remainingChunkBars * plan.beatsPerBar - (start - barStart) - 0.03),
            nearest(colour, 79, 67, 96), velocity(39.0, section.energy, harmony.tension * 8.0),
            voiceDefinition(VoiceId::HarmonicUpper).midiChannel, VoiceId::HarmonicUpper});
    }

    if (direction.forVoice(VoiceId::Atmosphere).maximumOnsets > 0) {
        const auto pitchClass = direction.phraseFunction == PhraseFunction::Suspend &&
            harmony.pitchClasses.size() > 3 ? harmony.pitchClasses.back() : harmony.bassPitchClass;
        chunk.notes.push_back({barStart,
            std::max(0.15, std::min(direction.harmonicHoldBars * plan.beatsPerBar - 0.06,
                                    remainingChunkBars * plan.beatsPerBar - 0.03)),
            nearest(pitchClass, 55, 43, 72), velocity(30.0, section.energy),
            voiceDefinition(VoiceId::Atmosphere).midiChannel, VoiceId::Atmosphere});
    }
}

} // namespace pulso
