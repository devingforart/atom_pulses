#include "TestSupport.h"

#include "core/Generator.h"
#include "core/HarmonyEngine.h"
#include "core/MusicalCritic.h"
#include "core/CompositionModel.h"
#include "core/PhraseDirector.h"
#include "core/PerformanceTiming.h"
#include "core/Scale.h"
#include "core/SongComposer.h"
#include "core/TonalContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace pulso;

namespace {

std::string motifFingerprint(const RhythmMotif& motif) {
    auto result = motif.kick + '|' + motif.snareClap + '|' + motif.closedHats + '|' +
                  motif.openHatsShaker + '|' + motif.lowPercussion + '|' + motif.highPercussion;
    for (const auto& ornament : motif.ornaments)
        result += '|' + std::to_string(ornament.step) + ':' +
                  std::string(rhythmInstrumentKey(ornament.instrument));
    return result;
}

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
    Pattern timingPattern;
    timingPattern.lengthBeats = 4.0;
    timingPattern.notes = {{0.13, 0.41, 36, 100, 10, VoiceId::CoreDrums},
                           {1.46, 0.69, 67, 88, 2, VoiceId::Lead}};
    timingPattern.controls = {{2.87, 11, 90, 2, VoiceId::Lead}};
    quantizePatternTiming(timingPattern, 4);
    require(std::all_of(timingPattern.notes.begin(), timingPattern.notes.end(), [](const auto& note) {
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.000001 &&
                       std::abs(note.endBeat() * 16.0 - std::round(note.endBeat() * 16.0)) < 0.000001;
            }) && std::abs(timingPattern.controls.front().beat * 16.0 -
                           std::round(timingPattern.controls.front().beat * 16.0)) < 0.000001,
            "Stored onsets must be exact while note-offs and controls retain fine articulation");
    require(performanceOffsetBeats(timingPattern.notes.front(), 42, 0, 120.0) == 0.0 &&
                std::abs(performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0)) <= 0.008 &&
                performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0) ==
                    performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0),
            "Performance timing must keep kick exact and remain deterministic and tightly bounded");
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
    require(std::all_of(drums.notes.begin(), drums.notes.end(), [](const auto& note) {
                return note.pitch != 36 || std::abs(note.startBeat - std::round(note.startBeat)) < 0.035;
            }), "The local house foundation must not invent unrequested breakbeat kicks between quarter notes");
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
    require(!longPlan.rhythmMotifs.empty() &&
                std::all_of(longPlan.sections.begin(), longPlan.sections.end(), [](const auto& section) {
                    return !section.rhythm.motifId.empty();
                }),
            "Every local long-form score must carry reusable rhythmic DNA across its sections");
    std::set<std::string> longFormRhythmicIdeas;
    for (const auto& motif : longPlan.rhythmMotifs)
        longFormRhythmicIdeas.insert(motifFingerprint(motif));
    require(longFormRhythmicIdeas.size() >= 3 &&
                std::any_of(longPlan.rhythmMotifs.begin(), longPlan.rhythmMotifs.end(), [](const auto& motif) {
                    return !motif.ornaments.empty();
                }),
            "Open local rhythm planning must provide contrasting motifs and an extensible instrument vocabulary");

    const auto liveDrumPlan = SongComposer::createLocalPlan(
        "Raw live drums with a heavy backbeat, tom conversation and irregular cymbal breath",
        96, 120.0, 4.0, 55191, 0, ScaleKind::Minor);
    const auto machinePulsePlan = SongComposer::createLocalPlan(
        "Hypnotic machine pulse, displaced metallic accents and sparse asymmetrical percussion",
        96, 120.0, 4.0, 55191, 0, ScaleKind::Minor);
    require(motifFingerprint(liveDrumPlan.rhythmMotifs.front()) !=
                motifFingerprint(machinePulsePlan.rhythmMotifs.front()) &&
                (std::abs(liveDrumPlan.rhythmLanguage.backbeatGravity -
                          machinePulsePlan.rhythmLanguage.backbeatGravity) > 0.001 ||
                 std::abs(liveDrumPlan.rhythmLanguage.syncopation -
                          machinePulsePlan.rhythmLanguage.syncopation) > 0.001),
            "Different musical intentions must not collapse onto the same hidden genre template");
    require(longPlan.sections.front().startBar == 0 &&
                longPlan.sections.back().startBar + longPlan.sections.back().bars == longPlan.totalBars,
            "Song sections must cover the entire target duration without gaps");
    auto directedBreaths = 0;
    auto extendedHarmonyBars = 0;
    std::set<int> narrativePhraseLengths;
    for (const auto& section : longPlan.sections) {
        const auto directions = PhraseDirector::create(longPlan, section);
        require(directions.size() == static_cast<std::size_t>(section.bars),
                "PhraseDirector must publish one complete attention contract per bar");
        for (const auto& direction : directions) {
            narrativePhraseLengths.insert(direction.phraseBars);
            const auto leadForeground = direction.forVoice(VoiceId::Lead).participation ==
                                        Participation::Foreground;
            const auto counterForeground = direction.forVoice(VoiceId::Countermelody).participation ==
                                           Participation::Foreground;
            require(!(leadForeground && counterForeground),
                    "Only one melodic voice may own foreground attention in a bar");
            if (direction.fullBreath) ++directedBreaths;
            if (direction.harmonicHoldBars > 1) ++extendedHarmonyBars;
        }
    }
    require(directedBreaths > 0 && extendedHarmonyBars > 0,
            "The formal score must include genuine breaths and variable harmonic rhythm");
    require(narrativePhraseLengths.size() >= 2,
            "Long-form narrative planning must vary phrase length instead of imposing one eight-bar mould");

    auto authoredSection = longPlan.sections.front();
    authoredSection.motifTreatment = "Invert the central question before the arrival";
    const auto authoredNarrative = NarrativePlanner::create(longPlan, authoredSection);
    require(std::any_of(authoredNarrative.begin(), authoredNarrative.end(), [](const auto& bar) {
                return bar.transformation == MotifTransformation::Invert;
            }), "AI-authored motif treatment must reach the note-level narrative planner");

    auto tenseSection = longPlan.sections[std::min<std::size_t>(1, longPlan.sections.size() - 1)];
    tenseSection.tension = 0.92;
    tenseSection.harmonicDirection = "Sustain ambiguity before resolving home";
    const auto tenseDirections = PhraseDirector::create(longPlan, tenseSection);
    HarmonyState harmonyState;
    const auto richHarmony = HarmonyEngine::composeSection(longPlan, tenseSection,
                                                            tenseDirections, harmonyState);
    require(std::any_of(richHarmony.begin(), richHarmony.end(), [](const auto& moment) {
                return moment.voiceCount == 4 && moment.pitchClasses.size() >= 4;
            }), "High-tension harmony must expose seventh/colour tones and four-part voice leading");
    GenerationContext songFoundation = phraseContext();
    songFoundation.role = Role::Ensemble;
    SongComposer longFormComposer;
    const auto longSong = longFormComposer.render(longPlan, songFoundation);
    const auto musicalQuality = MusicalCritic::review(longSong, longPlan);
    require(musicalQuality.overall > 0.35 && musicalQuality.variation > 0.45 &&
                musicalQuality.negativeSpace > 0.30,
            "The rendered score must pass minimum symbolic quality, variation and breathing thresholds");
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
    require(longSong.markers.size() == longPlan.sections.size() && !longSong.controls.empty() &&
                !longSong.expressions.empty(),
            "A complete song must include section markers, CC curves and expressive MIDI events");
    require(std::any_of(longSong.controls.begin(), longSong.controls.end(), [](const auto& event) {
                return event.controller == 11;
            }) && std::any_of(longSong.controls.begin(), longSong.controls.end(), [](const auto& event) {
                return event.controller == 74;
            }) && std::any_of(longSong.controls.begin(), longSong.controls.end(), [](const auto& event) {
                return event.controller == 64;
            }), "Performance profiles must render dynamics, timbre and intentional pedal data");
    require(std::any_of(longSong.expressions.begin(), longSong.expressions.end(), [](const auto& event) {
                return event.type == ExpressionEventType::PitchBend && event.value != 8192;
            }) && std::any_of(longSong.expressions.begin(), longSong.expressions.end(), [](const auto& event) {
                return event.type == ExpressionEventType::ChannelPressure;
            }) && std::any_of(longSong.expressions.begin(), longSong.expressions.end(), [](const auto& event) {
                return event.type == ExpressionEventType::PolyAftertouch;
            }), "The interpreter must produce safe bends, pressure and per-note aftertouch");
    require(std::none_of(longSong.expressions.begin(), longSong.expressions.end(), [](const auto& event) {
                return event.type == ExpressionEventType::PitchBend &&
                       isVoiceInFamily(event.voice, VoiceFamily::Rhythm);
            }), "Channel pitch expression must never detune the shared drum channel");
    require(std::all_of(longSong.expressions.begin(), longSong.expressions.end(), [](const auto& event) {
                const auto maximum = event.type == ExpressionEventType::PitchBend ? 16383 : 127;
                return std::isfinite(event.beat) && event.beat >= 0.0 && event.value >= 0 &&
                       event.value <= maximum && event.channel >= 1 && event.channel <= 16;
            }), "Every generated expression event must remain valid standard MIDI data");
    require(std::all_of(longPlan.sections.begin(), longPlan.sections.end(), [](const auto& section) {
                return !section.activeVoices.empty();
            }) && std::count_if(longPlan.sections.begin(), longPlan.sections.end(), [](const auto& section) {
                return section.activeVoices.size() < voiceDefinitions.size();
            }) > static_cast<int>(longPlan.sections.size() / 2),
            "Dynamic orchestration must use intentional subsets rather than every voice everywhere");
    const auto invalidRegisterNote = std::find_if(longSong.notes.begin(), longSong.notes.end(), [&](const auto& note) {
                if (note.voice == VoiceId::CoreDrums || note.voice == VoiceId::LowPercussion ||
                    note.voice == VoiceId::HighPercussion || note.voice == VoiceId::Transitions)
                    return !(note.pitch >= 0 && note.pitch <= 127);
                const auto& definition = voiceDefinition(note.voice);
                return !(note.pitch >= definition.minimumPitch && note.pitch <= definition.maximumPitch);
            });
    require(invalidRegisterNote == longSong.notes.end(),
            invalidRegisterNote == longSong.notes.end()
                ? "Every pitched orchestration voice must remain inside its designed register"
                : "Register violation: voice=" + std::to_string(static_cast<int>(invalidRegisterNote->voice)) +
                  " pitch=" + std::to_string(invalidRegisterNote->pitch));
    require(std::all_of(longSong.notes.begin(), longSong.notes.end(), [&](const auto& note) {
                if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
                    note.voice == VoiceId::Transitions) return true;
                if (isPitchClassInScale(note.pitch, longPlan.rootPitchClass, longPlan.scale)) return true;
                if (note.voice != VoiceId::Lead && note.voice != VoiceId::Countermelody) return false;
                return note.durationBeats <= 0.36;
            }), "A rendered song may only leave its scale through brief melodic passing motion");

    std::vector<std::set<VoiceId>> voicesByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> leadByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> counterByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> kickByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> breathAtEnd(static_cast<std::size_t>(longPlan.totalBars), true);
    std::vector<int> leadOnsets(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<int> counterOnsets(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> foundationByBar(static_cast<std::size_t>(longPlan.totalBars));
    for (const auto& note : longSong.notes) {
        const auto bar = std::clamp(static_cast<int>(note.startBeat / longPlan.beatsPerBar),
                                    0, longPlan.totalBars - 1);
        voicesByBar[static_cast<std::size_t>(bar)].insert(note.voice);
        if (note.voice == VoiceId::CoreDrums && note.pitch == 36)
            kickByBar[static_cast<std::size_t>(bar)] = true;
        if (note.voice == VoiceId::Lead) {
            leadByBar[static_cast<std::size_t>(bar)] = true;
            ++leadOnsets[static_cast<std::size_t>(bar)];
        }
        if (note.voice == VoiceId::Countermelody) {
            counterByBar[static_cast<std::size_t>(bar)] = true;
            ++counterOnsets[static_cast<std::size_t>(bar)];
        }
        if (note.voice == VoiceId::HarmonicFoundation)
            foundationByBar[static_cast<std::size_t>(bar)] = true;
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
    auto simultaneousForegroundBars = 0;
    for (std::size_t bar = 0; bar < leadByBar.size(); ++bar)
        if (leadByBar[bar] && counterByBar[bar]) ++simultaneousForegroundBars;
    require(simultaneousForegroundBars == 0,
            "Lead and countermelody must exchange foreground ownership instead of piling up");
    require(*std::max_element(leadOnsets.begin(), leadOnsets.end()) <= 4 &&
                *std::max_element(counterOnsets.begin(), counterOnsets.end()) <= 3,
            "Melodic phrases must obey explicit onset budgets per bar");
    require(std::count(foundationByBar.begin(), foundationByBar.end(), true) < longPlan.totalBars * 3 / 4,
            "Harmonic foundation must sustain across variable harmonic rhythm instead of retriggering every bar");
    require(std::count(kickByBar.begin(), kickByBar.end(), false) >= longPlan.totalBars / 24,
            "The dramatic arc must contain deliberate kick withdrawals without losing its other motion");
    auto observedDrop = false;
    auto observedDouble = false;
    auto observedMutedSection = false;
    const auto hasKickNear = [&](double beat) {
        return std::any_of(longSong.notes.begin(), longSong.notes.end(), [beat](const auto& note) {
            return note.voice == VoiceId::CoreDrums && note.pitch == 36 &&
                   std::abs(note.startBeat - beat) < 0.04;
        });
    };
    for (const auto& section : longPlan.sections) {
        if (section.rhythm.kickState == KickState::Muted) {
            observedMutedSection = true;
            for (auto localBar = 0; localBar < section.bars; ++localBar) {
                const auto barStart = (section.startBar + localBar) * longPlan.beatsPerBar;
                const auto pickup = std::any_of(section.rhythm.gestures.begin(), section.rhythm.gestures.end(),
                    [localBar](const auto& gesture) {
                        return gesture.barOffset == localBar && gesture.kind == RhythmGestureKind::PickupFill;
                    });
                if (pickup)
                    require(hasKickNear(barStart + 3.50 * longPlan.beatsPerBar / 4.0) &&
                                hasKickNear(barStart + 3.75 * longPlan.beatsPerBar / 4.0),
                            "An explicit pickup must bring the kick back before the next section");
                for (const auto& note : longSong.notes)
                    if (note.voice == VoiceId::CoreDrums && note.pitch == 36 &&
                        note.startBeat >= barStart && note.startBeat < barStart + longPlan.beatsPerBar)
                        require(pickup && note.startBeat >= barStart + longPlan.beatsPerBar * 0.75,
                                "Muted kick sections may contain only an explicitly authored pickup");
            }
        }
        for (const auto& gesture : section.rhythm.gestures) {
            const auto barStart = (section.startBar + gesture.barOffset) * longPlan.beatsPerBar;
            if (gesture.kind == RhythmGestureKind::DropLastKick) {
                observedDrop = true;
                require(!hasKickNear(barStart + longPlan.beatsPerBar * 0.75),
                        "A drop-last-kick gesture must create the requested structural vacuum");
            } else if (gesture.kind == RhythmGestureKind::DoubleKick) {
                observedDouble = true;
                require(hasKickNear(barStart + gesture.beat * longPlan.beatsPerBar / 4.0),
                        "An authored double-kick gesture must survive rendering and validation");
            }
        }
    }
    require(observedDrop && observedDouble && observedMutedSection,
            "The local score must demonstrate drop, double-kick and breakdown states");
    require(std::count(breathAtEnd.begin(), breathAtEnd.end(), true) > longPlan.totalBars / 3,
            "Rhythm and bass must create frequent phrase-end breathing windows");
    require(longFormComposer.render(longPlan, songFoundation).notes == longSong.notes,
            "The same long-form plan and DNA must render deterministically");

    const auto strictHousePlan = SongComposer::createLocalPlan(
        "Progressive house con bombo en negras constante", 64, 120.0, 4.0, 7781, 0, ScaleKind::Minor);
    const auto strictHouse = longFormComposer.render(strictHousePlan, songFoundation);
    for (auto bar = 0; bar < strictHousePlan.totalBars; ++bar)
        for (auto quarter = 0; quarter < 4; ++quarter) {
            const auto beat = bar * 4.0 + quarter;
            require(std::any_of(strictHouse.notes.begin(), strictHouse.notes.end(), [beat](const auto& note) {
                return note.voice == VoiceId::CoreDrums && note.pitch == 36 &&
                       std::abs(note.startBeat - beat) < 0.04;
            }), "A requested constant four-on-the-floor kick must survive every downstream stage");
        }
    for (const auto voice : {VoiceId::CoreDrums, VoiceId::SnareClap, VoiceId::ClosedHats,
                             VoiceId::OpenHatsShaker, VoiceId::LowPercussion, VoiceId::HighPercussion})
        require(std::any_of(strictHouse.notes.begin(), strictHouse.notes.end(), [voice](const auto& note) {
            return note.voice == voice;
        }), "Each rhythm instrument must retain an independently exportable identity");

    auto mutatedPlan = strictHousePlan;
    auto& mutatedSection = mutatedPlan.sections.front();
    mutatedSection.activeVoices.push_back(VoiceId::HighPercussion);
    mutatedSection.activeVoices.push_back(VoiceId::ClosedHats);
    mutatedSection.rhythm.mutations = {
        {0, RhythmLane::HighPercussion, RhythmMutationKind::Add, 6, 1, 111, "A distinct reply"},
        {0, RhythmLane::ClosedHats, RhythmMutationKind::Ratchet, 2, 3, 90, "Accelerate the answer"}};
    SongComposer::normalizePlan(mutatedPlan);
    const auto mutatedSong = longFormComposer.render(mutatedPlan, songFoundation);
    require(std::any_of(mutatedSong.notes.begin(), mutatedSong.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HighPercussion && note.velocity > 85 &&
                       std::abs(note.startBeat - 1.5) < 0.04;
            }) && std::count_if(mutatedSong.notes.begin(), mutatedSong.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::ClosedHats && note.startBeat >= 0.5 && note.startBeat < 0.75;
            }) >= 2,
            "Open rhythm mutations must produce audible, deterministic development of the shared cell");

    SongPlan contradictoryPlan = longPlan;
    contradictoryPlan.key = "F# major";
    contradictoryPlan.motifIntervals = {0, 1, 6, 11};
    SongComposer::normalizePlan(contradictoryPlan);
    require(contradictoryPlan.key == "C minor" &&
                std::all_of(contradictoryPlan.motifIntervals.begin(),
                            contradictoryPlan.motifIntervals.end(), [](int interval) {
                    return isPitchClassInScale(interval, 0, ScaleKind::Minor);
                }), "Key metadata and thematic DNA must obey one canonical tonal contract");

    Pattern brokenHarmony;
    brokenHarmony.lengthBeats = 8.0;
    brokenHarmony.notes = {
        {0.0, 0.5, 61, 90, 2, VoiceId::Lead},
        {0.5, 0.2, 61, 78, 2, VoiceId::Lead},
        {0.75, 0.5, 62, 86, 2, VoiceId::Lead},
        {0.0, 8.0, 64, 68, 3, VoiceId::HarmonicFoundation},
        {0.0, 1.0, 72, 45, 8, VoiceId::Atmosphere},
        {0.0, 1.0, 71, 82, 2, VoiceId::Lead}
    };
    const std::vector<std::vector<int>> changingHarmony{{0, 4, 7, 11}, {5, 9, 0}};
    const auto repair = repairTonalContract(brokenHarmony, 0, ScaleKind::Major, 4.0,
                                            changingHarmony, 0.20);
    require(repair.outOfScaleRepaired >= 1 && repair.intentionalChromaticNotes == 1 &&
                repair.harmonicOverlapsTrimmed == 1 && repair.verticalCollisionsRepaired >= 1,
            "Tonal validation must repair structural errors while preserving prepared passing notes");
    require(isPitchClassInScale(brokenHarmony.notes.front().pitch, 0, ScaleKind::Major),
            "An unsupported chromatic note on a strong beat must be repaired into the declared key");
    const auto compactPlan = SongComposer::createLocalPlan(
        "A slow compact song", 30, 30.0, 4.0, 31, 5, ScaleKind::Dorian);
    require(compactPlan.totalBars == 8 && !compactPlan.sections.empty() &&
                compactPlan.sections.back().startBar + compactPlan.sections.back().bars == 8,
            "Even the shortest slow song must form a valid contiguous dramatic arc");
}
