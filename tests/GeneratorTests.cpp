#include "TestSupport.h"

#include "core/Generator.h"
#include "core/Scale.h"
#include "core/SongComposer.h"

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

std::vector<NoteEvent> notesOnChannel(const Pattern& pattern, int channel) {
    std::vector<NoteEvent> notes;
    std::copy_if(pattern.notes.begin(), pattern.notes.end(), std::back_inserter(notes),
                 [=](const auto& note) { return note.channel == channel; });
    return notes;
}

double averageVelocity(const Pattern& pattern) {
    if (pattern.notes.empty()) return 0.0;
    auto total = 0.0;
    for (const auto& note : pattern.notes) total += note.velocity;
    return total / static_cast<double>(pattern.notes.size());
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
    context.groove = 0.0;
    context.humanize = 0.0;
    context.cohesion = 0.85;
    context.energy = 0.55;
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
                const auto bar = std::clamp(static_cast<int>(note.startBeat / context.beatsPerBar),
                                            0, context.bars - 1);
                const auto& chord = context.harmonyByBar[static_cast<std::size_t>(bar)];
                const auto pitchClass = positiveModulo(note.pitch, 12);
                return note.pitch >= 55 && note.pitch <= 88 && note.channel == 2 &&
                       (isPitchClassInScale(note.pitch, context.rootPitchClass, context.scale) ||
                        std::find(chord.begin(), chord.end(), pitchClass) != chord.end());
            }), "Countermelody must stay playable and use only scale or active-chord tones");
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
    require(positiveModulo(counter.notes.back().pitch, 12) == 0,
            "The countermelody must resolve to the returning phrase root");

    auto evolvedContext = context;
    evolvedContext.evolutionStep = 1;
    const auto evolved = generator.generate(evolvedContext);
    require(sharedEventRatio(counter, evolved) >= 0.55,
            "Evolution must preserve a recognisable majority of the phrase identity");

    for (const auto* pattern : {&bass, &drums, &counter, &evolved})
        require(std::all_of(pattern->notes.begin(), pattern->notes.end(), [&](const auto& note) {
                    return note.startBeat >= 0.0 && note.endBeat() <= pattern->lengthBeats;
                }), "All generated notes must fit inside the phrase");

    auto ensembleContext = phraseContext();
    ensembleContext.role = Role::Ensemble;
    const auto ensemble = generator.generate(ensembleContext);
    auto soloContext = ensembleContext;
    soloContext.role = Role::Bass;
    require(notesOnChannel(ensemble, 1) == generator.generate(soloContext).notes,
            "Ensemble bass must be the same interpreter of the shared composition plan");
    soloContext.role = Role::Countermelody;
    require(notesOnChannel(ensemble, 2) == generator.generate(soloContext).notes,
            "Ensemble melody must derive from the same global composition plan");
    soloContext.role = Role::Percussion;
    require(notesOnChannel(ensemble, 10) == generator.generate(soloContext).notes,
            "Ensemble drums must derive from the same global composition plan");

    auto lineageContext = phraseContext();
    lineageContext.role = Role::Countermelody;
    lineageContext.space = 0.1;
    lineageContext.repetition = 0.9;
    const auto lineageOriginal = generator.generate(lineageContext);
    lineageContext.variationIndex = 1;
    const auto lineageVariation = generator.generate(lineageContext);
    const auto relatedRatio = sharedEventRatio(lineageOriginal, lineageVariation);
    auto newDnaContext = lineageContext;
    newDnaContext.seed += 7919;
    newDnaContext.variationIndex = 0;
    const auto unrelated = generator.generate(newDnaContext);
    require(relatedRatio >= 0.35,
            "An evolved idea must preserve a recognisable part of its musical DNA");
    require(relatedRatio > sharedEventRatio(lineageOriginal, unrelated),
            "An evolved idea must be more closely related than a new DNA seed");

    auto grooveContext = phraseContext();
    grooveContext.role = Role::Percussion;
    grooveContext.space = 0.0;
    grooveContext.humanize = 0.0;
    grooveContext.groove = 0.0;
    const auto straight = generator.generate(grooveContext);
    grooveContext.groove = 1.0;
    const auto swung = generator.generate(grooveContext);
    require(straight.notes.size() == swung.notes.size(),
            "Groove must change feel without replacing the composition");
    require(std::all_of(straight.notes.begin(), straight.notes.end(), [](const auto& note) {
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.001;
            }), "Zero groove and humanize must remain exactly on the sixteenth grid");
    require(std::any_of(swung.notes.begin(), swung.notes.end(), [](const auto& note) {
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) > 0.10;
            }), "Maximum groove must create audible off-grid swing");

    auto lowEnergyContext = phraseContext();
    lowEnergyContext.role = Role::Ensemble;
    lowEnergyContext.energy = 0.0;
    const auto lowEnergy = generator.generate(lowEnergyContext);
    lowEnergyContext.energy = 1.0;
    const auto highEnergy = generator.generate(lowEnergyContext);
    require(averageVelocity(highEnergy) > averageVelocity(lowEnergy) + 8.0,
            "Energy must create a clear global dynamic difference across the ensemble");

    auto threeFourContext = phraseContext();
    threeFourContext.role = Role::Ensemble;
    threeFourContext.beatsPerBar = 3.0;
    const auto threeFour = generator.generate(threeFourContext);
    requireNear(threeFour.lengthBeats, 12.0, 0.001,
                "Four bars in 3/4 must produce a twelve-beat composition");
    require(std::all_of(threeFour.notes.begin(), threeFour.notes.end(), [&](const auto& note) {
                return note.startBeat >= 0.0 && note.endBeat() <= threeFour.lengthBeats;
            }), "Non-4/4 composition must keep every role inside the phrase bounds");

    auto chromaticContext = phraseContext();
    chromaticContext.role = Role::Countermelody;
    chromaticContext.scale = ScaleKind::Chromatic;
    chromaticContext.rootPitchClass = 0;
    chromaticContext.harmonyByBar = {{0, 4, 7}, {0, 4, 7}, {0, 4, 7}, {0, 4, 7}};
    const auto chromatic = generator.generate(chromaticContext);
    auto transposedChromaticContext = chromaticContext;
    transposedChromaticContext.rootPitchClass = 2;
    transposedChromaticContext.chordPitchClasses = {2, 6, 9};
    transposedChromaticContext.harmonyByBar = {{2, 6, 9}, {2, 6, 9}, {2, 6, 9}, {2, 6, 9}};
    const auto transposedChromatic = generator.generate(transposedChromaticContext);
    require(chromatic.notes.size() == transposedChromatic.notes.size(),
            "Transposition must preserve chromatic phrase structure");
    for (std::size_t index = 0; index < chromatic.notes.size(); ++index)
        require(positiveModulo(transposedChromatic.notes[index].pitch - chromatic.notes[index].pitch, 12) == 2,
                "Chromatic scale degrees must transpose with the selected root");

    const auto longPlan = SongComposer::createLocalPlan(
        "A nine minute cinematic suite", 540, 120.0, 4.0, 99173, 0, ScaleKind::Minor);
    require(longPlan.totalBars == 270 && longPlan.sections.size() >= 8,
            "A requested nine-minute song must become a complete multi-section form");
    require(longPlan.sections.front().startBar == 0 &&
                longPlan.sections.back().startBar + longPlan.sections.back().bars == longPlan.totalBars,
            "Song sections must cover the entire target duration without gaps");
    GenerationContext songFoundation = phraseContext();
    songFoundation.role = Role::Ensemble;
    SongComposer longFormComposer;
    const auto longSong = longFormComposer.render(longPlan, songFoundation);
    requireNear(longSong.lengthBeats, 1080.0, 0.001,
                "Nine minutes at 120 BPM must render exactly 1080 beats");
    require(!longSong.notes.empty() && longSong.notes.size() < 32768,
            "Long-form rendering must remain populated and safe for realtime publication");
    for (const auto channel : {1, 2, 3, 10})
        require(std::any_of(longSong.notes.begin(), longSong.notes.end(), [channel](const auto& note) {
                    return note.channel == channel;
                }), "A full song must retain every coordinated musical layer");
    std::set<VoiceId> renderedVoices;
    for (const auto& note : longSong.notes) renderedVoices.insert(note.voice);
    require(renderedVoices.size() == voiceDefinitions.size(),
            "The long-form orchestrator must use all twelve available voices across the complete arc");
    require(longSong.markers.size() == longPlan.sections.size() && !longSong.controls.empty(),
            "A complete song must include section markers and expressive MIDI automation");
    require(std::all_of(longPlan.sections.begin(), longPlan.sections.end(), [](const auto& section) {
                return !section.activeVoices.empty();
            }) && std::count_if(longPlan.sections.begin(), longPlan.sections.end(), [](const auto& section) {
                return section.activeVoices.size() < voiceDefinitions.size();
            }) > static_cast<int>(longPlan.sections.size() / 2),
            "Dynamic orchestration must use intentional subsets rather than every voice everywhere");
    require(std::all_of(longSong.notes.begin(), longSong.notes.end(), [&](const auto& note) {
                if (note.voice == VoiceId::CoreDrums || note.voice == VoiceId::LowPercussion ||
                    note.voice == VoiceId::HighPercussion || note.voice == VoiceId::Transitions)
                    return note.pitch >= 0 && note.pitch <= 127;
                const auto& definition = voiceDefinition(note.voice);
                return note.pitch >= definition.minimumPitch && note.pitch <= definition.maximumPitch;
            }), "Every pitched orchestration voice must remain inside its designed register");

    std::vector<std::set<VoiceId>> voicesByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> leadByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> onsetByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> breathAtEnd(static_cast<std::size_t>(longPlan.totalBars), true);
    for (const auto& note : longSong.notes) {
        const auto bar = std::clamp(static_cast<int>(note.startBeat / longPlan.beatsPerBar),
                                    0, longPlan.totalBars - 1);
        voicesByBar[static_cast<std::size_t>(bar)].insert(note.voice);
        onsetByBar[static_cast<std::size_t>(bar)] = true;
        if (note.voice == VoiceId::Lead) leadByBar[static_cast<std::size_t>(bar)] = true;
        const auto beatInBar = note.startBeat - bar * longPlan.beatsPerBar;
        if (beatInBar >= longPlan.beatsPerBar - 0.38 &&
            (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
             isVoiceInFamily(note.voice, VoiceFamily::Bass)))
            breathAtEnd[static_cast<std::size_t>(bar)] = false;
    }
    std::set<std::size_t> orchestrationDensities;
    for (const auto& voices : voicesByBar) orchestrationDensities.insert(voices.size());
    require(orchestrationDensities.size() >= 5,
            "A long-form song must breathe through several clearly different orchestration densities");
    require(std::count(leadByBar.begin(), leadByBar.end(), false) > longPlan.totalBars / 3,
            "The lead must leave substantial negative space instead of becoming an eternal arpeggio");
    require(std::count(onsetByBar.begin(), onsetByBar.end(), false) >= longPlan.totalBars / 24,
            "The dramatic arc must contain genuine bars of breath without new attacks");
    require(std::count(breathAtEnd.begin(), breathAtEnd.end(), true) > longPlan.totalBars / 3,
            "Rhythm and bass must create frequent phrase-end breathing windows");
    require(longFormComposer.render(longPlan, songFoundation).notes == longSong.notes,
            "The same long-form plan and DNA must render deterministically");
    const auto compactPlan = SongComposer::createLocalPlan(
        "A slow compact song", 30, 30.0, 4.0, 31, 5, ScaleKind::Dorian);
    require(compactPlan.totalBars == 8 && !compactPlan.sections.empty() &&
                compactPlan.sections.back().startBar + compactPlan.sections.back().bars == 8,
            "Even the shortest slow song must form a valid contiguous dramatic arc");
}
