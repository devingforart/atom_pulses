#include "TestSupport.h"

#include "core/Generator.h"
#include "core/HarmonyEngine.h"
#include "core/MusicalCritic.h"
#include "core/MusicalIdentityGate.h"
#include "core/NarrativeScore.h"
#include "core/CompositionModel.h"
#include "core/PhraseDirector.h"
#include "core/PerformanceExpression.h"
#include "core/PerformanceTiming.h"
#include "core/RhythmEngine.h"
#include "core/Scale.h"
#include "core/SongComposer.h"
#include "core/TonalContract.h"
#include "core/VerticalHarmonyGate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
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
                           {1.46, 0.69, 67, 88, 2, VoiceId::Lead},
                           {3.0 / 7.0, 5.0 / 7.0, 64, 82, 2, VoiceId::Lead, 0, true}};
    timingPattern.controls = {{2.87, 11, 90, 2, VoiceId::Lead},
                              {5.0 / 7.0, 1, 76, 2, VoiceId::Lead, 0, true}};
    quantizePatternTiming(timingPattern, 4);
    require(std::all_of(timingPattern.notes.begin(), timingPattern.notes.end(), [](const auto& note) {
                if (note.authoredTiming) return true;
                return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.000001 &&
                       std::abs(note.endBeat() * 16.0 - std::round(note.endBeat() * 16.0)) < 0.000001;
            }) && std::abs(timingPattern.controls.front().beat * 16.0 -
                           std::round(timingPattern.controls.front().beat * 16.0)) < 0.000001,
            "Stored onsets must be exact while note-offs and controls retain fine articulation");
    require(std::any_of(timingPattern.notes.begin(), timingPattern.notes.end(), [](const auto& note) {
                return note.authoredTiming && std::abs(note.startBeat - 3.0 / 7.0) < 0.000001 &&
                       std::abs(note.durationBeats - 5.0 / 7.0) < 0.000001;
            }) && std::abs(timingPattern.controls.back().beat - 5.0 / 7.0) < 0.000001,
            "AI-authored tuplets and control timing must survive the publication quantizer exactly");
    require(performanceOffsetBeats(timingPattern.notes.front(), 42, 0, 120.0) == 0.0 &&
                std::abs(performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0)) <= 0.008 &&
                performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0) ==
                    performanceOffsetBeats(timingPattern.notes.back(), 42, 1, 120.0),
            "Performance timing must keep kick exact and remain deterministic and tightly bounded");
    Pattern publicationTiming;
    publicationTiming.lengthBeats = 4.0;
    publicationTiming.notes = {{0.625, 0.37, 42, 90, 10, VoiceId::ClosedHats, 0, true}};
    ProductionPolish::enforceMetricContract(publicationTiming);
    require(std::abs(publicationTiming.notes.front().startBeat * 4.0 -
                     std::round(publicationTiming.notes.front().startBeat * 4.0)) < 0.000001 &&
                !publicationTiming.notes.front().authoredTiming,
            "Published MIDI must be exact even when a legacy AI score requested baked displacement");
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
    require(longPlan.totalBars == 272 && longPlan.sections.size() >= 8 &&
                SongComposer::phraseAlignedBars(225) == 224,
            "Long songs must approximate wall-clock duration with a phrase-aligned complete form");
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
    const auto clubPlan = SongComposer::createLocalPlan(
        "Dark electronic club track for a DJ set, physical four on the floor groove, evolving hook and breakdown",
        192, 124.0, 4.0, 88117, 2, ScaleKind::Minor);
    require(clubPlan.productionLanguage.domain == ProductionDomain::ClubElectronic &&
                clubPlan.productionLanguage.electronicIntent >= 0.85 &&
                clubPlan.timbrePalette.acousticElectronicBalance >= 0.85 &&
                clubPlan.productionModeSource == "local_inference",
            "Club intent must activate an electronic production grammar rather than orchestral defaults");
    const std::set<std::string> electronicCatalog{"kick_drum", "snare_clap", "hi_hats",
        "shakers", "latin_percussion", "orchestral_percussion", "sub_synth", "electric_bass",
        "poly_synth", "analog_pad", "lead_synth", "ambient_texture", "cymbals"};
    std::string clubCast;
    for (const auto& part : clubPlan.instruments) clubCast += part.instrumentId + ",";
    require(clubPlan.instruments.size() >= 12 &&
                std::all_of(clubPlan.instruments.begin(), clubPlan.instruments.end(), [&](const auto& part) {
                    return electronicCatalog.contains(part.instrumentId) && part.divisiVoices == 1;
                }),
            "Electronic production must use functional drum, bass, synth, hook and FX roles without implicit orchestra: " + clubCast);
    require(std::all_of(clubPlan.sections.begin(), clubPlan.sections.end(), [](const auto& section) {
                const auto melodicOwners = std::count(section.activeVoices.begin(), section.activeVoices.end(), VoiceId::Lead) +
                    std::count(section.activeVoices.begin(), section.activeVoices.end(), VoiceId::Countermelody);
                return melodicOwners <= 1;
            }), "A club arrangement must assign at most one foreground melodic owner per section");
    CompositionRenderReport clubReport;
    const auto renderedClub = SongComposer{}.render(clubPlan, phraseContext(), {}, &clubReport);
    require(renderedClub.productionReady && clubReport.electronicProduction.active &&
                clubReport.electronicProduction.lowEndCollisionsAfter <=
                    clubReport.electronicProduction.lowEndCollisionsBefore &&
                clubReport.electronicProduction.automationEventsAdded > 0 &&
                renderedClub.acousticElectronicBalance >= 0.85,
            "The electronic director must publish safe low end, structural automation and an audible club contract");
    require(std::none_of(renderedClub.notes.begin(), renderedClub.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HarmonicPulse && note.durationBeats > 1.001;
            }),
            "Electronic harmonic punctuation must never become a six-beat hanging chord");
    require(std::none_of(renderedClub.notes.begin(), renderedClub.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HarmonicFoundation && note.durationBeats > 3.938;
            }),
            "Electronic chord bodies must release before the next bar-level harmonic boundary");
    require((clubReport.electronicProduction.thematicWindows < 6 ||
             clubReport.electronicProduction.thematicRecurrenceRatio >= 0.24) &&
            (clubReport.electronicProduction.percussionNotes < 12 ||
             clubReport.electronicProduction.percussionArticulations >= 3),
            "The publication contract must preserve hook memory and audible percussion articulation");
    require(std::none_of(renderedClub.notes.begin(), renderedClub.notes.end(), [](const auto& note) {
                return !isVoiceInFamily(note.voice, VoiceFamily::Rhythm) &&
                       note.voice != VoiceId::Transitions && note.durationBeats < 0.124;
            }),
            "The audible production gate must remove or repair instrumental micro-fragments");
    require(clubReport.electronicProduction.maximumKicklessBarsAfter <= 16,
            "Published club arrangements must retain a macro pulse outside declared full silence");
    require(std::none_of(renderedClub.notes.begin(), renderedClub.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HarmonicUpper && note.pitch < 60;
            }),
            "Final continuity repairs must not pull upper harmony below its published register");
    require(std::none_of(renderedClub.notes.begin(), renderedClub.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HighPercussion && (note.pitch == 35 || note.pitch == 36);
            }),
            "Final high percussion must never collapse into another kick lane");
    auto consecutiveForegroundSilence = 0;
    auto maximumForegroundSilence = 0;
    for (auto start = 0.0; start < renderedClub.lengthBeats; start += clubPlan.beatsPerBar * 8.0) {
        const auto end = std::min(renderedClub.lengthBeats, start + clubPlan.beatsPerBar * 8.0);
        const auto activeForeground = std::any_of(renderedClub.notes.begin(), renderedClub.notes.end(),
            [&](const auto& note) {
                const auto part = std::find_if(renderedClub.parts.begin(), renderedClub.parts.end(),
                    [&](const auto& candidate) { return candidate.id == note.partId; });
                return note.startBeat < end && note.endBeat() > start &&
                       ((part != renderedClub.parts.end() &&
                         part->department == ScoreDepartment::Melody) ||
                        note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody);
            });
        consecutiveForegroundSilence = activeForeground ? 0 : consecutiveForegroundSilence + 1;
        maximumForegroundSilence = std::max(maximumForegroundSilence, consecutiveForegroundSilence);
    }
    require(maximumForegroundSilence <= 1,
            "A club arrangement may breathe for eight bars but never lose foreground narrative for 32 seconds");

    auto identityPlan = clubPlan;
    identityPlan.totalBars = 16;
    identityPlan.sections.clear();
    SongSection identitySection;
    identitySection.name = "Identity test peak";
    identitySection.startBar = 0;
    identitySection.bars = 16;
    identitySection.energy = 0.82;
    identitySection.density = 0.74;
    identityPlan.sections.push_back(identitySection);
    Pattern identityPattern;
    identityPattern.lengthBeats = 64.0;
    identityPattern.parts = {
        {1, "hi_hats", "Stable Hats", VoiceId::ClosedHats, ScoreDepartment::Rhythm,
         "Groove", 42, 46, 0.7, InstrumentSoundModel::Hats, "body", "detached", 1,
         "Drum Rack", "short closed hats"},
        {2, "orchestral_percussion", "Boundary FX", VoiceId::HighPercussion,
         ScoreDepartment::Rhythm, "Arrivals only", 35, 81, 0.4,
         InstrumentSoundModel::Timpani, "transition", "detached", 1,
         "Drum Rack", "reverse cymbal impact"},
        {3, "lead_synth", "Hook", VoiceId::Lead, ScoreDepartment::Melody,
         "Primary hook", 48, 96, 0.9, InstrumentSoundModel::LeadSynth, "body", "legato", 1,
         "Meld", "expressive mono hook"},
        {4, "alto_flute", "Answer", VoiceId::Countermelody, ScoreDepartment::Melody,
         "Derived response", 55, 88, 0.6, InstrumentSoundModel::Flute, "counterpoint", "legato", 1,
         "Instrument Rack", "breathy alto flute"},
        {5, "guitar", "Second Answer", VoiceId::Countermelody, ScoreDepartment::Melody,
         "Independent derived response", 48, 84, 0.55, InstrumentSoundModel::Guitar,
         "counterpoint", "detached", 1, "Instrument Rack", "warm clean guitar"}
    };
    for (const auto beat : {0.5, 2.5, 4.5, 8.5, 10.5, 12.5})
        identityPattern.notes.push_back({beat, 0.04, 42, 58, 10, VoiceId::ClosedHats, 1});
    for (const auto beat : {32.25, 33.75, 36.25, 40.25, 42.25, 45.75})
        identityPattern.notes.push_back({beat, 0.04, 42, 62, 10, VoiceId::ClosedHats, 1});
    for (auto beat = 4.0; beat < 60.0; beat += 4.0)
        identityPattern.notes.push_back({beat, 0.05, 63, 54, 10, VoiceId::HighPercussion, 2});
    identityPattern.notes.push_back({60.0, 3.5, 49, 82, 10, VoiceId::HighPercussion, 2});
    identityPattern.notes.push_back({63.75, 0.10, 56, 91, 10, VoiceId::HighPercussion, 2});
    identityPattern.notes.push_back({0.0, 0.5, 64, 90, 3, VoiceId::Lead, 3});
    identityPattern.notes.push_back({1.0, 0.5, 67, 86, 3, VoiceId::Lead, 3});
    identityPattern.notes.push_back({2.0, 0.5, 71, 92, 3, VoiceId::Lead, 3});
    identityPattern.notes.push_back({3.0, 0.5, 69, 80, 3, VoiceId::Lead, 3});
    for (const auto [beat, pitch] : std::array<std::pair<double, int>, 4>{{
             {32.5, 74}, {33.5, 61}, {34.5, 76}, {35.5, 58}}})
        identityPattern.notes.push_back({beat, 0.5, pitch, 72, 4, VoiceId::Countermelody, 4});
    for (const auto [beat, pitch] : std::array<std::pair<double, int>, 4>{{
             {40.25, 48}, {41.25, 83}, {42.25, 51}, {43.25, 80}}})
        identityPattern.notes.push_back({beat, 0.4, pitch, 70, 5, VoiceId::Countermelody, 5});
    const auto identityReport = MusicalIdentityGate::enforce(identityPattern, identityPlan);
    const auto remainingTransitions = std::count_if(identityPattern.notes.begin(), identityPattern.notes.end(),
        [](const auto& note) { return note.partId == 2; });
    require(identityReport.grooveRecallRatio >= 0.99 &&
                identityReport.responseLineageRatio >= 0.99 && identityReport.responseParts == 2 &&
                identityReport.transitionNotesRemoved >= 10 && remainingTransitions <= 2 &&
                std::all_of(identityPattern.notes.begin(), identityPattern.notes.end(), [](const auto& note) {
                    return note.partId != 1 || note.durationBeats >= 0.124;
                }),
            "The identity gate must recall groove DNA, derive responses, restrain transition lanes and author playable gates");

    auto developedGroovePlan = identityPlan;
    developedGroovePlan.totalBars = 32;
    developedGroovePlan.sections.front().bars = 32;
    Pattern developedGroove;
    developedGroove.lengthBeats = 128.0;
    developedGroove.parts = {identityPattern.parts.front()};
    for (auto window = 0; window < 4; ++window)
        for (auto bar = 0; bar < 8; ++bar)
            for (const auto beat : {0.5, 2.5})
                developedGroove.notes.push_back({window * 32.0 + bar * 4.0 + beat, 0.125, 42,
                    58, 10, VoiceId::ClosedHats, 1});
    const auto developedGrooveReport = MusicalIdentityGate::enforce(
        developedGroove, developedGroovePlan);
    std::set<std::string> grooveWindows;
    for (auto window = 0; window < 4; ++window) {
        std::string fingerprint;
        for (const auto& note : developedGroove.notes)
            if (note.startBeat >= window * 32.0 && note.startBeat < (window + 1) * 32.0)
                fingerprint += std::to_string(std::lround((note.startBeat - window * 32.0) * 4.0)) +
                    ":" + std::to_string(note.pitch) + "|";
        grooveWindows.insert(std::move(fingerprint));
    }
    require(developedGrooveReport.groovePhraseDevelopments == 4 &&
                developedGrooveReport.grooveDevelopmentNotes >= 2 && grooveWindows.size() >= 3,
            "Groove recall must preserve its skeleton while developing phrase endings instead of cloning whole windows: phrases=" +
                std::to_string(developedGrooveReport.groovePhraseDevelopments) + ", notes=" +
                std::to_string(developedGrooveReport.grooveDevelopmentNotes) + ", windows=" +
                std::to_string(grooveWindows.size()));

    Pattern verticalPattern;
    verticalPattern.lengthBeats = 4.0;
    verticalPattern.parts = {
        {1, "sub_synth", "Clean Sine Sub", VoiceId::SubBass, ScoreDepartment::Harmony,
         "Low foundation", 24, 48, 0.8, InstrumentSoundModel::SubSynth, "foundation", "legato", 1,
         "Operator", "clean mono sine sub"},
        {2, "analog_pad", "Warm Pad", VoiceId::HarmonicFoundation, ScoreDepartment::Harmony,
         "Harmonic support", 36, 84, 0.6, InstrumentSoundModel::AnalogPad, "body", "legato", 1,
         "Wavetable", "warm analog pad"}
    };
    verticalPattern.notes = {
        {0.25, 0.25, 34, 84, 1, VoiceId::SubBass, 1},
        {0.0, 1.0, 45, 68, 2, VoiceId::HarmonicFoundation, 2}
    };
    const auto verticalReport = VerticalHarmonyGate::enforce(verticalPattern);
    require(verticalReport.collisionsBefore == 1 && verticalReport.collisionsAfter == 0 &&
                verticalReport.supportNotesDucked == 1 &&
                VerticalHarmonyGate::audit(verticalPattern) == 0 &&
                std::all_of(verticalPattern.notes.begin(), verticalPattern.notes.end(), [](const auto& note) {
                    return note.pitch == 34 || note.pitch == 45;
                }),
            "Vertical harmony must preserve legal pitches while carving low semitone clashes out of sustained support");

    Pattern lateUpper;
    lateUpper.lengthBeats = 8.0;
    lateUpper.parts = {{1, "analog_pad", "Upper Air", VoiceId::HarmonicUpper,
        ScoreDepartment::Harmony, "Selective upper extension", 36, 96, 0.5,
        InstrumentSoundModel::AnalogPad, "extension", "sustained", 1,
        "Wavetable", "thin high spectral pad"}};
    lateUpper.notes = {{0.0, 2.0, 36, 64, 5, VoiceId::HarmonicUpper, 1}};
    const auto lateRegisterRepairs = OrchestrationScore::enforcePublishedRegisters(lateUpper);
    require(lateRegisterRepairs == 1 && lateUpper.notes.front().pitch >= 60 &&
                positiveModulo(lateUpper.notes.front().pitch, 12) == 0,
            "A post-orchestration Upper Air note must remain upper without changing pitch class");

    auto authoredClub = clubPlan;
    authoredClub.timbrePalette.description =
        "Dark felt piano and breathy alto-flute answers inside a restrained club spectrum";
    authoredClub.instruments.push_back({"authored_flute", "alto_flute", "Breathy Alto Flute",
        VoiceId::Countermelody, "Answers the hook", 55, 88, 0, 0.86, 0.84, 0.0, {}});
    SongComposer::normalizePlan(authoredClub);
    require(std::any_of(authoredClub.instruments.begin(), authoredClub.instruments.end(), [](const auto& part) {
                return part.id == "authored_flute" && part.instrumentId == "alto_flute";
            }) &&
            std::any_of(authoredClub.instruments.begin(), authoredClub.instruments.end(), [](const auto& part) {
                return part.instrumentId == "piano";
            }),
            "Club normalization must preserve GPT's authored cast and materialize instruments named by its sound world");

    auto closedElectronicCast = clubPlan;
    closedElectronicCast.instrumentCastAuthored = true;
    closedElectronicCast.instruments.erase(std::remove_if(closedElectronicCast.instruments.begin(),
        closedElectronicCast.instruments.end(), [](const auto& instrument) {
            return instrument.sourceVoice == VoiceId::HighPercussion ||
                   instrument.sourceVoice == VoiceId::HarmonicUpper;
        }), closedElectronicCast.instruments.end());
    SongComposer::normalizePlan(closedElectronicCast);
    require(std::none_of(closedElectronicCast.instruments.begin(), closedElectronicCast.instruments.end(),
                [](const auto& instrument) {
                    return instrument.sourceVoice == VoiceId::HighPercussion ||
                           instrument.sourceVoice == VoiceId::HarmonicUpper ||
                           instrument.role == "Dedicated orchestral owner";
                }) &&
            std::none_of(closedElectronicCast.voices.begin(), closedElectronicCast.voices.end(),
                [](const auto& voice) {
                    return voice.id == VoiceId::HighPercussion || voice.id == VoiceId::HarmonicUpper;
                }) && closedElectronicCast.implicitVoicesPruned >= 2,
            "An AI-authored electronic cast must prune each unowned voice instead of reopening the whole orchestra");

    auto macroPulsePlan = clubPlan;
    macroPulsePlan.totalBars = 48;
    macroPulsePlan.sections.resize(1);
    macroPulsePlan.sections.front().startBar = 0;
    macroPulsePlan.sections.front().bars = 48;
    macroPulsePlan.sections.front().name = "Extended breakdown";
    macroPulsePlan.sections.front().function = "Sustain tension with percussion and atmosphere";
    macroPulsePlan.sections.front().rhythm.kickState = KickState::Muted;
    macroPulsePlan.sections.front().rhythm.continuity = KickContinuity::Sectional;
    Pattern macroPulse;
    macroPulse.lengthBeats = 192.0;
    const auto macroPulseReport = ElectronicProductionDirector::shapePerformance(
        macroPulse, macroPulsePlan);
    require(macroPulseReport.maximumKicklessBarsBefore == 48 &&
                macroPulseReport.maximumKicklessBarsAfter <= 16 &&
                macroPulseReport.macroKickAnchorBarsCreated >= 2,
            "A club breakdown may breathe but cannot lose every pulse anchor for longer than sixteen bars: before=" +
                std::to_string(macroPulseReport.maximumKicklessBarsBefore) + ", after=" +
                std::to_string(macroPulseReport.maximumKicklessBarsAfter) + ", anchors=" +
                std::to_string(macroPulseReport.macroKickAnchorBarsCreated));

    auto bassDevelopmentPlan = clubPlan;
    bassDevelopmentPlan.productionLanguage.grooveEvolution = 0.40;
    bassDevelopmentPlan.totalBars = 16;
    bassDevelopmentPlan.sections.resize(1);
    bassDevelopmentPlan.sections.front().startBar = 0;
    bassDevelopmentPlan.sections.front().bars = 16;
    Pattern repeatedBassPattern;
    repeatedBassPattern.lengthBeats = 64.0;
    InstrumentPart movementPart;
    movementPart.id = 1;
    movementPart.catalogId = "electric_bass";
    movementPart.name = "Movement Bass";
    movementPart.sourceVoice = VoiceId::MovementBass;
    movementPart.department = ScoreDepartment::Harmony;
    movementPart.minimumPitch = 36;
    movementPart.maximumPitch = 60;
    repeatedBassPattern.parts.push_back(movementPart);
    for (auto bar = 0; bar < 16; ++bar) {
        repeatedBassPattern.notes.push_back({bar * 4.0 + 0.75, 0.25, 43, 76, 6,
            VoiceId::MovementBass, 1, true});
        for (auto beat = 0; beat < 4; ++beat)
            repeatedBassPattern.notes.push_back({bar * 4.0 + beat, 0.125, 36, 92, 10,
                VoiceId::CoreDrums, 0, true});
    }
    const auto bassDevelopmentReport = ElectronicProductionDirector::shapePerformance(
        repeatedBassPattern, bassDevelopmentPlan);
    require(bassDevelopmentReport.bassPhraseDevelopmentsCreated == 1 &&
                bassDevelopmentReport.bassNotesDeveloped == 1 &&
                std::all_of(repeatedBassPattern.notes.begin(), repeatedBassPattern.notes.end(), [](const auto& note) {
                    return std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) < 0.001;
                }),
            "A repeated movement-bass phrase must develop its ending while remaining on the strict grid: phrases=" +
                std::to_string(bassDevelopmentReport.bassPhraseDevelopmentsCreated) + ", notes=" +
                std::to_string(bassDevelopmentReport.bassNotesDeveloped));

    Pattern semanticPercussion;
    semanticPercussion.lengthBeats = 64.0;
    for (auto index = 0; index < 24; ++index) {
        semanticPercussion.notes.push_back({index * 0.5, 0.08, 36, 70, 10, VoiceId::HighPercussion});
        semanticPercussion.notes.push_back({index * 0.5 + 0.25, 0.08, 46, 58, 10, VoiceId::OpenHatsShaker});
        if (index < 8)
            semanticPercussion.notes.push_back({index * 1.25, 0.08, 45, 62, 10, VoiceId::LowPercussion});
    }
    const auto semanticReport = RhythmEngine::enforceContract(semanticPercussion, clubPlan);
    std::set<int> highArticulations;
    std::set<int> topArticulations;
    std::set<int> lowArticulations;
    for (const auto& note : semanticPercussion.notes) {
        if (note.voice == VoiceId::HighPercussion) highArticulations.insert(note.pitch);
        if (note.voice == VoiceId::OpenHatsShaker) topArticulations.insert(note.pitch);
        if (note.voice == VoiceId::LowPercussion) lowArticulations.insert(note.pitch);
    }
    require(semanticReport.semanticPitchRepairs > 0 &&
                semanticReport.articulationDiversifications > 0 &&
                highArticulations.size() >= 3 && topArticulations.size() >= 2 &&
                !highArticulations.contains(36) &&
                std::none_of(lowArticulations.begin(), lowArticulations.end(), [](auto pitch) {
                    return pitch >= 41 && pitch <= 50;
                }),
            "Authored percussion must be repaired into diverse GM articulations before orchestration");

    Pattern latePercussion;
    latePercussion.lengthBeats = 16.0;
    for (auto index = 0; index < 24; ++index)
        latePercussion.notes.push_back({index * 0.5, 0.125, 36, 62, 10,
            VoiceId::HighPercussion, 1});
    const auto latePercussionReport = RhythmEngine::enforceSemanticArticulations(
        latePercussion, clubPlan);
    std::set<int> lateArticulations;
    for (const auto& note : latePercussion.notes) lateArticulations.insert(note.pitch);
    require(latePercussionReport.semanticPitchRepairs == 24 && lateArticulations.size() >= 3 &&
                !lateArticulations.contains(36),
            "Late high-percussion notes must become concrete GM articulations without changing their attacks");

    Pattern qualityWarningPattern;
    qualityWarningPattern.productionReady = true;
    qualityWarningPattern.productionScore = 0.92;
    ElectronicProductionReport qualityOnly;
    qualityOnly.active = true;
    qualityOnly.score = 0.72;
    qualityOnly.thematicWindows = 8;
    qualityOnly.thematicRecurrenceRatio = 0.0;
    qualityOnly.percussionNotes = 24;
    qualityOnly.percussionArticulations = 1;
    qualityOnly.expectedEssentialInstruments = 2;
    qualityOnly.materializedEssentialInstruments = 1;
    ElectronicProductionDirector::stamp(qualityWarningPattern, qualityOnly);
    require(qualityWarningPattern.productionReady &&
                std::count_if(qualityWarningPattern.productionIssues.begin(),
                    qualityWarningPattern.productionIssues.end(), [](const auto& issue) {
                        return issue.starts_with("warning:");
                    }) >= 3,
            "Recoverable musical-quality observations must warn without triggering the integrity gate");
    auto hybridPlan = SongComposer::createLocalPlan(
        "Deep electronic club production with an orchestral chamber dialogue", 64, 122.0, 4.0,
        88118, 2, ScaleKind::Minor);
    require(hybridPlan.productionLanguage.domain == ProductionDomain::Hybrid,
            "A genuinely hybrid request must preserve both production domains");
    Pattern repeatedPercussion;
    repeatedPercussion.lengthBeats = 40.0;
    for (auto bar = 0; bar < 10; ++bar) {
        repeatedPercussion.notes.push_back({bar * 4.0 + 0.5, 0.125, 64, 88, 10, VoiceId::LowPercussion});
        repeatedPercussion.notes.push_back({bar * 4.0 + 2.5, 0.125, 65, 82, 10, VoiceId::LowPercussion});
    }
    const auto hybridShape = ElectronicProductionDirector::shapePerformance(repeatedPercussion, hybridPlan);
    const auto hybridAudit = ElectronicProductionDirector::audit(repeatedPercussion, hybridPlan);
    require(hybridShape.active && hybridAudit.active && hybridShape.rhythmNotesEvolved > 0 &&
                hybridAudit.maximumRhythmRun <= 4,
            "Hybrid electronic music must receive the same groove-evolution critic as club music");

    auto peakPlan = clubPlan;
    auto& peakSection = *std::max_element(peakPlan.sections.begin(), peakPlan.sections.end(),
        [](const auto& left, const auto& right) { return left.bars < right.bars; });
    peakSection.bars = std::max(16, peakSection.bars);
    peakSection.energy = 0.94;
    peakSection.density = 0.92;
    Pattern peakPattern;
    peakPattern.lengthBeats = peakPlan.totalBars * peakPlan.beatsPerBar;
    constexpr std::array peakVoices{VoiceId::CoreDrums, VoiceId::SnareClap, VoiceId::ClosedHats,
        VoiceId::OpenHatsShaker, VoiceId::LowPercussion, VoiceId::HighPercussion,
        VoiceId::SubBass, VoiceId::MovementBass, VoiceId::HarmonicFoundation,
        VoiceId::HarmonicPulse, VoiceId::HarmonicUpper, VoiceId::Lead,
        VoiceId::Countermelody, VoiceId::Atmosphere, VoiceId::Transitions};
    for (auto localBar = 0; localBar < 16; ++localBar) {
        const auto start = (peakSection.startBar + localBar) * peakPlan.beatsPerBar;
        for (auto quarter = 0; quarter < 4; ++quarter)
            peakPattern.notes.push_back({start + quarter, 0.10, 36, 100, 10, VoiceId::CoreDrums});
        peakPattern.notes.push_back({start + 3.5, 0.08, 36, 82, 10, VoiceId::CoreDrums});
        for (const auto voice : peakVoices) {
            if (voice == VoiceId::CoreDrums) continue;
            const auto rhythm = isVoiceInFamily(voice, VoiceFamily::Rhythm);
            peakPattern.notes.push_back({start + (rhythm ? 0.5 : 0.0),
                voice == VoiceId::HarmonicPulse ? 6.75 : 0.5,
                rhythm ? 62 : 60, 78, rhythm ? 10 : voiceDefinition(voice).midiChannel, voice});
            if (voice == VoiceId::LowPercussion)
                peakPattern.notes.push_back({start + 2.5, 0.15, 64, 72, 10, voice});
            if (voice == VoiceId::HarmonicFoundation)
                peakPattern.notes.push_back({start, 3.5, 64, 70, voiceDefinition(voice).midiChannel, voice});
        }
    }
    const auto peakShape = ElectronicProductionDirector::shapePerformance(peakPattern, peakPlan);
    const auto peakAudit = ElectronicProductionDirector::audit(peakPattern, peakPlan);
    require(peakShape.kickOrnamentsRemoved >= 14 && peakShape.phraseVariationsCreated > 0 &&
                peakShape.harmonicBreathsCreated > 0 && peakShape.supportNotesRotated > 0 &&
                peakAudit.kickOrnamentRatio <= 0.08 && peakAudit.peakActiveVoices <= 12 &&
                peakAudit.maximumHarmonicRun <= 8,
            "Electronic peaks must ration kick ornaments, evolve phrases, breathe harmony and rotate support roles");
    require(motifFingerprint(liveDrumPlan.rhythmMotifs.front()) !=
                motifFingerprint(machinePulsePlan.rhythmMotifs.front()) &&
                (std::abs(liveDrumPlan.rhythmLanguage.backbeatGravity -
                          machinePulsePlan.rhythmLanguage.backbeatGravity) > 0.001 ||
                 std::abs(liveDrumPlan.rhythmLanguage.syncopation -
                          machinePulsePlan.rhythmLanguage.syncopation) > 0.001),
            "Different musical intentions must not collapse onto the same hidden genre template");
    require(liveDrumPlan.chordPalette.size() >= 4 && machinePulsePlan.chordPalette.size() >= 4 &&
                (liveDrumPlan.chordPalette[2].rootPitchClass !=
                     machinePulsePlan.chordPalette[2].rootPitchClass ||
                 liveDrumPlan.chordPalette[2].pitchClasses !=
                     machinePulsePlan.chordPalette[2].pitchClasses ||
                 std::abs(liveDrumPlan.harmonicLanguage.extensionRichness -
                          machinePulsePlan.harmonicLanguage.extensionRichness) > 0.001 ||
                 std::abs(liveDrumPlan.harmonicLanguage.inversionMotion -
                          machinePulsePlan.harmonicLanguage.inversionMotion) > 0.001),
            "The offline safety composer must derive harmonic vocabulary from full direction, not one fixed progression");
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
    require(std::any_of(richHarmony.begin(), richHarmony.end(), [](const auto& bar) {
                return std::any_of(bar.begin(), bar.end(), [](const auto& moment) {
                    return moment.voiceCount == 4 && moment.pitchClasses.size() >= 4;
                });
            }), "High-tension harmony must expose seventh/colour tones and four-part voice leading");
    auto authoredHarmonyPlan = longPlan;
    authoredHarmonyPlan.harmonicLanguage.tonalPolicy = TonalPolicy::Expanded;
    auto& authoredHarmonySection = authoredHarmonyPlan.sections.front();
    authoredHarmonySection.tonalCenterPitchClass = positiveModulo(longPlan.rootPitchClass + 5, 12);
    authoredHarmonySection.modeHint = "temporary modal centre with chromatic threshold";
    authoredHarmonyPlan.chordPalette.push_back({"slash_chromatic", "Chromatic slash colour",
        positiveModulo(longPlan.rootPitchClass + 1, 12), positiveModulo(longPlan.rootPitchClass + 8, 12),
        {positiveModulo(longPlan.rootPitchClass + 1, 12), positiveModulo(longPlan.rootPitchClass + 5, 12),
         positiveModulo(longPlan.rootPitchClass + 8, 12), positiveModulo(longPlan.rootPitchClass + 11, 12)},
        HarmonicFunction::Chromatic, VoicingStrategy::Drop2, 0.82});
    authoredHarmonySection.harmonicEvents = {
        {0, 0.0, authoredHarmonyPlan.chordPalette.front().id, 0.45, "Establish"},
        {0, authoredHarmonyPlan.beatsPerBar * 0.5, "slash_chromatic", 0.88, "Pivot inside the bar"},
        {1, 0.0, authoredHarmonyPlan.chordPalette.back().id, 0.72, "Confirm the new colour"}};
    SongComposer::normalizePlan(authoredHarmonyPlan);
    const auto authoredDirections = PhraseDirector::create(authoredHarmonyPlan,
                                                            authoredHarmonyPlan.sections.front());
    HarmonyState authoredHarmonyState;
    const auto authoredTimeline = HarmonyEngine::composeSection(authoredHarmonyPlan,
        authoredHarmonyPlan.sections.front(), authoredDirections, authoredHarmonyState);
    require(authoredTimeline.front().size() == 2 &&
                std::abs(authoredTimeline.front()[1].beatOffset - authoredHarmonyPlan.beatsPerBar * 0.5) < 0.001 &&
                authoredTimeline.front()[1].bassPitchClass == positiveModulo(longPlan.rootPitchClass + 8, 12) &&
                authoredHarmonyPlan.sections.front().tonalCenterPitchClass ==
                    positiveModulo(longPlan.rootPitchClass + 5, 12),
            "HarmonicLanguage must preserve sub-bar changes, slash bass and section-level modulation");
    Pattern authoredHarmonyBar;
    authoredHarmonyBar.lengthBeats = authoredHarmonyPlan.beatsPerBar;
    HarmonyEngine::renderBar(authoredHarmonyBar, authoredHarmonyPlan,
        authoredHarmonyPlan.sections.front(), authoredDirections.front(), authoredTimeline.front(), 0, 1);
    require(std::any_of(authoredHarmonyBar.notes.begin(), authoredHarmonyBar.notes.end(), [&](const auto& note) {
                return note.voice == VoiceId::HarmonicFoundation &&
                       std::abs(note.startBeat - authoredHarmonyPlan.beatsPerBar * 0.5) < 0.001;
            }), "A sub-bar AI harmonic event must become an audible foundation chord, not metadata only");
    GenerationContext songFoundation = phraseContext();
    songFoundation.role = Role::Ensemble;
    SongComposer longFormComposer;
    CompositionRenderReport deepReport;
    const auto longSong = longFormComposer.render(longPlan, songFoundation, {}, &deepReport);
    const auto musicalQuality = MusicalCritic::review(longSong, longPlan);
    require(musicalQuality.overall > 0.35 && musicalQuality.variation > 0.45 &&
                musicalQuality.negativeSpace > 0.30,
            "The rendered score must pass minimum symbolic quality, variation and breathing thresholds: overall=" +
                std::to_string(musicalQuality.overall) + ", variation=" +
                std::to_string(musicalQuality.variation) + ", space=" +
                std::to_string(musicalQuality.negativeSpace));
    requireNear(longSong.lengthBeats, 1088.0, 0.001,
                "Nine minutes at 120 BPM must resolve to the nearest complete eight-bar phrase");
    require(!longSong.notes.empty() && longSong.notes.size() < 32768,
            "Long-form rendering must remain populated and safe for realtime publication");
    require(deepReport.longestGlobalSilenceAfter <= longPlan.beatsPerBar * 2.0 + 0.001,
            "The final orchestrated MIDI must not contain an accidental global silence longer than two bars");
    auto orderedFinalNotes = longSong.notes;
    std::sort(orderedFinalNotes.begin(), orderedFinalNotes.end(), [](const auto& left, const auto& right) {
        return std::tie(left.partId, left.channel, left.pitch, left.startBeat) <
               std::tie(right.partId, right.channel, right.pitch, right.startBeat);
    });
    require(std::adjacent_find(orderedFinalNotes.begin(), orderedFinalNotes.end(), [](const auto& left,
                                                                                      const auto& right) {
                return left.partId == right.partId && left.channel == right.channel &&
                       left.pitch == right.pitch && left.endBeat() >= right.startBeat - 0.0001;
            }) == orderedFinalNotes.end(),
            "Final tonal convergence must never leave overlapping ownership of one MIDI note");
    require(longSong.parts.size() >= 12 &&
                std::count_if(longSong.parts.begin(), longSong.parts.end(), [](const auto& part) {
                    return part.department == ScoreDepartment::Rhythm;
                }) >= 3 &&
                std::count_if(longSong.parts.begin(), longSong.parts.end(), [](const auto& part) {
                    return part.department == ScoreDepartment::Harmony;
                }) >= 8 &&
                std::count_if(longSong.parts.begin(), longSong.parts.end(), [](const auto& part) {
                    return part.department == ScoreDepartment::Melody;
                }) >= 3,
            "The score must expose a real multi-instrument orchestra across all three departments");
    require(deepReport.orchestration.independentNotes > 0 &&
                deepReport.orchestration.registerClarity >= 0.70 &&
                deepReport.orchestration.familyBalance >= 0.45,
            "Deep orchestration must write independent material and pass register/balance review");
    require(deepReport.production.ready && longSong.productionAuditPerformed &&
                longSong.productionReady && deepReport.production.metricViolations == 0 &&
                deepReport.production.expressionEventsPerNote <= 12.0 &&
                deepReport.expression.controlsAfter < deepReport.expression.controlsBefore,
            "Only a metrically exact, tonally valid and expression-efficient score may be published: ready=" +
                std::to_string(deepReport.production.ready) + ", metric=" +
                std::to_string(deepReport.production.metricViolations) + ", unsafe=" +
                std::to_string(deepReport.production.unsafeDurations) + ", orphan=" +
                std::to_string(deepReport.production.orphanEvents) + ", chromatic=" +
                std::to_string(deepReport.production.unsupportedChromaticNotes) + ", sustains=" +
                std::to_string(deepReport.production.invalidSustains) + ", tonal_clashes=" +
                std::to_string(deepReport.production.unintendedHarshOverlaps) + ", vertical=" +
                std::to_string(deepReport.production.lowRegisterVerticalClashes));
    require(std::count_if(longSong.parts.begin(), longSong.parts.end(), [](const auto& part) {
                return part.department == ScoreDepartment::Harmony &&
                       part.orchestralFunction != "body";
            }) >= 4,
            "The harmonic orchestra must expose explicit foundation, extension, counterpoint and colour functions");
    require(std::any_of(longSong.controls.begin(), longSong.controls.end(), [](const auto& event) {
                return event.partId > 0 && (event.controller == 1 || event.controller == 11 || event.controller == 74);
            }), "Instrument parts must carry their own exported dynamics, articulation and timbre controls");

    Pattern copiedRhythm;
    copiedRhythm.lengthBeats = 16.0;
    for (auto bar = 0; bar < 4; ++bar) {
        copiedRhythm.notes.push_back({bar * 4.0, 0.0625, 36, 100, 10, VoiceId::CoreDrums});
        copiedRhythm.notes.push_back({bar * 4.0 + 0.5, 0.0625, 42, 58, 10, VoiceId::ClosedHats});
        copiedRhythm.notes.push_back({bar * 4.0 + 1.5, 0.0625, 42, 54, 10, VoiceId::ClosedHats});
    }
    auto rhythmGuardPlan = longPlan;
    rhythmGuardPlan.totalBars = 4;
    rhythmGuardPlan.sections.resize(1);
    rhythmGuardPlan.sections.front().startBar = 0;
    rhythmGuardPlan.sections.front().bars = 4;
    const auto copiedRhythmReport = MusicalCritic::reviewAndRefine(copiedRhythm, rhythmGuardPlan);
    require(copiedRhythmReport.literalRhythmBarsVaried > 0 &&
                std::count_if(copiedRhythm.notes.begin(), copiedRhythm.notes.end(), [](const auto& note) {
                    return note.voice == VoiceId::CoreDrums;
                }) == 4,
            "The rhythm critic must break copied percussion runs without damaging a stable kick contract");
    Pattern intentionalOstinato;
    intentionalOstinato.lengthBeats = 32.0;
    for (auto bar = 0; bar < 8; ++bar) {
        intentionalOstinato.notes.push_back({bar * 4.0 + 0.5, 0.25, 42, 62, 10, VoiceId::ClosedHats});
        intentionalOstinato.notes.push_back({bar * 4.0 + 1.5, 0.25, 42, 58, 10, VoiceId::ClosedHats});
    }
    TonalAuditReport expressiveTension;
    expressiveTension.pitchedNotes = 100;
    expressiveTension.strongNonChordNotes = 18;
    const auto softQualityReport = ProductionPolish::audit(
        intentionalOstinato, expressiveTension, 4.0, 0.35, 0.20);
    ProductionPolish::stamp(intentionalOstinato, softQualityReport);
    require(softQualityReport.ready && intentionalOstinato.productionReady &&
                softQualityReport.maximumRhythmRun > 4 &&
                std::any_of(intentionalOstinato.productionIssues.begin(), intentionalOstinato.productionIssues.end(),
                    [](const auto& issue) { return issue == "warning:repeated_rhythm_run"; }),
            "Musical quality warnings must remain visible without deleting an otherwise valid song");
    auto corruptPattern = intentionalOstinato;
    corruptPattern.notes.front().partId = 999;
    const auto corruptReport = ProductionPolish::audit(corruptPattern, {}, 4.0, 1.0, 1.0);
    require(!corruptReport.ready && corruptReport.orphanEvents == 1,
            "The production gate must still block objectively corrupt MIDI ownership");
    require(std::all_of(longSong.parts.begin(), longSong.parts.end(), [](const auto& part) {
                return !part.liveDevice.empty() && !part.livePresetIntent.empty();
            }), "Every orchestral part must carry a Live-native device and preset intent");
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
                if (std::any_of(longPlan.chordPalette.begin(), longPlan.chordPalette.end(), [&](const auto& chord) {
                        return std::find(chord.pitchClasses.begin(), chord.pitchClasses.end(),
                                         positiveModulo(note.pitch, 12)) != chord.pitchClasses.end() ||
                               chord.bassPitchClass == positiveModulo(note.pitch, 12);
                    })) return true;
                if (note.voice != VoiceId::Lead && note.voice != VoiceId::Countermelody) return false;
                return note.durationBeats <= 0.36;
            }), "A rendered song may leave its home scale only through authored harmony or brief melodic passing motion");

    std::vector<std::set<VoiceId>> voicesByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> leadByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> counterByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> kickByBar(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> breathAtEnd(static_cast<std::size_t>(longPlan.totalBars), true);
    std::vector<int> leadOnsets(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<int> counterOnsets(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<std::set<int>> leadOnsetPositions(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<std::set<int>> counterOnsetPositions(static_cast<std::size_t>(longPlan.totalBars));
    std::vector<bool> foundationByBar(static_cast<std::size_t>(longPlan.totalBars));
    for (const auto& note : longSong.notes) {
        const auto bar = std::clamp(static_cast<int>(note.startBeat / longPlan.beatsPerBar),
                                    0, longPlan.totalBars - 1);
        voicesByBar[static_cast<std::size_t>(bar)].insert(note.voice);
        if (note.voice == VoiceId::CoreDrums && note.pitch == 36)
            kickByBar[static_cast<std::size_t>(bar)] = true;
        if (note.voice == VoiceId::Lead) {
            leadByBar[static_cast<std::size_t>(bar)] = true;
            leadOnsetPositions[static_cast<std::size_t>(bar)].insert(
                static_cast<int>(std::lround(note.startBeat * 16.0)));
        }
        if (note.voice == VoiceId::Countermelody) {
            counterByBar[static_cast<std::size_t>(bar)] = true;
            counterOnsetPositions[static_cast<std::size_t>(bar)].insert(
                static_cast<int>(std::lround(note.startBeat * 16.0)));
        }
        if (note.voice == VoiceId::HarmonicFoundation)
            foundationByBar[static_cast<std::size_t>(bar)] = true;
        const auto beatInBar = note.startBeat - bar * longPlan.beatsPerBar;
        if (beatInBar >= longPlan.beatsPerBar - 0.38 &&
            (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
             isVoiceInFamily(note.voice, VoiceFamily::Bass)))
            breathAtEnd[static_cast<std::size_t>(bar)] = false;
    }
    for (std::size_t bar = 0; bar < leadOnsets.size(); ++bar) {
        leadOnsets[bar] = static_cast<int>(leadOnsetPositions[bar].size());
        counterOnsets[bar] = static_cast<int>(counterOnsetPositions[bar].size());
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
            "Harmonic foundation must sustain across variable harmonic rhythm instead of retriggering every bar (" +
            std::to_string(std::count(foundationByBar.begin(), foundationByBar.end(), true)) + "/" +
            std::to_string(longPlan.totalBars) + ")");
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
                return note.voice == VoiceId::ClosedHats && note.startBeat >= 0.5 && note.startBeat < 1.0;
            }) >= 2,
            "Open rhythm mutations must produce audible, deterministic development on the publication grid");

    auto aiRhythmPlan = longPlan;
    aiRhythmPlan.voices.erase(std::remove_if(aiRhythmPlan.voices.begin(), aiRhythmPlan.voices.end(), [](const auto& voice) {
        return voice.id == VoiceId::SnareClap || voice.id == VoiceId::ClosedHats ||
               voice.id == VoiceId::OpenHatsShaker;
    }), aiRhythmPlan.voices.end());
    auto authoredRhythm = std::find_if(aiRhythmPlan.voices.begin(), aiRhythmPlan.voices.end(), [](const auto& voice) {
        return voice.id == VoiceId::CoreDrums;
    });
    require(authoredRhythm != aiRhythmPlan.voices.end(), "The test score needs a core rhythm voice");
    authoredRhythm->performance.authored = true;
    authoredRhythm->performance.intent = "AI-authored restrained acoustic attack";
    SongComposer::normalizePlan(aiRhythmPlan);
    for (const auto requiredLane : {VoiceId::SnareClap, VoiceId::ClosedHats, VoiceId::OpenHatsShaker}) {
        const auto lane = std::find_if(aiRhythmPlan.voices.begin(), aiRhythmPlan.voices.end(), [requiredLane](const auto& voice) {
            return voice.id == requiredLane;
        });
        require(lane != aiRhythmPlan.voices.end() && lane->performance.authored &&
                    lane->performance.intent.find("AI") != std::string::npos,
                "Technical drum lanes must retain the AI-authored rhythmic performance language");
    }

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
        {0.0, 1.0, 71, 82, 2, VoiceId::Countermelody}
    };
    const std::vector<std::vector<int>> changingHarmony{{0, 4, 7, 11}, {5, 9, 0}};
    const auto repair = repairTonalContract(brokenHarmony, 0, ScaleKind::Major, 4.0,
                                            changingHarmony, 0.20);
    require(repair.outOfScaleRepaired >= 2 && repair.intentionalChromaticNotes == 0 &&
                repair.harmonicOverlapsTrimmed == 1 && repair.verticalCollisionsRepaired >= 1,
            "Consolidated tonal validation must repair every external note, including passing notes");
    require(isPitchClassInScale(brokenHarmony.notes.front().pitch, 0, ScaleKind::Major),
            "An unsupported chromatic note on a strong beat must be repaired into the declared key");

    Pattern exactHarmony;
    exactHarmony.lengthBeats = 4.0;
    exactHarmony.notes = {
        {0.0, 4.0, 64, 76, 3, VoiceId::HarmonicFoundation}, // E cannot sustain into D minor/Bb.
        {2.0, 1.0, 62, 82, 2, VoiceId::Lead},
        {2.0, 1.0, 58, 60, 5, VoiceId::HarmonicUpper}
    };
    const std::vector<HarmonicWindow> exactWindows{
        {0.0, 2.0, 0, 0, {0, 4, 7}, HarmonicFunction::Tonic,
         VoicingStrategy::Open, 0.20, "c", "C"},
        {2.0, 4.0, 2, 2, {2, 5, 10}, HarmonicFunction::Predominant,
         VoicingStrategy::Open, 0.42, "dm_borrowed", "Dm/Bb"}
    };
    const auto exactRepair = repairTonalContract(
        exactHarmony, 0, ScaleKind::Major, 4.0, exactWindows);
    require(exactRepair.exactBoundaryTrims == 1 &&
                exactHarmony.notes.front().endBeat() <= 2.0 + 0.001 &&
                exactRepair.after.productionReady(),
            "The tonal contract must cut incompatible sustains at the exact sub-bar chord boundary");

    Pattern forbiddenCluster;
    forbiddenCluster.lengthBeats = 2.0;
    forbiddenCluster.notes = {
        {0.0, 1.0, 69, 84, 3, VoiceId::HarmonicFoundation},
        {0.0, 1.0, 70, 62, 5, VoiceId::HarmonicUpper}
    };
    const std::vector<HarmonicWindow> openVoicing{
        {0.0, 2.0, 5, 5, {5, 9, 10, 0}, HarmonicFunction::Colour,
         VoicingStrategy::Open, 0.70, "colour", "Colour"}
    };
    const auto forbiddenRepair = repairTonalContract(
        forbiddenCluster, 5, ScaleKind::Major, 4.0, openVoicing);
    require(forbiddenRepair.verticalCollisionsRepaired >= 1 &&
                forbiddenRepair.after.unintendedHarshOverlaps == 0,
            "Undeclared vertical semitones must be retuned, trimmed or removed");

    Pattern declaredCluster;
    declaredCluster.lengthBeats = 2.0;
    declaredCluster.notes = {
        {0.0, 1.0, 69, 84, 3, VoiceId::HarmonicFoundation},
        {0.0, 1.0, 70, 62, 5, VoiceId::HarmonicUpper}
    };
    auto clusterVoicing = openVoicing;
    clusterVoicing.front().voicing = VoicingStrategy::Cluster;
    const auto clusterRepair = repairTonalContract(
        declaredCluster, 5, ScaleKind::Major, 4.0, clusterVoicing, 0.035,
        TonalPolicy::Free);
    require(clusterRepair.notesRemoved == 0 && clusterRepair.notesRetunedForVoicing == 0 &&
                clusterRepair.after.intentionalClusters >= 1,
            "An explicit high-register cluster must survive as intentional harmonic colour");

    Pattern borrowedLeadingTone;
    borrowedLeadingTone.lengthBeats = 2.0;
    borrowedLeadingTone.notes = {{0.0, 1.0, 61, 80, 3, VoiceId::HarmonicFoundation}};
    const std::vector<HarmonicWindow> borrowedWindow{
        {0.0, 2.0, 9, 1, {1, 4, 9}, HarmonicFunction::Dominant,
         VoicingStrategy::Close, 0.75, "a7", "A7/C#"}
    };
    const auto borrowedRepair = repairTonalContract(
        borrowedLeadingTone, 2, ScaleKind::Minor, 4.0, borrowedWindow, 0.035,
        TonalPolicy::Expanded);
    require(borrowedLeadingTone.notes.front().pitch == 61 &&
                borrowedRepair.outOfScaleRepaired == 0 && borrowedRepair.after.productionReady(),
            "An AI-declared borrowed chord tone must remain legal even outside the home scale");

    auto consolidatedLeadingTone = borrowedLeadingTone;
    const auto consolidatedRepair = repairTonalContract(
        consolidatedLeadingTone, 2, ScaleKind::Minor, 4.0, borrowedWindow, 0.035,
        TonalPolicy::Consolidated);
    require(consolidatedLeadingTone.notes.front().pitch != 61 &&
                consolidatedRepair.outOfScaleRepaired == 1 && consolidatedRepair.after.productionReady(),
            "A declared chord must not legalise an out-of-key structural tone in consolidated mode");

    Pattern resolvedLeadingTone;
    resolvedLeadingTone.lengthBeats = 2.0;
    resolvedLeadingTone.notes = {
        {0.0, 0.25, 61, 78, 3, VoiceId::Lead},
        {0.5, 0.50, 62, 88, 3, VoiceId::Lead}
    };
    const std::vector<HarmonicWindow> cadentialWindow{
        {0.0, 2.0, 9, 9, {1, 4, 9}, HarmonicFunction::Dominant,
         VoicingStrategy::Close, 0.82, "a7_to_dm", "A7 resolving to D minor"}
    };
    const auto resolvedRepair = repairTonalContract(
        resolvedLeadingTone, 2, ScaleKind::Minor, 4.0, cadentialWindow, 0.035,
        TonalPolicy::Consolidated);
    require(resolvedLeadingTone.notes.front().pitch == 61 &&
                resolvedRepair.intentionalChromaticNotes == 1 &&
                resolvedRepair.after.productionReady(),
            "Consolidated minor may retain one declared leading tone only when it resolves by semitone to tonic");

    Pattern pitchedTransition;
    pitchedTransition.lengthBeats = 4.0;
    pitchedTransition.parts.push_back({1, "ambient_texture", "Filtered Wake",
        VoiceId::Transitions, ScoreDepartment::Harmony, "Pitched reverse texture", 36, 96,
        0.4, InstrumentSoundModel::Texture, "transition", "natural", 1,
        "Granulator III", "pitched filtered texture"});
    pitchedTransition.notes.push_back({0.0, 1.0, 51, 54, 9, VoiceId::Transitions, 1});
    const std::vector<HarmonicWindow> transitionHarmony{
        {0.0, 4.0, 2, 2, {2, 5, 9}, HarmonicFunction::Tonic,
         VoicingStrategy::Open, 0.20, "dm", "D minor"}
    };
    const auto transitionRepair = repairTonalContract(
        pitchedTransition, 2, ScaleKind::Minor, 4.0, transitionHarmony, 0.035,
        TonalPolicy::Consolidated);
    require(pitchedTransition.notes.front().pitch != 51 &&
                isPitchClassInScale(pitchedTransition.notes.front().pitch, 2, ScaleKind::Minor) &&
                transitionRepair.after.productionReady(),
            "A chromatic Live texture must remain tonal even when sourced by the transitions voice");

    Pattern dominatedPercussion;
    dominatedPercussion.lengthBeats = 64.0;
    for (auto index = 0; index < 48; ++index)
        dominatedPercussion.notes.push_back({index * 1.25, 0.08, 75, 58, 10,
                                              VoiceId::HighPercussion});
    auto percussionPlan = longPlan;
    percussionPlan.totalBars = 16;
    percussionPlan.sections = {{"Percussion", "Development", "", "", 0, 16,
        0.72, 0.55, 0.62, 0, percussionPlan.rootPitchClass, "minor", {},
        {VoiceId::HighPercussion}, {}}};
    const auto percussionRepair = RhythmEngine::enforceContract(dominatedPercussion,
                                                                 percussionPlan);
    std::map<int, int> percussionCounts;
    for (const auto& note : dominatedPercussion.notes) ++percussionCounts[note.pitch];
    const auto dominantCount = std::max_element(percussionCounts.begin(), percussionCounts.end(),
        [](const auto& left, const auto& right) { return left.second < right.second; })->second;
    require(percussionRepair.articulationDiversifications > 0 &&
                dominantCount <= static_cast<int>(dominatedPercussion.notes.size() * 2 / 3) &&
                percussionCounts.size() >= 3,
            "One percussion articulation must never monopolise a multi-articulation support lane");

    Pattern performedTail;
    performedTail.lengthBeats = 4.0;
    performedTail.seed = 91;
    performedTail.notes = {
        {0.0, 1.95, 64, 72, 3, VoiceId::HarmonicFoundation},
        {2.0, 1.0, 65, 68, 5, VoiceId::HarmonicUpper}
    };
    SongPlan performancePlan;
    performancePlan.totalBars = 1;
    performancePlan.beatsPerBar = 4.0;
    performancePlan.rootPitchClass = 0;
    performancePlan.scale = ScaleKind::Major;
    performancePlan.seed = performedTail.seed;
    SongSection performanceSection;
    performanceSection.name = "Performance boundary";
    performanceSection.bars = 1;
    performanceSection.activeVoices = {VoiceId::HarmonicFoundation, VoiceId::HarmonicUpper};
    performancePlan.sections = {performanceSection};
    const std::vector<HarmonicWindow> performanceWindows{
        {0.0, 2.0, 0, 0, {0, 4, 7}, HarmonicFunction::Tonic,
         VoicingStrategy::Open, 0.20, "c", "C"},
        {2.0, 4.0, 5, 5, {0, 5, 9}, HarmonicFunction::Predominant,
         VoicingStrategy::Open, 0.35, "f", "F"}
    };
    PerformanceExpression::apply(performedTail, performancePlan, true);
    const auto performedRepair = repairTonalContract(
        performedTail, 0, ScaleKind::Major, 4.0, performanceWindows);
    PerformanceExpression::apply(performedTail, performancePlan, false);
    require(performedRepair.before.invalidSustains >= 1 &&
                performedRepair.exactBoundaryTrims >= 1 &&
                performedRepair.after.productionReady() &&
                performedTail.notes.front().endBeat() < 2.0,
            "Expressive duration changes must be audited and repaired on the final performed MIDI");

    Pattern registeredColour;
    registeredColour.lengthBeats = 2.0;
    registeredColour.notes = {
        {0.0, 1.0, 34, 84, 1, VoiceId::SubBass},
        {0.0, 1.0, 52, 68, 3, VoiceId::HarmonicFoundation}
    };
    const std::vector<HarmonicWindow> colourWindow{
        {0.0, 2.0, 10, 10, {10, 2, 5, 9, 4}, HarmonicFunction::Colour,
         VoicingStrategy::Open, 0.68, "bb_lydian", "Bbmaj7(#11)"}
    };
    const auto colourRepair = repairTonalContract(
        registeredColour, 2, ScaleKind::Minor, 4.0, colourWindow, 0.035,
        TonalPolicy::Expanded);
    require(colourRepair.notesRemoved == 0 && colourRepair.notesRetunedForVoicing == 0 &&
                colourRepair.after.intentionalClusters >= 1,
            "A declared #11 may form a wide tritone over bass without being erased as an error");
    require(tonalPolicyForDirection("emotional progressive house") == TonalPolicy::Consolidated &&
                tonalPolicyForDirection("jazz harmony with a brief secondary dominant") == TonalPolicy::Expanded &&
                tonalPolicyForDirection("atonal polytonal dissonant study") == TonalPolicy::Free,
            "Only explicit harmonic language may widen the consolidated tonal boundary");
    const auto consolidatedPlan = SongComposer::createLocalPlan(
        "emotional progressive house", 180, 122.0, 4.0, 331, 3, ScaleKind::Major);
    require(consolidatedPlan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated &&
                std::all_of(consolidatedPlan.chordPalette.begin(), consolidatedPlan.chordPalette.end(),
                    [&](const auto& chord) {
                        return isPitchClassInScale(chord.rootPitchClass, 3, ScaleKind::Major) &&
                               isPitchClassInScale(chord.bassPitchClass, 3, ScaleKind::Major) &&
                               std::all_of(chord.pitchClasses.begin(), chord.pitchClasses.end(),
                                   [&](int pitch) { return isPitchClassInScale(pitch, 3, ScaleKind::Major); });
                    }),
            "Ordinary full-song plans must expose a completely diatonic structural palette");

    PerformanceScore authoredScore;
    authoredScore.cells.push_back({"question", 4.0, {VoiceId::Lead},
        {{0.375, 0.625, 64, 91, VoiceId::Lead},
         {2.25, 0.50, 67, 73, VoiceId::Lead}},
        {{0.0, 11, 70, VoiceId::Lead}, {2.25, 11, 108, VoiceId::Lead}}});
    authoredScore.cells.push_back(authoredScore.cells.front());
    authoredScore.cells.back().id = "question_copy";
    authoredScore.placements.push_back({"question", 0, 0.0, 2, 0, 1.0, 1.0});
    const auto authoredReport = PerformanceScoreEngine::normalize(authoredScore, 1, {8.0});
    require(authoredReport.cellsAccepted == 2 && authoredReport.exactDuplicateCells == 1 &&
                authoredReport.novelty < 0.51,
            "Performance fingerprints must expose renamed duplicate musical cells");
    Pattern authoredChunk;
    authoredChunk.lengthBeats = 8.0;
    authoredChunk.notes = {
        {0.0, 0.5, 72, 64, 2, VoiceId::Lead},
        {1.0, 1.0, 55, 70, 3, VoiceId::HarmonicFoundation}
    };
    PerformanceScoreEngine::replaceChunk(authoredChunk, authoredScore, 0, 0.0, 8.0);
    require(std::none_of(authoredChunk.notes.begin(), authoredChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead && std::abs(note.startBeat) < 0.001;
            }) && std::count_if(authoredChunk.notes.begin(), authoredChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead;
            }) == 4 && std::any_of(authoredChunk.notes.begin(), authoredChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::HarmonicFoundation;
            }) && std::all_of(authoredChunk.notes.begin(), authoredChunk.notes.end(), [](const auto& note) {
                return note.voice != VoiceId::Lead || note.origin == NoteOrigin::AiAuthored;
            }),
            "Explicit performance must replace only owned voices and preserve AI authorship provenance");

    PerformanceScore partialScore;
    partialScore.cells.push_back({"brief_answer", 2.0, {VoiceId::Lead},
        {{0.0, 0.5, 67, 88, VoiceId::Lead}}, {}});
    partialScore.placements.push_back({"brief_answer", 0, 2.0, 1, 0, 1.0, 1.0});
    PerformanceScoreEngine::normalize(partialScore, 1, {8.0});
    Pattern partialChunk;
    partialChunk.lengthBeats = 8.0;
    partialChunk.notes = {
        {0.0, 0.5, 60, 70, 2, VoiceId::Lead},
        {2.0, 0.5, 62, 70, 2, VoiceId::Lead},
        {5.0, 0.5, 64, 70, 2, VoiceId::Lead}
    };
    PerformanceScoreEngine::replaceChunk(partialChunk, partialScore, 0, 0.0, 8.0);
    require(std::any_of(partialChunk.notes.begin(), partialChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead && std::abs(note.startBeat) < 0.001;
            }) && std::none_of(partialChunk.notes.begin(), partialChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead && std::abs(note.startBeat - 2.0) < 0.001 && note.pitch == 62;
            }) && std::any_of(partialChunk.notes.begin(), partialChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead && std::abs(note.startBeat - 5.0) < 0.001;
            }),
            "A placed cell must own only its covered interval, never erase the rest of a section");

    PerformanceScore dialogueScore;
    dialogueScore.cells.push_back({"lead_question", 4.0, {VoiceId::Lead},
        {{0.5, 0.5, 62, 90, VoiceId::Lead}, {2.0, 0.5, 65, 78, VoiceId::Lead}}, {}});
    PerformancePlacement transformedAnswer;
    transformedAnswer.cellId = "lead_question";
    transformedAnswer.sectionIndex = 0;
    transformedAnswer.purpose = "Countermelody answers the question in inverted retrograde";
    transformedAnswer.voiceMap = {{VoiceId::Lead, VoiceId::Countermelody}};
    transformedAnswer.retrograde = true;
    transformedAnswer.invertContour = true;
    transformedAnswer.inversionAxis = 64;
    transformedAnswer.fragmentEnd = 4.0;
    dialogueScore.placements.push_back(transformedAnswer);
    PerformanceScoreEngine::normalize(dialogueScore, 1, {4.0});
    Pattern dialogueChunk;
    dialogueChunk.lengthBeats = 4.0;
    PerformanceScoreEngine::replaceChunk(dialogueChunk, dialogueScore, 0, 0.0, 4.0);
    require(std::any_of(dialogueChunk.notes.begin(), dialogueChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Countermelody && note.pitch == 66 &&
                       std::abs(note.startBeat - 3.0) < 0.001;
            }) && std::any_of(dialogueChunk.notes.begin(), dialogueChunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Countermelody && note.pitch == 63 &&
                       std::abs(note.startBeat - 1.5) < 0.001;
            }) && std::all_of(dialogueChunk.notes.begin(), dialogueChunk.notes.end(), [](const auto& note) {
                return note.origin == NoteOrigin::AiTransformed && note.narrativeId != 0;
            }),
            "AI placements must turn a cell into audible cross-instrument thematic transformation");

    auto narrativePlan = SongComposer::createLocalPlan(
        "Narrative authorship audit", 120, 120.0, 4.0, 818, 2, ScaleKind::Minor);
    narrativePlan.productionModeSource = "gpt_plan";
    PerformanceCell narrativeCell;
    narrativeCell.id = "central_statement";
    narrativeCell.themeId = "central_theme";
    narrativeCell.narrativeFunction = "establish";
    narrativeCell.lengthBeats = 4.0;
    narrativeCell.ownedVoices = {VoiceId::Lead, VoiceId::MovementBass,
        VoiceId::HarmonicFoundation, VoiceId::CoreDrums};
    narrativeCell.notes = {
        {0.0, 0.5, 74, 92, VoiceId::Lead}, {1.0, 0.5, 77, 84, VoiceId::Lead},
        {2.5, 0.75, 81, 88, VoiceId::Lead}, {0.5, 0.25, 50, 86, VoiceId::MovementBass},
        {1.5, 0.25, 53, 82, VoiceId::MovementBass}, {3.0, 0.25, 57, 88, VoiceId::MovementBass},
        {0.0, 3.5, 62, 68, VoiceId::HarmonicFoundation},
        {0.0, 0.125, 36, 110, VoiceId::CoreDrums}};
    narrativePlan.performanceScore = {};
    narrativePlan.performanceScore.cells.push_back(narrativeCell);
    for (std::size_t section = 0; section < narrativePlan.sections.size(); ++section) {
        PerformancePlacement placement;
        placement.cellId = narrativeCell.id;
        placement.sectionIndex = static_cast<int>(section);
        placement.repeats = narrativePlan.sections[section].bars;
        placement.fragmentEnd = narrativeCell.lengthBeats;
        placement.purpose = section == 0 ? "establish central theme" : "develop central theme";
        narrativePlan.performanceScore.placements.push_back(placement);
    }
    SongComposer::normalizePlan(narrativePlan);
    GenerationContext narrativeContext;
    narrativeContext.rootPitchClass = narrativePlan.rootPitchClass;
    narrativeContext.scale = narrativePlan.scale;
    narrativeContext.beatsPerBar = narrativePlan.beatsPerBar;
    narrativeContext.seed = narrativePlan.seed;
    CompositionRenderReport narrativeRender;
    const auto narrativeSong = SongComposer{}.render(
        narrativePlan, narrativeContext, {}, &narrativeRender);
    require(narrativeRender.narrative.active &&
                narrativeRender.narrative.primaryVoiceCoverage >= 0.45 &&
                narrativeRender.narrative.thematicRecallRatio >= 0.90 &&
                narrativeSong.narrativeAuditPerformed &&
                narrativeSong.aiAuthoredNoteRatio > 0.25,
            "A GPT-authored long-form score must expose measurable coverage, memory and note provenance: coverage=" +
                std::to_string(narrativeRender.narrative.primaryVoiceCoverage) +
                " recall=" + std::to_string(narrativeRender.narrative.thematicRecallRatio) +
                " authored=" + std::to_string(narrativeSong.aiAuthoredNoteRatio));

    auto weakNarrativePlan = narrativePlan;
    weakNarrativePlan.performanceScore = {};
    const auto weakAudit = NarrativeScoreGate::audit(Pattern{}, weakNarrativePlan);
    require(weakAudit.primaryVoiceCoverage < 0.01 &&
                std::find(weakAudit.issues.begin(), weakAudit.issues.end(),
                          "insufficient_ai_phrase_coverage") != weakAudit.issues.end(),
            "A semantic GPT plan without authored phrases must fail the narrative authorship contract");

    Pattern falseLabelMemory;
    falseLabelMemory.lengthBeats = 32.0;
    const std::array<std::array<int, 3>, 4> unrelatedContours{{
        {{60, 62, 65}}, {{72, 64, 71}}, {{67, 69, 61}}, {{74, 63, 68}}
    }};
    const std::array<std::array<double, 3>, 4> unrelatedRhythms{{
        {{0.0, 1.0, 2.0}}, {{0.0, 0.5, 3.0}}, {{0.0, 2.5, 3.5}}, {{0.0, 0.25, 3.75}}
    }};
    for (auto window = std::size_t{}; window < unrelatedContours.size(); ++window)
        for (auto note = std::size_t{}; note < unrelatedContours[window].size(); ++note)
            falseLabelMemory.notes.push_back({window * 8.0 + unrelatedRhythms[window][note],
                0.5, unrelatedContours[window][note], 88, 2, VoiceId::Lead, 0, false,
                NoteOrigin::AiAuthored, 4242});
    const auto falseLabelAudit = NarrativeScoreGate::audit(falseLabelMemory, narrativePlan);
    require(falseLabelAudit.audibleThematicWindows == 4 &&
                falseLabelAudit.thematicRecallRatio < 0.40 &&
                falseLabelAudit.audibleThematicSimilarity < 0.66,
            "A shared theme_id must not turn unrelated rendered phrases into thematic recall: windows=" +
                std::to_string(falseLabelAudit.audibleThematicWindows) + ", recall=" +
                std::to_string(falseLabelAudit.thematicRecallRatio) + ", similarity=" +
                std::to_string(falseLabelAudit.audibleThematicSimilarity));

    Pattern audibleMemory;
    audibleMemory.lengthBeats = 32.0;
    for (auto window = 0; window < 4; ++window)
        for (auto note = 0; note < 3; ++note)
            audibleMemory.notes.push_back({window * 8.0 + note,
                0.5 + (note == 2 ? 0.25 : 0.0), 60 + window * 2 + std::array{0, 3, 7}[note],
                88 - note * 3, 2, VoiceId::Lead, 0, false,
                window == 0 ? NoteOrigin::AiAuthored : NoteOrigin::AiTransformed, 5151});
    const auto audibleMemoryAudit = NarrativeScoreGate::audit(audibleMemory, narrativePlan);
    require(audibleMemoryAudit.thematicRecallRatio > 0.99 &&
                audibleMemoryAudit.audibleThematicSimilarity > 0.90,
            "Transposed statements with the same rhythm and contour must pass audible thematic memory");

    auto octavePlan = SongComposer::createLocalPlan(
        "octave normalization", 60, 120.0, 4.0, 91, 0, ScaleKind::Minor);
    require(!octavePlan.instruments.empty(), "Local orchestration must expose instruments");
    octavePlan.instruments.front().octaveShift = 7;
    SongComposer::normalizePlan(octavePlan);
    require(octavePlan.instruments.front().octaveShift == 12,
            "Orchestration octave shifts must never become chromatic transpositions");

    const auto compactPlan = SongComposer::createLocalPlan(
        "A slow compact song", 30, 30.0, 4.0, 31, 5, ScaleKind::Dorian);
    require(compactPlan.totalBars == 8 && !compactPlan.sections.empty() &&
                compactPlan.sections.back().startBar + compactPlan.sections.back().bars == 8,
            "Even the shortest slow song must form a valid contiguous dramatic arc");
}
