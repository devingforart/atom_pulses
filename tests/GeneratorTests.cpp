#include "TestSupport.h"

#include "core/Generator.h"
#include "core/Scale.h"

#include <algorithm>

using namespace pulso;

void runGeneratorTests() {
    Generator generator;
    GenerationContext context;
    context.rootPitchClass = 0;
    context.scale = ScaleKind::Minor;
    context.chordPitchClasses = {0, 3, 7};
    context.beatsPerBar = 4.0;
    context.seed = 42;

    context.role = Role::Bass;
    const auto bass = generator.generate(context);
    require(!bass.notes.empty(), "Bass generator must always create at least one note");
    require(std::all_of(bass.notes.begin(), bass.notes.end(), [](const auto& note) {
                return note.pitch >= 28 && note.pitch <= 52 && note.channel == 1;
            }), "Bass notes must stay in the bass register");
    require(generator.generate(context).notes == bass.notes, "Same seed and context must be deterministic");

    context.seed = 43;
    require(generator.generate(context).notes != bass.notes, "A new seed should create a variation");

    context.role = Role::Percussion;
    const auto drums = generator.generate(context);
    require(!drums.notes.empty(), "Percussion must produce notes");
    require(std::all_of(drums.notes.begin(), drums.notes.end(), [](const auto& note) {
                return note.channel == 10 && (note.pitch == 36 || note.pitch == 38 || note.pitch == 42 || note.pitch == 46);
            }), "Percussion must use the General MIDI drum channel and supported voices");

    context.role = Role::Countermelody;
    context.space = 0.0;
    const auto counter = generator.generate(context);
    require(std::all_of(counter.notes.begin(), counter.notes.end(), [&](const auto& note) {
                return note.pitch >= 55 && note.pitch <= 88 &&
                       isPitchClassInScale(note.pitch, context.rootPitchClass, context.scale);
            }), "Countermelody must remain playable and in scale");

    for (const auto* pattern : {&bass, &drums, &counter})
        require(std::all_of(pattern->notes.begin(), pattern->notes.end(), [&](const auto& note) {
                    return note.startBeat >= 0.0 && note.endBeat() <= pattern->lengthBeats;
                }), "All generated notes must fit inside the pattern");
}

