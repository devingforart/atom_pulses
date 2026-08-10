#include "SongComposer.h"

#include "Random.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numeric>

namespace pulso {
namespace {

constexpr std::array minorSemitones{0, 2, 3, 5, 7, 8, 10};
constexpr std::array majorSemitones{0, 2, 4, 5, 7, 9, 11};
constexpr std::array dorianSemitones{0, 2, 3, 5, 7, 9, 10};
constexpr std::array mixolydianSemitones{0, 2, 4, 5, 7, 9, 10};

int degreePitchClass(const SongPlan& plan, int degree) {
    const auto index = static_cast<std::size_t>(positiveModulo(degree, 7));
    const auto semitone = plan.scale == ScaleKind::Major ? majorSemitones[index]
        : plan.scale == ScaleKind::Dorian ? dorianSemitones[index]
        : plan.scale == ScaleKind::Mixolydian ? mixolydianSemitones[index]
                                              : minorSemitones[index];
    return positiveModulo(plan.rootPitchClass + semitone, 12);
}

std::vector<int> chordForDegree(const SongPlan& plan, int degree) {
    return {degreePitchClass(plan, degree), degreePitchClass(plan, degree + 2),
            degreePitchClass(plan, degree + 4)};
}

int nearestPitchClass(int pitchClass, int target, int minimum, int maximum) {
    auto best = std::clamp(target, minimum, maximum);
    auto distance = 999;
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (positiveModulo(pitch, 12) != pitchClass) continue;
        const auto nextDistance = std::abs(pitch - target);
        if (nextDistance < distance) {
            best = pitch;
            distance = nextDistance;
        }
    }
    return best;
}

void appendShifted(Pattern& destination, Pattern&& source, double beatOffset,
                   double songLength) {
    for (auto& note : source.notes) {
        note.startBeat += beatOffset;
        if (note.startBeat < songLength) destination.notes.push_back(note);
    }
    for (auto& control : source.controls) {
        control.beat += beatOffset;
        if (control.beat < songLength) destination.controls.push_back(control);
    }
    for (auto& marker : source.markers) {
        marker.beat += beatOffset;
        if (marker.beat < songLength) destination.markers.push_back(std::move(marker));
    }
}

bool containsCaseInsensitive(const std::string& text, const std::string& needle) {
    auto lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lower.find(needle) != std::string::npos;
}

std::vector<VoiceId> defaultActiveVoices(const SongSection& section) {
    std::vector<VoiceId> voices{VoiceId::HarmonicFoundation, VoiceId::Atmosphere};
    if (section.energy > 0.24) voices.push_back(VoiceId::Lead);
    if (section.energy > 0.34) voices.push_back(VoiceId::SubBass);
    if (section.energy > 0.42) {
        voices.push_back(VoiceId::CoreDrums);
        voices.push_back(VoiceId::HighPercussion);
    }
    if (section.energy > 0.54) {
        voices.push_back(VoiceId::HarmonicPulse);
        voices.push_back(VoiceId::MovementBass);
    }
    if (section.energy > 0.64) {
        voices.push_back(VoiceId::LowPercussion);
        voices.push_back(VoiceId::Countermelody);
    }
    if (section.energy > 0.74) voices.push_back(VoiceId::HarmonicUpper);
    if (section.tension > 0.52 || section.energy > 0.72) voices.push_back(VoiceId::Transitions);
    if (containsCaseInsensitive(section.name, "breakdown"))
        voices = {VoiceId::HarmonicFoundation, VoiceId::HarmonicUpper, VoiceId::Lead,
                  VoiceId::Atmosphere, VoiceId::HighPercussion, VoiceId::Transitions};
    if (containsCaseInsensitive(section.name, "coda"))
        voices = {VoiceId::HarmonicFoundation, VoiceId::HarmonicUpper, VoiceId::Lead,
                  VoiceId::Atmosphere, VoiceId::Transitions};
    return voices;
}

std::vector<PlannedVoice> defaultVoicePlan() {
    constexpr std::array functions{
        "Anchor pulse and sectional weight", "Add low-frequency rhythmic dialogue",
        "Create continuous high-frequency motion", "Define roots and physical foundation",
        "Create syncopated bass movement", "Carry voice-led harmonic continuity",
        "Translate harmony into rhythmic propulsion", "Add extensions and harmonic air",
        "State and transform the central motif", "Answer the lead in negative space",
        "Sustain depth, ambiguity and long transitions", "Signal arrivals, exits and energy changes"};
    constexpr std::array interactions{
        "Leads the rhythmic hierarchy", "Answers core drums without doubling them",
        "Fills subdivisions left open by low percussion", "Locks selectively with the kick",
        "Moves between sub-bass attacks", "Leaves register space for bass and lead",
        "Interlocks with high percussion", "Avoids lead register and adds chord colour",
        "Owns thematic identity", "Responds rather than shadows the lead",
        "Moves more slowly than every other voice", "Appears only near structural boundaries"};
    std::vector<PlannedVoice> result;
    result.reserve(voiceDefinitions.size());
    for (std::size_t index = 0; index < voiceDefinitions.size(); ++index) {
        const auto& definition = voiceDefinitions[index];
        result.push_back({definition.id, functions[index], interactions[index],
                          index == 10 ? 0.36 : 0.62, index == 2 || index == 6 ? 0.72 : 0.38,
                          definition.minimumPitch, definition.maximumPitch});
    }
    return result;
}

bool voiceIsActive(const SongSection& section, VoiceId voice) {
    return std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) !=
           section.activeVoices.end();
}

const PlannedVoice* plannedVoice(const SongPlan& plan, VoiceId id) {
    const auto found = std::find_if(plan.voices.begin(), plan.voices.end(),
                                    [id](const auto& voice) { return voice.id == id; });
    return found == plan.voices.end() ? nullptr : &*found;
}

int constrainToVoiceRegister(int pitch, const PlannedVoice* voice) {
    if (voice == nullptr) return std::clamp(pitch, 0, 127);
    while (pitch < voice->minimumPitch && pitch + 12 <= voice->maximumPitch) pitch += 12;
    while (pitch > voice->maximumPitch && pitch - 12 >= voice->minimumPitch) pitch -= 12;
    return std::clamp(pitch, voice->minimumPitch, voice->maximumPitch);
}

} // namespace

SongPlan SongComposer::createLocalPlan(const std::string& direction, int targetSeconds,
                                       double bpm, double beatsPerBar, std::uint64_t seed,
                                       int rootPitchClass, ScaleKind scale) {
    SongPlan plan;
    plan.targetSeconds = std::clamp(targetSeconds, 30, 1800);
    plan.bpm = std::clamp(bpm, 30.0, 300.0);
    plan.beatsPerBar = std::clamp(beatsPerBar, 2.0, 12.0);
    plan.totalBars = std::clamp(static_cast<int>(std::lround(
                                    plan.targetSeconds * plan.bpm / 60.0 / plan.beatsPerBar)),
                                8, 512);
    plan.seed = seed;
    plan.rootPitchClass = positiveModulo(rootPitchClass, 12);
    plan.scale = scale;
    plan.title = direction.empty() ? "Longform Idea" : direction.substr(0, 48);
    plan.key = std::array{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
                   [static_cast<std::size_t>(plan.rootPitchClass)] +
               std::string(plan.scale == ScaleKind::Major ? " major" : " minor");
    plan.summary = "A complete thematic arc with recurring material, contrast, climax and resolution.";

    Random random(seed ^ 0x534F4E47504C414EULL);
    const auto third = plan.scale == ScaleKind::Major ? 4 : 3;
    plan.motifIntervals = {0, third, 7, 5, third, random.range(0, 1) == 0 ? 10 : 12};
    plan.chordDegrees = plan.scale == ScaleKind::Major ? std::vector<int>{0, 4, 5, 3}
                                                       : std::vector<int>{0, 5, 3, 6};
    plan.voices = defaultVoicePlan();

    struct Template { const char* name; const char* function; double share; double energy; double tension; double density; int motif; };
    constexpr std::array form{
        Template{"Prologue", "Introduce atmosphere and fragments of the central motif", 0.07, 0.22, 0.18, 0.25, 0},
        Template{"Theme A", "State the primary theme clearly", 0.12, 0.48, 0.30, 0.52, 1},
        Template{"Lift", "Increase motion and harmonic expectation", 0.08, 0.62, 0.58, 0.64, 2},
        Template{"First Arrival", "Deliver the first memorable release", 0.13, 0.76, 0.48, 0.76, 1},
        Template{"Development", "Transform and expand the established material", 0.14, 0.66, 0.70, 0.68, 3},
        Template{"Breakdown", "Create space and harmonic ambiguity", 0.09, 0.28, 0.62, 0.30, 4},
        Template{"Return", "Recall the theme with greater consequence", 0.12, 0.72, 0.55, 0.72, 1},
        Template{"Ascent", "Accumulate rhythmic and harmonic momentum", 0.10, 0.84, 0.82, 0.84, 5},
        Template{"Climax", "Resolve the full dramatic argument", 0.09, 0.96, 0.92, 0.94, 1},
        Template{"Coda", "Dissolve the motif into a conclusive cadence", 0.06, 0.24, 0.08, 0.22, 0}
    };

    const auto sectionCount = std::min(form.size(), static_cast<std::size_t>(
        std::max(3, plan.totalBars / 2)));
    std::vector<std::size_t> selectedFormIndices;
    auto selectedShare = 0.0;
    for (std::size_t index = 0; index < sectionCount; ++index) {
        const auto formIndex = index * (form.size() - 1) / (sectionCount - 1);
        selectedFormIndices.push_back(formIndex);
        selectedShare += form[formIndex].share;
    }
    auto allocated = 0;
    for (std::size_t index = 0; index < selectedFormIndices.size(); ++index) {
        const auto& item = form[selectedFormIndices[index]];
        auto bars = index + 1 == selectedFormIndices.size() ? plan.totalBars - allocated
            : std::max(1, static_cast<int>(std::lround(plan.totalBars * item.share / selectedShare)));
        if (index + 1 != selectedFormIndices.size() && plan.totalBars >= 40)
            bars = std::max(4, static_cast<int>(std::lround(bars / 4.0)) * 4);
        const auto minimumRemaining = static_cast<int>(selectedFormIndices.size() - index - 1);
        bars = std::clamp(bars, 1, plan.totalBars - allocated - minimumRemaining);
        plan.sections.push_back({item.name, item.function,
            index < 5 ? "Move away from tonic while preserving functional voice leading"
                      : "Return progressively toward the tonic and final cadence",
            item.motif == 1 ? "Recognisable restatement of the primary motif"
                                   : "Transform the primary motif without losing its contour",
            allocated, bars, item.energy, item.tension,
            item.density, item.motif});
        plan.sections.back().activeVoices = defaultActiveVoices(plan.sections.back());
        allocated += bars;
    }
    if (containsCaseInsensitive(direction, "ambient")) {
        for (auto& section : plan.sections) {
            section.density *= 0.65;
            section.energy *= 0.82;
        }
    }
    normalizePlan(plan);
    return plan;
}

void SongComposer::normalizePlan(SongPlan& plan) {
    plan.targetSeconds = std::clamp(plan.targetSeconds, 30, 1800);
    plan.bpm = std::clamp(plan.bpm, 30.0, 300.0);
    plan.beatsPerBar = std::clamp(plan.beatsPerBar, 2.0, 12.0);
    plan.totalBars = std::clamp(plan.totalBars, 8, 512);
    plan.rootPitchClass = positiveModulo(plan.rootPitchClass, 12);
    if (plan.motifIntervals.size() < 3) plan.motifIntervals = {0, 3, 7, 5};
    if (plan.chordDegrees.size() < 2) plan.chordDegrees = {0, 5, 3, 6};
    for (auto& value : plan.motifIntervals) value = std::clamp(value, -24, 24);
    for (auto& value : plan.chordDegrees) value = positiveModulo(value, 7);
    if (plan.voices.empty()) plan.voices = defaultVoicePlan();
    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> seenVoices{};
    plan.voices.erase(std::remove_if(plan.voices.begin(), plan.voices.end(), [&](auto& voice) {
        const auto index = static_cast<std::size_t>(voice.id);
        if (index >= seenVoices.size() || seenVoices[index]) return true;
        seenVoices[index] = true;
        const auto& definition = voiceDefinition(voice.id);
        voice.activity = std::clamp(voice.activity, 0.0, 1.0);
        voice.syncopation = std::clamp(voice.syncopation, 0.0, 1.0);
        voice.minimumPitch = std::clamp(voice.minimumPitch, definition.minimumPitch, definition.maximumPitch);
        voice.maximumPitch = std::clamp(voice.maximumPitch, voice.minimumPitch, definition.maximumPitch);
        return false;
    }), plan.voices.end());

    if (plan.sections.empty()) {
        plan.sections.push_back({"Complete Arc", "Present, develop and resolve the central idea",
                                 "Depart from and return to tonic", "Develop the central motif",
                                 0, plan.totalBars, 0.65, 0.55, 0.65, 1});
    }
    if (plan.sections.size() > static_cast<std::size_t>(plan.totalBars))
        plan.sections.resize(static_cast<std::size_t>(plan.totalBars));
    auto cursor = 0;
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        auto& section = plan.sections[index];
        section.startBar = cursor;
        section.energy = std::clamp(section.energy, 0.0, 1.0);
        section.tension = std::clamp(section.tension, 0.0, 1.0);
        section.density = std::clamp(section.density, 0.0, 1.0);
        if (section.activeVoices.empty()) section.activeVoices = defaultActiveVoices(section);
        std::array<bool, static_cast<std::size_t>(VoiceId::Count)> activeSeen{};
        section.activeVoices.erase(std::remove_if(section.activeVoices.begin(), section.activeVoices.end(),
            [&](VoiceId voice) {
                const auto voiceIndex = static_cast<std::size_t>(voice);
                if (voiceIndex >= activeSeen.size() || activeSeen[voiceIndex] || !seenVoices[voiceIndex]) return true;
                activeSeen[voiceIndex] = true;
                return false;
            }), section.activeVoices.end());
        if (section.activeVoices.empty() && !plan.voices.empty())
            section.activeVoices.push_back(plan.voices.front().id);
        const auto remainingSections = static_cast<int>(plan.sections.size() - index - 1);
        section.bars = index + 1 == plan.sections.size()
                           ? plan.totalBars - cursor
                           : std::clamp(section.bars, 1, plan.totalBars - cursor - remainingSections);
        cursor += section.bars;
    }
    if (plan.sections.back().bars < 1) {
        plan.sections.back().bars = 1;
        plan.totalBars = std::max(plan.totalBars, cursor + 1);
    }
}

Pattern SongComposer::render(const SongPlan& sourcePlan, const GenerationContext& foundation,
                             const ProgressCallback& progress) const {
    auto plan = sourcePlan;
    normalizePlan(plan);
    Pattern song;
    song.lengthBeats = plan.totalBars * plan.beatsPerBar;
    song.seed = plan.seed;
    Generator generator;
    std::size_t workUnits = 0;
    for (const auto& section : plan.sections)
        workUnits += static_cast<std::size_t>((section.bars + 15) / 16);
    auto completed = std::size_t{};
    auto globalChunk = std::uint64_t{};
    std::array<int, 3> previousVoicing{48, 55, 60};

    for (const auto& section : plan.sections) {
        song.markers.push_back({section.startBar * plan.beatsPerBar, section.name});
        auto sectionBar = 0;
        while (sectionBar < section.bars) {
            const auto chunkBars = std::min(16, section.bars - sectionBar);
            auto context = foundation;
            context.role = Role::Ensemble;
            context.bars = chunkBars;
            context.beatsPerBar = plan.beatsPerBar;
            context.seed = plan.seed;
            context.variationIndex = foundation.variationIndex * 7ULL +
                                     static_cast<std::uint64_t>(std::max(0, section.motifVariant));
            context.evolutionStep = globalChunk++;
            context.rootPitchClass = plan.rootPitchClass;
            context.scale = plan.scale;
            context.thematicIntervals = plan.motifIntervals;
            context.energy = section.energy;
            context.complexity = std::clamp(0.20 + section.density * 0.68, 0.0, 1.0);
            context.development = std::clamp(0.25 + section.tension * 0.70, 0.0, 1.0);
            context.cohesion = 0.90;
            context.repetition = section.motifVariant == 1 ? 0.82 : 0.68;
            context.risk = std::clamp(0.12 + section.tension * 0.42, 0.0, 0.62);
            context.harmonyByBar.clear();
            for (auto bar = 0; bar < chunkBars; ++bar) {
                const auto absoluteBar = section.startBar + sectionBar + bar;
                auto progressionIndex = absoluteBar + section.motifVariant;
                if (&section == &plan.sections.back() && bar + sectionBar + 1 == section.bars)
                    progressionIndex = 0;
                context.harmonyByBar.push_back(chordForDegree(
                    plan, plan.chordDegrees[static_cast<std::size_t>(progressionIndex) % plan.chordDegrees.size()]));
            }

            auto chunk = generator.generate(context);
            const auto offset = (section.startBar + sectionBar) * plan.beatsPerBar;
            for (auto& note : chunk.notes) {
                note.voice = inferVoiceFromChannel(note.channel);
                const auto* voice = plannedVoice(plan, note.voice);
                if (note.voice != VoiceId::CoreDrums)
                    note.pitch = constrainToVoiceRegister(note.pitch, voice);
            }

            if (voiceIsActive(section, VoiceId::MovementBass)) {
                const auto originalNotes = chunk.notes;
                auto bassIndex = 0;
                for (const auto& source : originalNotes) {
                    if (source.voice != VoiceId::SubBass || bassIndex++ % 2 != 1) continue;
                    auto movement = source;
                    movement.voice = VoiceId::MovementBass;
                    movement.channel = voiceDefinition(movement.voice).midiChannel;
                    movement.pitch = constrainToVoiceRegister(source.pitch + 12,
                                                               plannedVoice(plan, movement.voice));
                    movement.durationBeats = std::min(source.durationBeats, 0.48);
                    movement.velocity = std::max(1, source.velocity - 12);
                    chunk.notes.push_back(movement);
                }
            }

            if (voiceIsActive(section, VoiceId::Countermelody)) {
                auto counterContext = context;
                counterContext.role = Role::Countermelody;
                counterContext.variationIndex += 17;
                counterContext.complexity *= 0.72;
                auto counter = generator.generate(counterContext);
                for (auto note : counter.notes) {
                    note.voice = VoiceId::Countermelody;
                    note.channel = voiceDefinition(note.voice).midiChannel;
                    note.startBeat = std::min(chunkBars * plan.beatsPerBar - 0.02,
                                              note.startBeat + plan.beatsPerBar * 0.5);
                    note.pitch = constrainToVoiceRegister(note.pitch - 5, plannedVoice(plan, note.voice));
                    note.velocity = std::max(1, note.velocity - 14);
                    const auto collision = std::any_of(chunk.notes.begin(), chunk.notes.end(), [&](const auto& lead) {
                        return lead.voice == VoiceId::Lead && std::abs(lead.startBeat - note.startBeat) < 0.13 &&
                               std::abs(lead.pitch - note.pitch) < 3;
                    });
                    if (collision) note.pitch = constrainToVoiceRegister(note.pitch - 12, plannedVoice(plan, note.voice));
                    chunk.notes.push_back(note);
                }
            }

            for (auto bar = 0; bar < chunkBars; ++bar) {
                const auto& chord = context.harmonyByBar[static_cast<std::size_t>(bar)];
                const auto barStart = bar * plan.beatsPerBar;
                std::array<int, 3> voicing{};
                for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex) {
                    const auto target = previousVoicing[static_cast<std::size_t>(voiceIndex)];
                    voicing[static_cast<std::size_t>(voiceIndex)] = nearestPitchClass(
                        chord[static_cast<std::size_t>(voiceIndex)], target, 45 + voiceIndex * 4, 72 + voiceIndex * 3);
                    if (voiceIsActive(section, VoiceId::HarmonicFoundation))
                        chunk.notes.push_back({barStart, std::max(0.25, plan.beatsPerBar * 0.96),
                            constrainToVoiceRegister(voicing[static_cast<std::size_t>(voiceIndex)],
                                                     plannedVoice(plan, VoiceId::HarmonicFoundation)),
                            std::clamp(static_cast<int>(54 + section.energy * 34), 1, 127),
                            voiceDefinition(VoiceId::HarmonicFoundation).midiChannel,
                            VoiceId::HarmonicFoundation});
                }
                previousVoicing = voicing;

                if (voiceIsActive(section, VoiceId::HarmonicPulse)) {
                    constexpr std::array pulsePositions{0.0, 1.5, 2.75};
                    const auto pulseCount = section.density > 0.72 ? 3 : 2;
                    for (auto pulse = 0; pulse < pulseCount; ++pulse)
                        for (auto chordTone = 0; chordTone < 2; ++chordTone)
                            chunk.notes.push_back({barStart + std::min(pulsePositions[static_cast<std::size_t>(pulse)],
                                                                       plan.beatsPerBar - 0.25),
                                0.22 + section.energy * 0.18,
                                constrainToVoiceRegister(voicing[static_cast<std::size_t>(chordTone)] + 12,
                                                         plannedVoice(plan, VoiceId::HarmonicPulse)),
                                std::clamp(static_cast<int>(58 + section.energy * 32), 1, 127),
                                voiceDefinition(VoiceId::HarmonicPulse).midiChannel, VoiceId::HarmonicPulse});
                }
                if (voiceIsActive(section, VoiceId::HarmonicUpper) && bar % 2 == 0) {
                    const auto extension = degreePitchClass(plan, plan.chordDegrees[
                        static_cast<std::size_t>(section.startBar + sectionBar + bar) % plan.chordDegrees.size()] + 6);
                    chunk.notes.push_back({barStart, std::min(plan.beatsPerBar * 1.92,
                                                              (chunkBars - bar) * plan.beatsPerBar - 0.02),
                        nearestPitchClass(extension, 79, 67, 96),
                        std::clamp(static_cast<int>(45 + section.energy * 27), 1, 127),
                        voiceDefinition(VoiceId::HarmonicUpper).midiChannel, VoiceId::HarmonicUpper});
                }
                if (voiceIsActive(section, VoiceId::Atmosphere) && bar % 4 == 0) {
                    chunk.notes.push_back({barStart,
                        std::min(plan.beatsPerBar * 3.92, (chunkBars - bar) * plan.beatsPerBar - 0.02),
                        nearestPitchClass(chord.front(), 55, 43, 72),
                        std::clamp(static_cast<int>(36 + section.energy * 20), 1, 127),
                        voiceDefinition(VoiceId::Atmosphere).midiChannel, VoiceId::Atmosphere});
                }

                Random rhythm(plan.seed ^ static_cast<std::uint64_t>(section.startBar + sectionBar + bar + 1) *
                                      0x9E3779B97F4A7C15ULL);
                if (voiceIsActive(section, VoiceId::LowPercussion)) {
                    const auto first = plan.beatsPerBar * (rhythm.chance(0.5) ? 0.375 : 0.5);
                    chunk.notes.push_back({barStart + first, 0.18, rhythm.chance(0.5) ? 45 : 47,
                        std::clamp(static_cast<int>(54 + section.energy * 38), 1, 127), 10,
                        VoiceId::LowPercussion});
                    if (section.density > 0.62)
                        chunk.notes.push_back({barStart + std::min(plan.beatsPerBar - 0.35, first + 1.25),
                            0.16, 43, std::clamp(static_cast<int>(48 + section.energy * 34), 1, 127), 10,
                            VoiceId::LowPercussion});
                }
                if (voiceIsActive(section, VoiceId::HighPercussion)) {
                    const auto subdivisions = section.density > 0.68 ? 8 : 4;
                    for (auto step = 0; step < subdivisions; ++step) {
                        if ((step + bar + section.motifVariant) % 3 == 1 && section.density < 0.78) continue;
                        const auto position = static_cast<double>(step) * plan.beatsPerBar / subdivisions;
                        chunk.notes.push_back({barStart + position, 0.08, step % 2 == 0 ? 42 : 70,
                            std::clamp(static_cast<int>(42 + section.energy * 28 + (step % 2) * 7), 1, 127),
                            10, VoiceId::HighPercussion});
                    }
                }
            }

            const auto finalChunkOfSection = sectionBar + chunkBars == section.bars;
            if (voiceIsActive(section, VoiceId::Transitions) && finalChunkOfSection) {
                const auto transitionStart = std::max(0.0, chunkBars * plan.beatsPerBar - plan.beatsPerBar);
                for (auto step = 0; step < 8; ++step)
                    chunk.notes.push_back({transitionStart + step * plan.beatsPerBar / 8.0, 0.08,
                        step < 4 ? 38 : 40, std::clamp(55 + step * 7, 1, 127), 10,
                        VoiceId::Transitions});
                chunk.controls.push_back({transitionStart, 74, 28, 9, VoiceId::Transitions});
                chunk.controls.push_back({chunkBars * plan.beatsPerBar - 0.05, 74, 118, 9, VoiceId::Transitions});
            }

            for (const auto voiceId : section.activeVoices) {
                const auto& definition = voiceDefinition(voiceId);
                const auto expression = std::clamp(static_cast<int>(48 + section.energy * 70), 1, 127);
                chunk.controls.push_back({0.0, 11, expression, definition.midiChannel, voiceId});
                chunk.controls.push_back({chunkBars * plan.beatsPerBar * 0.5, 1,
                    std::clamp(static_cast<int>(section.tension * 104), 0, 127),
                    definition.midiChannel, voiceId});
            }

            chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [&](const auto& note) {
                return !voiceIsActive(section, note.voice);
            }), chunk.notes.end());

            appendShifted(song, std::move(chunk), offset, song.lengthBeats);
            sectionBar += chunkBars;
            ++completed;
            if (progress) progress(completed, workUnits, section);
        }
    }

    std::sort(song.notes.begin(), song.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.channel != right.channel) return left.channel < right.channel;
        return left.pitch < right.pitch;
    });
    song.notes.erase(std::unique(song.notes.begin(), song.notes.end(), [](const auto& left, const auto& right) {
        return std::abs(left.startBeat - right.startBeat) < 0.0001 && left.channel == right.channel &&
               left.pitch == right.pitch && left.voice == right.voice;
    }), song.notes.end());
    std::sort(song.controls.begin(), song.controls.end(), [](const auto& left, const auto& right) {
        if (left.beat != right.beat) return left.beat < right.beat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.controller < right.controller;
    });
    return song;
}

} // namespace pulso
