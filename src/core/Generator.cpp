#include "Generator.h"

#include "Random.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace pulso {
namespace {

enum class HarmonicFunction { Tonic, Predominant, Dominant, Colour };
enum class PhraseFunction { Statement, Answer, Development, Cadence };

struct MotifCell {
    int step{};
    int durationSteps{1};
    int degree{};
    int accent{};
    bool essential{};
};

struct BarIntent {
    PhraseFunction function{PhraseFunction::Statement};
    HarmonicFunction harmony{HarmonicFunction::Colour};
    double tension{};
    int degreeShift{};
    int rhythmicShift{};
    bool invertContour{};
    bool fragment{};
};

struct CompositionPlan {
    std::vector<MotifCell> motif;
    std::vector<BarIntent> bars;
};

double unit(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

int stepsPerBar(const GenerationContext& context) noexcept {
    return std::clamp(static_cast<int>(std::lround(std::max(1.0, context.beatsPerBar) * 4.0)), 4, 32);
}

int fitMotifStep(const GenerationContext& context, int fourFourStep) noexcept {
    const auto steps = stepsPerBar(context);
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(fourFourStep) *
                                                   (steps - 1) / 15.0)), 0, steps - 1);
}

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
    const auto third = context.scale == ScaleKind::Minor || context.scale == ScaleKind::Dorian ? 3 : 4;
    return {positiveModulo(context.rootPitchClass, 12),
            positiveModulo(context.rootPitchClass + third, 12),
            positiveModulo(context.rootPitchClass + 7, 12)};
}

std::vector<int> harmonyForBar(const GenerationContext& context, int bar) {
    if (bar >= 0 && bar < static_cast<int>(context.harmonyByBar.size())) {
        auto harmony = normalizeHarmonyPreservingRoot(context.harmonyByBar[static_cast<std::size_t>(bar)]);
        if (!harmony.empty()) return harmony;
    }
    return fallbackHarmony(context);
}

HarmonicFunction classifyHarmony(const GenerationContext& context, std::span<const int> chord) {
    if (chord.empty()) return HarmonicFunction::Colour;
    const auto relativeRoot = positiveModulo(chord.front() - context.rootPitchClass, 12);
    if (relativeRoot == 0 || relativeRoot == 3 || relativeRoot == 4 || relativeRoot == 8 || relativeRoot == 9)
        return HarmonicFunction::Tonic;
    if (relativeRoot == 2 || relativeRoot == 5) return HarmonicFunction::Predominant;
    if (relativeRoot == 7 || relativeRoot == 10 || relativeRoot == 11) return HarmonicFunction::Dominant;
    return HarmonicFunction::Colour;
}

double harmonicTension(HarmonicFunction function) noexcept {
    switch (function) {
    case HarmonicFunction::Tonic: return 0.15;
    case HarmonicFunction::Predominant: return 0.46;
    case HarmonicFunction::Dominant: return 0.82;
    case HarmonicFunction::Colour: return 0.58;
    }
    return 0.5;
}

int nearestPitchClassTo(int pitchClass, int reference, int minimum, int maximum) {
    auto best = std::clamp(reference, minimum, maximum);
    auto distance = std::numeric_limits<int>::max();
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto candidateDistance = std::abs(pitch - reference);
        if (candidateDistance < distance) {
            best = pitch;
            distance = candidateDistance;
        }
    }
    return best;
}

int nearestChordToneTo(std::span<const int> chord, int reference, int minimum, int maximum) {
    auto best = std::clamp(reference, minimum, maximum);
    auto distance = std::numeric_limits<int>::max();
    for (const auto pitchClass : chord) {
        const auto candidate = nearestPitchClassTo(pitchClass, reference, minimum, maximum);
        const auto candidateDistance = std::abs(candidate - reference);
        if (candidateDistance < distance) {
            best = candidate;
            distance = candidateDistance;
        }
    }
    return best;
}

int scaleDegreePitch(const GenerationContext& context, int degree, int reference) {
    const auto intervals = intervalsFor(context.scale);
    const auto size = static_cast<int>(intervals.size());
    const auto root = nearestPitchClassTo(context.rootPitchClass, reference, reference - 7, reference + 7);
    if (size == 12) return root + degree;
    const auto octave = static_cast<int>(std::floor(static_cast<double>(degree) / size));
    return root + intervals[static_cast<std::size_t>(positiveModulo(degree, size))] + octave * 12;
}

int scaleApproachBelow(const GenerationContext& context, int targetPitchClass) {
    for (auto distance = 1; distance <= 4; ++distance) {
        const auto candidate = positiveModulo(targetPitchClass - distance, 12);
        if (isPitchClassInScale(candidate, context.rootPitchClass, context.scale)) return candidate;
    }
    return positiveModulo(targetPitchClass - 1, 12);
}

bool sourceHasOnsetNear(const GenerationContext& context, double beat, double tolerance = 0.14) {
    return std::any_of(context.sourceNotes.begin(), context.sourceNotes.end(), [=](const auto& note) {
        return std::abs(note.beat - beat) <= tolerance;
    });
}

std::uint64_t eventSeed(const GenerationContext& context, int bar, int step, std::uint64_t salt) {
    auto seed = context.seed ^ salt;
    seed ^= static_cast<std::uint64_t>(bar + 1) * 0x9E3779B97F4A7C15ULL;
    seed ^= static_cast<std::uint64_t>(step + 17) * 0xD1B54A32D192ED03ULL;
    seed ^= context.variationIndex * 0x94D049BB133111EBULL;
    seed ^= context.evolutionStep * 0xBF58476D1CE4E5B9ULL;
    return seed;
}

double expressiveStart(const GenerationContext& context, int bar, int step, std::uint64_t salt) {
    const auto barStart = static_cast<double>(bar) * context.beatsPerBar;
    const auto barEnd = barStart + context.beatsPerBar;
    auto beat = barStart + static_cast<double>(step) * 0.25;
    if (step % 4 == 2) beat += unit(context.groove) * 0.115;
    if (step != 0 && unit(context.humanize) > 0.0) {
        Random random(eventSeed(context, bar, step, salt));
        beat += (random.unit() * 2.0 - 1.0) * unit(context.humanize) * 0.022;
    }
    return std::clamp(beat, barStart, barEnd - 0.015);
}

int expressiveVelocity(const GenerationContext& context, int base, int bar, int step,
                       double tension, std::uint64_t salt) {
    Random random(eventSeed(context, bar, step, salt));
    const auto human = static_cast<int>(std::lround((random.unit() * 2.0 - 1.0) *
                                                     unit(context.humanize) * 11.0));
    const auto energy = static_cast<int>(std::lround((unit(context.energy) - 0.5) * 18.0));
    const auto arc = static_cast<int>(std::lround(tension * unit(context.development) * 9.0));
    return std::clamp(base + human + energy + arc, 1, 127);
}

CompositionPlan buildCompositionPlan(const GenerationContext& context) {
    CompositionPlan plan;
    Random dna(context.seed ^ 0x434F4D504F534552ULL);
    const auto complexity = unit(context.complexity);
    const auto space = unit(context.space);
    const auto desiredCells = std::clamp(4 + static_cast<int>(std::lround(complexity * 5.0 - space * 2.0)), 3, 9);

    std::array<bool, 16> selected{};
    selected[0] = true;
    auto count = 1;
    while (count < desiredCells) {
        const auto step = dna.range(1, 15);
        if (selected[static_cast<std::size_t>(step)]) continue;
        const auto strong = step % 4 == 0;
        const auto syncopated = step % 2 == 1;
        auto accept = strong ? 0.74 : (syncopated ? 0.42 + complexity * 0.42 : 0.66);
        const auto sourceBeat = static_cast<double>(step) / 16.0 * context.beatsPerBar;
        if (sourceHasOnsetNear(context, sourceBeat)) accept += unit(context.follow) * 0.25;
        if (!dna.chance(accept)) continue;
        selected[static_cast<std::size_t>(step)] = true;
        ++count;
    }

    auto degree = dna.range(0, 4);
    for (auto step = 0; step < 16; ++step) {
        if (!selected[static_cast<std::size_t>(step)]) continue;
        if (!plan.motif.empty()) {
            constexpr std::array moves{-2, -1, -1, 0, 1, 1, 2};
            degree += moves[static_cast<std::size_t>(dna.range(0, static_cast<int>(moves.size()) - 1))];
            degree = std::clamp(degree, -2, 8);
        }
        const auto nextSelected = std::find(selected.begin() + step + 1, selected.end(), true);
        const auto nextStep = nextSelected == selected.end() ? 16 : static_cast<int>(nextSelected - selected.begin());
        const auto duration = std::clamp(nextStep - step, 1, dna.chance(0.28) ? 4 : 2);
        const auto sourceBeat = static_cast<double>(step) / 16.0 * context.beatsPerBar;
        const auto essential = step == 0 || step % 4 == 0 || sourceHasOnsetNear(context, sourceBeat);
        plan.motif.push_back({step, duration, degree, dna.range(-7, 7), essential});
    }

    const auto bars = std::clamp(context.bars, 1, 16);
    plan.bars.reserve(static_cast<std::size_t>(bars));
    for (auto bar = 0; bar < bars; ++bar) {
        const auto chord = harmonyForBar(context, bar);
        const auto positionInSection = bar % 4;
        const auto finalBar = bar == bars - 1;
        BarIntent intent;
        intent.function = finalBar ? PhraseFunction::Cadence :
                          (positionInSection == 0 ? PhraseFunction::Statement :
                           positionInSection == 1 ? PhraseFunction::Answer :
                           PhraseFunction::Development);
        intent.harmony = classifyHarmony(context, chord);
        const auto phraseProgress = bars <= 1 ? 0.0 : static_cast<double>(bar) / (bars - 1);
        intent.tension = std::clamp(harmonicTension(intent.harmony) * 0.55 + phraseProgress * 0.45,
                                    0.0, 1.0);

        const auto lineage = static_cast<int>((context.variationIndex + context.evolutionStep) % 4);
        if (intent.function == PhraseFunction::Answer) {
            intent.degreeShift = lineage % 2 == 0 ? 1 : -1;
            intent.invertContour = lineage == 2;
        } else if (intent.function == PhraseFunction::Development) {
            intent.degreeShift = positionInSection == 2 ? 2 : -1;
            intent.rhythmicShift = lineage == 1 ? 1 : (lineage == 3 ? -1 : 0);
        } else if (intent.function == PhraseFunction::Cadence) {
            intent.fragment = true;
        }
        plan.bars.push_back(intent);
    }
    return plan;
}

int transformedStep(const MotifCell& cell, const BarIntent& intent, double cohesion) {
    if (cell.essential || cohesion > 0.84) return cell.step;
    return std::clamp(cell.step + intent.rhythmicShift, 0, 15);
}

int transformedDegree(const MotifCell& cell, const BarIntent& intent) {
    const auto contour = intent.invertContour ? -cell.degree : cell.degree;
    return contour + intent.degreeShift;
}

void normalizePattern(Pattern& pattern) {
    for (auto& note : pattern.notes) {
        note.startBeat = std::clamp(note.startBeat, 0.0, std::max(0.0, pattern.lengthBeats - 0.025));
        note.durationBeats = std::clamp(note.durationBeats, 0.025,
                                        std::max(0.025, pattern.lengthBeats - note.startBeat));
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.channel = std::clamp(note.channel, 1, 16);
    }
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.channel != right.channel) return left.channel < right.channel;
        return left.pitch < right.pitch;
    });

    std::array<std::array<int, 128>, 16> previous;
    for (auto& channel : previous) channel.fill(-1);
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        auto& note = pattern.notes[index];
        auto& previousIndex = previous[static_cast<std::size_t>(note.channel - 1)]
                                      [static_cast<std::size_t>(note.pitch)];
        if (previousIndex >= 0) {
            auto& earlier = pattern.notes[static_cast<std::size_t>(previousIndex)];
            if (earlier.endBeat() > note.startBeat)
                earlier.durationBeats = std::max(0.025, note.startBeat - earlier.startBeat - 0.005);
        }
        previousIndex = static_cast<int>(index);
    }
}

Pattern renderBass(const GenerationContext& context, const CompositionPlan& plan) {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    auto previousPitch = pitchClassToMidi(harmonyForBar(context, 0).front(), 2, 28, 52);
    const auto cohesion = unit(context.cohesion);

    for (auto bar = 0; bar < bars; ++bar) {
        const auto chord = harmonyForBar(context, bar);
        const auto& intent = plan.bars[static_cast<std::size_t>(bar)];
        for (const auto& cell : plan.motif) {
            if (intent.fragment && !cell.essential && cell.step > 8) continue;
            Random choice(eventSeed(context, bar, cell.step, 0x42415353ULL));
            if (!cell.essential && choice.chance(unit(context.space) * 0.32 *
                                                  (1.0 - unit(context.repetition)))) continue;
            if (!cell.essential && !choice.chance(0.48 + cohesion * 0.30 +
                                                   unit(context.repetition) * 0.24)) continue;

            const auto step = fitMotifStep(context, transformedStep(cell, intent, cohesion));
            const auto strong = step % 4 == 0;
            auto pitchClass = chord.front();
            if (!strong && chord.size() > 1) {
                const auto index = static_cast<std::size_t>(positiveModulo(transformedDegree(cell, intent),
                                                                          static_cast<int>(chord.size())));
                pitchClass = chord[index];
            }
            if (!strong && unit(context.risk) > 0.62 && choice.chance((unit(context.risk) - 0.62) * 0.6))
                pitchClass = positiveModulo(nearestPitchInScale(pitchClass + (choice.chance(0.5) ? 2 : -2),
                                                                 context.rootPitchClass, context.scale), 12);

            auto pitch = nearestPitchClassTo(pitchClass, previousPitch, 28, 52);
            if (std::abs(pitch - previousPitch) > 7) pitch = nearestChordToneTo(chord, previousPitch, 28, 52);
            const auto start = expressiveStart(context, bar, step, 0x42415353ULL);
            const auto duration = std::min(cell.durationSteps * context.beatsPerBar / 16.0 * 0.88,
                                           bar * context.beatsPerBar + context.beatsPerBar - start + 0.001);
            const auto baseVelocity = step == 0 ? 106 : (strong ? 94 : 80) + cell.accent / 3;
            result.notes.push_back({start, duration, pitch,
                                    expressiveVelocity(context, baseVelocity, bar, step, intent.tension,
                                                       0x42415353ULL), 1});
            previousPitch = pitch;
        }
    }

    if (bars > 1 && unit(context.development) > 0.12) {
        const auto targetRoot = harmonyForBar(context, 0).front();
        const auto approach = scaleApproachBelow(context, targetRoot);
        const auto start = result.lengthBeats - 0.25;
        result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                               return note.startBeat >= start - 0.001;
                           }), result.notes.end());
        result.notes.push_back({start, 0.22, nearestPitchClassTo(approach, previousPitch, 28, 52),
                                expressiveVelocity(context, 88, bars - 1, 15, 1.0,
                                                   0x434144454E4345ULL), 1});
    }
    return result;
}

Pattern renderDrums(const GenerationContext& context, const CompositionPlan& plan) {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    const auto complexity = unit(context.complexity);
    const auto energy = unit(context.energy);

    for (auto bar = 0; bar < bars; ++bar) {
        const auto& intent = plan.bars[static_cast<std::size_t>(bar)];
        std::array<bool, 32> motifSteps{};
        for (const auto& cell : plan.motif)
            motifSteps[static_cast<std::size_t>(fitMotifStep(
                context, transformedStep(cell, intent, unit(context.cohesion))))] = true;

        const auto barSteps = stepsPerBar(context);
        for (auto step = 0; step < barSteps; ++step) {
            Random choice(eventSeed(context, bar, step, 0x4452554D53ULL));
            const auto midpointKick = ((barSteps / 2 + 2) / 4) * 4;
            const auto kick = step == 0 || step == midpointKick ||
                              (motifSteps[static_cast<std::size_t>(step)] &&
                               step != 4 && step != 12 && choice.chance(0.28 + energy * 0.34));
            const auto backbeat = step == 4 || (barSteps >= 16 && step == 12);
            const auto ghostSnare = !backbeat && step % 2 == 1 &&
                                    motifSteps[static_cast<std::size_t>(step)] &&
                                    choice.chance(complexity * 0.28);
            const auto hat = step % 2 == 0 ||
                             (step % 2 == 1 && choice.chance(complexity * energy * 0.55));
            const auto start = expressiveStart(context, bar, step, 0x4452554D53ULL);
            if (kick)
                result.notes.push_back({start, 0.07, 36,
                                        expressiveVelocity(context, step == 0 ? 112 : 96, bar, step,
                                                           intent.tension, 0x4B49434BULL), 10});
            if (backbeat || ghostSnare)
                result.notes.push_back({start, 0.07, 38,
                                        expressiveVelocity(context, backbeat ? 106 : 58, bar, step,
                                                           intent.tension, 0x534E415245ULL), 10});
            if (hat && choice.chance(0.94 - unit(context.space) * 0.35)) {
                const auto open = step == barSteps - 2 && intent.tension > 0.55 && choice.chance(energy * 0.65);
                result.notes.push_back({start, open ? 0.18 : 0.045, open ? 46 : 42,
                                        expressiveVelocity(context, 67 + (step % 4 == 2 ? 11 : 0), bar,
                                                           step, intent.tension, 0x484154ULL), 10});
            }
        }

        if (intent.function == PhraseFunction::Cadence && bars > 1 &&
            unit(context.development) > 0.15) {
            const auto fillStart = (bar + 1) * context.beatsPerBar - 1.0;
            result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                                   return note.startBeat >= fillStart && (note.pitch == 42 || note.pitch == 46);
                               }), result.notes.end());
            constexpr std::array pitches{45, 47, 50, 38};
            for (std::size_t index = 0; index < pitches.size(); ++index) {
                const auto step = stepsPerBar(context) - 4 + static_cast<int>(index);
                result.notes.push_back({expressiveStart(context, bar, step, 0x46494C4CULL), 0.07,
                                        pitches[index], expressiveVelocity(context, 78 + static_cast<int>(index) * 8,
                                                                          bar, step, 1.0, 0x46494C4CULL), 10});
            }
        }
    }
    return result;
}

Pattern renderCountermelody(const GenerationContext& context, const CompositionPlan& plan) {
    const auto bars = std::clamp(context.bars, 1, 16);
    Pattern result{{}, std::max(1.0, context.beatsPerBar) * bars, context.seed};
    auto previousPitch = scaleDegreePitch(context, plan.motif.front().degree + 2, 67);
    previousPitch = std::clamp(previousPitch, 55, 88);

    for (auto bar = 0; bar < bars; ++bar) {
        const auto chord = harmonyForBar(context, bar);
        const auto& intent = plan.bars[static_cast<std::size_t>(bar)];
        const auto responseShift = intent.function == PhraseFunction::Statement ? 0 : 2;

        for (const auto& cell : plan.motif) {
            if (intent.fragment && !cell.essential && cell.step < 8) continue;
            Random choice(eventSeed(context, bar, cell.step, 0x4D454C4F4459ULL));
            if (!cell.essential && choice.chance(unit(context.space) * 0.46 *
                                                  (1.0 - unit(context.repetition) * 0.82))) continue;
            if (sourceHasOnsetNear(context, static_cast<double>(cell.step) / 16.0 *
                                                context.beatsPerBar, 0.18) &&
                choice.chance(unit(context.follow) * 0.48)) continue;

            auto step = cell.step == 0 ? 0 : transformedStep(cell, intent, unit(context.cohesion));
            if (cell.step != 0 && unit(context.cohesion) > 0.45)
                step = std::clamp(step + responseShift, 1, 15);
            step = fitMotifStep(context, step);
            auto degree = transformedDegree(cell, intent) + 2;
            if (unit(context.risk) > 0.58 && choice.chance((unit(context.risk) - 0.58) * 0.45))
                degree += choice.chance(0.5) ? 1 : -1;

            auto pitch = scaleDegreePitch(context, degree, previousPitch);
            while (pitch > 88) pitch -= 12;
            while (pitch < 55) pitch += 12;
            const auto strong = step % 4 == 0;
            if (strong) pitch = nearestChordToneTo(chord, pitch, 55, 88);
            if (std::abs(pitch - previousPitch) > 7)
                pitch = nearestPitchClassTo(pitch, previousPitch, 55, 88);
            if (std::abs(pitch - previousPitch) > 9)
                pitch = nearestChordToneTo(chord, previousPitch, 55, 88);

            const auto start = expressiveStart(context, bar, step, 0x4D454C4F4459ULL);
            const auto duration = std::min(cell.durationSteps * context.beatsPerBar / 16.0 * 0.92 +
                                               (strong ? 0.18 : 0.0),
                                           bar * context.beatsPerBar + context.beatsPerBar - start + 0.001);
            result.notes.push_back({start, duration, pitch,
                                    expressiveVelocity(context, (strong ? 88 : 76) + cell.accent / 2,
                                                       bar, step, intent.tension, 0x4D454C4F4459ULL), 2});
            previousPitch = pitch;
        }
    }

    if (bars > 1 && unit(context.development) > 0.10) {
        const auto start = result.lengthBeats - 0.25;
        const auto targetRoot = harmonyForBar(context, 0).front();
        result.notes.erase(std::remove_if(result.notes.begin(), result.notes.end(), [=](const auto& note) {
                               return note.startBeat >= start - 0.001;
                           }), result.notes.end());
        result.notes.push_back({start, 0.23, nearestPitchClassTo(targetRoot, previousPitch, 55, 88),
                                expressiveVelocity(context, 94, bars - 1, 15, 1.0,
                                                   0x5245534F4C5645ULL), 2});
    }
    return result;
}

void appendPattern(Pattern& destination, Pattern&& source) {
    destination.notes.insert(destination.notes.end(),
                             std::make_move_iterator(source.notes.begin()),
                             std::make_move_iterator(source.notes.end()));
}

} // namespace

Pattern Generator::generate(const GenerationContext& context) const {
    const auto plan = buildCompositionPlan(context);
    Pattern result;
    switch (context.role) {
    case Role::Ensemble:
        result = {{}, std::max(1.0, context.beatsPerBar) * std::clamp(context.bars, 1, 16), context.seed};
        appendPattern(result, renderBass(context, plan));
        appendPattern(result, renderDrums(context, plan));
        appendPattern(result, renderCountermelody(context, plan));
        break;
    case Role::Bass: result = renderBass(context, plan); break;
    case Role::Percussion: result = renderDrums(context, plan); break;
    case Role::Countermelody: result = renderCountermelody(context, plan); break;
    }
    normalizePattern(result);
    return result;
}

} // namespace pulso
