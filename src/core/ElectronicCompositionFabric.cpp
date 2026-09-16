#include "ElectronicCompositionFabric.h"

#include "ElectronicRoleContract.h"
#include "HarmonicFloorContext.h"
#include "OrchestrationScore.h"
#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
    return ElectronicRoleContract::motionOwner(part);
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
    // "counterpoint" is a texture/function, not proof that the line is a reply.
    // GPT commonly gives the protagonist that function; treating it as dialogue made
    // the lead-owner set empty and silently disabled the complete narrative pass.
    return part.sourceVoice == VoiceId::Countermelody ||
           part.lineRelationship == "call_response" ||
           contains(part.role, "reply") || contains(part.role, "response") ||
           contains(part.role, "answer");
}

bool aiAuthoredScore(const SongPlan& plan) noexcept {
    return plan.instrumentCastAuthored && !plan.performanceScore.empty();
}

bool thematicHandoff(const InstrumentAssignment& part) noexcept {
    return part.lineRelationship == "relay" || part.lineRelationship == "timbral_handoff";
}

bool sharedDestination(const InstrumentAssignment& part) noexcept {
    return part.lineRelationship == "relay" || part.lineRelationship == "timbral_handoff" ||
           part.lineRelationship == "doubling" ||
           part.lineRelationship == "octave_reinforcement";
}

bool pitchedRendererMember(const InstrumentAssignment& part) noexcept {
    return !isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm) &&
           part.sourceVoice != VoiceId::Transitions &&
           part.orchestralFunction != "transition";
}

std::size_t activeSectionOverlap(const InstrumentAssignment& left,
                                 const InstrumentAssignment& right) {
    if (left.activeSections.empty() || right.activeSections.empty()) return 1;
    return static_cast<std::size_t>(std::count_if(left.activeSections.begin(),
        left.activeSections.end(), [&](const auto& section) {
            return std::find(right.activeSections.begin(), right.activeSections.end(), section) !=
                   right.activeSections.end();
        }));
}

bool activeIn(const InstrumentAssignment& part, const SongSection& section) {
    return part.activeSections.empty() ||
        std::find(part.activeSections.begin(), part.activeSections.end(), section.name) !=
            part.activeSections.end();
}

std::size_t speakerFor(const SongPlan& plan, const std::vector<std::size_t>& leads,
                       const SongSection* section) noexcept {
    if (leads.empty()) return std::numeric_limits<std::size_t>::max();
    const auto active = [&](std::size_t index) {
        return section == nullptr || activeIn(plan.instruments[index], *section);
    };
    const auto primary = std::find_if(leads.begin(), leads.end(), [&](auto index) {
        return plan.instruments[index].id == plan.narrativeSpine.protagonistInstrumentId;
    });
    if (primary != leads.end() && active(*primary)) return *primary;
    if (primary != leads.end()) {
        const auto& owner = plan.instruments[*primary];
        const auto lane = owner.contentLaneId.empty() ? owner.id : owner.contentLaneId;
        const auto handoff = std::find_if(leads.begin(), leads.end(), [&](auto index) {
            const auto& candidate = plan.instruments[index];
            const auto candidateLane = candidate.contentLaneId.empty()
                ? candidate.id : candidate.contentLaneId;
            return active(index) && thematicHandoff(candidate) && candidateLane == lane;
        });
        if (handoff != leads.end()) return *handoff;
    }
    const auto fallback = std::find_if(leads.begin(), leads.end(), active);
    return fallback == leads.end() ? std::numeric_limits<std::size_t>::max() : *fallback;
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

bool addNote(Pattern& pattern, const InstrumentAssignment& assignment, std::uint16_t partId,
             double start, double duration, int pitch, int velocity, std::uint32_t narrativeId,
             ElectronicFabricReport& report, std::size_t& counter,
             std::size_t maximumSoundingParts = 7) {
    if (start < 0.0 || start >= pattern.lengthBeats || duration <= .001) return false;
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
        if (!soundingParts.contains(partId) && soundingParts.size() >= maximumSoundingParts) return false;
    }
    pattern.notes.push_back({start, std::min(duration, pattern.lengthBeats - start), pitch,
        std::clamp(velocity, 1, 127), voiceDefinition(assignment.sourceVoice).midiChannel,
        assignment.sourceVoice, partId, true, NoteOrigin::PlanDerived, narrativeId});
    ++report.notesCreated;
    ++counter;
    return true;
}

const SongSection* sectionAt(const SongPlan& plan, double beat) noexcept {
    const SongSection* result = plan.sections.empty() ? nullptr : &plan.sections.front();
    for (const auto& section : plan.sections) {
        if (beat + .001 < section.startBar * plan.beatsPerBar) break;
        result = &section;
    }
    return result;
}

std::size_t densityBudget(const SongPlan& plan, double beat) noexcept {
    const auto* section = sectionAt(plan, beat);
    const auto density = section == nullptr ? .5 : section->density;
    return static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::lround(5.0 + density * 3.0)), 5, 8));
}

std::uint64_t mixed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

int fitScalePitch(int target, const InstrumentAssignment& assignment,
                  int rootPitchClass, ScaleKind scale) noexcept {
    const auto legal = nearestPitchInScale(target, rootPitchClass, scale);
    return nearestPitch(positiveModulo(legal, 12), legal, assignment);
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

    // Local fallback plans do not pass through the AI manifest reconciler. Give them
    // the same single-conductor/multiple-support semantics without altering a closed
    // AI-authored cast whose explicit decision was to omit recurrence altogether.
    if (!plan.instrumentCastAuthored && ElectronicRoleContract::requiresMotionOwner(plan) &&
        ElectronicRoleContract::motionOwnerCount(plan) != 1) {
        (void) ElectronicRoleContract::electPrimaryMotionOwner(
            plan.instruments, plan.narrativeSpine.protagonistInstrumentId);
    }

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

    // A relay, handoff or doubling is one musical line changing colour, not a new
    // independent idea.  GPT occasionally returned unique lane ids despite declaring
    // one of those relationships, which made one leitmotif look like many unrelated
    // DAW tracks. Canonicalise thematic handoffs onto the protagonist's lane.
    const auto protagonist = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& part) { return part.id == plan.narrativeSpine.protagonistInstrumentId; });
    if (protagonist != plan.instruments.end()) {
        const auto protagonistLane = protagonist->contentLaneId.empty()
            ? protagonist->id : protagonist->contentLaneId;
        protagonist->contentLaneId = protagonistLane;
        for (auto& part : plan.instruments) {
            if (part.sourceVoice != VoiceId::Lead || &part == &*protagonist) continue;
            const auto sharedLine = part.lineRelationship == "relay" ||
                part.lineRelationship == "timbral_handoff" ||
                part.lineRelationship == "doubling" ||
                part.lineRelationship == "octave_reinforcement";
            if (sharedLine) part.contentLaneId = protagonistLane;
        }
    }

    // A large production cast is not the same thing as a large number of musical
    // arguments. Keep a bounded set of pitched content owners and turn the remaining
    // colours into explicit hand-offs. The performance writer can then spend its
    // budget developing complete lines; the renderer rotates those lines through the
    // declared DAW tracks without cloning them or dropping the cast.
    // Performance writing freezes this architecture. normalizePlan is deliberately
    // called again after all blocks are assembled; re-electing owners at that point
    // would promote previously unwritten destinations and invalidate a valid score.
    if (plan.instrumentCastAuthored && plan.performanceScore.empty()) {
        std::vector<std::size_t> pitched;
        for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
            const auto& part = plan.instruments[index];
            if (isVoiceInFamily(part.sourceVoice, VoiceFamily::Rhythm) ||
                part.sourceVoice == VoiceId::Transitions ||
                part.orchestralFunction == "transition") continue;
            pitched.push_back(index);
        }
        if (pitched.size() >= 24) {
            const auto ownerTarget = std::min<std::size_t>(24,
                std::max<std::size_t>(18, static_cast<std::size_t>(
                    std::lround(static_cast<double>(pitched.size()) * .55))));
            std::stable_sort(pitched.begin(), pitched.end(), [&](auto left, auto right) {
                const auto priority = [&](const auto& part) {
                    auto value = part.prominence;
                    if (part.id == plan.narrativeSpine.protagonistInstrumentId) value += 10.0;
                    if (ElectronicRoleContract::motionOwner(part)) value += 6.0;
                    if (floorPart(part)) value += 4.0;
                    if (dialoguePart(part)) value += 3.0;
                    if (part.lineRelationship == "independent") value += 1.0;
                    return value;
                };
                return priority(plan.instruments[left]) > priority(plan.instruments[right]);
            });
            const auto essentialOwner = [&](std::size_t index) {
                const auto& part = plan.instruments[index];
                const auto relationship = part.lineRelationship;
                const auto explicitlyShared = relationship == "relay" ||
                    relationship == "timbral_handoff" || relationship == "doubling" ||
                    relationship == "octave_reinforcement";
                if (explicitlyShared) return false;
                return part.id == plan.narrativeSpine.protagonistInstrumentId ||
                    ElectronicRoleContract::motionOwner(part) || floorPart(part) ||
                    dialoguePart(part) || part.sourceVoice == VoiceId::SubBass ||
                    part.sourceVoice == VoiceId::MovementBass ||
                    part.orchestralFunction == "foundation" ||
                    part.orchestralFunction == "body" ||
                    part.orchestralFunction == "counterpoint" || part.prominence >= .55;
            };
            std::vector<std::size_t> owners;
            for (const auto index : pitched)
                if (essentialOwner(index)) owners.push_back(index);
            for (const auto index : pitched) {
                if (owners.size() >= ownerTarget) break;
                if (std::find(owners.begin(), owners.end(), index) == owners.end())
                    owners.push_back(index);
            }
            for (const auto index : pitched) {
                if (std::find(owners.begin(), owners.end(), index) != owners.end()) {
                    auto& owner = plan.instruments[index];
                    if (owner.lineRelationship != "call_response")
                        owner.lineRelationship = "independent";
                    // Promoting an existing destination makes it a genuine new owner;
                    // it must no longer retain another owner's lane id.
                    owner.contentLaneId = owner.id == plan.narrativeSpine.protagonistInstrumentId &&
                            !owner.contentLaneId.empty()
                        ? owner.contentLaneId : owner.id;
                    continue;
                }
                auto& layer = plan.instruments[index];
                const auto explicitlyShared = layer.lineRelationship == "relay" ||
                    layer.lineRelationship == "timbral_handoff" ||
                    layer.lineRelationship == "doubling" ||
                    layer.lineRelationship == "octave_reinforcement";
                const auto genuineColour = (layer.orchestralFunction == "color" ||
                    layer.sourceVoice == VoiceId::Atmosphere) && layer.prominence < .58;
                if (!explicitlyShared && !genuineColour) {
                    layer.contentLaneId = layer.id;
                    layer.lineRelationship = "independent";
                    continue;
                }
                const auto best = std::max_element(owners.begin(), owners.end(),
                    [&](auto left, auto right) {
                        const auto affinity = [&](auto candidateIndex) {
                            const auto& candidate = plan.instruments[candidateIndex];
                            auto score = candidate.sourceVoice == layer.sourceVoice ? 8.0 : 0.0;
                            if (voiceDefinition(candidate.sourceVoice).family ==
                                voiceDefinition(layer.sourceVoice).family) score += 3.0;
                            if (candidate.orchestralFunction == layer.orchestralFunction) score += 2.0;
                            return score + candidate.prominence;
                        };
                        return affinity(left) < affinity(right);
                    });
                if (best == owners.end()) continue;
                const auto& owner = plan.instruments[*best];
                layer.contentLaneId = owner.contentLaneId.empty() ? owner.id : owner.contentLaneId;
                layer.lineRelationship = "timbral_handoff";
                layer.doubling = 0.0;
            }
        }
    }

    // Every renderer-owned destination must terminate at one concrete independent
    // owner. Repair malformed/unique lane ids locally before performance blocks are
    // selected; this changes orchestration routing only and never authors notes.
    std::vector<std::size_t> canonicalOwners;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& part = plan.instruments[index];
        if (pitchedRendererMember(part) && !sharedDestination(part))
            canonicalOwners.push_back(index);
    }
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        auto& destination = plan.instruments[index];
        if (!pitchedRendererMember(destination) || !sharedDestination(destination)) continue;
        const auto destinationLane = destination.contentLaneId.empty()
            ? destination.id : destination.contentLaneId;
        const auto hasOwner = std::any_of(canonicalOwners.begin(), canonicalOwners.end(),
            [&](auto ownerIndex) {
                const auto& owner = plan.instruments[ownerIndex];
                const auto ownerLane = owner.contentLaneId.empty() ? owner.id : owner.contentLaneId;
                return ownerLane == destinationLane;
            });
        if (hasOwner) continue;
        const auto best = std::max_element(canonicalOwners.begin(), canonicalOwners.end(),
            [&](auto left, auto right) {
                const auto affinity = [&](auto ownerIndex) {
                    const auto& owner = plan.instruments[ownerIndex];
                    auto score = owner.sourceVoice == destination.sourceVoice ? 12.0 : 0.0;
                    if (voiceDefinition(owner.sourceVoice).family ==
                        voiceDefinition(destination.sourceVoice).family) score += 5.0;
                    if (owner.orchestralFunction == destination.orchestralFunction) score += 3.0;
                    score += static_cast<double>(activeSectionOverlap(owner, destination)) * 2.0;
                    return score + owner.prominence;
                };
                return affinity(left) < affinity(right);
            });
        if (best == canonicalOwners.end()) {
            // A cast made entirely from shared labels has no source to render. Promote
            // one actual identity instead of keeping a destination that can never sound.
            destination.lineRelationship = "independent";
            destination.contentLaneId = destination.id;
            canonicalOwners.push_back(index);
            continue;
        }
        const auto& owner = plan.instruments[*best];
        destination.contentLaneId = owner.contentLaneId.empty() ? owner.id : owner.contentLaneId;
    }

    // A closed GPT cast is compositional authority. The local fabric may complete
    // notes inside roles GPT explicitly declared, but it must not invent a generic
    // pad/arp/lead/reply roster behind the model's back.
    if (plan.instrumentCastAuthored) return;

    ensureVoice(plan, VoiceId::HarmonicFoundation, "Continuous multi-layer harmonic floor", .78);
    ensureVoice(plan, VoiceId::Lead, "Primary narrative speaker", .42);
    ensureVoice(plan, VoiceId::Countermelody, "Sparse answer to the primary speaker", .28);

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
    for (std::size_t i = floors.size(); i < 3 && plan.instruments.size() < 64; ++i)
        plan.instruments.push_back(floorDefaults[i]);

    // Local/emergency plans receive a minimum speaker and harmonic floor. Motion is
    // deliberately optional: an arpeggio is never a universal property of electronic
    // music and therefore is not synthesized unless the plan explicitly declares one.
    if (std::none_of(plan.instruments.begin(), plan.instruments.end(), [](const auto& part) {
            return part.sourceVoice == VoiceId::Lead && part.orchestralFunction != "color";
        }) && plan.instruments.size() < 64)
        plan.instruments.push_back(makePart("fabric_primary_speaker", "lead_synth", "Primary Speaker",
            VoiceId::Lead, "Narrative protagonist with statement, answer and return", "counterpoint",
            "legato", "Wavetable", "warm expressive dark mono lead", .46, .68));
    auto dialogues = indicesFor(plan, dialoguePart);
    if (dialogues.size() < 2 && plan.instruments.size() < 64)
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

bool ElectronicCompositionFabric::rendererOwnedDestination(
        const SongPlan& plan, const InstrumentAssignment& destination) noexcept {
    if (!pitchedRendererMember(destination) || !sharedDestination(destination)) return false;
    const auto lane = destination.contentLaneId.empty() ? destination.id : destination.contentLaneId;
    return std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& owner) {
        if (&owner == &destination || !pitchedRendererMember(owner) || sharedDestination(owner))
            return false;
        const auto ownerLane = owner.contentLaneId.empty() ? owner.id : owner.contentLaneId;
        return ownerLane == lane;
    });
}

ThematicOwnershipReport ElectronicCompositionFabric::concentrateThematicOwnership(
    Pattern& pattern, const SongPlan& plan) {
    ThematicOwnershipReport report;
    report.active = electronic(plan) && !pattern.parts.empty();
    if (!report.active) return report;
    const auto populated = [&](std::uint16_t id) {
        return std::any_of(pattern.notes.begin(), pattern.notes.end(),
            [&](const auto& note) { return note.partId == id; });
    };
    const auto foregroundPart = [](const InstrumentPart& part) {
        return part.sourceVoice == VoiceId::Lead || part.sourceVoice == VoiceId::Countermelody;
    };
    for (const auto& part : pattern.parts)
        if (foregroundPart(part) && populated(part.id)) ++report.foregroundTracksBefore;

    const InstrumentPart* primary = nullptr;
    for (const auto& part : pattern.parts) {
        if (part.id == 0 || part.id > plan.instruments.size()) continue;
        if (plan.instruments[part.id - 1].id == plan.narrativeSpine.protagonistInstrumentId) {
            primary = &part;
            break;
        }
    }
    if (primary == nullptr) {
        const auto found = std::max_element(pattern.parts.begin(), pattern.parts.end(),
            [&](const auto& left, const auto& right) {
                const auto leftLead = left.sourceVoice == VoiceId::Lead;
                const auto rightLead = right.sourceVoice == VoiceId::Lead;
                if (leftLead != rightLead) return !leftLead;
                return left.prominence < right.prominence;
            });
        if (found != pattern.parts.end() && found->sourceVoice == VoiceId::Lead) primary = &*found;
    }
    if (primary == nullptr) return report;

    const InstrumentPart* answerer = nullptr;
    for (const auto& part : pattern.parts) {
        if (part.id == primary->id || !foregroundPart(part)) continue;
        if (part.lineRelationship != "call_response") continue;
        if (answerer == nullptr || part.prominence > answerer->prominence) answerer = &part;
    }

    const auto lineage = [&](std::uint16_t id) {
        std::set<std::uint32_t> result;
        for (const auto& note : pattern.notes)
            if (note.partId == id && note.narrativeId != 0) result.insert(note.narrativeId);
        return result;
    };
    const auto primaryLineage = lineage(primary->id);
    const auto sharesLineage = [&](const InstrumentPart& part) {
        const auto candidate = lineage(part.id);
        return std::any_of(candidate.begin(), candidate.end(),
            [&](auto id) { return primaryLineage.contains(id); });
    };
    const auto assignment = [&](const InstrumentPart& part) -> const InstrumentAssignment* {
        return part.id > 0 && part.id <= plan.instruments.size()
            ? &plan.instruments[part.id - 1] : nullptr;
    };
    const auto remap = [&](const InstrumentPart& source, const InstrumentPart& target) {
        const auto* targetAssignment = assignment(target);
        if (targetAssignment == nullptr) return;
        auto moved = false;
        for (auto& note : pattern.notes) {
            if (note.partId != source.id) continue;
            note.partId = target.id;
            note.voice = target.sourceVoice;
            note.channel = voiceDefinition(target.sourceVoice).midiChannel;
            note.pitch = nearestPitch(positiveModulo(note.pitch, 12), note.pitch, *targetAssignment);
            moved = true;
            ++report.notesReassigned;
        }
        for (auto& control : pattern.controls) {
            if (control.partId != source.id) continue;
            control.partId = target.id;
            control.voice = target.sourceVoice;
            control.channel = voiceDefinition(target.sourceVoice).midiChannel;
        }
        for (auto& expression : pattern.expressions) {
            if (expression.partId != source.id) continue;
            expression.partId = target.id;
            expression.voice = target.sourceVoice;
            expression.channel = voiceDefinition(target.sourceVoice).midiChannel;
        }
        if (moved) ++report.consolidatedTracks;
    };

    for (const auto& part : pattern.parts) {
        if (part.id == primary->id || !populated(part.id) || !foregroundPart(part)) continue;
        // A declared answer is allowed to quote a small clue from the protagonist.
        // Preserve the strongest answerer and fold additional answers into it before
        // considering lineage: shared narrative IDs alone must not erase dialogue.
        if (part.lineRelationship == "call_response") {
            if (answerer != nullptr && part.id != answerer->id) remap(part, *answerer);
            continue;
        }
        const auto sharedPrimary = part.lineRelationship == "relay" ||
            part.lineRelationship == "timbral_handoff" ||
            part.lineRelationship == "doubling" ||
            part.lineRelationship == "octave_reinforcement" ||
            (part.sourceVoice == VoiceId::Lead &&
             part.lineRelationship == "independent" && sharesLineage(part));
        if (sharedPrimary) {
            remap(part, *primary);
        }
    }

    for (const auto& part : pattern.parts)
        if (foregroundPart(part) && populated(part.id)) ++report.foregroundTracksAfter;
    return report;
}

ElectronicFabricReport ElectronicCompositionFabric::materialize(Pattern& pattern,
                                                                  const SongPlan& plan) {
    ElectronicFabricReport report;
    report.active = electronic(plan) && !pattern.parts.empty() && !plan.chordPalette.empty();
    if (!report.active) return report;
    const auto partId = [](std::size_t index) { return static_cast<std::uint16_t>(index + 1); };
    auto floors = indicesFor(plan, floorPart);
    const auto arps = indicesFor(plan, arpPart);
    auto dialogues = indicesFor(plan, dialoguePart);
    std::stable_sort(dialogues.begin(), dialogues.end(), [&](auto left, auto right) {
        const auto leftExplicit = plan.instruments[left].lineRelationship == "call_response";
        const auto rightExplicit = plan.instruments[right].lineRelationship == "call_response";
        if (leftExplicit != rightExplicit) return leftExplicit;
        return plan.instruments[left].prominence > plan.instruments[right].prominence;
    });
    std::vector<std::size_t> leads;
    for (std::size_t i = 0; i < plan.instruments.size(); ++i)
        if (plan.instruments[i].sourceVoice == VoiceId::Lead && !dialoguePart(plan.instruments[i]))
            leads.push_back(i);
    std::sort(leads.begin(), leads.end(), [&](auto a, auto b) {
        const auto aChosen = plan.instruments[a].id == plan.narrativeSpine.protagonistInstrumentId;
        const auto bChosen = plan.instruments[b].id == plan.narrativeSpine.protagonistInstrumentId;
        if (aChosen != bChosen) return aChosen;
        return plan.instruments[a].prominence > plan.instruments[b].prominence;
    });

    // The harmonic floor is a group contract, not a permanently held single pad.
    // Independent registers rotate and breathe with the AI-authored sectional arc.
    if (floors.size() > 4) floors.resize(4);
    std::vector<int> previousPitch(floors.size(), 60);
    for (std::size_t i = 0; i < floors.size(); ++i)
        previousPitch[i] = std::clamp((plan.instruments[floors[i]].minimumPitch +
            plan.instruments[floors[i]].maximumPitch) / 2, 36, 84);
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto beat = bar * plan.beatsPerBar;
        const auto& chord = chordAt(plan, beat);
        if (chord.pitchClasses.empty()) continue;
        const auto* section = sectionAt(plan, beat);
        const auto sectionIndex = section == nullptr ? std::size_t{} :
            static_cast<std::size_t>(std::distance(plan.sections.data(), section));
        const auto rotation = static_cast<std::size_t>((bar / 4 + sectionIndex) %
            std::max<std::size_t>(1, floors.size()));
        const auto phraseBar = bar % 16;
        const auto breath = phraseBar == 15 ||
            (section != nullptr && section->energy < .30 && phraseBar == 7);
        const auto arrival = section != nullptr && section->energy >= .68 &&
            (phraseBar == 0 || phraseBar == 12);
        const auto desired = std::min<std::size_t>(floors.size(), breath ? 1U : arrival ? 3U : 2U);
        std::set<std::size_t> alreadyActive;
        for (const auto index : floors)
            if (overlapsPart(pattern, partId(index), beat, beat + plan.beatsPerBar * .75))
                alreadyActive.insert(index);
        // GPT owns complete floor writing. Local material only closes an objective gap;
        // it does not add another chord layer merely because a preferred pad is resting.
        auto activeCount = alreadyActive.size();
        for (std::size_t attempt = 0; attempt < floors.size() && activeCount < desired; ++attempt) {
            const auto floorOrdinal = (rotation + attempt) % floors.size();
            const auto index = floors[floorOrdinal];
            const auto slot = activeCount;
            const auto& assignment = plan.instruments[index];
            if (alreadyActive.contains(index)) continue;
            const auto tone = chord.pitchClasses[(floorOrdinal + static_cast<std::size_t>(bar / 4)) %
                                                  chord.pitchClasses.size()];
            const auto pitch = nearestPitch(tone, previousPitch[floorOrdinal], assignment);
            const auto held = slot == 0 || phraseBar % 4 != 2;
            const auto duration = held ? plan.beatsPerBar - 1.0 / 32.0 :
                plan.beatsPerBar * .5 - 1.0 / 32.0;
            addNote(pattern, assignment, partId(index), beat, duration,
                pitch, 44 + static_cast<int>(slot) * 6 + (arrival ? 8 : 0),
                0x464c4f52u + static_cast<std::uint32_t>(floorOrdinal), report,
                report.foundationNotesCreated, densityBudget(plan, beat));
            previousPitch[floorOrdinal] = pitch;
            alreadyActive.insert(index);
            ++activeCount;
        }
    }

    // A recognisable electronic motion line: repeated cells, sectional omissions,
    // directional changes and a full-bar breath at the end of every phrase.
    if (!arps.empty() && !aiAuthoredScore(plan)) {
        for (auto block = 0; block * 8 < plan.totalBars; ++block) {
            const auto blockBar = block * 8;
            const auto blockStart = blockBar * plan.beatsPerBar;
            const auto* section = sectionAt(plan, blockStart);
            std::vector<std::size_t> eligible;
            for (const auto index : arps)
                if (section == nullptr || activeIn(plan.instruments[index], *section)) eligible.push_back(index);
            if (eligible.empty() || block == 0 || block % 4 == 3) continue;
            const auto groupNotes = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.startBeat >= blockStart && note.startBeat < blockStart + 8 * plan.beatsPerBar &&
                    std::any_of(eligible.begin(), eligible.end(), [&](const auto candidate) {
                        return note.partId == partId(candidate);
                    });
            });
            // A developed AI sequence is authoritative. Only materially empty blocks
            // receive a derived motion line.
            if (groupNotes >= 6) continue;
            const auto startOffset = static_cast<std::size_t>(mixed(plan.seed ^
                static_cast<std::uint64_t>(block)) % eligible.size());
            auto index = eligible[startOffset];
            for (std::size_t offset = 0; offset < eligible.size(); ++offset) {
                const auto candidate = eligible[(startOffset + offset) % eligible.size()];
                if (!overlapsPart(pattern, partId(candidate), blockStart,
                                  blockStart + 8 * plan.beatsPerBar)) {
                    index = candidate;
                    break;
                }
            }
            const auto& assignment = plan.instruments[index];
            if (overlapsPart(pattern, partId(index), blockStart,
                             blockStart + 8 * plan.beatsPerBar)) continue;
            const auto variant = mixed(plan.seed ^ (static_cast<std::uint64_t>(block) << 17U));
            const auto activeBars = 4 + static_cast<int>(variant % 4U);
            const auto stepsPerBar = std::max(1, static_cast<int>(std::lround(plan.beatsPerBar * 2.0)));
            auto ordinal = 0;
            for (auto bar = 0; bar < activeBars && blockBar + bar < plan.totalBars; ++bar) {
                for (auto step = 0; step < stepsPerBar; ++step, ++ordinal) {
                    const auto restCycle = 7 + static_cast<int>((variant >> 8U) % 6U);
                    if ((ordinal + block) % restCycle == static_cast<int>((variant >> 16U) % restCycle) ||
                        (step + 1 == stepsPerBar && (bar + block) % 2 == 1)) continue;
                    const auto beat = (blockBar + bar) * plan.beatsPerBar + step * .5;
                    const auto& chord = chordAt(plan, beat);
                    if (chord.pitchClasses.empty()) continue;
                    const auto ascending = ((variant >> 24U) + static_cast<std::uint64_t>(bar)) % 2U == 0;
                    const auto motifShift = plan.motifIntervals.empty() ? 0 :
                        std::abs(plan.motifIntervals[static_cast<std::size_t>(ordinal) % plan.motifIntervals.size()]);
                    const auto arpStep = step + motifShift;
                    const auto position = ascending ? arpStep % static_cast<int>(chord.pitchClasses.size()) :
                        static_cast<int>(chord.pitchClasses.size()) - 1 -
                            arpStep % static_cast<int>(chord.pitchClasses.size());
                    const auto target = 60 + (((variant >> 32U) + static_cast<std::uint64_t>(bar / 2)) % 3U == 2U ? 12 : 0);
                    const auto pitch = nearestPitch(chord.pitchClasses[static_cast<std::size_t>(position)],
                        target, assignment);
                    addNote(pattern, assignment, partId(index), beat, .32, pitch,
                        46 + (step % 4 == 0 ? 18 : step % 2 == 0 ? 7 : 0),
                        0x41525000u + static_cast<std::uint32_t>(block), report,
                        report.arpeggioNotesCreated, densityBudget(plan, beat));
                }
            }
        }
    }

    // One protagonist owns the story at a time. Its statement, transformation and
    // return are derived from the AI motif, but rhythm and contour change per phrase.
    std::vector<std::pair<double, std::vector<int>>> createdPhrases;
    if (!leads.empty() && !aiAuthoredScore(plan)) {
        struct PhraseWindow { double start{}; NarrativeStage stage{NarrativeStage::Transformation}; };
        std::vector<PhraseWindow> phraseWindows;
        std::set<NarrativeStage> scheduledStages;
        if (plan.narrativeSpine.authored) {
            for (const auto& act : plan.narrativeSpine.acts) {
                if (act.stage == NarrativeStage::Departure || act.stage == NarrativeStage::Aftermath) continue;
                // One structural phrase per dramatic job. Repeated section labels may
                // describe many supporting scenes, but must not turn the protagonist into
                // a continuous monologue.
                if (!scheduledStages.insert(act.stage).second) continue;
                const auto section = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& candidate) {
                    return candidate.name == act.sectionName;
                });
                if (section == plan.sections.end()) continue;
                const auto localBar = std::clamp(section->bars / 3, 0, std::max(0, section->bars - 4));
                phraseWindows.push_back({(section->startBar + localBar) * plan.beatsPerBar, act.stage});
            }
        } else {
            const auto count = std::max<std::size_t>(3, static_cast<std::size_t>(plan.totalBars / 24));
            for (std::size_t index = 0; index < count; ++index)
                phraseWindows.push_back({std::floor((static_cast<double>(index) + .55) *
                    plan.totalBars / static_cast<double>(count)) * plan.beatsPerBar,
                    index + 1 == count ? NarrativeStage::Resolution : NarrativeStage::Transformation});
        }
        if (phraseWindows.size() < 3) {
            const auto count = std::max<std::size_t>(3, static_cast<std::size_t>(plan.totalBars / 24));
            for (std::size_t index = phraseWindows.size(); index < count; ++index)
                phraseWindows.push_back({std::floor((static_cast<double>(index) + .55) *
                    plan.totalBars / static_cast<double>(count) / 4.0) * 4.0 * plan.beatsPerBar,
                    index + 1 == count ? NarrativeStage::Resolution : NarrativeStage::Transformation});
        }
        std::sort(phraseWindows.begin(), phraseWindows.end(), [](const auto& a, const auto& b) {
            return a.start < b.start;
        });
        const auto targetWindows = phraseWindows.size();
        std::set<int> scheduledWindows;
        for (std::size_t phraseIndex = 0; phraseIndex < targetWindows; ++phraseIndex) {
            const auto stage = phraseWindows[phraseIndex].stage;
            auto bar = static_cast<int>(std::floor(phraseWindows[phraseIndex].start / plan.beatsPerBar));
            bar = std::clamp((bar / 4) * 4, 4, std::max(4, plan.totalBars - 4));
            while (scheduledWindows.contains(bar) && bar + 4 < plan.totalBars) bar += 4;
            if (!scheduledWindows.insert(bar).second) continue;
            const auto start = bar * plan.beatsPerBar;
            const auto end = std::min(pattern.lengthBeats, start + 4 * plan.beatsPerBar);
            const auto existing = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.voice == VoiceId::Lead && note.startBeat >= start && note.startBeat < end;
            });
            if (existing >= 3) continue;
            const auto* section = sectionAt(plan, start);
            const auto index = speakerFor(plan, leads, section);
            if (index == std::numeric_limits<std::size_t>::max()) continue;
            const auto& assignment = plan.instruments[index];
            const auto variant = mixed(plan.seed ^ (static_cast<std::uint64_t>(phraseIndex) << 21U));
            const auto noteCount = static_cast<std::size_t>(5U + variant % 4U);
            std::vector<int> pitches;
            pitches.reserve(noteCount);
            auto onset = 0.0;
            auto previous = fitScalePitch(64, assignment, plan.rootPitchClass, plan.scale);
            const auto returnPhrase = stage == NarrativeStage::Resolution;
            const auto climaxPhrase = stage == NarrativeStage::Climax;
            const auto questionPhrase = stage == NarrativeStage::Question;
            for (std::size_t noteIndex = 0; noteIndex < noteCount; ++noteIndex) {
                if (noteIndex > 0) {
                    constexpr std::array<double, 7> gaps{.5, .75, 1.0, 1.25, 1.5, 2.0, 2.5};
                    onset += gaps[static_cast<std::size_t>((variant >> ((noteIndex % 7) * 7U)) % gaps.size())];
                }
                if (start + onset >= end - .5) break;
                const auto beat = start + onset;
                const auto motifIndex = returnPhrase ? noteIndex :
                    (phraseIndex % 3 == 1 ? noteCount - 1 - noteIndex : noteIndex);
                const auto motif = plan.motifIntervals.empty() ? static_cast<int>(motifIndex % 4) * 2 :
                    plan.motifIntervals[motifIndex % plan.motifIntervals.size()];
                const auto transformation = returnPhrase ? 0 : phraseIndex % 3 == 1 ? -motif * 2 :
                    phraseIndex % 3 == 2 ? motif + (noteIndex >= noteCount / 2 ? 2 : 0) : motif;
                auto pitch = fitScalePitch(64 + transformation, assignment,
                                           plan.rootPitchClass, plan.scale);
                if (climaxPhrase) pitch = fitScalePitch(pitch + 12, assignment,
                    plan.rootPitchClass, plan.scale);
                while (pitch - previous > 7 && pitch - 12 >= assignment.minimumPitch) pitch -= 12;
                while (previous - pitch > 7 && pitch + 12 <= assignment.maximumPitch) pitch += 12;
                const auto& chord = chordAt(plan, beat);
                if (noteIndex == 0 && !chord.pitchClasses.empty())
                    pitch = nearestPitch(chord.pitchClasses.front(), pitch, assignment);
                if (noteIndex + 1 == noteCount) {
                    if (returnPhrase)
                        pitch = nearestPitch(plan.rootPitchClass, 60, assignment);
                    else if (questionPhrase)
                        pitch = nearestPitch(positiveModulo(plan.rootPitchClass + 2, 12), pitch, assignment);
                }
                const auto remaining = end - beat;
                const auto duration = std::min(remaining - .05,
                    noteIndex + 1 == noteCount ? (returnPhrase ? 2.5 : questionPhrase ? .55 : 1.35) :
                    std::clamp(.28 + static_cast<double>((variant >> (noteIndex * 3U)) & 3U) * .18,
                               .28, .82));
                const auto written = addNote(pattern, assignment, partId(index), beat, duration, pitch,
                    62 + static_cast<int>((section == nullptr ? .5 : section->energy) * 20) +
                        (noteIndex == 0 ? 8 : 0) + (climaxPhrase ? 10 : 0) - (returnPhrase ? 6 : 0),
                    // An AI-authored spine declares one audible lineage. Legacy/local
                    // plans keep independent ids because their generated gestures were
                    // never declared as literal thematic returns.
                    plan.narrativeSpine.authored ? 0x4c454144u :
                        0x4c450000u + static_cast<std::uint32_t>(phraseIndex), report,
                    report.protagonistNotesCreated, densityBudget(plan, beat) + 1);
                if (written) {
                    pitches.push_back(pitch);
                    previous = pitch;
                }
            }
            if (pitches.size() >= 3) createdPhrases.emplace_back(start, std::move(pitches));
        }
    }

    // Replies are derived from the protagonist but occupy their own lanes and rests.
    for (std::size_t phraseIndex = 0; phraseIndex < createdPhrases.size() && !dialogues.empty(); ++phraseIndex) {
        // One answerer owns the motif-derived reply. Other melodic instruments retain
        // only their independently authored material instead of receiving another copy
        // of the same leitmotif from the local fabric.
        const auto dialogueIndex = dialogues.front();
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
                report.dialogueNotesCreated, densityBudget(plan, responseStart + n * .75));
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
        // In an AI score, a named colour, counterpoint or transition must arrive with
        // its actual GPT performance. Quietly developing dozens of nominal tracks here
        // made the renderer—not the model—the effective composer. Only the harmonic
        // floor above is technical continuity; incomplete authored colours are left to
        // validation/compaction.
        if (aiAuthoredScore(plan)) continue;
        const auto minimumBars = assignment.sourceVoice == VoiceId::Atmosphere ? 8U : 6U;
        if (existing.size() >= 8 && activeBarsFor(existing, plan.beatsPerBar) >= minimumBars) continue;
        const auto spacingBars = assignment.orchestralFunction == "transition" ? 24 :
            assignment.orchestralFunction == "extension" || assignment.sourceVoice == VoiceId::Atmosphere ? 16 : 8;
        auto previous = std::clamp((assignment.minimumPitch + assignment.maximumPitch) / 2,
                                   assignment.minimumPitch, assignment.maximumPitch);
        for (auto bar = static_cast<int>(index % 7) + 4; bar < plan.totalBars; bar += spacingBars) {
            const auto beat = bar * plan.beatsPerBar;
            const auto* section = sectionAt(plan, beat);
            if (section != nullptr && !activeIn(assignment, *section)) continue;
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
                report.supportNotesCreated, densityBudget(plan, beat));
        }
    }

    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        if (a.partId != b.partId) return a.partId < b.partId;
        return a.pitch < b.pitch;
    });

    const auto audited = audit(pattern, plan);
    report.independentLines = audited.independentLines;
    report.meaningfulLines = audited.meaningfulLines;
    report.protagonistPhraseWindows = audited.protagonistPhraseWindows;
    report.arpeggioNoteCount = audited.arpeggioNoteCount;
    report.dialogueLines = audited.dialogueLines;
    report.harmonicFloorCoverage = audited.harmonicFloorCoverage;
    report.medianHarmonicFloorLayers = audited.medianHarmonicFloorLayers;
    report.ready = audited.ready;
    return report;
}

TimbralHandoffReport ElectronicCompositionFabric::realizeTimbralHandoffs(
    Pattern& pattern, const SongPlan& plan) {
    TimbralHandoffReport report;
    report.active = electronic(plan) && plan.instrumentCastAuthored && !pattern.parts.empty();
    if (!report.active) return report;

    std::map<std::string, std::vector<std::size_t>> membersByLane;
    for (std::size_t index = 0; index < plan.instruments.size() && index < pattern.parts.size(); ++index) {
        const auto& assignment = plan.instruments[index];
        if (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Rhythm) ||
            assignment.sourceVoice == VoiceId::Transitions) continue;
        const auto lane = assignment.contentLaneId.empty() ? assignment.id : assignment.contentLaneId;
        membersByLane[lane].push_back(index);
    }
    report.contentLanes = membersByLane.size();
    const auto phraseBeats = std::max(plan.beatsPerBar * 2.0, 4.0);
    for (const auto& [lane, members] : membersByLane) {
        (void) lane;
        if (members.size() < 2) continue;
        report.timbralDestinations += members.size() - 1;
        std::map<int, std::vector<std::size_t>> notesByWindow;
        for (std::size_t noteIndex = 0; noteIndex < pattern.notes.size(); ++noteIndex) {
            const auto& note = pattern.notes[noteIndex];
            if (note.partId == 0) continue;
            const auto assignmentIndex = static_cast<std::size_t>(note.partId - 1);
            if (std::find(members.begin(), members.end(), assignmentIndex) == members.end()) continue;
            notesByWindow[static_cast<int>(std::floor(note.startBeat / phraseBeats))]
                .push_back(noteIndex);
        }
        std::map<std::size_t, std::size_t> assignedWindows;
        for (const auto& [window, noteIndices] : notesByWindow) {
            const auto beat = window * phraseBeats;
            const auto* section = sectionAt(plan, beat);
            std::vector<std::size_t> eligible;
            for (const auto index : members)
                if (section == nullptr || activeIn(plan.instruments[index], *section))
                    eligible.push_back(index);
            if (eligible.empty()) eligible = members;
            // Give every compatible timbral destination a phrase before returning to
            // an already-used colour. A plain modulo over a changing eligible set can
            // starve one destination for the entire arrangement.
            const auto destination = *std::min_element(eligible.begin(), eligible.end(),
                [&](auto left, auto right) {
                    if (assignedWindows[left] != assignedWindows[right])
                        return assignedWindows[left] < assignedWindows[right];
                    return left < right;
                });
            ++assignedWindows[destination];
            const auto& assignment = plan.instruments[destination];
            const auto partId = static_cast<std::uint16_t>(destination + 1);
            auto changed = false;
            for (const auto noteIndex : noteIndices) {
                auto& note = pattern.notes[noteIndex];
                if (note.partId == partId) continue;
                note.partId = partId;
                note.voice = assignment.sourceVoice;
                note.channel = voiceDefinition(assignment.sourceVoice).midiChannel;
                note.pitch = nearestPitch(positiveModulo(note.pitch, 12), note.pitch, assignment);
                ++report.notesReassigned;
                changed = true;
            }
            if (changed) ++report.phraseWindowsReassigned;
        }
    }
    std::set<std::uint16_t> populated;
    for (const auto& note : pattern.notes)
        if (note.partId > 0) populated.insert(note.partId);
    report.populatedDestinations = populated.size();
    report.exactCast = populated.size() == plan.instruments.size();
    return report;
}

ElectronicFabricReport ElectronicCompositionFabric::convergePublication(
    Pattern& pattern, const SongPlan& plan) {
    ElectronicFabricReport report;
    report.active = electronic(plan) && !pattern.parts.empty() && !plan.chordPalette.empty();
    if (!report.active) return report;
    const auto partId = [](std::size_t index) { return static_cast<std::uint16_t>(index + 1); };
    const auto retained = [&](std::size_t index) {
        return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.partId == partId(index);
        });
    };

    auto floors = indicesFor(plan, floorPart);
    floors.erase(std::remove_if(floors.begin(), floors.end(), [&](auto index) {
        return !retained(index);
    }), floors.end());
    if (floors.size() > 4) floors.resize(4);

    // Keep deliberate end-of-phrase breaths, but protect enough two-layer bars that
    // a long electronic work has an audible harmonic ground instead of isolated pads.
    // We close to 90% so later collision/duration repair can spend a small margin while
    // the published contract remains at or above 85%.
    if (floors.size() >= 2) {
        const auto layersAt = [&](int bar) {
            auto count = std::size_t{};
            const auto start = bar * plan.beatsPerBar;
            for (const auto index : floors)
                if (overlapsPart(pattern, partId(index), start, start + plan.beatsPerBar)) ++count;
            return count;
        };
        auto covered = std::size_t{};
        for (auto bar = 0; bar < plan.totalBars; ++bar)
            if (layersAt(bar) >= 2) ++covered;
        const auto target = static_cast<std::size_t>(std::ceil(plan.totalBars * .90));
        std::vector<int> candidates;
        for (auto preserveBreaths : {true, false}) {
            for (auto bar = 0; bar < plan.totalBars; ++bar) {
                const auto phraseBreath = bar % 16 == 15;
                if (preserveBreaths == phraseBreath || layersAt(bar) >= 2) continue;
                candidates.push_back(bar);
            }
        }
        for (const auto bar : candidates) {
            if (covered >= target) break;
            const auto before = layersAt(bar);
            if (before >= 2) continue;
            const auto beat = bar * plan.beatsPerBar;
            const auto& chord = chordAt(plan, beat);
            if (chord.pitchClasses.empty()) continue;
            auto active = before;
            for (std::size_t ordinal = 0; ordinal < floors.size() && active < 2; ++ordinal) {
                const auto index = floors[(static_cast<std::size_t>(bar) + ordinal) % floors.size()];
                if (overlapsPart(pattern, partId(index), beat, beat + plan.beatsPerBar)) continue;
                const auto& assignment = plan.instruments[index];
                const auto tone = chord.pitchClasses[(ordinal + static_cast<std::size_t>(bar / 4)) %
                                                      chord.pitchClasses.size()];
                const auto pitch = nearestPitch(tone,
                    (assignment.minimumPitch + assignment.maximumPitch) / 2, assignment);
                if (addNote(pattern, assignment, partId(index), beat,
                            plan.beatsPerBar - 1.0 / 32.0, pitch,
                            40 + static_cast<int>(ordinal) * 5,
                            0x46434c4fu + static_cast<std::uint32_t>(bar), report,
                            report.publicationClosureNotesCreated, densityBudget(plan, beat) + 2))
                    ++active;
            }
            if (before < 2 && layersAt(bar) >= 2) {
                ++covered;
                ++report.harmonicFloorBarsRepaired;
            }
        }
    }

    std::vector<std::size_t> leads;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index)
        if (plan.instruments[index].sourceVoice == VoiceId::Lead &&
            !dialoguePart(plan.instruments[index]) && retained(index)) leads.push_back(index);
    std::sort(leads.begin(), leads.end(), [&](auto left, auto right) {
        const auto leftChosen = plan.instruments[left].id == plan.narrativeSpine.protagonistInstrumentId;
        const auto rightChosen = plan.instruments[right].id == plan.narrativeSpine.protagonistInstrumentId;
        if (leftChosen != rightChosen) return leftChosen;
        return plan.instruments[left].prominence > plan.instruments[right].prominence;
    });

    // Presence is measured in eight-bar phrase windows, not raw sounding duration.
    // This preserves silence inside a sentence while ensuring the narrator returns
    // often enough to carry a long-form story.
    if (!leads.empty() && !aiAuthoredScore(plan)) {
        std::vector<int> eligible;
        std::vector<int> missing;
        auto present = std::size_t{};
        for (auto bar = 0; bar < plan.totalBars; bar += 8) {
            const auto beat = bar * plan.beatsPerBar;
            const auto* section = sectionAt(plan, beat);
            const auto active = std::any_of(leads.begin(), leads.end(), [&](auto index) {
                return section == nullptr || activeIn(plan.instruments[index], *section);
            });
            if (!active) continue;
            eligible.push_back(bar);
            const auto notes = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.voice == VoiceId::Lead && note.startBeat >= beat &&
                    note.startBeat < beat + plan.beatsPerBar * 8.0 &&
                    (note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed ||
                     note.origin == NoteOrigin::PlanDerived);
            });
            if (notes >= 3) ++present;
            else missing.push_back(bar);
        }
        const auto target = static_cast<std::size_t>(std::ceil(eligible.size() * .65));
        for (const auto bar : missing) {
            if (present >= target) break;
            const auto* section = sectionAt(plan, bar * plan.beatsPerBar);
            const auto index = speakerFor(plan, leads, section);
            if (index == std::numeric_limits<std::size_t>::max()) continue;
            const auto& assignment = plan.instruments[index];
            const auto start = bar * plan.beatsPerBar + plan.beatsPerBar * .5;
            const auto variant = static_cast<int>(mixed(plan.seed ^ static_cast<std::uint64_t>(bar)) % 4U);
            constexpr std::array<double, 5> onsets{0.0, .75, 1.75, 3.0, 5.5};
            auto written = std::size_t{};
            for (std::size_t note = 0; note < onsets.size(); ++note) {
                const auto motif = plan.motifIntervals.empty() ? static_cast<int>(note) * 2 :
                    plan.motifIntervals[note % plan.motifIntervals.size()];
                // Preserve the recognisable contour/onset skeleton while changing
                // harmonic degree and register. Phrase coverage must reinforce memory,
                // not create a new unrelated melody in every empty window.
                const auto transformed = motif + (variant - 1) * 2;
                auto pitch = fitScalePitch(64 + transformed, assignment,
                                           plan.rootPitchClass, plan.scale);
                const auto& chord = chordAt(plan, start + onsets[note]);
                if (note == 0 && !chord.pitchClasses.empty())
                    pitch = nearestPitch(chord.pitchClasses.front(), pitch, assignment);
                if (addNote(pattern, assignment, partId(index), start + onsets[note],
                            note + 1 == onsets.size() ? 1.25 : .42, pitch,
                            58 + (note == 0 ? 9 : 0) + variant * 3,
                            0x4c500000u + static_cast<std::uint32_t>(bar / 8),
                            report, report.publicationClosureNotesCreated,
                            densityBudget(plan, start + onsets[note]) + 1)) ++written;
            }
            if (written >= 3) {
                ++present;
                ++report.protagonistWindowsRepaired;
            }
        }
    }

    // The final resolution act receives a quiet, explicit tonic arrival. Remove only
    // its last foreground/floor fragment, retain the preceding argument, and rewrite
    // the coda from the declared motif and tonic so the harmonic debt is audibly paid.
    if (!leads.empty() && !floors.empty()) {
        const SongSection* resolution = plan.sections.empty() ? nullptr : &plan.sections.back();
        for (const auto& act : plan.narrativeSpine.acts) {
            if (act.stage != NarrativeStage::Resolution) continue;
            const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
                return section.name == act.sectionName;
            });
            if (found != plan.sections.end()) resolution = &*found;
        }
        if (resolution != nullptr && resolution->bars >= 2) {
            const auto end = std::min(pattern.lengthBeats,
                (resolution->startBar + resolution->bars) * plan.beatsPerBar);
            const auto codaStart = end - plan.beatsPerBar * 2.0;
            const auto finalBar = end - plan.beatsPerBar;
            std::set<std::uint16_t> floorIds;
            for (const auto index : floors) floorIds.insert(partId(index));
            pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.startBeat >= finalBar && note.startBeat < end &&
                    (note.voice == VoiceId::HarmonicPulse || note.voice == VoiceId::Atmosphere);
            }), pattern.notes.end());
            for (auto& note : pattern.notes) {
                if (!floorIds.contains(note.partId) || note.startBeat >= finalBar ||
                    note.endBeat() <= finalBar) continue;
                note.durationBeats = std::max(.04, finalBar - note.startBeat - 1.0 / 32.0);
            }
            for (auto& note : pattern.notes) {
                if (!floorIds.contains(note.partId) || note.startBeat < codaStart ||
                    note.startBeat >= finalBar || note.partId == 0 ||
                    note.partId > plan.instruments.size()) continue;
                const auto& owner = plan.instruments[note.partId - 1];
                const auto& chord = chordAt(plan, note.startBeat);
                if (!chord.pitchClasses.empty())
                    note.pitch = nearestPitch(chord.pitchClasses.front(), note.pitch, owner);
                note.origin = NoteOrigin::PlanDerived;
                note.narrativeId = 0x434f4441u;
            }
            pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return floorIds.contains(note.partId) && note.startBeat >= finalBar && note.startBeat < end;
            }), pattern.notes.end());

            auto lastForeground = pattern.notes.end();
            for (auto it = pattern.notes.begin(); it != pattern.notes.end(); ++it) {
                if ((it->voice != VoiceId::Lead && it->voice != VoiceId::Countermelody) ||
                    it->startBeat < resolution->startBar * plan.beatsPerBar || it->startBeat >= end)
                    continue;
                if (lastForeground == pattern.notes.end() || it->startBeat > lastForeground->startBeat)
                    lastForeground = it;
            }
            if (!aiAuthoredScore(plan) && lastForeground != pattern.notes.end() && lastForeground->partId > 0 &&
                lastForeground->partId <= plan.instruments.size()) {
                const auto ownerIndex = static_cast<std::size_t>(lastForeground->partId - 1);
                const auto ownerPartId = lastForeground->partId;
                const auto lastStart = lastForeground->startBeat;
                const auto& owner = plan.instruments[ownerIndex];
                lastForeground->pitch = nearestPitch(plan.rootPitchClass, 60, owner);
                lastForeground->durationBeats = std::max(lastForeground->durationBeats,
                    std::min(2.5, end - lastForeground->startBeat - 1.0 / 32.0));
                if (lastForeground->origin == NoteOrigin::Procedural ||
                    lastForeground->origin == NoteOrigin::LocalContinuity ||
                    lastForeground->origin == NoteOrigin::LocalRepair)
                    lastForeground->origin = NoteOrigin::PlanDerived;
                ++report.resolutionCodaNotesCreated;
                // If GPT supplied only an isolated final note, precede it with a short
                // motif-derived preparation. This makes the tonic a consequence rather
                // than a patched pitch while retaining the original phrase skeleton.
                constexpr std::array<double, 3> preparationOnsets{0.0, 1.5, 3.0};
                for (std::size_t note = 0; note < preparationOnsets.size(); ++note) {
                    const auto beat = codaStart + preparationOnsets[note];
                    if (beat >= lastStart - .25) break;
                    const auto motif = plan.motifIntervals.empty() ? static_cast<int>(note + 1) * 2 :
                        plan.motifIntervals[(note + 1) % plan.motifIntervals.size()];
                    if (addNote(pattern, owner, ownerPartId, beat, .5,
                                nearestPitch(positiveModulo(plan.rootPitchClass + motif, 12), 63, owner),
                                58 - static_cast<int>(note) * 3, 0x434f4441u, report,
                                report.publicationClosureNotesCreated, 8))
                        ++report.resolutionCodaNotesCreated;
                }
            } else if (!aiAuthoredScore(plan)) {
                const auto leadIndex = leads.front();
                const auto& lead = plan.instruments[leadIndex];
                constexpr std::array<double, 4> codaOnsets{0.0, 1.0, 2.5, 4.0};
                for (std::size_t note = 0; note < codaOnsets.size(); ++note) {
                    const auto motif = plan.motifIntervals.empty() ? static_cast<int>(note) * 2 :
                        plan.motifIntervals[note % plan.motifIntervals.size()];
                    const auto targetPitchClass = note + 1 == codaOnsets.size() ? plan.rootPitchClass :
                        positiveModulo(plan.rootPitchClass + motif, 12);
                    const auto pitch = nearestPitch(targetPitchClass, 60 + (note < 2 ? 3 : 0), lead);
                    if (addNote(pattern, lead, partId(leadIndex), codaStart + codaOnsets[note],
                                note + 1 == codaOnsets.size() ? end - (codaStart + codaOnsets[note]) -
                                    1.0 / 32.0 : .55,
                                pitch, 60 - static_cast<int>(note) * 4, 0x434f4441u, report,
                                report.publicationClosureNotesCreated, 8))
                        ++report.resolutionCodaNotesCreated;
                }
            }
            for (std::size_t layer = 0; layer < std::min<std::size_t>(2, floors.size()); ++layer) {
                const auto index = floors[layer];
                const auto& assignment = plan.instruments[index];
                const auto pitchClass = layer == 0 ? plan.rootPitchClass :
                    positiveModulo(plan.rootPitchClass + 7, 12);
                if (addNote(pattern, assignment, partId(index), finalBar,
                            plan.beatsPerBar - 1.0 / 32.0,
                            nearestPitch(pitchClass,
                                (assignment.minimumPitch + assignment.maximumPitch) / 2, assignment),
                            42 + static_cast<int>(layer) * 4,
                            0x434f4441u, report, report.publicationClosureNotesCreated, 8))
                    ++report.resolutionCodaNotesCreated;
            }
        }
    }

    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    const auto audited = audit(pattern, plan);
    report.independentLines = audited.independentLines;
    report.meaningfulLines = audited.meaningfulLines;
    report.protagonistPhraseWindows = audited.protagonistPhraseWindows;
    report.arpeggioNoteCount = audited.arpeggioNoteCount;
    report.dialogueLines = audited.dialogueLines;
    report.harmonicFloorCoverage = audited.harmonicFloorCoverage;
    report.medianHarmonicFloorLayers = audited.medianHarmonicFloorLayers;
    report.ready = audited.ready;
    return report;
}

ElectronicFabricReport ElectronicCompositionFabric::audit(const Pattern& pattern,
                                                           const SongPlan& plan) {
    ElectronicFabricReport report;
    report.active = electronic(plan) && !pattern.parts.empty() && !plan.chordPalette.empty();
    if (!report.active) return report;
    const auto partId = [](std::size_t index) { return static_cast<std::uint16_t>(index + 1); };
    auto floors = indicesFor(plan, floorPart);
    if (floors.size() > 4) floors.resize(4);
    const auto arps = indicesFor(plan, arpPart);
    const auto dialogues = indicesFor(plan, dialoguePart);
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
        auto covered = std::size_t{};
        for (std::size_t bar = 0; bar < floorLayers.size(); ++bar)
            if (floorLayers[bar] >= HarmonicFloorContext::requiredLayers(
                    plan, static_cast<double>(bar) * plan.beatsPerBar)) ++covered;
        report.harmonicFloorCoverage = static_cast<double>(covered) /
            static_cast<double>(floorLayers.size());
        auto copy = floorLayers;
        const auto middle = copy.begin() + static_cast<std::ptrdiff_t>(copy.size() / 2);
        std::nth_element(copy.begin(), middle, copy.end());
        report.medianHarmonicFloorLayers = static_cast<double>(*middle);
    }
    // Electronic motion is evaluated only when the authored cast asks for it. The
    // owner may be an arpeggio, sequence, orbit or ostinato; a transition or repeated
    // bass can never satisfy this contract by accident.
    const auto requiredArpeggioNotes = arps.empty()
        ? std::size_t{} : std::min<std::size_t>(32, std::max(8, plan.totalBars / 4));
    const auto requiredDialogueLines = dialogues.empty() ? std::size_t{} : std::size_t{1};
    report.ready = report.harmonicFloorCoverage >= .85 && report.medianHarmonicFloorLayers >= 2.0 &&
        report.protagonistPhraseWindows >= std::max<std::size_t>(3, plan.totalBars / 24) &&
        report.arpeggioNoteCount >= requiredArpeggioNotes && report.dialogueLines >= requiredDialogueLines &&
        report.meaningfulLines * 4 >= report.independentLines * 3;
    return report;
}

} // namespace pulso
