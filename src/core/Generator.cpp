#include "Generator.h"

#include "Random.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso {
namespace {

double clampUnit(double value) { return std::clamp(value, 0.0, 1.0); }

std::vector<int> harmonicMaterial(const GenerationContext& context) {
    auto chord = normalizePitchClasses(context.chordPitchClasses);
    if (chord.empty()) {
        const auto intervals = intervalsFor(context.scale);
        for (const auto interval : intervals)
            chord.push_back(positiveModulo(context.rootPitchClass + interval, 12));
    }
    return chord;
}

void normalizePattern(Pattern& pattern) {
    for (auto& note : pattern.notes) {
        note.startBeat = std::clamp(note.startBeat, 0.0, pattern.lengthBeats - 0.001);
        note.durationBeats = std::clamp(note.durationBeats, 0.03, pattern.lengthBeats - note.startBeat);
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.channel = std::clamp(note.channel, 1, 16);
    }
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        return a.pitch < b.pitch;
    });
}

bool sourceHasOnsetNear(const GenerationContext& context, double beat, double tolerance = 0.13) {
    return std::any_of(context.sourceNotes.begin(), context.sourceNotes.end(), [=](const auto& note) {
        return std::abs(note.beat - beat) <= tolerance;
    });
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
    Pattern result{{}, std::max(1.0, context.beatsPerBar), context.seed};
    Random random(context.seed ^ 0xBA5511ULL);
    const auto chord = harmonicMaterial(context);
    const auto steps = std::max(2, static_cast<int>(std::round(result.lengthBeats * 2.0)));
    const auto follow = clampUnit(context.follow);
    const auto risk = clampUnit(context.risk);
    const auto space = clampUnit(context.space);

    for (int step = 0; step < steps; ++step) {
        const auto beat = static_cast<double>(step) * 0.5;
        const bool strong = step == 0 || step % 4 == 0;
        const bool followsSource = sourceHasOnsetNear(context, beat);
        auto probability = strong ? 0.96 : 0.58;
        probability += followsSource ? 0.28 * follow : -0.10 * follow;
        probability -= space * 0.52;
        if (!random.chance(probability)) continue;

        auto pitchClass = chord.front();
        if (!strong && chord.size() > 1 && random.chance(0.35 + risk * 0.45))
            pitchClass = chord[static_cast<std::size_t>(random.range(0, static_cast<int>(chord.size()) - 1))];
        if (risk > 0.55 && random.chance((risk - 0.55) * 0.75))
            pitchClass = positiveModulo(pitchClass + (random.chance(0.5) ? 1 : -1), 12);

        auto pitch = pitchClassToMidi(pitchClass, 2, 28, 52);
        pitch = nearestPitchInScale(pitch, context.rootPitchClass, context.scale);
        const auto duration = random.chance(0.22 + follow * 0.30) ? 0.46 : 0.23;
        const auto velocity = (strong ? 105 : 82) + random.range(-7, 7);
        result.notes.push_back({beat, duration, pitch, velocity, 1});
    }

    if (result.notes.empty())
        result.notes.push_back({0.0, 0.75, pitchClassToMidi(chord.front(), 2, 28, 52), 100, 1});
    return result;
}

Pattern Generator::generatePercussion(const GenerationContext& context) const {
    Pattern result{{}, std::max(1.0, context.beatsPerBar), context.seed};
    Random random(context.seed ^ 0xD12A5ULL);
    const auto space = clampUnit(context.space);
    const auto risk = clampUnit(context.risk);
    const auto sixteenths = std::max(4, static_cast<int>(std::round(result.lengthBeats * 4.0)));

    for (int step = 0; step < sixteenths; ++step) {
        const auto beat = static_cast<double>(step) * 0.25;
        const bool quarter = step % 4 == 0;
        const bool backbeat = step % 8 == 4;
        if ((quarter || random.chance(0.10 + risk * 0.20)) && random.chance(0.92 - space * 0.25))
            result.notes.push_back({beat, 0.08, 36, quarter ? 112 : 82, 10});
        if (backbeat && random.chance(0.98 - space * 0.18))
            result.notes.push_back({beat, 0.08, 38, 108 + random.range(-5, 5), 10});
        const bool offEighth = step % 2 == 1;
        if (random.chance((offEighth ? 0.86 : 0.62) - space * 0.58))
            result.notes.push_back({beat, 0.05, random.chance(risk * 0.18) ? 46 : 42,
                                    66 + (offEighth ? 12 : 0) + random.range(-8, 8), 10});
    }
    return result;
}

Pattern Generator::generateCountermelody(const GenerationContext& context) const {
    Pattern result{{}, std::max(1.0, context.beatsPerBar), context.seed};
    Random random(context.seed ^ 0xC0A17E2ULL);
    const auto chord = harmonicMaterial(context);
    const auto risk = clampUnit(context.risk);
    const auto follow = clampUnit(context.follow);
    const auto space = clampUnit(context.space);
    const auto steps = std::max(2, static_cast<int>(std::round(result.lengthBeats * 2.0)));
    auto previousPitch = pitchClassToMidi(chord.front(), 4, 60, 84);

    for (int step = 0; step < steps; ++step) {
        const auto beat = static_cast<double>(step) * 0.5;
        const bool sourceBusy = sourceHasOnsetNear(context, beat, 0.20);
        auto probability = 0.58 - space * 0.43;
        probability += sourceBusy ? -0.36 * follow : 0.20 * follow;
        if (!random.chance(probability)) continue;

        auto pitchClass = chord[static_cast<std::size_t>(random.range(0, static_cast<int>(chord.size()) - 1))];
        auto candidate = pitchClassToMidi(pitchClass, 4, 60, 84);
        while (candidate - previousPitch > 7) candidate -= 12;
        while (previousPitch - candidate > 7) candidate += 12;
        if (risk > 0.45 && random.chance(risk * 0.42))
            candidate = nearestPitchInScale(candidate + (random.chance(0.5) ? 2 : -2),
                                            context.rootPitchClass, context.scale);
        candidate = std::clamp(candidate, 55, 88);
        result.notes.push_back({beat, random.chance(0.55) ? 0.44 : 0.94, candidate,
                                78 + random.range(-9, 12), 1});
        previousPitch = candidate;
    }

    return result;
}

} // namespace pulso

