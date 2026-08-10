#include "TestSupport.h"

#include "core/Generator.h"
#include "core/Scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>
#include <tuple>
#include <vector>

using namespace pulso;

namespace {

std::vector<int> rhythmicSignature(const Pattern& pattern, int bar, double beatsPerBar) {
    std::vector<int> result;
    const auto start = bar * beatsPerBar;
    const auto end = start + beatsPerBar;
    for (const auto& note : pattern.notes)
        if (note.startBeat >= start && note.startBeat < end)
            result.push_back(static_cast<int>(std::lround((note.startBeat - start) * 4.0)));
    std::sort(result.begin(), result.end());
    return result;
}

const NoteEvent* noteAt(const Pattern& pattern, double beat, int pitch = -1) {
    const auto found = std::find_if(pattern.notes.begin(), pattern.notes.end(), [=](const auto& note) {
        return std::abs(note.startBeat - beat) < 0.001 && (pitch < 0 || note.pitch == pitch);
    });
    return found == pattern.notes.end() ? nullptr : &*found;
}

double sharedEventRatio(const Pattern& first, const Pattern& second) {
    std::set<std::tuple<int, int, int>> a;
    std::set<std::tuple<int, int, int>> b;
    for (const auto& note : first.notes)
        a.emplace(static_cast<int>(std::lround(note.startBeat * 4.0)), note.pitch, note.channel);
    for (const auto& note : second.notes)
        b.emplace(static_cast<int>(std::lround(note.startBeat * 4.0)), note.pitch, note.channel);
    std::vector<std::tuple<int, int, int>> intersection;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(intersection));
    return a.empty() ? 1.0 : static_cast<double>(intersection.size()) / static_cast<double>(a.size());
}

GenerationContext phraseContext() {
    GenerationContext context;
    context.rootPitchClass = 0;
    context.scale = ScaleKind::Minor;
    context.chordPitchClasses = {0, 3, 7};
    context.harmonyByBar = {{0, 3, 7}, {8, 0, 3}, {5, 8, 0}, {7, 11, 2}};
    context.beatsPerBar = 4.0;
    context.bars = 4;
    context.seed = 42;
    context.repetition = 0.82;
    context.complexity = 0.48;
    context.development = 0.52;
    return context;
}

} // namespace

void runGeneratorTests() {
    Generator generator;
    auto context = phraseContext();

    context.role = Role::Bass;
    const auto bass = generator.generate(context);
    requireNear(bass.lengthBeats, 16.0, 0.001, "Four bars must produce a 16-beat phrase");
    require(!bass.notes.empty(), "Bass generator must always create notes");
    require(generator.generate(context).notes == bass.notes, "Same seed and context must be deterministic");
    require(std::all_of(bass.notes.begin(), bass.notes.end(), [](const auto& note) {
                return note.pitch >= 28 && note.pitch <= 52 && note.channel == 1;
            }), "Bass notes must stay in the bass register");

    const std::array expectedRoots{0, 8, 5, 7};
    for (auto bar = 0; bar < context.bars; ++bar) {
        const auto* downbeat = noteAt(bass, bar * context.beatsPerBar);
        require(downbeat != nullptr, "Every bass bar must have a downbeat anchor");
        require(positiveModulo(downbeat->pitch, 12) == expectedRoots[static_cast<std::size_t>(bar)],
                "Bass downbeats must state each chord root");
    }
    require(positiveModulo(bass.notes.back().pitch, 12) == 10,
            "C-minor phrase must cadence through the diatonic note below C");

    auto repeatedContext = context;
    repeatedContext.harmonyByBar = {{0, 3, 7}, {0, 3, 7}, {0, 3, 7}, {0, 3, 7}};
    repeatedContext.repetition = 1.0;
    repeatedContext.development = 0.0;
    const auto repeatedBass = generator.generate(repeatedContext);
    require(rhythmicSignature(repeatedBass, 0, 4.0) == rhythmicSignature(repeatedBass, 1, 4.0),
            "Maximum repetition must preserve the motif rhythm between bars");

    context.seed = 43;
    require(generator.generate(context).notes != bass.notes, "A new seed must create a new motif");
    context.seed = 42;

    context.role = Role::Percussion;
    const auto drums = generator.generate(context);
    require(!drums.notes.empty(), "Percussion must produce notes");
    require(std::all_of(drums.notes.begin(), drums.notes.end(), [](const auto& note) {
                const std::array supported{36, 38, 42, 45, 46, 47, 50};
                return note.channel == 10 &&
                       std::find(supported.begin(), supported.end(), note.pitch) != supported.end();
            }), "Percussion must use supported General MIDI voices on channel 10");
    for (auto bar = 0; bar < context.bars; ++bar) {
        require(noteAt(drums, bar * 4.0 + 1.0, 38) != nullptr,
                "Each 4/4 bar must preserve the snare backbeat on beat two");
        require(noteAt(drums, bar * 4.0 + 3.0, 38) != nullptr,
                "Each 4/4 bar must preserve the snare backbeat on beat four");
    }
    require(noteAt(drums, 15.0, 45) != nullptr && noteAt(drums, 15.75, 38) != nullptr,
            "The final bar must contain a directed fill into the loop point");

    context.role = Role::Countermelody;
    context.space = 0.0;
    const auto counter = generator.generate(context);
    require(!counter.notes.empty(), "Countermelody must produce a phrase");
    require(std::all_of(counter.notes.begin(), counter.notes.end(), [&](const auto& note) {
                return note.pitch >= 55 && note.pitch <= 88 &&
                       isPitchClassInScale(note.pitch, context.rootPitchClass, context.scale);
            }), "Countermelody must remain playable and diatonic");
    for (std::size_t index = 1; index < counter.notes.size(); ++index)
        require(std::abs(counter.notes[index].pitch - counter.notes[index - 1].pitch) <= 9,
                "Melodic voice leading must avoid unexplained leaps larger than a sixth");
    for (auto bar = 0; bar < context.bars; ++bar) {
        const auto* anchor = noteAt(counter, bar * 4.0);
        require(anchor != nullptr, "Every melodic bar must begin with an anchor");
        const auto& chord = context.harmonyByBar[static_cast<std::size_t>(bar)];
        require(std::find(chord.begin(), chord.end(), positiveModulo(anchor->pitch, 12)) != chord.end(),
                "Melodic downbeat anchors must be chord tones");
    }
    require(positiveModulo(counter.notes.back().pitch, 12) == 7,
            "The countermelody must resolve to the final chord root");

    auto evolvedContext = context;
    evolvedContext.evolutionStep = 1;
    const auto evolved = generator.generate(evolvedContext);
    require(sharedEventRatio(counter, evolved) >= 0.55,
            "Evolution must preserve a recognisable majority of the phrase identity");

    for (const auto* pattern : {&bass, &drums, &counter, &evolved})
        require(std::all_of(pattern->notes.begin(), pattern->notes.end(), [&](const auto& note) {
                    return note.startBeat >= 0.0 && note.endBeat() <= pattern->lengthBeats;
                }), "All generated notes must fit inside the phrase");

    auto chromaticContext = phraseContext();
    chromaticContext.role = Role::Countermelody;
    chromaticContext.scale = ScaleKind::Chromatic;
    chromaticContext.rootPitchClass = 0;
    chromaticContext.harmonyByBar = {{0, 4, 7}, {0, 4, 7}, {0, 4, 7}, {0, 4, 7}};
    const auto chromatic = generator.generate(chromaticContext);
    require(noteAt(chromatic, 0.0) != nullptr &&
                positiveModulo(noteAt(chromatic, 0.0)->pitch, 12) == 0,
            "Chromatic scale degrees must remain relative to the selected root");
}
