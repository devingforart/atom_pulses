#include "TrackViability.h"

#include "ArrangementDensityPlanner.h"
#include "ElectronicRoleContract.h"
#include "ElectronicSoundscape.h"
#include "OrchestrationScore.h"
#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string_view>
#include <tuple>

namespace pulso {
namespace {

struct Evidence {
    std::size_t notes{};
    std::size_t activeBars{};
    std::size_t phrases{};
    std::size_t authoredSeeds{};
};

// A phrase is not made distinct by being repeated in another placement.  Count
// fingerprints over eight-bar windows so a 192-bar ostinato with thousands of
// notes still reports one musical idea instead of passing as a developed line.
std::size_t distinctPhraseCount(const std::vector<const NoteEvent*>& notes,
                                double beatsPerBar) {
    if (notes.empty()) return 0;
    const auto width = std::max(1.0, beatsPerBar) * 8.0;
    std::map<int, std::vector<const NoteEvent*>> windows;
    for (const auto* note : notes)
        windows[static_cast<int>(std::floor(note->startBeat / width))].push_back(note);
    std::set<std::uint64_t> fingerprints;
    for (auto& [window, phrase] : windows) {
        (void) window;
        if (phrase.size() < 3) continue;
        std::sort(phrase.begin(), phrase.end(), [](const auto* left, const auto* right) {
            return std::tie(left->startBeat, left->pitch, left->durationBeats) <
                   std::tie(right->startBeat, right->pitch, right->durationBeats);
        });
        const auto origin = phrase.front()->startBeat;
        const auto pitchOrigin = phrase.front()->pitch;
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto* note : phrase) {
            const auto onset = static_cast<std::uint64_t>(std::llround((note->startBeat - origin) * 8.0));
            const auto duration = static_cast<std::uint64_t>(std::llround(note->durationBeats * 8.0));
            const auto contour = static_cast<std::uint64_t>(std::clamp(note->pitch - pitchOrigin, -48, 48) + 48);
            hash ^= onset + contour * 131 + duration * 17;
            hash *= 1099511628211ULL;
        }
        fingerprints.insert(hash);
    }
    return std::max<std::size_t>(notes.empty() ? 0 : 1, fingerprints.size());
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool contains(std::string_view text, std::string_view token) noexcept {
    return text.find(token) != std::string_view::npos;
}

const InstrumentAssignment* assignmentFor(const InstrumentPart& part,
                                          const SongPlan& plan) noexcept {
    if (part.id > 0 && part.id <= plan.instruments.size())
        return &plan.instruments[part.id - 1];
    const auto found = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& item) { return item.id == part.contentLaneId; });
    return found == plan.instruments.end() ? nullptr : &*found;
}

const SoundscapeLayerPlan* layerFor(const InstrumentPart& part,
                                    const SongPlan& plan) noexcept {
    const auto* assignment = assignmentFor(part, plan);
    if (assignment == nullptr) return nullptr;
    const auto found = std::find_if(plan.soundscape.layers.begin(), plan.soundscape.layers.end(),
        [&](const auto& layer) { return layer.instrumentId == assignment->id; });
    return found == plan.soundscape.layers.end() ? nullptr : &*found;
}

TrackFunction functionFor(const InstrumentPart& part, const SongPlan& plan) {
    if (part.department == ScoreDepartment::Rhythm) return TrackFunction::Rhythm;
    const auto role = lower(part.role + " " + part.orchestralFunction);
    const auto relationship = lower(part.lineRelationship);
    const auto* layer = layerFor(part, plan);
    if (layer != nullptr && layer->kind == SoundscapeLayerKind::OneShot)
        return TrackFunction::OneShot;
    if (part.sourceVoice == VoiceId::Transitions || part.orchestralFunction == "transition" ||
        (layer != nullptr && layer->kind == SoundscapeLayerKind::Transition))
        return TrackFunction::Transition;
    if (part.sourceVoice == VoiceId::SubBass || part.sourceVoice == VoiceId::MovementBass ||
        part.catalogId == "sub_synth" || part.catalogId == "rolling_mid_bass" ||
        part.catalogId == "electric_bass" || part.catalogId == "reese_layer")
        return TrackFunction::Bass;
    if (part.sourceVoice == VoiceId::Countermelody || relationship == "call_response" ||
        contains(role, "reply") || contains(role, "response") || contains(role, "answer"))
        return TrackFunction::Dialogue;
    if (part.sourceVoice == VoiceId::Lead) return TrackFunction::Protagonist;
    if (part.sourceVoice == VoiceId::Atmosphere ||
        (layer != nullptr && layer->kind == SoundscapeLayerKind::Environment) ||
        part.catalogId == "ambient_texture" || part.catalogId == "granular_pad" ||
        part.catalogId == "spectral_drone" || part.catalogId == "shimmer_tail")
        return TrackFunction::Environment;
    if (part.catalogId == "hypnotic_arp" || part.catalogId == "fm_sequence" ||
        part.catalogId == "filtered_stab" || part.catalogId == "dub_chord" ||
        part.sourceVoice == VoiceId::HarmonicPulse || contains(role, "pulse") ||
        contains(role, "arpeggio"))
        return TrackFunction::Pulse;
    if (part.sourceVoice == VoiceId::HarmonicFoundation &&
        (part.orchestralFunction == "foundation" || contains(role, "floor") ||
         contains(role, "bed") || contains(role, "pad")))
        return TrackFunction::HarmonicFloor;
    return TrackFunction::HarmonicVoice;
}

std::size_t availableBars(const InstrumentPart& part, const SongPlan& plan) {
    const auto* assignment = assignmentFor(part, plan);
    if (assignment == nullptr || assignment->activeSections.empty())
        return static_cast<std::size_t>(std::max(1, plan.totalBars));
    auto result = std::size_t{};
    for (const auto& section : plan.sections)
        if (std::find(assignment->activeSections.begin(), assignment->activeSections.end(),
                      section.name) != assignment->activeSections.end())
            result += static_cast<std::size_t>(section.bars);
    return std::max<std::size_t>(1, result);
}

Evidence evidence(const Pattern& pattern, std::uint16_t partId, double beatsPerBar) {
    std::vector<const NoteEvent*> notes;
    std::set<int> bars;
    for (const auto& note : pattern.notes) {
        if (note.partId != partId) continue;
        notes.push_back(&note);
        const auto first = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        const auto last = static_cast<int>(std::floor(
            std::max(note.startBeat, note.endBeat() - .001) / beatsPerBar));
        for (auto bar = first; bar <= last; ++bar) bars.insert(bar);
    }
    Evidence result;
    result.notes = notes.size();
    result.activeBars = bars.size();
    result.authoredSeeds = std::count_if(notes.begin(), notes.end(), [](const auto* note) {
        return note->origin == NoteOrigin::AiAuthored || note->origin == NoteOrigin::AiTransformed;
    });
    std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
        return left->startBeat < right->startBeat;
    });
    if (!notes.empty()) {
        result.phrases = 1;
        auto soundingUntil = notes.front()->endBeat();
        for (std::size_t index = 1; index < notes.size(); ++index) {
            if (notes[index]->startBeat - soundingUntil >= beatsPerBar * .75) ++result.phrases;
            soundingUntil = std::max(soundingUntil, notes[index]->endBeat());
        }
        // Prefer distinct musical statements over raw silence-separated chunks.
        // This catches the common failure mode where one cell is copied for the
        // whole arrangement while still allowing a genuinely sparse one-shot lane.
        result.phrases = std::max(result.phrases, distinctPhraseCount(notes, beatsPerBar));
    }
    return result;
}

bool viable(const Evidence& value, const TrackViabilityContract& contract,
            const SongPlan& plan) noexcept {
    const auto exact = value.notes >= contract.minimumNotes &&
        value.activeBars >= contract.minimumActiveBars &&
        value.phrases >= contract.minimumPhrases;
    return exact || (plan.instrumentCastAuthored && value.authoredSeeds > 0 &&
        TrackViability::marginalActiveBarAcceptance(
            value.notes, value.activeBars, value.phrases, contract));
}

bool protectedIndependentAuthorship(const InstrumentPart& part, const SongPlan& plan,
                                    const Evidence& value) noexcept {
    return plan.instrumentCastAuthored && !plan.performanceScore.empty() &&
        part.lineRelationship == "independent" && value.authoredSeeds > 0 &&
        value.notes >= 8 && value.activeBars >= 4 && value.phrases >= 2;
}

bool essentialAuthoredIdentity(const InstrumentPart& part, const SongPlan& plan) noexcept {
    const auto* assignment = assignmentFor(part, plan);
    return assignment != nullptr &&
        (assignment->explicitPromptIdentity ||
         assignment->id == plan.narrativeSpine.protagonistInstrumentId ||
         ElectronicRoleContract::motionOwner(*assignment) ||
         contains(lower(assignment->role), "primary_chord_bed"));
}

std::uint16_t primaryBassPartId(const Pattern& pattern, const SongPlan& plan) {
    auto result = std::uint16_t{};
    auto best = -1.0;
    for (const auto& part : pattern.parts) {
        if (functionFor(part, plan) != TrackFunction::Bass) continue;
        const auto current = evidence(pattern, part.id, plan.beatsPerBar);
        if (current.notes == 0) continue;
        // A single narrative owner wins over nominal sub layers. Authored substance
        // breaks ties, so a five-note label can never become the conductor by name.
        const auto score = (part.sourceVoice == VoiceId::MovementBass ? 10000.0 : 0.0) +
            static_cast<double>(current.authoredSeeds) * 12.0 +
            static_cast<double>(current.activeBars) * 4.0 +
            static_cast<double>(current.notes) + part.prominence;
        if (score > best) { best = score; result = part.id; }
    }
    return result;
}

const SongSection* sectionAt(const SongPlan& plan, int bar) noexcept {
    const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
        return bar >= section.startBar && bar < section.startBar + section.bars;
    });
    return found == plan.sections.end() ? nullptr : &*found;
}

bool activeIn(const InstrumentPart& part, const SongPlan& plan, int bar) {
    const auto* assignment = assignmentFor(part, plan);
    const auto* section = sectionAt(plan, bar);
    return assignment != nullptr && section != nullptr &&
        (assignment->activeSections.empty() ||
         std::find(assignment->activeSections.begin(), assignment->activeSections.end(), section->name) !=
             assignment->activeSections.end());
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

int nearestPitch(int pitchClass, int target, const InstrumentPart& part) noexcept {
    auto best = std::clamp(target, part.minimumPitch, part.maximumPitch);
    auto distance = 1000;
    for (auto pitch = part.minimumPitch; pitch <= part.maximumPitch; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto current = std::abs(pitch - target);
        if (current < distance) { best = pitch; distance = current; }
    }
    return best;
}

std::size_t soundingParts(const Pattern& pattern, double beat) {
    std::set<std::uint16_t> result;
    for (const auto& note : pattern.notes)
        if (note.partId != 0 && note.startBeat <= beat + .001 && note.endBeat() > beat + .001)
            result.insert(note.partId);
    return result.size();
}

std::size_t develop(Pattern& pattern, const SongPlan& plan, const InstrumentPart& part,
                    const TrackViabilityContract& contract) {
    auto before = evidence(pattern, part.id, plan.beatsPerBar);
    const auto authoredAiScore = plan.instrumentCastAuthored && !plan.performanceScore.empty();
    const auto* assignment = assignmentFor(part, plan);
    const auto protectedAiLine = assignment != nullptr &&
        (assignment->id == plan.narrativeSpine.protagonistInstrumentId ||
         ElectronicRoleContract::motionOwner(*assignment) ||
         lower(assignment->role).find("primary_chord_bed") != std::string::npos);
    // These three responsibilities define the song's identity, propulsion and
    // explicit harmony. If GPT underwrites one of them, the bounded AI editor must
    // repair it; local pattern synthesis would silently replace authorship and was
    // the source of procedural basses and monophonic pseudo-chord beds.
    if (authoredAiScore && protectedAiLine) return 0;
    // A handful of isolated notes does not justify an exported instrument lane.
    // Preserve that authored gesture during terminal relay, but do not inflate it
    // procedurally until a label looks like a composed part.
    if (authoredAiScore && before.notes <= 5 &&
        !essentialAuthoredIdentity(part, plan)) return 0;
    if (authoredAiScore && contract.function != TrackFunction::HarmonicFloor &&
        contract.function != TrackFunction::Environment &&
        contract.function != TrackFunction::Pulse &&
        contract.function != TrackFunction::Bass &&
        !(contract.function == TrackFunction::HarmonicVoice &&
          (part.sourceVoice == VoiceId::HarmonicFoundation ||
           part.sourceVoice == VoiceId::HarmonicUpper))) return 0;
    // Only GPT-authored notes may seed new composition. A lone local placeholder is
    // evidence that the instrument was named but never actually written.
    if (before.authoredSeeds < 2 || contract.eventException) return 0;
    std::vector<const NoteEvent*> seeds;
    for (const auto& note : pattern.notes)
        if (note.partId == part.id &&
            (note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed))
            seeds.push_back(&note);
    std::sort(seeds.begin(), seeds.end(), [](const auto* a, const auto* b) {
        return a->startBeat < b->startBeat;
    });
    const auto identity = seeds.front()->narrativeId == 0 ?
        0x54560000u + static_cast<std::uint32_t>(part.id) : seeds.front()->narrativeId;
    const auto baseTarget = seeds.front()->pitch;
    const auto maximumParts = ArrangementDensityPlanner::targetsFor(plan).maximumSimultaneousParts + 1;
    auto created = std::size_t{};
    std::vector<int> candidates;
    for (const auto& section : plan.sections) {
        const auto stride = contract.function == TrackFunction::HarmonicFloor ||
            contract.function == TrackFunction::Environment ? 4 : 6;
        for (auto local = static_cast<int>(part.id % 3); local < section.bars; local += stride) {
            const auto bar = section.startBar + local;
            if (activeIn(part, plan, bar)) candidates.push_back(bar);
        }
    }
    if (candidates.empty()) return 0;
    const auto rotation = static_cast<std::size_t>((plan.seed ^ part.id) % candidates.size());
    std::rotate(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(rotation), candidates.end());

    for (std::size_t attempt = 0; attempt < candidates.size() * 2; ++attempt) {
        const auto current = evidence(pattern, part.id, plan.beatsPerBar);
        if (viable(current, contract, plan)) break;
        const auto bar = candidates[attempt % candidates.size()];
        const auto start = bar * plan.beatsPerBar;
        if (soundingParts(pattern, start + .01) >= maximumParts) continue;
        const auto& chord = chordAt(plan, start);
        if (chord.pitchClasses.empty()) continue;
        auto add = [&](double offset, double duration, int pitch, int velocity) {
            const auto beat = start + offset;
            if (beat >= pattern.lengthBeats || duration <= .01) return;
            const auto duplicate = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.partId == part.id && std::abs(note.startBeat - beat) < .01 && note.pitch == pitch;
            });
            if (duplicate) return;
            pattern.notes.push_back({beat, std::min(duration, pattern.lengthBeats - beat), pitch,
                std::clamp(velocity, 1, 127), voiceDefinition(part.sourceVoice).midiChannel,
                part.sourceVoice, part.id, true, NoteOrigin::PlanDerived, identity});
            ++created;
        };
        const auto variation = static_cast<int>((attempt + part.id + plan.seed) % 4);
        const auto chordTone = [&](std::size_t ordinal, int target) {
            return nearestPitch(chord.pitchClasses[ordinal % chord.pitchClasses.size()], target, part);
        };
        switch (contract.function) {
            case TrackFunction::HarmonicFloor:
                // Three sounding bars plus one breath creates an actual phrase boundary;
                // four adjacent held notes would satisfy coverage while remaining one bed.
                for (auto local = 0; local < 3 && bar + local < plan.totalBars; ++local) {
                    const auto beat = local * plan.beatsPerBar;
                    const auto& localChord = chordAt(plan, start + beat);
                    const auto tone = localChord.pitchClasses.empty() ? plan.rootPitchClass :
                        localChord.pitchClasses[(part.id + local) % localChord.pitchClasses.size()];
                    add(beat, plan.beatsPerBar - 1.0 / 32.0,
                        nearestPitch(tone, baseTarget + (variation - 2) * 2, part), 43 + variation * 4);
                }
                break;
            case TrackFunction::Environment:
                add(0.0, std::min(plan.beatsPerBar * 2.0 - 1.0 / 32.0,
                                  pattern.lengthBeats - start),
                    chordTone(attempt + 1, baseTarget + (variation - 1) * 5), 34 + variation * 4);
                break;
            case TrackFunction::Bass:
                for (auto note = 0; note < 4; ++note) {
                    const auto& localChord = chordAt(plan, start + note * plan.beatsPerBar);
                    const auto tone = localChord.pitchClasses.empty() ? plan.rootPitchClass :
                        (note == 3 && localChord.pitchClasses.size() > 1 ? localChord.pitchClasses[1] :
                         localChord.pitchClasses.front());
                    add(note * plan.beatsPerBar + (note == 2 ? .75 : .5), .38,
                        nearestPitch(tone, baseTarget + (variation == 3 ? 5 : 0), part), 69 + note * 3);
                }
                break;
            case TrackFunction::Pulse:
                for (auto note = 0; note < 6; ++note)
                    if (!((note + variation) % 5 == 0))
                        add(note * .5, .28, chordTone(note + variation, baseTarget + (variation > 1 ? 12 : 0)),
                            52 + (note % 2 == 0 ? 13 : 3));
                break;
            case TrackFunction::Protagonist: {
                constexpr auto count = 5;
                for (auto note = 0; note < count; ++note) {
                    const auto motif = plan.motifIntervals.empty() ? note * 2 :
                        plan.motifIntervals[static_cast<std::size_t>((note + variation) % plan.motifIntervals.size())];
                    const auto target = baseTarget + (variation == 2 ? -motif : motif);
                    add(note * .75, note + 1 == count ? 1.15 : .42, nearestPitchInScale(target,
                            plan.rootPitchClass, plan.scale), 55 + variation * 5 + (note == 0 ? 8 : 0));
                }
                break;
            }
            case TrackFunction::Dialogue: {
                // A reply quotes only a small intervallic clue, then moves in contrary
                // motion after a deliberate gap. It must not be another complete copy
                // of the protagonist distributed onto a different MIDI track.
                constexpr auto count = 4;
                const auto clue = plan.motifIntervals.empty() ? 3 :
                    std::abs(plan.motifIntervals[static_cast<std::size_t>(variation) %
                                                  plan.motifIntervals.size()]);
                for (auto note = 0; note < count; ++note) {
                    const auto contrary = note == 0 ? 0 : -(clue + note * 2) + variation;
                    const auto target = baseTarget - 5 + contrary;
                    add(1.5 + note * (note == 0 ? .75 : 1.0),
                        note + 1 == count ? 1.25 : .34,
                        nearestPitchInScale(target, plan.rootPitchClass, plan.scale),
                        51 + variation * 4 + (note == 0 ? 7 : 0));
                }
                break;
            }
            case TrackFunction::HarmonicVoice: {
                // Independent inner orchestration follows the chord trajectory and
                // voice-leading register; it does not consume the song's leitmotif.
                auto previous = baseTarget + (variation - 2) * 4;
                for (auto note = 0; note < 3; ++note) {
                    const auto offset = note * plan.beatsPerBar;
                    const auto& localChord = chordAt(plan, start + offset);
                    if (localChord.pitchClasses.empty()) continue;
                    const auto tone = localChord.pitchClasses[
                        (static_cast<std::size_t>(part.id) + static_cast<std::size_t>(note)) %
                        localChord.pitchClasses.size()];
                    const auto pitch = nearestPitch(tone, previous, part);
                    add(offset, plan.beatsPerBar * (note == 1 ? .55 : .92) - .03125,
                        pitch, 43 + variation * 4 + (note == 2 ? 5 : 0));
                    previous = pitch;
                }
                break;
            }
            case TrackFunction::Rhythm:
            case TrackFunction::Transition:
            case TrackFunction::OneShot:
                break;
        }
    }
    return created;
}

bool compatible(TrackFunction source, TrackFunction target,
                ScoreDepartment sourceDepartment, ScoreDepartment targetDepartment) noexcept {
    if (sourceDepartment != targetDepartment) return false;
    if (source == target) return true;
    return (source == TrackFunction::HarmonicVoice && target == TrackFunction::HarmonicFloor) ||
           (source == TrackFunction::Dialogue && target == TrackFunction::Protagonist) ||
           (source == TrackFunction::Environment && target == TrackFunction::HarmonicFloor) ||
           (source == TrackFunction::Transition && target == TrackFunction::OneShot);
}

void removePerformanceFor(Pattern& pattern, std::uint16_t partId) {
    pattern.controls.erase(std::remove_if(pattern.controls.begin(), pattern.controls.end(),
        [&](const auto& event) { return event.partId == partId; }), pattern.controls.end());
    pattern.expressions.erase(std::remove_if(pattern.expressions.begin(), pattern.expressions.end(),
        [&](const auto& event) { return event.partId == partId; }), pattern.expressions.end());
}

void transferPerformanceFor(Pattern& pattern, std::uint16_t sourceId,
                            const InstrumentPart& target) {
    for (auto& event : pattern.controls) {
        if (event.partId != sourceId) continue;
        event.partId = target.id;
        event.voice = target.sourceVoice;
        event.channel = voiceDefinition(target.sourceVoice).midiChannel;
    }
    for (auto& event : pattern.expressions) {
        if (event.partId != sourceId) continue;
        event.partId = target.id;
        event.voice = target.sourceVoice;
        event.channel = voiceDefinition(target.sourceVoice).midiChannel;
    }
}

} // namespace

bool TrackViability::marginalActiveBarAcceptance(
    std::size_t notes, std::size_t activeBars, std::size_t phrases,
    const TrackViabilityContract& contract) noexcept {
    return notes >= contract.minimumNotes &&
        phrases >= contract.minimumPhrases &&
        activeBars < contract.minimumActiveBars &&
        contract.minimumActiveBars - activeBars == 1;
}

bool TrackViability::acceptsCoverage(
    std::size_t notes, std::size_t activeBars, std::size_t phrases,
    const TrackViabilityContract& contract) noexcept {
    const auto exact = notes >= contract.minimumNotes &&
        activeBars >= contract.minimumActiveBars &&
        phrases >= contract.minimumPhrases;
    return exact || marginalActiveBarAcceptance(notes, activeBars, phrases, contract);
}

TrackViabilityContract TrackViability::contractFor(const InstrumentPart& part,
                                                    const SongPlan& plan) {
    TrackViabilityContract result;
    result.function = functionFor(part, plan);
    const auto horizon = static_cast<std::size_t>(std::max(1, plan.totalBars));
    const auto available = availableBars(part, plan);
    const auto capped = [&](std::size_t target) { return std::min(available, std::max<std::size_t>(1, target)); };
    switch (result.function) {
        case TrackFunction::Rhythm:
            // AI rhythm is a coordinated long-form narrative, not merely proof that a
            // drum exists. Derive coverage from the authored rhythmic language rather
            // than imposing a genre template. Legacy/local plans keep the permissive
            // articulation contract because RhythmEngine owns their realization.
            if (!plan.instrumentCastAuthored || plan.percussionFreeIntent) {
                result.minimumActiveBars = 1;
                result.minimumNotes = 1;
                result.minimumPhrases = 1;
                result.eventException = true;
                break;
            }
            {
                auto ratio = .025 + plan.rhythmLanguage.callResponse * .055;
                if (part.sourceVoice == VoiceId::CoreDrums)
                    ratio = .12 + plan.rhythmLanguage.pulseStability * .18;
                else if (part.sourceVoice == VoiceId::SnareClap)
                    ratio = .055 + plan.rhythmLanguage.backbeatGravity * .14;
                else if (part.sourceVoice == VoiceId::ClosedHats)
                    ratio = .05 + plan.rhythmLanguage.orchestrationMotion * .12;
                else if (part.sourceVoice == VoiceId::OpenHatsShaker)
                    ratio = .035 + plan.rhythmLanguage.orchestrationMotion * .08;
                result.minimumActiveBars = capped(std::max<std::size_t>(4,
                    static_cast<std::size_t>(std::lround(horizon * ratio))));
                const auto attacksPerBar = part.sourceVoice == VoiceId::CoreDrums ||
                    part.sourceVoice == VoiceId::ClosedHats ? 2U : 1U;
                result.minimumNotes = std::max<std::size_t>(6,
                    result.minimumActiveBars * attacksPerBar);
                result.minimumPhrases = result.minimumActiveBars >= 96 ? 4 :
                    result.minimumActiveBars >= 12 ? 3 : 2;
            }
            break;
        case TrackFunction::Bass:
            // A bass lane is a phrase-level foundation, not a four-bar token.  Keep
            // intentional rests in the score, but require roughly 35--45% of the
            // available horizon so a large cast does not turn into an empty grid.
            result.minimumActiveBars = capped(std::max<std::size_t>(12,
                static_cast<std::size_t>(std::lround(horizon *
                    (part.sourceVoice == VoiceId::SubBass ? .44 : .36)))));
            result.minimumNotes = std::max<std::size_t>(20, result.minimumActiveBars);
            result.minimumPhrases = horizon >= 96 ? 4 : 3;
            break;
        case TrackFunction::HarmonicFloor:
            // Pads/floor layers provide the continuous harmonic floor.  Their
            // coverage is intentionally high while the attention director still
            // inserts phrase breaths and sectional subtraction.
            result.minimumActiveBars = capped(std::max<std::size_t>(16,
                static_cast<std::size_t>(std::lround(horizon * .58))));
            result.minimumNotes = std::max<std::size_t>(16, result.minimumActiveBars);
            result.minimumPhrases = horizon >= 96 ? 4 : 3;
            break;
        case TrackFunction::HarmonicVoice:
            result.minimumActiveBars = capped(std::max<std::size_t>(10,
                static_cast<std::size_t>(std::lround(horizon * .28))));
            result.minimumNotes = std::max<std::size_t>(12, result.minimumActiveBars);
            result.minimumPhrases = horizon >= 96 ? 3 : 2;
            break;
        case TrackFunction::Pulse:
            result.minimumActiveBars = capped(std::max<std::size_t>(10,
                static_cast<std::size_t>(std::lround(horizon * .24))));
            result.minimumNotes = std::max<std::size_t>(24, result.minimumActiveBars * 2);
            result.minimumPhrases = horizon >= 96 ? 4 : 3;
            break;
        case TrackFunction::Protagonist:
            result.minimumActiveBars = capped(std::max<std::size_t>(10,
                static_cast<std::size_t>(std::lround(horizon * .22))));
            result.minimumNotes = std::max<std::size_t>(18, result.minimumActiveBars);
            result.minimumPhrases = horizon >= 96 ? 4 : 3;
            break;
        case TrackFunction::Dialogue:
            result.minimumActiveBars = capped(std::max<std::size_t>(8,
                static_cast<std::size_t>(std::lround(horizon * .14))));
            result.minimumNotes = std::max<std::size_t>(12, result.minimumActiveBars);
            result.minimumPhrases = horizon >= 96 ? 3 : 2;
            break;
        case TrackFunction::Environment:
            // Textures remain episodic, but must recur often enough to be heard as
            // an evolving soundscape rather than a named-but-empty destination.
            result.minimumActiveBars = capped(std::max<std::size_t>(8,
                static_cast<std::size_t>(std::lround(horizon * .16))));
            result.minimumNotes = std::max<std::size_t>(6, result.minimumActiveBars / 2);
            result.minimumPhrases = horizon >= 96 ? 3 : 2;
            break;
        case TrackFunction::Transition:
            result.minimumActiveBars = 1;
            result.minimumNotes = 2;
            result.minimumPhrases = 2;
            result.eventException = true;
            break;
        case TrackFunction::OneShot:
            result.minimumActiveBars = 1;
            result.minimumNotes = 1;
            result.minimumPhrases = 1;
            result.eventException = true;
            break;
    }
    if (const auto* layer = layerFor(part, plan); layer != nullptr && !result.eventException) {
        result.minimumActiveBars = std::min(available, std::max(result.minimumActiveBars,
            static_cast<std::size_t>(std::max(1, layer->minimumActiveBars))));
        result.minimumPhrases = std::max(result.minimumPhrases,
            static_cast<std::size_t>(std::max(1, layer->minimumPhrases)));
    }
    return result;
}

TrackViabilityContract TrackViability::contractFor(const InstrumentAssignment& assignment,
                                                    const SongPlan& plan) {
    InstrumentPart part;
    part.contentLaneId = assignment.id;
    part.catalogId = assignment.instrumentId;
    part.name = assignment.name;
    part.sourceVoice = assignment.sourceVoice;
    part.role = assignment.role;
    part.minimumPitch = assignment.minimumPitch;
    part.maximumPitch = assignment.maximumPitch;
    part.orchestralFunction = assignment.orchestralFunction;
    part.articulation = assignment.articulation;
    part.lineRelationship = assignment.lineRelationship;
    if (const auto* definition = instrumentDefinition(assignment.instrumentId))
        part.department = definition->department;
    else if (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Rhythm))
        part.department = ScoreDepartment::Rhythm;
    else if (assignment.sourceVoice == VoiceId::Lead ||
             assignment.sourceVoice == VoiceId::Countermelody)
        part.department = ScoreDepartment::Melody;
    else
        part.department = ScoreDepartment::Harmony;
    return contractFor(part, plan);
}

TrackViabilityReport TrackViability::enforce(Pattern& pattern, SongPlan& plan) {
    TrackViabilityReport report;
    report.active = !pattern.parts.empty();
    report.declaredTracks = plan.instruments.size();
    if (!report.active) return report;
    for (const auto& part : pattern.parts) {
        const auto current = evidence(pattern, part.id, plan.beatsPerBar);
        if (current.notes == 0) continue;
        ++report.populatedBefore;
        if (viable(current, contractFor(part, plan), plan)) ++report.meaningfulBefore;
    }

    // A later tonal, vertical-harmony or duration pass can remove material that was
    // viable earlier. Converge from the exact current score instead of assuming one
    // construction pass is sufficient. Each round either develops, relays or removes
    // at least one incomplete lane, so this loop is bounded and deterministic.
    std::set<std::string> removedAssignments;
    for (auto round = 0; round < 3; ++round) {
        auto changed = false;
        for (const auto& part : pattern.parts) {
            const auto current = evidence(pattern, part.id, plan.beatsPerBar);
            const auto contract = contractFor(part, plan);
            if (current.notes == 0 || viable(current, contract, plan)) continue;
            const auto created = develop(pattern, plan, part, contract);
            if (created > 0) {
                ++report.developedTracks;
                report.notesCreated += created;
                changed = true;
            }
        }

        std::set<std::uint16_t> viableParts;
        for (const auto& part : pattern.parts)
            if (viable(evidence(pattern, part.id, plan.beatsPerBar), contractFor(part, plan), plan))
                viableParts.insert(part.id);

        for (const auto& source : pattern.parts) {
            const auto sourceEvidence = evidence(pattern, source.id, plan.beatsPerBar);
            const auto sourceContract = contractFor(source, plan);
            if (sourceEvidence.notes == 0 || viableParts.contains(source.id)) continue;
            // An independent GPT line is a real orchestration decision. Folding it into
            // another instrument turns a large ensemble into a handful of supertracks
            // and changes timbre, register and dialogue. Keep it visible as incomplete
            // evidence so the AI contract can repair it; only relays/doublings may merge.
            if (protectedIndependentAuthorship(source, plan, sourceEvidence)) continue;
            const InstrumentPart* target = nullptr;
            const auto primaryBass = sourceContract.function == TrackFunction::Bass
                ? primaryBassPartId(pattern, plan) : std::uint16_t{};
            for (const auto& candidate : pattern.parts) {
                if (!viableParts.contains(candidate.id) || candidate.id == source.id) continue;
                if (compatible(sourceContract.function, contractFor(candidate, plan).function,
                               source.department, candidate.department)) {
                    target = &candidate;
                    if (primaryBass != 0) {
                        if (candidate.id == primaryBass) break;
                    } else if (sourceContract.function == contractFor(candidate, plan).function) break;
                }
            }
            const auto preserveAuthoredEvent = sourceEvidence.authoredSeeds > 0 && target != nullptr;
            if (preserveAuthoredEvent) {
                for (auto& note : pattern.notes) {
                    if (note.partId != source.id) continue;
                    note.partId = target->id;
                    note.voice = target->sourceVoice;
                    note.channel = voiceDefinition(target->sourceVoice).midiChannel;
                    note.pitch = nearestPitch(positiveModulo(note.pitch, 12), note.pitch, *target);
                }
                transferPerformanceFor(pattern, source.id, *target);
                ++report.mergedTracks;
            } else {
                pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                    [&](const auto& note) { return note.partId == source.id; }), pattern.notes.end());
                ++report.prunedTracks;
            }
            if (!preserveAuthoredEvent) removePerformanceFor(pattern, source.id);
            if (const auto* assignment = assignmentFor(source, plan))
                removedAssignments.insert(assignment->id);
            changed = true;
        }
        if (audit(pattern, plan).tokenTracks == 0 || !changed) break;
    }
    plan.soundscape.layers.erase(std::remove_if(plan.soundscape.layers.begin(), plan.soundscape.layers.end(),
        [&](const auto& layer) { return removedAssignments.contains(layer.instrumentId); }),
        plan.soundscape.layers.end());
    auto finalReport = audit(pattern, plan);
    finalReport.populatedBefore = report.populatedBefore;
    finalReport.meaningfulBefore = report.meaningfulBefore;
    finalReport.developedTracks = report.developedTracks;
    finalReport.notesCreated = report.notesCreated;
    finalReport.mergedTracks = report.mergedTracks;
    finalReport.prunedTracks = report.prunedTracks;
    if (finalReport.developedTracks > 0)
        finalReport.issues.push_back("ai_tracks_required_plan_derived_development");
    if (finalReport.prunedTracks > 0 || finalReport.mergedTracks > 0)
        finalReport.issues.push_back("token_tracks_compacted_before_publication");
    return finalReport;
}

TrackViabilityReport TrackViability::audit(const Pattern& pattern, const SongPlan& plan) {
    TrackViabilityReport report;
    report.active = !pattern.parts.empty();
    report.declaredTracks = plan.instruments.size();
    if (!report.active) return report;
    for (const auto& part : pattern.parts) {
        const auto current = evidence(pattern, part.id, plan.beatsPerBar);
        if (current.notes == 0) continue;
        ++report.retainedTracks;
        const auto contract = contractFor(part, plan);
        if (contract.eventException) ++report.eventTracks;
        if (viable(current, contract, plan)) ++report.viableTracks;
        else ++report.tokenTracks;
    }
    report.viabilityRatio = static_cast<double>(report.viableTracks) /
        std::max<std::size_t>(1, report.retainedTracks);
    report.retentionRatio = static_cast<double>(report.retainedTracks) /
        std::max<std::size_t>(1, report.declaredTracks);
    const auto retentionFit = std::clamp(report.retentionRatio / .65, 0.0, 1.0);
    report.score = report.viabilityRatio * .68 + retentionFit * .32;
    const auto densityMinimum = ArrangementDensityPlanner::targetsFor(plan).minimumPopulatedParts;
    // Pruning an inflated cast is a successful outcome, not a publication failure.
    // The hard invariant is that every retained track is complete; the arrangement
    // still has to retain the musically justified minimum for its requested scale.
    report.ready = report.tokenTracks == 0 && report.viabilityRatio >= .98 &&
        report.retainedTracks >= std::min(report.declaredTracks, densityMinimum);
    if (report.tokenTracks > 0) report.issues.push_back("tracks_without_functional_development");
    if (report.retainedTracks < std::min(report.declaredTracks, densityMinimum))
        report.issues.push_back("instrument_cast_exceeds_authored_material");
    return report;
}

TrackViabilityReport TrackViability::compactIncomplete(Pattern& pattern, SongPlan& plan) {
    TrackViabilityReport report;
    report.active = !pattern.parts.empty();
    report.declaredTracks = plan.instruments.size();
    std::set<std::string> removedAssignments;
    std::set<std::uint16_t> viableParts;
    for (const auto& part : pattern.parts)
        if (viable(evidence(pattern, part.id, plan.beatsPerBar), contractFor(part, plan), plan))
            viableParts.insert(part.id);
    for (const auto& part : pattern.parts) {
        const auto current = evidence(pattern, part.id, plan.beatsPerBar);
        if (current.notes == 0 || viable(current, contractFor(part, plan), plan)) continue;
        // This is the terminal publication boundary. Independent authorship earned
        // its opportunity to be repaired in enforce(), but a still-incomplete colour
        // lane must not survive merely because it has an "independent" label. Preserve
        // only identities that define the song; relay every other authored gesture to
        // a compatible developed owner instead of exporting a token track.
        if (essentialAuthoredIdentity(part, plan)) continue;
        const auto sourceFunction = contractFor(part, plan).function;
        const auto hasStructuralAuthorship = std::any_of(pattern.notes.begin(), pattern.notes.end(),
            [&](const auto& note) {
                return note.partId == part.id &&
                    (note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed ||
                     note.origin == NoteOrigin::PlanDerived);
            });
        const InstrumentPart* target = nullptr;
        const auto primaryBass = sourceFunction == TrackFunction::Bass
            ? primaryBassPartId(pattern, plan) : std::uint16_t{};
        for (const auto& candidate : pattern.parts) {
            if (candidate.id == part.id || !viableParts.contains(candidate.id)) continue;
            if (!compatible(sourceFunction, contractFor(candidate, plan).function,
                            part.department, candidate.department)) continue;
            target = &candidate;
            if (primaryBass != 0) {
                if (candidate.id == primaryBass) break;
            } else if (sourceFunction == contractFor(candidate, plan).function) break;
        }
        // At the terminal boundary authorship is more valuable than a nominal timbre
        // lane. If exact role compatibility is unavailable, relay the gesture to the
        // closest viable instrument in the same department rather than deleting GPT's
        // musical decision.
        if (target == nullptr && hasStructuralAuthorship) {
            auto bestDistance = 10000;
            for (const auto& candidate : pattern.parts) {
                if (candidate.id == part.id || !viableParts.contains(candidate.id) ||
                    candidate.department != part.department) continue;
                const auto distance = std::abs(candidate.minimumPitch - part.minimumPitch) +
                    std::abs(candidate.maximumPitch - part.maximumPitch);
                if (distance < bestDistance) { target = &candidate; bestDistance = distance; }
            }
        }
        if (target != nullptr && hasStructuralAuthorship) {
            for (auto& note : pattern.notes) {
                if (note.partId != part.id) continue;
                note.partId = target->id;
                note.voice = target->sourceVoice;
                note.channel = voiceDefinition(target->sourceVoice).midiChannel;
                note.pitch = nearestPitch(positiveModulo(note.pitch, 12), note.pitch, *target);
            }
            transferPerformanceFor(pattern, part.id, *target);
            ++report.mergedTracks;
        } else {
            pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                [&](const auto& note) { return note.partId == part.id; }), pattern.notes.end());
            ++report.prunedTracks;
        }
        if (target == nullptr || !hasStructuralAuthorship) removePerformanceFor(pattern, part.id);
        if (const auto* assignment = assignmentFor(part, plan))
            removedAssignments.insert(assignment->id);
    }
    plan.soundscape.layers.erase(std::remove_if(plan.soundscape.layers.begin(), plan.soundscape.layers.end(),
        [&](const auto& layer) { return removedAssignments.contains(layer.instrumentId); }),
        plan.soundscape.layers.end());
    const auto final = audit(pattern, plan);
    report.retainedTracks = final.retainedTracks;
    report.viableTracks = final.viableTracks;
    report.tokenTracks = final.tokenTracks;
    report.eventTracks = final.eventTracks;
    report.viabilityRatio = final.viabilityRatio;
    report.retentionRatio = final.retentionRatio;
    report.score = final.score;
    report.ready = final.ready;
    report.issues = final.issues;
    return report;
}

void TrackViability::stamp(Pattern& pattern, const TrackViabilityReport& report) {
    pattern.trackViabilityAudited = report.active;
    pattern.trackViabilityReady = report.ready;
    pattern.trackViabilityScore = report.score;
    pattern.declaredViabilityTracks = report.declaredTracks;
    pattern.retainedViabilityTracks = report.retainedTracks;
    pattern.viableInstrumentTracks = report.viableTracks;
    pattern.tokenInstrumentTracks = report.tokenTracks;
    pattern.developedInstrumentTracks = report.developedTracks;
    pattern.mergedInstrumentTracks = report.mergedTracks;
    pattern.prunedInstrumentTracks = report.prunedTracks;
    for (const auto& issue : report.issues)
        pattern.productionIssues.push_back("track_viability:" + issue);
    pattern.creativeReady = pattern.creativeReady && report.ready;
    pattern.creativeScore = std::clamp(pattern.creativeScore * .75 + report.score * .25, 0.0, 1.0);
}

} // namespace pulso
