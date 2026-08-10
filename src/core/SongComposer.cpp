#include "SongComposer.h"

#include "Random.h"
#include "PhraseDirector.h"
#include "RhythmEngine.h"
#include "TonalContract.h"

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
        voices.push_back(VoiceId::SnareClap);
        voices.push_back(VoiceId::ClosedHats);
        voices.push_back(VoiceId::OpenHatsShaker);
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
        "Sustain depth, ambiguity and long transitions", "Signal arrivals, exits and energy changes",
        "Place the backbeat and reinforce arrivals", "Maintain controlled high-frequency motion",
        "Create offbeat lift and a quiet sixteenth-note current"};
    constexpr std::array interactions{
        "Leads the rhythmic hierarchy", "Answers core drums without doubling them",
        "Fills subdivisions left open by low percussion", "Locks selectively with the kick",
        "Moves between sub-bass attacks", "Leaves register space for bass and lead",
        "Interlocks with high percussion", "Avoids lead register and adds chord colour",
        "Owns thematic identity", "Responds rather than shadows the lead",
        "Moves more slowly than every other voice", "Appears only near structural boundaries",
        "Supports the kick without sharing its event budget", "Stays lighter than the clap and kick",
        "Interlocks with percussion without masking the groove"};
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

std::uint64_t mix64(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double signedHumanValue(std::uint64_t seed, VoiceId voice, int absoluteBar, int ordinal) noexcept {
    const auto value = mix64(seed ^ (static_cast<std::uint64_t>(voice) + 1) * 0x9e3779b97f4a7c15ULL ^
                             static_cast<std::uint64_t>(absoluteBar + 1) * 0xd1b54a32d192ed03ULL ^
                             static_cast<std::uint64_t>(ordinal + 1) * 0x94d049bb133111ebULL);
    return static_cast<double>(value & 0xffffu) / 32767.5 - 1.0;
}

void applyDirectedPerformance(Pattern& chunk, const SongPlan& plan, const SongSection& section,
                              const std::vector<BarDirection>& directions,
                              int sectionBar, double beatsPerBar) {
    auto ordinal = 0;
    std::sort(chunk.notes.begin(), chunk.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.pitch < right.pitch;
    });
    std::vector<std::array<int, static_cast<std::size_t>(VoiceId::Count)>> onsetCounts(
        static_cast<std::size_t>(std::max(1, static_cast<int>(chunk.lengthBeats / beatsPerBar))), {});
    chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [&](const auto& note) mutable {
        const auto chunkBar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)),
                                         0, std::max(0, static_cast<int>(chunk.lengthBeats / beatsPerBar) - 1));
        const auto localBar = sectionBar + chunkBar;
        if (localBar < 0 || localBar >= static_cast<int>(directions.size()) ||
            note.voice == VoiceId::Unspecified) return true;
        const auto& instruction = directions[static_cast<std::size_t>(localBar)].forVoice(note.voice);
        const auto beatInBar = note.startBeat - chunkBar * beatsPerBar;
        if (instruction.maximumOnsets <= 0 || beatInBar + 0.0001 < instruction.entryBeat ||
            beatInBar >= instruction.exitBeat) return true;
        const auto voiceIndex = static_cast<std::size_t>(note.voice);
        auto& count = onsetCounts[static_cast<std::size_t>(chunkBar)][voiceIndex];
        return count++ >= instruction.maximumOnsets;
    }), chunk.notes.end());

    std::array<int, static_cast<std::size_t>(VoiceId::Count)> voiceOrdinals{};
    for (auto& note : chunk.notes) {
        const auto chunkBar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)), 0, 15);
        const auto absoluteBar = section.startBar + sectionBar + chunkBar;
        const auto index = note.voice == VoiceId::Unspecified ? 0 : static_cast<std::size_t>(note.voice);
        const auto noteOrdinal = index < voiceOrdinals.size() ? voiceOrdinals[index]++ : ordinal++;
        const auto human = signedHumanValue(plan.seed, note.voice, absoluteBar, noteOrdinal);
        const auto phrasePosition = positiveModulo(sectionBar + chunkBar, 8) / 7.0;
        const auto arc = 0.91 + 0.10 * std::sin(phrasePosition * 3.14159265358979323846);
        const auto& instruction = directions[static_cast<std::size_t>(sectionBar + chunkBar)].forVoice(note.voice);
        note.velocity = std::clamp(static_cast<int>(std::lround(
            note.velocity * arc * (0.72 + instruction.expression * 0.38) + human * 4.0)), 1, 127);

        auto maximumOffset = 0.0;
        if (note.voice == VoiceId::ClosedHats) maximumOffset = 0.006;
        else if (note.voice == VoiceId::OpenHatsShaker) maximumOffset = 0.011;
        else if (note.voice == VoiceId::SnareClap) maximumOffset = 0.004;
        else if (note.voice == VoiceId::HighPercussion) maximumOffset = 0.020;
        else if (note.voice == VoiceId::LowPercussion) maximumOffset = 0.014;
        else if (note.voice == VoiceId::HarmonicPulse || note.voice == VoiceId::MovementBass) maximumOffset = 0.010;
        else if (isVoiceInFamily(note.voice, VoiceFamily::Melodic)) maximumOffset = 0.013;
        if (note.voice != VoiceId::CoreDrums) {
            const auto barStart = chunkBar * beatsPerBar;
            note.startBeat = std::clamp(note.startBeat + human * maximumOffset,
                                        barStart, barStart + beatsPerBar - 0.015);
        }
        if (isVoiceInFamily(note.voice, VoiceFamily::Melodic))
            note.durationBeats = std::max(0.04, note.durationBeats * (0.94 + human * 0.05));
    }
}

void addDirectedMelody(Pattern& chunk, const SongPlan& plan, const SongSection& section,
                       const GenerationContext& context, const std::vector<BarDirection>& directions,
                       int sectionBar, int chunkBars, int& previousLead, int& previousCounter,
                       std::size_t& motifCursor) {
    constexpr std::array<std::array<double, 4>, 4> leadStarts{{
        {{0.25, 1.45, 2.80, -1.0}}, {{0.0, 0.85, 2.35, -1.0}},
        {{0.45, 1.20, 2.90, -1.0}}, {{0.20, 1.90, -1.0, -1.0}}
    }};
    constexpr std::array<std::array<double, 4>, 4> leadDurations{{
        {{0.52, 0.68, 0.92, 0.0}}, {{0.42, 0.78, 1.18, 0.0}},
        {{0.58, 0.48, 0.72, 0.0}}, {{0.72, 1.10, 0.0, 0.0}}
    }};
    constexpr std::array<double, 4> counterStarts{0.55, 1.82, 3.02, -1.0};
    constexpr std::array<double, 4> counterDurations{0.62, 0.84, 0.54, 0.0};

    for (auto bar = 0; bar < chunkBars; ++bar) {
        const auto localBar = sectionBar + bar;
        if (localBar >= static_cast<int>(directions.size())) break;
        const auto& direction = directions[static_cast<std::size_t>(localBar)];
        const auto voice = direction.foreground;
        if (voice != VoiceId::Lead && voice != VoiceId::Countermelody) continue;
        const auto& instruction = direction.forVoice(voice);
        if (instruction.maximumOnsets <= 0) continue;
        const auto barStart = bar * plan.beatsPerBar;
        const auto scale = plan.beatsPerBar / 4.0;
        const auto phrase = positiveModulo(localBar, 8);
        const auto patternIndex = phrase == 0 ? 0 : phrase == 1 ? 1 : phrase == 4 ? 2 : 3;
        const auto& starts = voice == VoiceId::Lead ? leadStarts[static_cast<std::size_t>(patternIndex)]
                                                    : counterStarts;
        const auto& durations = voice == VoiceId::Lead ? leadDurations[static_cast<std::size_t>(patternIndex)]
                                                       : counterDurations;
        auto& previousPitch = voice == VoiceId::Lead ? previousLead : previousCounter;
        const auto count = std::min(instruction.maximumOnsets, 4);
        for (auto noteIndex = 0; noteIndex < count; ++noteIndex) {
            if (starts[static_cast<std::size_t>(noteIndex)] < 0.0) break;
            const auto position = starts[static_cast<std::size_t>(noteIndex)] * scale;
            if (position < instruction.entryBeat || position >= instruction.exitBeat) continue;
            const auto motifIndex = voice == VoiceId::Lead ? motifCursor++ :
                motifCursor + static_cast<std::size_t>(noteIndex + 2);
            auto interval = plan.motifIntervals[motifIndex % plan.motifIntervals.size()];
            if (voice == VoiceId::Countermelody) interval = 7 - interval;
            if (section.motifVariant % 3 == 2) interval += noteIndex % 2 == 0 ? 2 : -2;
            auto pitchClass = positiveModulo(plan.rootPitchClass + interval, 12);
            if (noteIndex == 0 && direction.arrival)
                pitchClass = context.harmonyByBar[static_cast<std::size_t>(bar)].front();
            const auto target = previousPitch + std::clamp(interval, -5, 5);
            const auto* planned = plannedVoice(plan, voice);
            auto pitch = nearestPitchClass(pitchClass, target,
                planned == nullptr ? 52 : planned->minimumPitch,
                planned == nullptr ? 92 : planned->maximumPitch);
            if (std::abs(pitch - previousPitch) > 8)
                pitch = nearestPitchClass(pitchClass, previousPitch,
                    planned == nullptr ? 52 : planned->minimumPitch,
                    planned == nullptr ? 92 : planned->maximumPitch);
            const auto duration = std::min(durations[static_cast<std::size_t>(noteIndex)] * scale,
                                           instruction.exitBeat - position - 0.02);
            if (duration <= 0.03) continue;
            chunk.notes.push_back({barStart + position, duration, pitch,
                std::clamp(static_cast<int>(58 + section.energy * 36 + (noteIndex == 0 ? 7 : 0)), 1, 127),
                voiceDefinition(voice).midiChannel, voice});
            previousPitch = pitch;
        }
    }
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
    plan.rhythmMotifs.push_back({"A", 2, 16,
        "10001000100010001000100010001000",
        "00002000000020000000200000002000",
        "00100010001000100010001000100010",
        "00000010000000100000001000000020",
        "00000100000100000000001000010000",
        "00010000001000000001000000100000"});

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
        auto& rhythm = plan.sections.back().rhythm;
        rhythm.authored = true;
        rhythm.motifId = "A";
        rhythm.percussionDensity = item.density;
        rhythm.syncopation = std::clamp(0.22 + item.tension * 0.42, 0.0, 1.0);
        rhythm.swing = containsCaseInsensitive(direction, "organic") ? 0.16 : 0.08;
        rhythm.kickState = containsCaseInsensitive(item.name, "breakdown") ? KickState::Muted
            : containsCaseInsensitive(item.name, "prologue") || containsCaseInsensitive(item.name, "coda")
                ? KickState::Reduced : KickState::FourOnFloor;
        rhythm.continuity = rhythm.kickState == KickState::FourOnFloor
            ? KickContinuity::Required : KickContinuity::Sectional;
        for (auto phraseEnd = 7; phraseEnd < bars; phraseEnd += 8) {
            rhythm.gestures.push_back({phraseEnd, RhythmGestureKind::DropLastKick, 3.0, 0.60});
            rhythm.gestures.push_back({phraseEnd, RhythmGestureKind::PercussionFill, 3.0, 0.68});
        }
        if (item.energy > 0.68 && bars > 5)
            rhythm.gestures.push_back({std::min(5, bars - 1), RhythmGestureKind::DoubleKick, 3.75, 0.72});
        if (bars > 3 && item.density > 0.48)
            rhythm.mutations.push_back({std::min(3, bars - 1), RhythmLane::LowPercussion,
                RhythmMutationKind::Shift, 5, item.motif % 2 == 0 ? 1 : -1, 72,
                "Answer the established percussion cell without replacing it"});
        if (bars > 7 && item.energy > 0.65)
            rhythm.mutations.push_back({std::min(7, bars - 1), RhythmLane::OpenHatsShaker,
                RhythmMutationKind::Ratchet, 14, 3, 73,
                "Create a short phrase-ending acceleration"});
        if (rhythm.kickState == KickState::Muted && bars > 0)
            rhythm.gestures.push_back({bars - 1, RhythmGestureKind::PickupFill, 3.5, 0.76});
        allocated += bars;
    }
    const auto strictFourOnFloor = containsCaseInsensitive(direction, "constant kick") ||
        containsCaseInsensitive(direction, "four on the floor") ||
        containsCaseInsensitive(direction, "four-on-the-floor") ||
        containsCaseInsensitive(direction, "bombo en negras") || containsCaseInsensitive(direction, "bombo constante");
    if (strictFourOnFloor) {
        for (auto& section : plan.sections) {
            section.rhythm.kickState = KickState::FourOnFloor;
            section.rhythm.continuity = KickContinuity::Required;
            section.rhythm.gestures.erase(std::remove_if(section.rhythm.gestures.begin(),
                section.rhythm.gestures.end(), [](const auto& gesture) {
                    return gesture.kind == RhythmGestureKind::DropLastKick ||
                           gesture.kind == RhythmGestureKind::HalfBarMute ||
                           gesture.kind == RhythmGestureKind::FullBarMute;
                }), section.rhythm.gestures.end());
        }
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
    canonicalizeMotif(plan.motifIntervals, plan.scale);
    plan.key = canonicalKeyName(plan.rootPitchClass, plan.scale);
    for (auto& value : plan.chordDegrees) value = positiveModulo(value, 7);
    if (plan.voices.empty()) plan.voices = defaultVoicePlan();
    if (plan.rhythmMotifs.size() > 6) plan.rhythmMotifs.resize(6);
    std::vector<std::string> motifIds;
    for (std::size_t index = 0; index < plan.rhythmMotifs.size(); ++index) {
        auto& motif = plan.rhythmMotifs[index];
        motif.bars = std::clamp(motif.bars, 1, 4);
        motif.stepsPerBar = motif.stepsPerBar <= 8 ? 8 : 16;
        if (motif.id.empty()) motif.id = "R" + std::to_string(index + 1);
        if (std::find(motifIds.begin(), motifIds.end(), motif.id) != motifIds.end())
            motif.id += "_" + std::to_string(index + 1);
        motifIds.push_back(motif.id);
        const auto expected = static_cast<std::size_t>(motif.bars * motif.stepsPerBar);
        const auto cleanMask = [expected](std::string& mask) {
            mask.resize(expected, '0');
            for (auto& value : mask) if (value != '0' && value != '1' && value != '2') value = '0';
        };
        cleanMask(motif.kick);
        cleanMask(motif.snareClap);
        cleanMask(motif.closedHats);
        cleanMask(motif.openHatsShaker);
        cleanMask(motif.lowPercussion);
        cleanMask(motif.highPercussion);
    }
    const auto defaults = defaultVoicePlan();
    for (const auto rhythmVoice : {VoiceId::SnareClap, VoiceId::ClosedHats, VoiceId::OpenHatsShaker})
        if (std::none_of(plan.voices.begin(), plan.voices.end(), [rhythmVoice](const auto& voice) {
                return voice.id == rhythmVoice;
            }))
            plan.voices.push_back(*std::find_if(defaults.begin(), defaults.end(), [rhythmVoice](const auto& voice) {
                return voice.id == rhythmVoice;
            }));
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
        if (!section.rhythm.authored) {
            section.rhythm.kickState = containsCaseInsensitive(section.name, "breakdown")
                ? KickState::Muted : section.energy < 0.30 ? KickState::Reduced
                : section.energy < 0.44 ? KickState::Sparse : KickState::FourOnFloor;
            section.rhythm.continuity = section.rhythm.kickState == KickState::FourOnFloor
                ? KickContinuity::Required : KickContinuity::Sectional;
            section.rhythm.percussionDensity = section.density;
            section.rhythm.syncopation = std::clamp(0.18 + section.tension * 0.48, 0.0, 1.0);
            section.rhythm.swing = 0.08;
        }
        section.rhythm.percussionDensity = std::clamp(section.rhythm.percussionDensity, 0.0, 1.0);
        section.rhythm.syncopation = std::clamp(section.rhythm.syncopation, 0.0, 1.0);
        section.rhythm.swing = std::clamp(section.rhythm.swing, 0.0, 0.35);
        if (!plan.rhythmMotifs.empty() &&
            std::find(motifIds.begin(), motifIds.end(), section.rhythm.motifId) == motifIds.end())
            section.rhythm.motifId = plan.rhythmMotifs.front().id;
        for (auto& gesture : section.rhythm.gestures) {
            gesture.barOffset = std::clamp(gesture.barOffset, 0, std::max(0, section.bars - 1));
            gesture.beat = std::clamp(gesture.beat, 0.0, plan.beatsPerBar - 0.05);
            gesture.intensity = std::clamp(gesture.intensity, 0.0, 1.0);
        }
        if (section.activeVoices.empty()) section.activeVoices = defaultActiveVoices(section);
        const auto hasRhythm = std::any_of(section.activeVoices.begin(), section.activeVoices.end(), [](VoiceId voice) {
            return isVoiceInFamily(voice, VoiceFamily::Rhythm);
        });
        const auto hasKickGesture = std::any_of(section.rhythm.gestures.begin(),
            section.rhythm.gestures.end(), [](const auto& gesture) {
                return gesture.kind == RhythmGestureKind::DoubleKick ||
                       gesture.kind == RhythmGestureKind::PickupFill;
            });
        if ((section.rhythm.kickState != KickState::Muted || hasKickGesture) &&
            std::find(section.activeVoices.begin(), section.activeVoices.end(), VoiceId::CoreDrums) == section.activeVoices.end())
            section.activeVoices.push_back(VoiceId::CoreDrums);
        if (hasRhythm || section.rhythm.kickState != KickState::Muted)
            for (const auto voice : {VoiceId::SnareClap, VoiceId::ClosedHats, VoiceId::OpenHatsShaker})
                if (std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) == section.activeVoices.end())
                    section.activeVoices.push_back(voice);
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
        for (auto& gesture : section.rhythm.gestures)
            gesture.barOffset = std::clamp(gesture.barOffset, 0, section.bars - 1);
        const auto* rhythmMotif = plan.rhythmMotifs.empty() ? nullptr : &*std::find_if(
            plan.rhythmMotifs.begin(), plan.rhythmMotifs.end(), [&](const auto& motif) {
                return motif.id == section.rhythm.motifId;
            });
        for (auto& mutation : section.rhythm.mutations) {
            mutation.barOffset = std::clamp(mutation.barOffset, 0, section.bars - 1);
            mutation.step = std::clamp(mutation.step, 0,
                rhythmMotif == nullptr ? 15 : rhythmMotif->stepsPerBar - 1);
            mutation.amount = mutation.kind == RhythmMutationKind::Ratchet
                ? std::clamp(mutation.amount, 2, 4) : std::clamp(mutation.amount, -4, 4);
            mutation.velocity = std::clamp(mutation.velocity, 1, 127);
            if (mutation.purpose.size() > 160) mutation.purpose.resize(160);
        }
        if (section.rhythm.mutations.size() > 32) section.rhythm.mutations.resize(32);
        std::sort(section.rhythm.gestures.begin(), section.rhythm.gestures.end(), [](const auto& left, const auto& right) {
            if (left.barOffset != right.barOffset) return left.barOffset < right.barOffset;
            if (left.kind != right.kind) return left.kind < right.kind;
            return left.beat < right.beat;
        });
        section.rhythm.gestures.erase(std::unique(section.rhythm.gestures.begin(),
            section.rhythm.gestures.end(), [](const auto& left, const auto& right) {
                return left.barOffset == right.barOffset && left.kind == right.kind &&
                       std::abs(left.beat - right.beat) < 0.01;
            }), section.rhythm.gestures.end());
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
    auto previousLeadPitch = 72;
    auto previousCounterPitch = 65;
    auto motifCursor = std::size_t{};
    std::vector<std::vector<int>> songHarmony(static_cast<std::size_t>(plan.totalBars));

    for (const auto& section : plan.sections) {
        const auto directions = PhraseDirector::create(plan, section);
        for (auto localBar = 0; localBar < section.bars; ++localBar) {
            const auto& direction = directions[static_cast<std::size_t>(localBar)];
            auto progressionIndex = direction.harmonicStep + section.motifVariant;
            if (&section == &plan.sections.back() && localBar + 1 == section.bars)
                progressionIndex = 0;
            const auto absoluteBar = section.startBar + localBar;
            if (absoluteBar >= 0 && absoluteBar < plan.totalBars)
                songHarmony[static_cast<std::size_t>(absoluteBar)] = chordForDegree(
                    plan, plan.chordDegrees[static_cast<std::size_t>(progressionIndex) %
                                            plan.chordDegrees.size()]);
        }
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
                context.harmonyByBar.push_back(songHarmony[static_cast<std::size_t>(absoluteBar)]);
            }

            auto chunk = generator.generate(context);
            const auto offset = (section.startBar + sectionBar) * plan.beatsPerBar;
            for (auto& note : chunk.notes) {
                note.voice = inferVoiceFromChannel(note.channel);
                const auto* voice = plannedVoice(plan, note.voice);
                if (note.voice != VoiceId::CoreDrums)
                    note.pitch = constrainToVoiceRegister(note.pitch, voice);
            }
            chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [](const auto& note) {
                return note.voice != VoiceId::Unspecified && isVoiceInFamily(note.voice, VoiceFamily::Rhythm);
            }), chunk.notes.end());
            chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [](const auto& note) {
                return note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody;
            }), chunk.notes.end());

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

            addDirectedMelody(chunk, plan, section, context, directions, sectionBar, chunkBars,
                              previousLeadPitch, previousCounterPitch, motifCursor);

            for (auto bar = 0; bar < chunkBars; ++bar) {
                const auto& chord = context.harmonyByBar[static_cast<std::size_t>(bar)];
                const auto barStart = bar * plan.beatsPerBar;
                const auto localBar = sectionBar + bar;
                const auto& direction = directions[static_cast<std::size_t>(localBar)];
                std::array<int, 3> voicing{};
                for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex) {
                    const auto target = previousVoicing[static_cast<std::size_t>(voiceIndex)];
                    voicing[static_cast<std::size_t>(voiceIndex)] = nearestPitchClass(
                        chord[static_cast<std::size_t>(voiceIndex)], target, 45 + voiceIndex * 4, 72 + voiceIndex * 3);
                    if (direction.forVoice(VoiceId::HarmonicFoundation).maximumOnsets > 0)
                        chunk.notes.push_back({barStart,
                            std::min((section.bars - localBar) * plan.beatsPerBar - 0.03,
                                     direction.harmonicHoldBars * plan.beatsPerBar - 0.06),
                            constrainToVoiceRegister(voicing[static_cast<std::size_t>(voiceIndex)],
                                                     plannedVoice(plan, VoiceId::HarmonicFoundation)),
                            std::clamp(static_cast<int>(54 + section.energy * 34), 1, 127),
                            voiceDefinition(VoiceId::HarmonicFoundation).midiChannel,
                            VoiceId::HarmonicFoundation});
                }
                previousVoicing = voicing;

                const auto pulseBudget = direction.forVoice(VoiceId::HarmonicPulse).maximumOnsets;
                if (pulseBudget > 0) {
                    constexpr std::array pulsePositions{0.35, 1.65, 3.05};
                    const auto pulseCount = std::clamp(pulseBudget / 2, 1, 3);
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
                if (direction.forVoice(VoiceId::HarmonicUpper).maximumOnsets > 0) {
                    const auto extension = degreePitchClass(plan, plan.chordDegrees[
                        static_cast<std::size_t>(section.startBar + sectionBar + bar) % plan.chordDegrees.size()] + 6);
                    const auto upperStart = std::min(plan.beatsPerBar - 0.25, plan.beatsPerBar * 0.125);
                    chunk.notes.push_back({barStart + upperStart, std::min(plan.beatsPerBar * 1.65,
                                                              (chunkBars - bar) * plan.beatsPerBar - 0.02),
                        nearestPitchClass(extension, 79, 67, 96),
                        std::clamp(static_cast<int>(45 + section.energy * 27), 1, 127),
                        voiceDefinition(VoiceId::HarmonicUpper).midiChannel, VoiceId::HarmonicUpper});
                }
                if (direction.forVoice(VoiceId::Atmosphere).maximumOnsets > 0) {
                    chunk.notes.push_back({barStart,
                        std::min(direction.harmonicHoldBars * plan.beatsPerBar - 0.04,
                                 (chunkBars - bar) * plan.beatsPerBar - 0.02),
                        nearestPitchClass(chord.front(), 55, 43, 72),
                        std::clamp(static_cast<int>(36 + section.energy * 20), 1, 127),
                        voiceDefinition(VoiceId::Atmosphere).midiChannel, VoiceId::Atmosphere});
                }

            }

            RhythmEngine::renderChunk(chunk, plan, section, directions, sectionBar, chunkBars);

            const auto finalChunkOfSection = sectionBar + chunkBars == section.bars;
            if (voiceIsActive(section, VoiceId::Transitions) && finalChunkOfSection) {
                const auto transitionStart = std::max(0.0, chunkBars * plan.beatsPerBar - plan.beatsPerBar);
                chunk.notes.push_back({transitionStart, std::max(0.25, plan.beatsPerBar * 0.94),
                    72, std::clamp(static_cast<int>(48 + section.tension * 48), 1, 127),
                    voiceDefinition(VoiceId::Transitions).midiChannel, VoiceId::Transitions});
                chunk.notes.push_back({chunkBars * plan.beatsPerBar - 0.24, 0.20, 84,
                    std::clamp(static_cast<int>(62 + section.energy * 38), 1, 127),
                    voiceDefinition(VoiceId::Transitions).midiChannel, VoiceId::Transitions});
                chunk.controls.push_back({transitionStart, 74, 28, 9, VoiceId::Transitions});
                chunk.controls.push_back({chunkBars * plan.beatsPerBar - 0.05, 74, 118, 9, VoiceId::Transitions});
            }

            for (auto bar = 0; bar < chunkBars; ++bar) {
                const auto& direction = directions[static_cast<std::size_t>(sectionBar + bar)];
                for (const auto voiceId : section.activeVoices) {
                    const auto& definition = voiceDefinition(voiceId);
                    const auto& instruction = direction.forVoice(voiceId);
                    if (instruction.maximumOnsets <= 0) continue;
                    const auto expression = std::clamp(
                        static_cast<int>(std::lround(instruction.expression * 127.0)), 1, 127);
                    chunk.controls.push_back({bar * plan.beatsPerBar, 11, expression,
                                              definition.midiChannel, voiceId});
                    if (bar % 4 == 0)
                        chunk.controls.push_back({bar * plan.beatsPerBar, 1,
                            std::clamp(static_cast<int>(section.tension * 104), 0, 127),
                            definition.midiChannel, voiceId});
                }
            }

            chunk.notes.erase(std::remove_if(chunk.notes.begin(), chunk.notes.end(), [&](const auto& note) {
                return !voiceIsActive(section, note.voice);
            }), chunk.notes.end());
            applyDirectedPerformance(chunk, plan, section, directions, sectionBar, plan.beatsPerBar);
            RhythmEngine::coordinateBassWithKick(chunk, section, plan.beatsPerBar);

            appendShifted(song, std::move(chunk), offset, song.lengthBeats);
            sectionBar += chunkBars;
            ++completed;
            if (progress) progress(completed, workUnits, section);
        }
    }

    [[maybe_unused]] const auto tonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, songHarmony);
    [[maybe_unused]] const auto rhythmReport = RhythmEngine::enforceContract(song, plan);

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
