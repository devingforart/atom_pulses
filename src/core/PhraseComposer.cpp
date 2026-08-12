#include "PhraseComposer.h"

#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pulso {
namespace {

constexpr std::array minorSemitones{0, 2, 3, 5, 7, 8, 10};
constexpr std::array majorSemitones{0, 2, 4, 5, 7, 9, 11};
constexpr std::array dorianSemitones{0, 2, 3, 5, 7, 9, 10};
constexpr std::array mixolydianSemitones{0, 2, 4, 5, 7, 9, 10};

std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

const std::array<int, 7>& scaleSemitones(ScaleKind scale) noexcept {
    if (scale == ScaleKind::Major) return majorSemitones;
    if (scale == ScaleKind::Dorian) return dorianSemitones;
    if (scale == ScaleKind::Mixolydian) return mixolydianSemitones;
    return minorSemitones;
}

int scalePitchClass(const SongPlan& plan, int degree) noexcept {
    const auto octave = static_cast<int>(std::floor(static_cast<double>(degree) / 7.0));
    const auto index = positiveModulo(degree, 7);
    return plan.rootPitchClass + scaleSemitones(plan.scale)[static_cast<std::size_t>(index)] + octave * 12;
}

int nearestPitchClass(int pitchClass, int target, int minimum, int maximum) noexcept {
    auto result = std::clamp(target, minimum, maximum);
    auto best = std::numeric_limits<int>::max();
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto cost = std::abs(pitch - target);
        if (cost < best) {
            best = cost;
            result = pitch;
        }
    }
    return result;
}

int degreeForInterval(const SongPlan& plan, int interval) noexcept {
    auto bestDegree = 0;
    auto bestDistance = std::numeric_limits<int>::max();
    for (auto degree = -7; degree <= 14; ++degree) {
        const auto candidate = scalePitchClass(plan, degree) - plan.rootPitchClass;
        const auto distance = std::abs(candidate - interval);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestDegree = degree;
        }
    }
    return bestDegree;
}

int transformedDegree(const SongPlan& plan, int interval, MotifTransformation transformation,
                      int ordinal, int phraseIndex) noexcept {
    auto degree = degreeForInterval(plan, interval);
    switch (transformation) {
        case MotifTransformation::Original: break;
        case MotifTransformation::Fragment: degree = ordinal < 2 ? degree : degree / 2; break;
        case MotifTransformation::Sequence: degree += phraseIndex % 3 - 1; break;
        case MotifTransformation::Invert: degree = -degree; break;
        case MotifTransformation::Augment: degree = ordinal % 2 == 0 ? degree : degree / 2; break;
        case MotifTransformation::Displace: degree += ordinal % 2 == 0 ? 1 : -1; break;
        case MotifTransformation::Cadence: degree = ordinal == 0 ? 1 : 0; break;
    }
    return degree;
}

std::vector<double> composeOnsets(const SongPlan& plan, const SongSection& section,
                                  const BarDirection& direction, VoiceId voice,
                                  int absoluteBar, int maximum) {
    std::vector<double> result;
    if (maximum <= 0 || direction.fullBreath) return result;
    auto target = maximum;
    if (direction.phraseFunction == PhraseFunction::Suspend) target = std::min(target, 1);
    else if (direction.motifTransformation == MotifTransformation::Fragment) target = std::min(target, 2);
    else if (direction.phraseFunction == PhraseFunction::Answer) target = std::min(target, 3);
    const auto subdivision = plan.beatsPerBar / 8.0;
    const auto identity = mix(plan.seed ^ static_cast<std::uint64_t>(absoluteBar + 1) *
        0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(voice) * 0xd1b54a32d192ed03ULL ^
        static_cast<std::uint64_t>(direction.phraseIndex + 1));
    std::array<bool, 8> selected{};
    auto step = static_cast<int>((identity >> 8) % 5);
    if (direction.phraseFunction == PhraseFunction::Establish) step = 0;
    if (voice == VoiceId::Countermelody) step = std::max(1, step);
    for (auto ordinal = 0; ordinal < target; ++ordinal) {
        const auto stride = 2 + static_cast<int>((identity >> (ordinal * 5 % 48)) % 3);
        step = positiveModulo(step + (ordinal == 0 ? 0 : stride), 8);
        if (selected[static_cast<std::size_t>(step)]) step = positiveModulo(step + 1, 8);
        selected[static_cast<std::size_t>(step)] = true;
        auto onset = step * subdivision;
        if (direction.motifTransformation == MotifTransformation::Displace)
            onset += subdivision * 0.5;
        if (direction.arrival && ordinal + 1 == target)
            onset = std::min(plan.beatsPerBar - subdivision, 6.0 * subdivision);
        if (onset + 0.001 >= direction.forVoice(voice).entryBeat &&
            onset < direction.forVoice(voice).exitBeat - 0.08)
            result.push_back(onset);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (section.density < 0.34 && result.size() > 1) result.resize(result.size() - 1);
    return result;
}

double articulationRatio(const BarDirection& direction, int ordinal) noexcept {
    if (direction.motifTransformation == MotifTransformation::Augment) return 0.93;
    if (direction.motifTransformation == MotifTransformation::Fragment) return 0.48;
    if (direction.phraseFunction == PhraseFunction::Question) return ordinal % 2 == 0 ? 0.62 : 0.82;
    if (direction.arrival) return ordinal == 0 ? 0.72 : 0.95;
    return ordinal % 3 == 2 ? 0.58 : 0.78;
}

int phraseVelocity(const SongSection& section, const BarDirection& direction,
                   int ordinal, int count) noexcept {
    const auto targetAccent = count > 1 && ordinal + 1 == count && direction.arrival ? 9.0 : 0.0;
    const auto contour = std::sin((ordinal + 1.0) / (count + 1.0) * 3.14159265358979323846) * 7.0;
    return std::clamp(static_cast<int>(std::lround(51.0 + section.energy * 35.0 +
                                                   direction.intensity * 8.0 + contour + targetAccent)),
                      1, 127);
}

bool active(const SongSection& section, VoiceId voice) {
    return std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) !=
           section.activeVoices.end();
}

const HarmonicMoment& momentAt(const std::vector<HarmonicMoment>& moments, double beat) {
    auto found = std::upper_bound(moments.begin(), moments.end(), beat,
        [](double candidate, const auto& moment) { return candidate < moment.beatOffset; });
    return found == moments.begin() ? moments.front() : *std::prev(found);
}

void renderMelodyVoice(Pattern& chunk, const SongPlan& plan, const SongSection& section,
                       const BarDirection& direction, const std::vector<HarmonicMoment>& moments,
                       VoiceId voice, int absoluteBar, int chunkBar,
                       PhrasePerformanceState& state) {
    const auto& instruction = direction.forVoice(voice);
    if (instruction.maximumOnsets <= 0 || !active(section, voice)) return;
    const auto onsets = composeOnsets(plan, section, direction, voice, absoluteBar,
                                      instruction.maximumOnsets);
    if (onsets.empty()) return;
    const auto voiceIndex = static_cast<std::size_t>(voice);
    auto& previous = state.previousPitch[voiceIndex];
    auto& cursor = state.motifCursor[voiceIndex];
    const auto& definition = voiceDefinition(voice);
    const auto registerCentre = voice == VoiceId::Lead ? 72 : 64;
    for (std::size_t ordinal = 0; ordinal < onsets.size(); ++ordinal) {
        const auto& harmony = momentAt(moments, onsets[ordinal]);
        const auto motifIndex = voice == VoiceId::Lead ? cursor++ : cursor++ + 2;
        const auto sourceInterval = plan.motifIntervals[motifIndex % plan.motifIntervals.size()];
        auto degree = transformedDegree(plan, sourceInterval, direction.motifTransformation,
                                        static_cast<int>(ordinal), direction.phraseIndex);
        if (voice == VoiceId::Countermelody) degree = 4 - degree;
        auto pitchClass = positiveModulo(scalePitchClass(plan, degree), 12);
        const auto strong = onsets[ordinal] < 0.01 ||
            std::abs(std::fmod(onsets[ordinal], 1.0)) < 0.01 || direction.arrival;
        if (strong) {
            const auto chordIndex = static_cast<std::size_t>(positiveModulo(
                static_cast<int>(ordinal) + direction.phraseIndex, harmony.voiceCount));
            pitchClass = harmony.pitchClasses[chordIndex];
        }
        if (direction.motifTransformation == MotifTransformation::Cadence && ordinal + 1 == onsets.size())
            pitchClass = harmony.bassPitchClass;
        const auto contour = static_cast<int>(std::lround(
            std::sin(direction.phrasePosition * 3.14159265358979323846) *
            (voice == VoiceId::Lead ? 6.0 : -4.0)));
        const auto target = previous == 0 ? registerCentre + contour : previous + std::clamp(degree, -4, 4);
        auto pitch = nearestPitchClass(pitchClass, target, definition.minimumPitch,
                                       definition.maximumPitch);
        if (previous != 0 && std::abs(pitch - previous) > 9)
            pitch = nearestPitchClass(pitchClass, previous, definition.minimumPitch,
                                      definition.maximumPitch);
        const auto next = ordinal + 1 < onsets.size() ? onsets[ordinal + 1] : instruction.exitBeat;
        const auto available = std::max(0.10, next - onsets[ordinal]);
        const auto duration = std::max(0.10, std::min(available * articulationRatio(
            direction, static_cast<int>(ordinal)), instruction.exitBeat - onsets[ordinal] - 0.03));
        chunk.notes.push_back({chunkBar * plan.beatsPerBar + onsets[ordinal], duration, pitch,
            phraseVelocity(section, direction, static_cast<int>(ordinal), static_cast<int>(onsets.size())),
            definition.midiChannel, voice});
        previous = pitch;
    }
}

} // namespace

PhrasePerformanceState::PhrasePerformanceState() noexcept {
    previousPitch[static_cast<std::size_t>(VoiceId::Lead)] = 72;
    previousPitch[static_cast<std::size_t>(VoiceId::Countermelody)] = 64;
    previousPitch[static_cast<std::size_t>(VoiceId::SubBass)] = 36;
    previousPitch[static_cast<std::size_t>(VoiceId::MovementBass)] = 48;
}

void PhraseComposer::renderMelodicVoices(Pattern& chunk, const SongPlan& plan,
                                         const SongSection& section,
                                         const std::vector<BarDirection>& directions,
                                         const HarmonicTimeline& harmony,
                                         int sectionBar, int chunkBars,
                                         PhrasePerformanceState& state) {
    for (auto bar = 0; bar < chunkBars; ++bar) {
        const auto localBar = sectionBar + bar;
        const auto& direction = directions[static_cast<std::size_t>(localBar)];
        const auto& moment = harmony[static_cast<std::size_t>(localBar)];
        if (direction.foreground == VoiceId::Lead)
            renderMelodyVoice(chunk, plan, section, direction, moment, VoiceId::Lead,
                              section.startBar + localBar, bar, state);
        else if (direction.foreground == VoiceId::Countermelody)
            renderMelodyVoice(chunk, plan, section, direction, moment, VoiceId::Countermelody,
                              section.startBar + localBar, bar, state);
    }
}

void PhraseComposer::renderBassVoices(Pattern& chunk, const SongPlan& plan,
                                      const SongSection& section,
                                      const std::vector<BarDirection>& directions,
                                      const HarmonicTimeline& harmony,
                                      int sectionBar, int chunkBars,
                                      PhrasePerformanceState& state) {
    chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [](const auto& note) {
        return note.voice == VoiceId::SubBass || note.voice == VoiceId::MovementBass;
    }), chunk.notes.end());
    for (auto bar = 0; bar < chunkBars; ++bar) {
        const auto localBar = sectionBar + bar;
        const auto& direction = directions[static_cast<std::size_t>(localBar)];
        const auto& moments = harmony[static_cast<std::size_t>(localBar)];
        if (moments.empty()) continue;
        const auto barStart = bar * plan.beatsPerBar;
        const auto subBudget = direction.forVoice(VoiceId::SubBass).maximumOnsets;
        if (subBudget > 0 && active(section, VoiceId::SubBass)) {
            const auto& definition = voiceDefinition(VoiceId::SubBass);
            auto& previous = state.previousPitch[static_cast<std::size_t>(VoiceId::SubBass)];
            const auto count = std::clamp(subBudget, 1, direction.breath ? 1 : 2);
            for (auto onset = 0; onset < count; ++onset) {
                const auto beat = onset < static_cast<int>(moments.size())
                    ? moments[static_cast<std::size_t>(onset)].beatOffset
                    : (onset == 0 ? 0.0 : plan.beatsPerBar * 0.5);
                const auto& moment = momentAt(moments, beat);
                const auto root = nearestPitchClass(moment.bassPitchClass, previous,
                                                    definition.minimumPitch, definition.maximumPitch);
                const auto end = onset + 1 < count && onset + 1 < static_cast<int>(moments.size())
                    ? moments[static_cast<std::size_t>(onset + 1)].beatOffset
                    : direction.forVoice(VoiceId::SubBass).exitBeat;
                chunk.notes.push_back({barStart + beat, std::max(0.18, end - beat - 0.08), root,
                    std::clamp(static_cast<int>(67 + section.energy * 25 + (onset == 0 ? 5 : -3)), 1, 127),
                    definition.midiChannel, VoiceId::SubBass});
                previous = root;
            }
        }

        const auto movementBudget = direction.forVoice(VoiceId::MovementBass).maximumOnsets;
        if (movementBudget > 0 && active(section, VoiceId::MovementBass)) {
            const auto& definition = voiceDefinition(VoiceId::MovementBass);
            auto& previous = state.previousPitch[static_cast<std::size_t>(VoiceId::MovementBass)];
            const auto count = std::clamp(movementBudget, 1, 3);
            const auto identity = mix(plan.seed ^ static_cast<std::uint64_t>(section.startBar + localBar + 1));
            for (auto ordinal = 0; ordinal < count; ++ordinal) {
                const auto step = positiveModulo(3 + static_cast<int>(identity % 3) + ordinal * 3, 8);
                const auto beat = step * plan.beatsPerBar / 8.0;
                if (beat >= direction.forVoice(VoiceId::MovementBass).exitBeat - 0.12) continue;
                const auto& moment = momentAt(moments, beat);
                auto pitchClass = ordinal == 0 && moment.pitchClasses.size() > 2
                    ? moment.pitchClasses[2] : moment.pitchClasses[static_cast<std::size_t>(
                        positiveModulo(ordinal + direction.phraseIndex, moment.voiceCount))];
                if (direction.arrival && ordinal + 1 == count) pitchClass = moment.bassPitchClass;
                const auto target = previous + (ordinal % 2 == 0 ? 2 : -2);
                const auto pitch = nearestPitchClass(pitchClass, target,
                                                     definition.minimumPitch, definition.maximumPitch);
                chunk.notes.push_back({barStart + beat, plan.beatsPerBar / 8.0 *
                    (direction.motifTransformation == MotifTransformation::Augment ? 1.35 : 0.68),
                    pitch, std::clamp(static_cast<int>(58 + section.energy * 30 + ordinal * 2), 1, 127),
                    definition.midiChannel, VoiceId::MovementBass});
                previous = pitch;
            }
        }
    }
}

} // namespace pulso
