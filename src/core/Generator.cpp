#include "Generator.h"

#include "Random.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pulso {
namespace {

struct MotifCell {
    int step{};
    double duration{};
    int harmonicIndex{};
    int velocityOffset{};
};

double clampUnit(double value) { return std::clamp(value, 0.0, 1.0); }

std::vector<int> normalizeHarmonyPreservingRoot(std::span<const int> pitches) {
    std::vector<int> result;
    result.reserve(pitches.size());
    for (const auto pitch : pitches) {
        const auto pitchClass = positiveModulo(pitch, 12);
        if (std::find(result.begin(), result.end(), pitchClass) == result.end())
            result.push_back(pitchClass);
    }
    return result;
}

std::vector<int> fallbackHarmony(const GenerationContext& context) {
    auto chord = normalizeHarmonyPreservingRoot(context.chordPitchClasses);
    if (!chord.empty()) return chord;

    if (context.scale == ScaleKind::Chromatic) {
        const std::array harmony{context.rootPitchClass,
                                 positiveModulo(context.rootPitchClass + 4, 12),
                                 positiveModulo(context.rootPitchClass + 7, 12)};
        return normalizeHarmonyPreservingRoot(harmony);
    }

    const auto intervals = intervalsFor(context.scale);
    const auto thirdIndex = std::min<std::size_t>(2, intervals.size() - 1);
    const auto fifthIndex = std::min<std::size_t>(4, intervals.size() - 1);
    const std::array harmony{
        context.rootPitchClass,
        positiveModulo(context.rootPitchClass + intervals[thirdIndex], 12),
        positiveModulo(context.rootPitchClass + intervals[fifthIndex], 12)};
    return normalizeHarmonyPreservingRoot(harmony);
}

std::vector<int> harmonyForBar(const GenerationContext& context, int bar) {
    if (bar >= 0 && bar < static_cast<int>(context.harmonyByBar.size())) {
        auto harmony = normalizeHarmonyPreservingRoot(context.harmonyByBar[static_cast<std::size_t>(bar)]);
        if (!harmony.empty()) return harmony;
    }
    return fallbackHarmony(context);
}

int nearestPitchClassTo(int pitchClass, int reference, int minimum, int maximum) {
    auto best = std::clamp(reference, minimum, maximum);
    auto bestDistance = std::numeric_limits<int>::max();
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto distance = std::abs(pitch - reference);
        if (distance < bestDistance) {
            best = pitch;
            bestDistance = distance;
        }
    }
    return best;
}

int nearestChordToneTo(std::span<const int> chord, int reference, int minimum, int maximum) {
    auto best = std::clamp(reference, minimum, maximum);
    auto bestDistance = std::numeric_limits<int>::max();
    for (const auto pitchClass : chord) {
        const auto candidate = nearestPitchClassTo(pitchClass, reference, minimum, maximum);
        const auto distance = std::abs(candidate - reference);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

int scaleDegreePitch(const GenerationContext& context, int degree, int basePitch = 60) {
    const auto intervals = intervalsFor(context.scale);
    const auto scaleSize = static_cast<int>(intervals.size());
    const auto rootNearBase = nearestPitchClassTo(context.rootPitchClass, basePitch,
                                                  basePitch - 6, basePitch + 6);
    if (scaleSize == 12) return rootNearBase + degree;
    const auto octave = static_cast<int>(std::floor(static_cast<double>(degree) / scaleSize));
    const auto normalizedDegree = positiveModulo(degree, scaleSize);
    return rootNearBase + intervals[static_cast<std::size_t>(normalizedDegree)] + octave * 12;
}

int scaleApproachBelow(const GenerationContext& context, int targetPitchClass) {
    for (auto distance = 1; distance <= 4; ++distance) {
        const auto candidate = positiveModulo(targetPitchClass - distance, 12);
        if (isPitchClassInScale(candidate, context.rootPitchClass, context.scale)) return candidate;
    }
    return positiveModulo(targetPitchClass - 1, 12);
}

bool sourceHasOnsetNear(const GenerationContext& context, double beat, double tolerance = 0.13) {
    return std::any_of(context.sourceNotes.begin(), context.sourceNotes.end(), [=](const auto& note) {
        return std::abs(note.beat - beat) <= tolerance;
    });
}

double phraseProgress(int bar, int bars) {
    return bars <= 1 ? 0.0 : static_cast<double>(bar) / static_cast<double>(bars - 1);
}

void normalizePattern(Pattern& pattern) {
    for (auto& note : pattern.notes) {
        note.startBeat = std::clamp(note.startBeat, 0.0, pattern.lengthBeats - 0.001);
        note.durationBeats = std::clamp(note.durationBeats, 0.03,
                                        pattern.lengthBeats - note.startBeat);
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.channel = std::clamp(note.channel, 1, 16);
    }
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.pitch < b.pitch;
    });
}

std::vector<MotifCell> makeBassMotif(const GenerationContext& context) {
    Random random(context.seed ^ 0xBA5511ULL);
    const auto beats = std::max(1, static_cast<int>(std::round(context.beatsPerBar)));
    const auto totalSteps = beats * 4;
    const auto complexity = clampUnit(context.complexity);
    const auto space = clampUnit(context.space);
    const auto follow = clampUnit(context.follow);
    std::vector<MotifCell> motif;

    for (auto step = 0; step < totalSteps; ++step) {
        const auto beat = static_cast<double>(step) * 0.25;
        const auto onBeat = step % 4 == 0;
        const auto onEighth = step % 2 == 0;
        const auto downbeat = step == 0;
        auto probability = downbeat ? 1.0 : (onBeat ? 0.64 : (onEighth ? 0.38 : 0.08));
        probability += complexity * (onEighth ? 0.18 : 0.30);
        probability -= space * (downbeat ? 0.0 : 0.52);
        if (sourceHasOnsetNear(context, beat)) probability += follow * 0.28;
        if (!random.chance(probability)) continue;

        const auto harmonicIndex = downbeat ? 0 : random.range(0, 2);
        const auto duration = onBeat && random.chance(0.45 + follow * 0.30) ? 0.46 :
                              (onEighth ? 0.23 : 0.115);
        motif.push_back({step, duration, harmonicIndex, random.range(-6, 6)});
    }
    if (motif.empty() || motif.front().step != 0) motif.insert(motif.begin(), {0, 0.46, 0, 0});
    return motif;
}

std::vector<MotifCell> makeMelodyMotif(const GenerationContext& context) {
    Random random(context.seed ^ 0xC0A17E2ULL);
    const auto beats = std::max(1, static_cast<int>(std::round(context.beatsPerBar)));
    const auto slots = beats * 2;
    const auto complexity = clampUnit(context.complexity);
    const auto space = clampUnit(context.space);
    const auto follow = clampUnit(context.follow);
    std::vector<MotifCell> motif;
    auto degree = 2;

    for (auto slot = 0; slot < slots; ++slot) {
        const auto beat = static_cast<double>(slot) * 0.5;
        const auto strong = slot % 2 == 0;
        auto probability = (strong ? 0.63 : 0.42) + complexity * 0.16 - space * 0.48;
        if (sourceHasOnsetNear(context, beat, 0.20)) probability -= follow * 0.42;
        if (!random.chance(probability)) continue;

        if (!motif.empty()) {
            const std::array moves{-2, -1, -1, 0, 1, 1, 2};
            degree += moves[static_cast<std::size_t>(random.range(0, static_cast<int>(moves.size()) - 1))];
            degree = std::clamp(degree, -2, 9);
        }
        motif.push_back({slot * 2, random.chance(0.58) ? 0.44 : 0.94, degree,
                         random.range(-8, 8)});
    }
    if (motif.empty() || motif.front().step != 0) motif.insert(motif.begin(), {0, 0.94, 2, 0});
    return motif;
}

} // namespace

Pattern Generator::generate(const GenerationContext& context) const {
    Pattern result;
    switch (context.role) {
    case Role::Bass: result = generateBass(context); break;
    case Role::Percussion: result = generatePercussion(context); break;
    case Role::Countermelody: result = generateCountermelody(context); break;
    }
    normalizePattern(result);
    return result;
}

Pattern Generator::generateBass(const GenerationContext& context) const {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    const auto motif = makeBassMotif(context);
    const auto repetition = clampUnit(context.repetition);
    const auto development = clampUnit(context.development);
    const auto risk = clampUnit(context.risk);
    auto previousPitch = pitchClassToMidi(harmonyForBar(context, 0).front(), 2, 28, 52);

    for (auto bar = 0; bar < bars; ++bar) {
        const auto chord = harmonyForBar(context, bar);
        const auto progress = phraseProgress(bar, bars);
        Random barRandom(context.seed ^ (static_cast<std::uint64_t>(bar + 1) * 0x9e3779b97f4a7c15ULL) ^
                         (context.evolutionStep * 0xD1B54A32D192ED03ULL));
        const auto variation = (1.0 - repetition) * 0.55 + development * progress * 0.20 +
                               std::min(0.18, static_cast<double>(context.evolutionStep) * development * 0.025);

        for (const auto& cell : motif) {
            if (cell.step != 0 && barRandom.chance(variation * 0.22)) continue;
            auto step = cell.step;
            if (cell.step != 0 && barRandom.chance(variation * 0.20))
                step = std::clamp(step + (barRandom.chance(0.5) ? 1 : -1), 0,
                                  static_cast<int>(context.beatsPerBar * 4.0) - 1);
            const auto strong = step % 8 == 0;
            auto pitchClass = strong ? chord.front() :
                chord[static_cast<std::size_t>(cell.harmonicIndex % static_cast<int>(chord.size()))];

            if (!strong && risk > 0.58 && barRandom.chance((risk - 0.58) * variation)) {
                const auto candidate = pitchClass + (barRandom.chance(0.5) ? 2 : -2);
                pitchClass = positiveModulo(nearestPitchInScale(candidate, context.rootPitchClass,
                                                                 context.scale), 12);
            }
            auto pitch = nearestPitchClassTo(pitchClass, previousPitch, 28, 52);
            if (std::abs(pitch - previousPitch) > 7)
                pitch = nearestChordToneTo(chord, previousPitch, 28, 52);
            const auto start = bar * context.beatsPerBar + static_cast<double>(step) * 0.25;
            const auto velocity = (step == 0 ? 108 : (strong ? 96 : 82)) + cell.velocityOffset;
            result.notes.push_back({start, cell.duration, pitch, velocity, 1});
            previousPitch = pitch;
        }
    }

    if (bars > 1 && development > 0.15) {
        const auto nextRoot = harmonyForBar(context, 0).front();
        const auto approachPc = scaleApproachBelow(context, nextRoot);
        const auto approachBeat = result.lengthBeats - 0.25;
        result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                               return note.startBeat >= approachBeat - 0.001;
                           }), result.notes.end());
        result.notes.push_back({approachBeat, 0.22,
                                nearestPitchClassTo(approachPc, previousPitch, 28, 52), 88, 1});
    }
    return result;
}

Pattern Generator::generatePercussion(const GenerationContext& context) const {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    const auto stepsPerBar = std::max(4, static_cast<int>(std::round(context.beatsPerBar * 4.0)));
    const auto repetition = clampUnit(context.repetition);
    const auto complexity = clampUnit(context.complexity);
    const auto development = clampUnit(context.development);
    const auto space = clampUnit(context.space);

    Random motifRandom(context.seed ^ 0xD12A5ULL);
    std::vector<bool> kick(static_cast<std::size_t>(stepsPerBar), false);
    std::vector<bool> snare(static_cast<std::size_t>(stepsPerBar), false);
    std::vector<bool> hat(static_cast<std::size_t>(stepsPerBar), false);
    kick[0] = true;
    for (auto step = 0; step < stepsPerBar; ++step) {
        if (step % 8 == 4) snare[static_cast<std::size_t>(step)] = true;
        if (step % 2 == 0 && motifRandom.chance(0.92 - space * 0.50))
            hat[static_cast<std::size_t>(step)] = true;
        if (step > 0 && step % 8 == 0) kick[static_cast<std::size_t>(step)] = true;
        if (step > 0 && step % 4 != 0 && motifRandom.chance(complexity * 0.16))
            kick[static_cast<std::size_t>(step)] = true;
        if (step % 2 == 1 && motifRandom.chance(complexity * 0.28 - space * 0.12))
            hat[static_cast<std::size_t>(step)] = true;
    }

    for (auto bar = 0; bar < bars; ++bar) {
        const auto progress = phraseProgress(bar, bars);
        Random barRandom(context.seed ^ (static_cast<std::uint64_t>(bar + 1) * 0x94D049BB133111EBULL) ^
                         (context.evolutionStep * 0x2545F4914F6CDD1DULL));
        const auto variation = (1.0 - repetition) * 0.42 + development * progress * 0.16 +
                               std::min(0.15, static_cast<double>(context.evolutionStep) * development * 0.02);
        const auto finalBar = bar == bars - 1;

        for (auto step = 0; step < stepsPerBar; ++step) {
            const auto beat = bar * context.beatsPerBar + static_cast<double>(step) * 0.25;
            auto playKick = kick[static_cast<std::size_t>(step)];
            auto playSnare = snare[static_cast<std::size_t>(step)];
            auto playHat = hat[static_cast<std::size_t>(step)];
            if (step != 0 && barRandom.chance(variation * 0.12)) playKick = !playKick;
            if (!playSnare && barRandom.chance(variation * complexity * 0.08)) playSnare = true;
            if (barRandom.chance(variation * 0.14)) playHat = !playHat;
            if (playKick) result.notes.push_back({beat, 0.08, 36, step == 0 ? 114 : 96, 10});
            if (playSnare) result.notes.push_back({beat, 0.08, 38, step % 8 == 4 ? 110 : 68, 10});
            if (playHat) {
                const auto open = step == stepsPerBar - 2 && progress > 0.45 &&
                                  barRandom.chance(development * 0.55);
                result.notes.push_back({beat, 0.05, open ? 46 : 42,
                                        68 + (step % 4 == 2 ? 12 : 0) + barRandom.range(-5, 5), 10});
            }
        }

        if (finalBar && bars > 1 && development > 0.18) {
            const auto fillStart = (bar + 1) * context.beatsPerBar - 1.0;
            result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                                   return note.startBeat >= fillStart && note.pitch == 42;
                               }), result.notes.end());
            constexpr std::array fillPitches{45, 47, 50, 38};
            for (std::size_t index = 0; index < fillPitches.size(); ++index)
                result.notes.push_back({fillStart + static_cast<double>(index) * 0.25, 0.08,
                                        fillPitches[index], 82 + static_cast<int>(index) * 8, 10});
        }
    }
    return result;
}

Pattern Generator::generateCountermelody(const GenerationContext& context) const {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    const auto motif = makeMelodyMotif(context);
    const auto repetition = clampUnit(context.repetition);
    const auto development = clampUnit(context.development);
    const auto risk = clampUnit(context.risk);
    auto previousPitch = scaleDegreePitch(context, motif.front().harmonicIndex, 67);
    previousPitch = std::clamp(previousPitch, 55, 88);

    for (auto bar = 0; bar < bars; ++bar) {
        const auto chord = harmonyForBar(context, bar);
        const auto progress = phraseProgress(bar, bars);
        const auto arc = development < 0.2 ? 0 :
                         (progress < 0.34 ? 0 : (progress < 0.72 ? 1 : 0));
        Random barRandom(context.seed ^ (static_cast<std::uint64_t>(bar + 1) * 0xBF58476D1CE4E5B9ULL) ^
                         (context.evolutionStep * 0x9E3779B97F4A7C15ULL));
        const auto variation = (1.0 - repetition) * 0.50 + development * progress * 0.18 +
                               std::min(0.16, static_cast<double>(context.evolutionStep) * development * 0.02);

        for (const auto& cell : motif) {
            const auto localBeat = static_cast<double>(cell.step) * 0.25;
            if (sourceHasOnsetNear(context, localBeat, 0.18) &&
                barRandom.chance(clampUnit(context.follow) * 0.50)) continue;
            if (cell.step != 0 && barRandom.chance(variation * 0.18)) continue;

            auto degree = cell.harmonicIndex + arc;
            if (barRandom.chance(variation * risk * 0.30)) degree += barRandom.chance(0.5) ? 1 : -1;
            auto candidate = scaleDegreePitch(context, degree, 67);
            candidate = nearestPitchClassTo(candidate, previousPitch, 55, 88);
            const auto strong = cell.step % 4 == 0;
            if (strong) candidate = nearestChordToneTo(chord, candidate, 55, 88);
            if (std::abs(candidate - previousPitch) > 7)
                candidate = nearestPitchClassTo(candidate, previousPitch, 55, 88);
            if (std::abs(candidate - previousPitch) > 9)
                candidate = nearestChordToneTo(chord, previousPitch, 55, 88);

            const auto start = bar * context.beatsPerBar + localBeat;
            result.notes.push_back({start, cell.duration, candidate,
                                    (strong ? 88 : 78) + cell.velocityOffset, 1});
            previousPitch = candidate;
        }
    }

    if (bars > 1 && development > 0.12) {
        const auto finalStart = result.lengthBeats - 0.5;
        const auto finalChord = harmonyForBar(context, bars - 1);
        const auto target = nearestPitchClassTo(finalChord.front(), previousPitch, 55, 88);
        result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                               return note.startBeat >= finalStart;
                           }), result.notes.end());
        result.notes.push_back({finalStart, 0.48, target, 92, 1});
    }
    return result;
}

} // namespace pulso
