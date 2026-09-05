#include "ElectronicCompositionFabric.h"

#include "OrchestrationScore.h"
#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <vector>

namespace pulso {
namespace {

bool electronic(const SongPlan& plan) noexcept {
    return plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
}

bool contains(std::string_view text, std::string_view token) noexcept {
    return text.find(token) != std::string_view::npos;
}

bool arpPart(const InstrumentAssignment& part) noexcept {
    return part.instrumentId == "hypnotic_arp" || part.instrumentId == "fm_sequence" ||
           part.articulation == "ostinato" || contains(part.role, "arpeggio") ||
           contains(part.role, "arpeggiated");
}

bool floorPart(const InstrumentAssignment& part) noexcept {
    if (part.sourceVoice != VoiceId::HarmonicFoundation &&
        part.sourceVoice != VoiceId::HarmonicUpper && part.sourceVoice != VoiceId::Atmosphere)
        return false;
    return part.orchestralFunction == "foundation" || part.orchestralFunction == "body" ||
           part.instrumentId == "analog_pad" || part.instrumentId == "granular_pad" ||
           part.instrumentId == "spectral_drone" || contains(part.role, "pad") ||
           contains(part.role, "chord body");
}

bool dialoguePart(const InstrumentAssignment& part) noexcept {
    return part.sourceVoice == VoiceId::Countermelody ||
        (part.sourceVoice == VoiceId::Lead && part.orchestralFunction == "counterpoint");
}

bool activeIn(const InstrumentAssignment& part, const SongSection& section) {
    return part.activeSections.empty() ||
        std::find(part.activeSections.begin(), part.activeSections.end(), section.name) !=
            part.activeSections.end();
}

void ensureVoice(SongPlan& plan, VoiceId id, std::string function, double activity) {
    if (std::any_of(plan.voices.begin(), plan.voices.end(),
        [id](const auto& voice) { return voice.id == id; })) return;
    const auto& definition = voiceDefinition(id);
    PlannedVoice voice;
    voice.id = id;
    voice.function = std::move(function);
    voice.interaction = "Derive new material from the shared harmonic and thematic narrative";
    voice.activity = activity;
    voice.syncopation = id == VoiceId::HarmonicPulse || id == VoiceId::Countermelody ? .38 : .12;
    voice.minimumPitch = definition.minimumPitch;
    voice.maximumPitch = definition.maximumPitch;
    plan.voices.push_back(std::move(voice));
}

InstrumentAssignment makePart(std::string id, std::string catalog, std::string name,
                              VoiceId voice, std::string role, std::string function,
                              std::string articulation, std::string device,
                              std::string preset, double activity, double prominence) {
    InstrumentAssignment result;
    result.id = std::move(id);
    result.instrumentId = std::move(catalog);
    result.name = std::move(name);
    result.sourceVoice = voice;
    result.role = std::move(role);
    if (const auto* definition = instrumentDefinition(result.instrumentId)) {
        result.minimumPitch = definition->minimumPitch;
        result.maximumPitch = definition->maximumPitch;
    }
    result.activity = activity;
    result.prominence = prominence;
    result.doubling = 0.0;
    result.orchestralFunction = std::move(function);
    result.articulation = std::move(articulation);
    result.liveDevice = std::move(device);
    result.livePresetIntent = std::move(preset);
    result.contentLaneId = result.id;
    return result;
}

void activateEverywhere(SongPlan& plan, VoiceId voice) {
    for (auto& section : plan.sections)
        if (std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) ==
            section.activeVoices.end()) section.activeVoices.push_back(voice);
}

const HarmonicChord& chordAt(const SongPlan& plan, double beat) {
    static const HarmonicChord fallback{"fallback", "Tonic", 0, 0, {0, 3, 7}};
    if (plan.chordPalette.empty()) return fallback;
    const SongSection* section = plan.sections.empty() ? nullptr : &plan.sections.front();
    for (const auto& candidate : plan.sections)
        if (beat >= candidate.startBar * plan.beatsPerBar) section = &candidate;
    if (section == nullptr || section->harmonicEvents.empty()) return plan.chordPalette.front();
    const auto local = beat - section->startBar * plan.beatsPerBar;
    const HarmonicEvent* event = &section->harmonicEvents.front();
    for (const auto& candidate : section->harmonicEvents) {
        const auto onset = candidate.barOffset * plan.beatsPerBar + candidate.beatOffset;
        if (onset <= local + .001) event = &candidate;
    }
    const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
        [&](const auto& candidate) { return candidate.id == event->chordId; });
    return chord == plan.chordPalette.end() ? plan.chordPalette.front() : *chord;
}

int nearestPitch(int pitchClass, int target, const InstrumentAssignment& part) noexcept {
    auto best = std::clamp(target, part.minimumPitch, part.maximumPitch);
    auto distance = 1000;
    for (auto pitch = part.minimumPitch; pitch <= part.maximumPitch; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto candidateDistance = std::abs(pitch - target);
        if (candidateDistance < distance) { best = pitch; distance = candidateDistance; }
    }
    return best;
}

bool overlapsPart(const Pattern& pattern, std::uint16_t partId, double start, double end) {
    return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        return note.partId == partId && note.startBeat < end - .001 && note.endBeat() > start + .001;
    });
}

void addNote(Pattern& pattern, const InstrumentAssignment& assignment, std::uint16_t partId,
             double start, double duration, int pitch, int velocity, std::uint32_t narrativeId,
             ElectronicFabricReport& report, std::size_t& counter) {
    if (start < 0.0 || start >= pattern.lengthBeats || duration <= .001) return;
    std::vector<double> checkpoints{start + .001};
    const auto proposedEnd = start + duration;
    for (const auto& note : pattern.notes)
        if (note.startBeat > start + .001 && note.startBeat < proposedEnd - .001)
            checkpoints.push_back(note.startBeat + .001);
    for (const auto checkpoint : checkpoints) {
        std::set<std::uint16_t> soundingParts;
        for (const auto& note : pattern.notes)
            if (note.partId != 0 && note.startBeat <= checkpoint && note.endBeat() > checkpoint)
                soundingParts.insert(note.partId);
        // Keep the score mixable. Structural depth comes from interlocking lines over
        // time, not an ever-growing instantaneous tutti.
        if (!soundingParts.contains(partId) && soundingParts.size() >= 7) return;
    }
    pattern.notes.push_back({start, std::min(duration, pattern.lengthBeats - start), pitch,
        std::clamp(velocity, 1, 127), voiceDefinition(assignment.sourceVoice).midiChannel,
        assignment.sourceVoice, partId, true, NoteOrigin::PlanDerived, narrativeId});
    ++report.notesCreated;
    ++counter;
}

std::vector<std::size_t> indicesFor(const SongPlan& plan,
                                    bool (*predicate)(const InstrumentAssignment&)) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < plan.instruments.size(); ++i)
        if (predicate(plan.instruments[i])) result.push_back(i);
    std::sort(result.begin(), result.end(), [&](auto left, auto right) {
        return plan.instruments[left].prominence > plan.instruments[right].prominence;
    });
    return result;
}

std::size_t activeBarsFor(const std::vector<const NoteEvent*>& notes, double beatsPerBar) {
    std::set<int> bars;
    for (const auto* note : notes) {
        const auto first = static_cast<int>(std::floor(note->startBeat / beatsPerBar));
        const auto last = static_cast<int>(std::floor(std::max(note->startBeat,
            note->endBeat() - .001) / beatsPerBar));
        for (auto bar = first; bar <= last; ++bar) bars.insert(bar);
    }
    return bars.size();
}

} // namespace

void ElectronicCompositionFabric::normalizePlan(SongPlan& plan) {
    if (!electronic(plan)) return;

    std::set<std::string> independentLanes;
    for (auto& part : plan.instruments) {
        if (part.contentLaneId.empty()) part.contentLaneId = part.id;
        if (part.lineRelationship != "independent" && part.lineRelationship != "doubling" &&
            part.lineRelationship != "relay" && part.lineRelationship != "call_response" &&
            part.lineRelationship != "octave_reinforcement" &&
            part.lineRelationship != "timbral_handoff") part.lineRelationship = "independent";
        if (part.lineRelationship == "independent" && !independentLanes.insert(part.contentLaneId).second) {
            part.contentLaneId += "_" + part.id;
            independentLanes.insert(part.contentLaneId);
        }
    }

    ensureVoice(plan, VoiceId::HarmonicFoundation, "Continuous multi-layer harmonic floor", .78);
    ensureVoice(plan, VoiceId::HarmonicPulse, "Evolving electronic arpeggio and pulse", .56);
    ensureVoice(plan, VoiceId::Lead, "Primary narrative speaker", .42);
    ensureVoice(plan, VoiceId::Countermelody, "Derived melodic dialogue", .34);

    auto floors = indicesFor(plan, floorPart);
    const std::array floorDefaults{
        makePart("fabric_foundation_pad", "analog_pad", "Foundation Pad", VoiceId::HarmonicFoundation,
            "Continuous low-mid chord memory", "foundation", "sustained", "Wavetable",
            "warm wide evolving analog pad", .78, .62),
        makePart("fabric_inner_pad", "poly_synth", "Inner Harmonic Body", VoiceId::HarmonicFoundation,
            "Independent inner chord voice", "body", "sustained", "Meld",
            "dark warm slow polyphonic body", .68, .52),
        makePart("fabric_air_pad", "granular_pad", "Evolving Air Pad", VoiceId::Atmosphere,
            "Slow upper-air harmonic continuity", "foundation", "swelling", "Granulator III",
            "deep evolving granular harmonic pad", .58, .42)};
    for (std::size_t i = floors.size(); i < 3 && plan.instruments.size() < 48; ++i)
        plan.instruments.push_back(floorDefaults[i]);

    if (std::none_of(plan.instruments.begin(), plan.instruments.end(), arpPart) &&
        plan.instruments.size() < 48)
        plan.instruments.push_back(makePart("fabric_hypnotic_arp", "hypnotic_arp", "Hypnotic Arp",
            VoiceId::HarmonicPulse, "Independent evolving electronic arpeggio", "counterpoint",
            "ostinato", "Wavetable", "muted warm hypnotic arpeggiated pulse", .58, .48));
    if (std::none_of(plan.instruments.begin(), plan.instruments.end(), [](const auto& part) {
            return part.sourceVoice == VoiceId::Lead && part.orchestralFunction != "color";
        }) && plan.instruments.size() < 48)
        plan.instruments.push_back(makePart("fabric_primary_speaker", "lead_synth", "Primary Speaker",
            VoiceId::Lead, "Narrative protagonist with statement, answer and return", "counterpoint",
            "legato", "Wavetable", "warm expressive dark mono lead", .46, .68));
    auto dialogues = indicesFor(plan, dialoguePart);
    if (dialogues.size() < 2 && plan.instruments.size() < 48)
        plan.instruments.push_back(makePart("fabric_dialogue_pluck", "deep_pluck", "Deep Pluck Reply",
            VoiceId::Countermelody, "Independent answer derived from the protagonist", "counterpoint",
            "staccato", "Drift", "round dark expressive pluck response", .34, .40));

    floors = indicesFor(plan, floorPart);
    for (std::size_t i = 0; i < std::min<std::size_t>(3, floors.size()); ++i) {
        auto& part = plan.instruments[floors[i]];
        part.activeSections.clear();
        part.activity = std::max(part.activity, .66);
        part.contentLaneId = part.id;
        part.lineRelationship = "independent";
    }
    activateEverywhere(plan, VoiceId::HarmonicFoundation);
    activateEverywhere(plan, VoiceId::HarmonicPulse);
    // Foreground availability remains sectional. The fabric renderer can write a reply
    // onto its concrete part without declaring lead and countermelody simultaneously.
}

ElectronicFabricReport ElectronicCompositionFabric::materialize(Pattern& pattern,
                                                                  const SongPlan& plan) {
    ElectronicFabricReport report;
    report.active = electronic(plan) && !pattern.parts.empty() && !plan.chordPalette.empty();
    if (!report.active) return report;
    const auto partId = [](std::size_t index) { return static_cast<std::uint16_t>(index + 1); };
    auto floors = indicesFor(plan, floorPart);
    const auto arps = indicesFor(plan, arpPart);
    const auto dialogues = indicesFor(plan, dialoguePart);
    std::vector<std::size_t> leads;
    for (std::size_t i = 0; i < plan.instruments.size(); ++i)
        if (plan.instruments[i].sourceVoice == VoiceId::Lead && !dialoguePart(plan.instruments[i]))
            leads.push_back(i);
    std::sort(leads.begin(), leads.end(), [&](auto a, auto b) {
        return plan.instruments[a].prominence > plan.instruments[b].prominence;
    });

    // The harmonic floor is a group contract, not a permanently held single pad.
    // Three independent registers rotate, while at least two remain present per bar.
    if (floors.size() > 4) floors.resize(4);
    std::vector<int> previousPitch(floors.size(), 60);
    for (std::size_t i = 0; i < floors.size(); ++i)
        previousPitch[i] = std::clamp((plan.instruments[floors[i]].minimumPitch +
            plan.instruments[floors[i]].maximumPitch) / 2, 36, 84);
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto beat = bar * plan.beatsPerBar;
        const auto& chord = chordAt(plan, beat);
        if (chord.pitchClasses.empty()) continue;
        const auto rotation = static_cast<std::size_t>((bar / 8) % std::max<std::size_t>(1, floors.size()));
        const auto desired = std::min<std::size_t>(floors.size(), bar % 16 >= 12 ? 3 : 2);
        for (std::size_t slot = 0; slot < desired; ++slot) {
            const auto floorOrdinal = (rotation + slot) % floors.size();
            const auto index = floors[floorOrdinal];
            const auto& assignment = plan.instruments[index];
            if (overlapsPart(pattern, partId(index), beat, beat + plan.beatsPerBar * .75)) continue;
            const auto tone = chord.pitchClasses[(floorOrdinal + static_cast<std::size_t>(bar / 4)) %
                                                  chord.pitchClasses.size()];
            const auto pitch = nearestPitch(tone, previousPitch[floorOrdinal], assignment);
            addNote(pattern, assignment, partId(index), beat, plan.beatsPerBar - 1.0 / 32.0,
                pitch, 48 + static_cast<int>(slot) * 5 + (bar % 8 == 0 ? 5 : 0),
                0x464c4f52u + static_cast<std::uint32_t>(floorOrdinal), report,
                report.foundationNotesCreated);
            previousPitch[floorOrdinal] = pitch;
        }
    }

    // A recognisable electronic motion line: repeated cells, sectional omissions,
    // directional changes and a full-bar breath at the end of every phrase.
    if (!arps.empty()) {
        const auto index = arps.front();
        const auto& assignment = plan.instruments[index];
        for (auto block = 0; block * 8 < plan.totalBars; ++block) {
            const auto blockBar = block * 8;
            const auto blockStart = blockBar * plan.beatsPerBar;
            if (block == 0 || block % 4 == 3 ||
                overlapsPart(pattern, partId(index), blockStart, blockStart + 8 * plan.beatsPerBar)) continue;
            const auto activeBars = block % 3 == 1 ? 5 : 7;
            auto ordinal = 0;
            for (auto bar = 0; bar < activeBars && blockBar + bar < plan.totalBars; ++bar) {
                for (auto step = 0; step < 8; ++step, ++ordinal) {
                    if ((ordinal + block) % 11 == 7 || (step == 7 && bar % 2 == 1)) continue;
                    const auto beat = (blockBar + bar) * plan.beatsPerBar + step * .5;
                    const auto& chord = chordAt(plan, beat);
                    if (chord.pitchClasses.empty()) continue;
                    const auto ascending = block % 2 == 0;
                    const auto position = ascending ? step % static_cast<int>(chord.pitchClasses.size()) :
                        static_cast<int>(chord.pitchClasses.size()) - 1 -
                            step % static_cast<int>(chord.pitchClasses.size());
                    auto target = 60 + (block % 3 == 2 && step == 6 ? 12 : 0);
                    const auto pitch = nearestPitch(chord.pitchClasses[static_cast<std::size_t>(position)],
                        target, assignment);
                    addNote(pattern, assignment, partId(index), beat, .32, pitch,
                        48 + (step % 4 == 0 ? 18 : step % 2 == 0 ? 8 : 0),
                        0x41525000u + static_cast<std::uint32_t>(block), report,
                        report.arpeggioNotesCreated);
                }
            }
        }
    }

    // One protagonist owns the story. Phrases use the AI motif and harmony, while
    // deliberate four/eight-bar gaps keep it from becoming a perpetual solo.
    std::vector<std::pair<double, std::vector<int>>> createdPhrases;
    if (!leads.empty()) {
        const auto index = leads.front();
        const auto& assignment = plan.instruments[index];
        constexpr std::array rhythm{0.0, .75, 1.5, 2.75, 4.0, 5.5, 7.0, 9.5};
        for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
            const auto& section = plan.sections[sectionIndex];
            if (section.bars < 4 || sectionIndex == 0 || !activeIn(assignment, section)) continue;
            const auto stride = section.bars >= 16 ? 12 : 8;
            for (auto localBar = sectionIndex % 2 == 0 ? 1 : 2;
                 localBar + 3 < section.bars; localBar += stride) {
                const auto start = (section.startBar + localBar) * plan.beatsPerBar;
                const auto end = start + 4 * plan.beatsPerBar;
                const auto existing = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                    return note.voice == VoiceId::Lead && note.startBeat >= start && note.startBeat < end;
                });
                if (existing >= 3) continue;
                std::vector<int> pitches;
                pitches.reserve(rhythm.size());
                auto centre = std::clamp(64 + static_cast<int>(sectionIndex % 3) * 2,
                                         assignment.minimumPitch, assignment.maximumPitch);
                for (std::size_t noteIndex = 0; noteIndex < rhythm.size(); ++noteIndex) {
                    const auto beat = start + rhythm[noteIndex];
                    const auto& chord = chordAt(plan, beat);
                    const auto motif = plan.motifIntervals.empty() ? static_cast<int>(noteIndex % 4) :
                        plan.motifIntervals[noteIndex % plan.motifIntervals.size()];
                    auto target = nearestPitchInScale(centre + motif +
                        (sectionIndex % 4 == 2 && noteIndex >= 4 ? 2 : 0),
                        plan.rootPitchClass, plan.scale);
                    if (noteIndex == 0 && !chord.pitchClasses.empty())
                        target = nearestPitch(chord.pitchClasses.front(), target, assignment);
                    target = std::clamp(target, assignment.minimumPitch, assignment.maximumPitch);
                    const auto duration = noteIndex == 3 || noteIndex == 7 ? 1.15 :
                                          noteIndex % 3 == 1 ? .42 : .62;
                    addNote(pattern, assignment, partId(index), beat, duration, target,
                        66 + static_cast<int>(section.energy * 20) +
                            (noteIndex == 0 || noteIndex == 4 ? 8 : 0),
                        0x4c454144u, report, report.protagonistNotesCreated);
                    pitches.push_back(target);
                }
                createdPhrases.emplace_back(start, std::move(pitches));
            }
        }
    }

    // Replies are derived from the protagonist but occupy their own lanes and rests.
    for (std::size_t phraseIndex = 0; phraseIndex < createdPhrases.size() && !dialogues.empty(); ++phraseIndex) {
        const auto dialogueIndex = dialogues[phraseIndex % std::min<std::size_t>(dialogues.size(), 3)];
        const auto& assignment = plan.instruments[dialogueIndex];
        const auto responseStart = createdPhrases[phraseIndex].first + 11.0;
        if (responseStart + 3.0 >= pattern.lengthBeats ||
            overlapsPart(pattern, partId(dialogueIndex), responseStart, responseStart + 4.0)) continue;
        const auto& source = createdPhrases[phraseIndex].second;
        for (auto n = 0; n < 4; ++n) {
            const auto sourcePitch = source[source.size() - 1 - static_cast<std::size_t>(n)];
            auto pitch = nearestPitchInScale(sourcePitch - 12 + (n == 3 ? 2 : 0),
                                             plan.rootPitchClass, plan.scale);
            pitch = std::clamp(pitch, assignment.minimumPitch, assignment.maximumPitch);
            addNote(pattern, assignment, partId(dialogueIndex), responseStart + n * .75,
                n == 3 ? .9 : .34, pitch, 54 + n * 5, 0x4449414cu, report,
                report.dialogueNotesCreated);
        }
    }

    // Give every remaining declared harmonic color an actual independent gesture.
    // This prevents a large cast from degenerating into one-to-four-note token tracks.
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& assignment = plan.instruments[index];
        const auto* definition = instrumentDefinition(assignment.instrumentId);
        if (definition == nullptr || definition->department == ScoreDepartment::Rhythm ||
            std::find(floors.begin(), floors.end(), index) != floors.end() ||
            std::find(arps.begin(), arps.end(), index) != arps.end() ||
            std::find(leads.begin(), leads.end(), index) != leads.end() ||
            std::find(dialogues.begin(), dialogues.end(), index) != dialogues.end()) continue;
        std::vector<const NoteEvent*> existing;
        for (const auto& note : pattern.notes)
            if (note.partId == partId(index)) existing.push_back(&note);
        const auto minimumBars = assignment.sourceVoice == VoiceId::Atmosphere ? 8U : 6U;
        if (existing.size() >= 8 && activeBarsFor(existing, plan.beatsPerBar) >= minimumBars) continue;
        const auto spacingBars = assignment.orchestralFunction == "transition" ? 24 :
            assignment.orchestralFunction == "extension" || assignment.sourceVoice == VoiceId::Atmosphere ? 16 : 8;
        auto previous = std::clamp((assignment.minimumPitch + assignment.maximumPitch) / 2,
                                   assignment.minimumPitch, assignment.maximumPitch);
        for (auto bar = static_cast<int>(index % 7) + 4; bar < plan.totalBars; bar += spacingBars) {
            const auto beat = bar * plan.beatsPerBar;
            if (overlapsPart(pattern, partId(index), beat, beat + plan.beatsPerBar)) continue;
            const auto& chord = chordAt(plan, beat);
            if (chord.pitchClasses.empty()) continue;
            const auto tone = chord.pitchClasses[(index + static_cast<std::size_t>(bar / spacingBars)) %
                                                  chord.pitchClasses.size()];
            const auto pitch = nearestPitch(tone, previous, assignment);
            const auto duration = assignment.orchestralFunction == "transition" ? plan.beatsPerBar * 1.5 :
                assignment.sourceVoice == VoiceId::Atmosphere ? plan.beatsPerBar * 2.0 :
                assignment.articulation == "staccato" ? .35 : plan.beatsPerBar * .9;
            addNote(pattern, assignment, partId(index), beat, duration, pitch,
                38 + static_cast<int>(assignment.prominence * 32),
                0x53555050u + static_cast<std::uint32_t>(index), report,
                report.supportNotesCreated);
        }
    }

    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        if (a.partId != b.partId) return a.partId < b.partId;
        return a.pitch < b.pitch;
    });

    std::map<std::string, std::vector<const NoteEvent*>> notesByLane;
    for (const auto& note : pattern.notes) {
        if (note.partId == 0 || note.partId > plan.instruments.size()) continue;
        const auto& assignment = plan.instruments[note.partId - 1];
        const auto lane = assignment.contentLaneId.empty() ? assignment.id : assignment.contentLaneId;
        notesByLane[lane].push_back(&note);
    }
    report.independentLines = notesByLane.size();
    for (const auto& [lane, notes] : notesByLane) {
        (void) lane;
        if (notes.size() >= 6 && activeBarsFor(notes, plan.beatsPerBar) >= 4) ++report.meaningfulLines;
    }
    for (auto window = 0.0; window < pattern.lengthBeats; window += plan.beatsPerBar * 4.0) {
        const auto count = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::Lead && note.startBeat >= window &&
                   note.startBeat < window + plan.beatsPerBar * 4.0;
        });
        if (count >= 3) ++report.protagonistPhraseWindows;
    }
    for (const auto index : arps)
        report.arpeggioNoteCount += std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.partId == partId(index);
        });
    for (const auto index : dialogues) {
        std::vector<const NoteEvent*> notes;
        for (const auto& note : pattern.notes) if (note.partId == partId(index)) notes.push_back(&note);
        if (notes.size() >= 6 && activeBarsFor(notes, plan.beatsPerBar) >= 4) ++report.dialogueLines;
    }
    std::vector<std::size_t> floorLayers;
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        std::set<std::uint16_t> active;
        const auto start = bar * plan.beatsPerBar;
        for (const auto index : floors)
            if (overlapsPart(pattern, partId(index), start, start + plan.beatsPerBar))
                active.insert(partId(index));
        floorLayers.push_back(active.size());
    }
    if (!floorLayers.empty()) {
        report.harmonicFloorCoverage = static_cast<double>(std::count_if(floorLayers.begin(), floorLayers.end(),
            [](auto layers) { return layers >= 2; })) / static_cast<double>(floorLayers.size());
        auto copy = floorLayers;
        const auto middle = copy.begin() + static_cast<std::ptrdiff_t>(copy.size() / 2);
        std::nth_element(copy.begin(), middle, copy.end());
        report.medianHarmonicFloorLayers = static_cast<double>(*middle);
    }
    report.ready = report.harmonicFloorCoverage >= .80 && report.medianHarmonicFloorLayers >= 2.0 &&
        report.protagonistPhraseWindows >= std::max<std::size_t>(3, plan.totalBars / 24) &&
        report.arpeggioNoteCount >= 32 && report.dialogueLines >= 1 &&
        report.meaningfulLines * 4 >= report.independentLines * 3;
    return report;
}

} // namespace pulso
