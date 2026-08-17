#include "OrchestrationScore.h"

#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace pulso {
namespace {

constexpr std::array catalog{
    InstrumentDefinition{"kick_drum", "Kick Drum", ScoreDepartment::Rhythm, VoiceId::CoreDrums, 35, 36, false, 1.0},
    InstrumentDefinition{"snare_clap", "Snare / Clap", ScoreDepartment::Rhythm, VoiceId::SnareClap, 37, 40, false, 0.9},
    InstrumentDefinition{"hi_hats", "Hi-Hats", ScoreDepartment::Rhythm, VoiceId::ClosedHats, 42, 46, false, 0.7},
    InstrumentDefinition{"timpani", "Timpani", ScoreDepartment::Rhythm, VoiceId::LowPercussion, 36, 57, false, 0.8},
    InstrumentDefinition{"taiko_ensemble", "Taiko Ensemble", ScoreDepartment::Rhythm, VoiceId::LowPercussion, 35, 60, false, 0.9},
    InstrumentDefinition{"latin_percussion", "Latin Percussion", ScoreDepartment::Rhythm, VoiceId::LowPercussion, 41, 77, false, 0.65},
    InstrumentDefinition{"shakers", "Shakers", ScoreDepartment::Rhythm, VoiceId::OpenHatsShaker, 46, 82, false, 0.45},
    InstrumentDefinition{"cymbals", "Cymbals", ScoreDepartment::Rhythm, VoiceId::Transitions, 49, 57, false, 0.5},
    InstrumentDefinition{"piano", "Piano", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 36, 96, true, 0.8},
    InstrumentDefinition{"harp", "Harp", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 36, 96, true, 0.55},
    InstrumentDefinition{"violin_1", "Violin I", ScoreDepartment::Melody, VoiceId::Lead, 55, 103, false, 0.85},
    InstrumentDefinition{"violin_2", "Violin II", ScoreDepartment::Harmony, VoiceId::HarmonicUpper, 55, 100, false, 0.7},
    InstrumentDefinition{"viola", "Viola", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 48, 88, false, 0.65},
    InstrumentDefinition{"cello", "Cello", ScoreDepartment::Melody, VoiceId::Countermelody, 36, 76, false, 0.75},
    InstrumentDefinition{"contrabass", "Contrabass", ScoreDepartment::Harmony, VoiceId::SubBass, 28, 60, false, 0.85},
    InstrumentDefinition{"flute", "Flute", ScoreDepartment::Melody, VoiceId::Lead, 60, 96, false, 0.62},
    InstrumentDefinition{"oboe", "Oboe", ScoreDepartment::Melody, VoiceId::Lead, 58, 91, false, 0.58},
    InstrumentDefinition{"clarinet", "Clarinet", ScoreDepartment::Melody, VoiceId::Countermelody, 50, 94, false, 0.62},
    InstrumentDefinition{"bass_clarinet", "Bass Clarinet", ScoreDepartment::Melody, VoiceId::MovementBass, 34, 74, false, 0.62},
    InstrumentDefinition{"bassoon", "Bassoon", ScoreDepartment::Harmony, VoiceId::MovementBass, 34, 72, false, 0.66},
    InstrumentDefinition{"french_horns", "French Horns", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 41, 82, true, 0.82},
    InstrumentDefinition{"trumpets", "Trumpets", ScoreDepartment::Harmony, VoiceId::HarmonicUpper, 54, 94, true, 0.72},
    InstrumentDefinition{"trombones", "Trombones", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 34, 72, true, 0.78},
    InstrumentDefinition{"choir", "Choir", ScoreDepartment::Harmony, VoiceId::Atmosphere, 43, 84, true, 0.7},
    InstrumentDefinition{"mallets", "Mallets", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 48, 96, true, 0.54},
    InstrumentDefinition{"electric_bass", "Electric Bass", ScoreDepartment::Harmony, VoiceId::MovementBass, 28, 64, false, 0.82},
    InstrumentDefinition{"sub_synth", "Sub Synth", ScoreDepartment::Harmony, VoiceId::SubBass, 24, 52, false, 0.75},
    InstrumentDefinition{"analog_pad", "Analog Pad", ScoreDepartment::Harmony, VoiceId::Atmosphere, 36, 88, true, 0.62},
    InstrumentDefinition{"poly_synth", "Poly Synth", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 45, 96, true, 0.65},
    InstrumentDefinition{"lead_synth", "Lead Synth", ScoreDepartment::Melody, VoiceId::Lead, 48, 96, false, 0.7},
    InstrumentDefinition{"guitar", "Guitar", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 40, 88, true, 0.62},
    InstrumentDefinition{"ambient_texture", "Ambient Texture", ScoreDepartment::Harmony, VoiceId::Atmosphere, 36, 100, true, 0.45},
    InstrumentDefinition{"piccolo", "Piccolo", ScoreDepartment::Melody, VoiceId::Lead, 74, 108, false, 0.45},
    InstrumentDefinition{"alto_flute", "Alto Flute", ScoreDepartment::Melody, VoiceId::Countermelody, 55, 88, false, 0.54},
    InstrumentDefinition{"english_horn", "English Horn", ScoreDepartment::Melody, VoiceId::Countermelody, 52, 84, false, 0.60},
    InstrumentDefinition{"contrabassoon", "Contrabassoon", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 22, 58, false, 0.72},
    InstrumentDefinition{"bass_trombone", "Bass Trombone", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 28, 65, false, 0.76},
    InstrumentDefinition{"tuba", "Tuba", ScoreDepartment::Harmony, VoiceId::SubBass, 24, 60, false, 0.80},
    InstrumentDefinition{"celesta", "Celesta", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 48, 108, true, 0.48},
    InstrumentDefinition{"vibraphone", "Vibraphone", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 53, 89, true, 0.52},
    InstrumentDefinition{"marimba", "Marimba", ScoreDepartment::Harmony, VoiceId::HarmonicPulse, 36, 96, true, 0.58},
    InstrumentDefinition{"tubular_bells", "Tubular Bells", ScoreDepartment::Harmony, VoiceId::HarmonicUpper, 48, 84, false, 0.42},
    InstrumentDefinition{"orchestral_percussion", "Orchestral Percussion", ScoreDepartment::Rhythm, VoiceId::HighPercussion, 35, 81, false, 0.68},
    InstrumentDefinition{"string_ensemble", "String Ensemble", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 36, 100, true, 0.82},
    InstrumentDefinition{"chamber_strings", "Chamber Strings", ScoreDepartment::Harmony, VoiceId::HarmonicUpper, 48, 100, true, 0.64},
    InstrumentDefinition{"brass_ensemble", "Brass Ensemble", ScoreDepartment::Harmony, VoiceId::HarmonicFoundation, 34, 94, true, 0.78},
    InstrumentDefinition{"woodwind_ensemble", "Woodwind Ensemble", ScoreDepartment::Harmony, VoiceId::HarmonicUpper, 48, 96, true, 0.66}
};

bool activeInSection(const InstrumentAssignment& assignment, const SongSection& section) {
    return assignment.activeSections.empty() ||
        std::find(assignment.activeSections.begin(), assignment.activeSections.end(), section.name) !=
            assignment.activeSections.end();
}

const SongSection& sectionAt(const SongPlan& plan, double beat) {
    const auto bar = static_cast<int>(std::floor(beat / plan.beatsPerBar));
    const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [bar](const auto& section) {
        return bar >= section.startBar && bar < section.startBar + section.bars;
    });
    return found == plan.sections.end() ? plan.sections.back() : *found;
}

int fitPitch(int pitch, const InstrumentAssignment& assignment) {
    auto result = pitch + assignment.octaveShift;
    while (result < assignment.minimumPitch && result + 12 <= assignment.maximumPitch) result += 12;
    while (result > assignment.maximumPitch && result - 12 >= assignment.minimumPitch) result -= 12;
    return std::clamp(result, assignment.minimumPitch, assignment.maximumPitch);
}

std::uint64_t mixed(std::uint64_t seed, std::uint64_t salt) noexcept {
    auto value = seed ^ (salt + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double mixedUnit(std::uint64_t seed, std::uint64_t salt) noexcept {
    return static_cast<double>(mixed(seed, salt) >> 11U) * (1.0 / 9007199254740992.0);
}

std::string resolvedFunction(const InstrumentAssignment& assignment) {
    if (!assignment.orchestralFunction.empty() && assignment.orchestralFunction != "body")
        return assignment.orchestralFunction;
    if (assignment.sourceVoice == VoiceId::SubBass || assignment.sourceVoice == VoiceId::MovementBass)
        return "foundation";
    if (assignment.sourceVoice == VoiceId::HarmonicPulse) return "color";
    if (assignment.sourceVoice == VoiceId::HarmonicUpper) return "extension";
    if (assignment.sourceVoice == VoiceId::Atmosphere) return "transition";
    return "body";
}

std::pair<std::string, std::string> defaultLiveSound(std::string_view id) {
    if (id == "kick_drum" || id == "snare_clap" || id == "hi_hats" ||
        id == "latin_percussion" || id == "shakers" || id == "cymbals" ||
        id == "orchestral_percussion") return {"Drum Rack", std::string(id)};
    if (id == "timpani" || id == "taiko_ensemble" || id == "mallets" || id == "marimba" ||
        id == "vibraphone" || id == "tubular_bells" || id == "celesta")
        return {"Sampler", std::string(id) + " acoustic"};
    if (id == "piano" || id == "electric_bass") return {"Electric", std::string(id) + " warm"};
    if (id == "harp" || id == "guitar") return {"Tension", std::string(id) + " expressive"};
    if (id == "sub_synth") return {"Operator", "deep clean sub"};
    if (id == "analog_pad" || id == "poly_synth" || id == "lead_synth")
        return {"Wavetable", std::string(id) + " evolving"};
    if (id == "ambient_texture") return {"Granulator III", "evolving atmospheric texture"};
    if (id.find("string") != std::string_view::npos || id == "violin_1" || id == "violin_2" ||
        id == "viola" || id == "cello" || id == "contrabass" || id == "choir" ||
        id == "french_horns" || id == "trumpets" || id == "trombones" || id == "bass_trombone" ||
        id == "tuba" || id == "brass_ensemble" || id == "flute" || id == "piccolo" ||
        id == "alto_flute" || id == "oboe" || id == "english_horn" || id == "clarinet" ||
        id == "bass_clarinet" || id == "bassoon" || id == "contrabassoon" ||
        id == "woodwind_ensemble") return {"Instrument Rack", std::string(id) + " orchestral"};
    return {"Drift", std::string(id) + " expressive"};
}

const HarmonicChord& chordAt(const SongPlan& plan, const SongSection& section, double beat) {
    const auto localBeat = beat - section.startBar * plan.beatsPerBar;
    const HarmonicEvent* selected = section.harmonicEvents.empty() ? nullptr : &section.harmonicEvents.front();
    for (const auto& event : section.harmonicEvents) {
        const auto eventBeat = event.barOffset * plan.beatsPerBar + event.beatOffset;
        if (eventBeat <= localBeat + 0.0001) selected = &event;
    }
    if (selected != nullptr) {
        const auto found = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(), [&](const auto& chord) {
            return chord.id == selected->chordId;
        });
        if (found != plan.chordPalette.end()) return *found;
    }
    return plan.chordPalette.front();
}

int nearestChordPitch(int pitchClass, int around, const InstrumentAssignment& assignment) {
    auto best = std::clamp(around, assignment.minimumPitch, assignment.maximumPitch);
    auto distance = 1000;
    for (auto pitch = assignment.minimumPitch; pitch <= assignment.maximumPitch; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto candidateDistance = std::abs(pitch - around);
        if (candidateDistance < distance) { distance = candidateDistance; best = pitch; }
    }
    return best;
}

} // namespace

std::span<const InstrumentDefinition> instrumentCatalog() noexcept { return catalog; }

const InstrumentDefinition* instrumentDefinition(std::string_view id) noexcept {
    const auto found = std::find_if(catalog.begin(), catalog.end(), [id](const auto& item) {
        return item.id == id;
    });
    return found == catalog.end() ? nullptr : &*found;
}

InstrumentSoundModel instrumentSoundModel(std::string_view id) noexcept {
    if (id == "kick_drum") return InstrumentSoundModel::Kick;
    if (id == "snare_clap") return InstrumentSoundModel::SnareClap;
    if (id == "hi_hats") return InstrumentSoundModel::Hats;
    if (id == "timpani") return InstrumentSoundModel::Timpani;
    if (id == "taiko_ensemble") return InstrumentSoundModel::Taiko;
    if (id == "latin_percussion") return InstrumentSoundModel::LatinPercussion;
    if (id == "shakers") return InstrumentSoundModel::Shaker;
    if (id == "cymbals") return InstrumentSoundModel::Cymbal;
    if (id == "piano") return InstrumentSoundModel::Piano;
    if (id == "harp") return InstrumentSoundModel::Harp;
    if (id == "violin_1" || id == "violin_2") return InstrumentSoundModel::HighStrings;
    if (id == "viola") return InstrumentSoundModel::MidStrings;
    if (id == "cello" || id == "contrabass") return InstrumentSoundModel::LowStrings;
    if (id == "flute" || id == "piccolo" || id == "alto_flute") return InstrumentSoundModel::Flute;
    if (id == "oboe" || id == "english_horn") return InstrumentSoundModel::Oboe;
    if (id == "clarinet" || id == "bass_clarinet") return InstrumentSoundModel::Clarinet;
    if (id == "bassoon" || id == "contrabassoon") return InstrumentSoundModel::Bassoon;
    if (id == "french_horns") return InstrumentSoundModel::Horns;
    if (id == "trumpets" || id == "trombones" || id == "bass_trombone" || id == "tuba" || id == "brass_ensemble") return InstrumentSoundModel::Brass;
    if (id == "choir") return InstrumentSoundModel::Choir;
    if (id == "mallets" || id == "celesta" || id == "vibraphone" || id == "marimba" || id == "tubular_bells") return InstrumentSoundModel::Mallets;
    if (id == "string_ensemble" || id == "chamber_strings") return InstrumentSoundModel::MidStrings;
    if (id == "woodwind_ensemble") return InstrumentSoundModel::Clarinet;
    if (id == "orchestral_percussion") return InstrumentSoundModel::Timpani;
    if (id == "electric_bass") return InstrumentSoundModel::ElectricBass;
    if (id == "sub_synth") return InstrumentSoundModel::SubSynth;
    if (id == "analog_pad") return InstrumentSoundModel::AnalogPad;
    if (id == "poly_synth") return InstrumentSoundModel::PolySynth;
    if (id == "lead_synth") return InstrumentSoundModel::LeadSynth;
    if (id == "guitar") return InstrumentSoundModel::Guitar;
    if (id == "ambient_texture") return InstrumentSoundModel::Texture;
    return InstrumentSoundModel::Generic;
}

std::vector<InstrumentAssignment> defaultOrchestrationAssignments() {
    constexpr std::array defaults{
        "kick_drum", "snare_clap", "hi_hats", "timpani", "shakers",
        "contrabass", "cello", "piano", "viola", "violin_2", "french_horns",
        "bassoon", "harp", "choir", "string_ensemble", "woodwind_ensemble",
        "violin_1", "clarinet", "flute", "english_horn", "analog_pad", "cymbals"
    };
    std::vector<InstrumentAssignment> result;
    result.reserve(defaults.size());
    for (std::size_t index = 0; index < defaults.size(); ++index) {
        const auto& definition = *instrumentDefinition(defaults[index]);
        result.push_back({"part_" + std::to_string(index + 1), std::string(definition.id),
            std::string(definition.name), definition.preferredVoice,
            definition.department == ScoreDepartment::Rhythm ? "rhythmic function" :
            definition.department == ScoreDepartment::Melody ? "rotating thematic voice" :
            "orchestral support", definition.minimumPitch, definition.maximumPitch, 0,
            0.58, definition.weight * 0.65, 0.16, {}});
        auto& assignment = result.back();
        if (assignment.sourceVoice == VoiceId::SubBass || assignment.sourceVoice == VoiceId::MovementBass)
            assignment.orchestralFunction = "foundation";
        else if (assignment.sourceVoice == VoiceId::HarmonicUpper)
            assignment.orchestralFunction = "extension";
        else if (assignment.instrumentId == "bassoon" || assignment.instrumentId == "woodwind_ensemble")
            assignment.orchestralFunction = "counterpoint";
        else if (assignment.sourceVoice == VoiceId::Atmosphere)
            assignment.orchestralFunction = "transition";
        assignment.articulation = assignment.instrumentId == "harp" ? "detached" :
                                  assignment.instrumentId == "string_ensemble" ? "legato" : "natural";
        assignment.divisiVoices = assignment.instrumentId == "string_ensemble" ? 3 :
                                  assignment.instrumentId == "french_horns" ? 2 : 1;
        const auto liveSound = defaultLiveSound(assignment.instrumentId);
        assignment.liveDevice = liveSound.first;
        assignment.livePresetIntent = liveSound.second;
    }
    return result;
}

OrchestrationReport OrchestrationScore::realize(Pattern& pattern, const SongPlan& plan) {
    OrchestrationReport report;
    report.implicitVoicesPruned = plan.implicitVoicesPruned;
    report.implicitPerformanceNotesPruned = plan.implicitPerformanceNotesPruned;
    if (pattern.notes.empty() || plan.sections.empty()) return report;
    const auto& assignments = plan.instruments;
    if (assignments.empty()) return report;

    pattern.parts.clear();
    pattern.parts.reserve(assignments.size());
    for (std::size_t index = 0; index < assignments.size(); ++index) {
        const auto& assignment = assignments[index];
        const auto* definition = instrumentDefinition(assignment.instrumentId);
        const auto melodicCounterpoint = voiceDefinition(assignment.sourceVoice).family ==
                                          VoiceFamily::Melodic &&
                                          assignment.orchestralFunction == "counterpoint";
        const auto department = melodicCounterpoint ? ScoreDepartment::Melody : definition == nullptr ?
            (isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Rhythm) ? ScoreDepartment::Rhythm :
             isVoiceInFamily(assignment.sourceVoice, VoiceFamily::Melodic) ? ScoreDepartment::Melody :
             ScoreDepartment::Harmony) : definition->department;
        pattern.parts.push_back({static_cast<std::uint16_t>(index + 1), assignment.instrumentId,
            assignment.name, assignment.sourceVoice, department, assignment.role,
            assignment.minimumPitch, assignment.maximumPitch, assignment.prominence,
            instrumentSoundModel(assignment.instrumentId), assignment.orchestralFunction,
            assignment.articulation, assignment.divisiVoices, assignment.liveDevice,
            assignment.livePresetIntent});
        ++report.parts;
        if (department == ScoreDepartment::Rhythm) ++report.rhythmParts;
        else if (department == ScoreDepartment::Melody) ++report.melodyParts;
        else ++report.harmonyParts;
    }

    for (const auto& section : plan.sections) {
        const auto chamberThreshold = 0.52 + plan.orchestrationLanguage.chamberContrast * 0.18;
        if (section.density < chamberThreshold) ++report.chamberSections;
        const auto tuttiThreshold = 0.76 + plan.orchestrationLanguage.tuttiRarity * 0.16;
        if (section.energy >= tuttiThreshold && section.density >= tuttiThreshold - 0.08)
            ++report.tuttiSections;
    }

    std::map<std::pair<int, VoiceId>, std::size_t> foregroundByPhrase;
    std::map<VoiceId, std::size_t> previousForegroundOwner;
    std::map<VoiceId, int> foregroundOwnerRun;
    std::vector<NoteEvent> realized;
    realized.reserve(std::min<std::size_t>(32768, pattern.notes.size() * 2));
    for (const auto& source : pattern.notes) {
        const auto& section = sectionAt(plan, source.startBeat);
        const auto chamberAmount = std::clamp((0.62 - section.density) / 0.62, 0.0, 1.0) *
                                   plan.orchestrationLanguage.chamberContrast;
        const auto ensembleGain = 0.68 + plan.orchestrationLanguage.ensembleScale * 0.52;
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < assignments.size(); ++index) {
            const auto effectiveActivity = std::clamp(assignments[index].activity * ensembleGain *
                (1.0 - chamberAmount * (0.18 + assignments[index].prominence * 0.16)), 0.08, 1.0);
            const auto melodicForeground = source.voice == VoiceId::Lead ||
                                             source.voice == VoiceId::Countermelody;
            const auto passesActivity = melodicForeground ? effectiveActivity >= 0.08 :
                mixedUnit(plan.seed, static_cast<std::uint64_t>(section.startBar * 97) + index * 17U) <=
                    effectiveActivity;
            if (assignments[index].sourceVoice == source.voice && activeInSection(assignments[index], section) &&
                source.pitch >= assignments[index].minimumPitch - 12 &&
                source.pitch <= assignments[index].maximumPitch + 12 &&
                passesActivity)
                candidates.push_back(index);
        }
        if (candidates.empty())
            for (std::size_t index = 0; index < assignments.size(); ++index)
                if (assignments[index].sourceVoice == source.voice && activeInSection(assignments[index], section))
                    candidates.push_back(index);
        const auto hasOrchestralOwner = std::any_of(assignments.begin(), assignments.end(),
            [&](const auto& assignment) { return assignment.sourceVoice == source.voice; });
        if (candidates.empty()) {
            if (hasOrchestralOwner) continue;
            auto note = source;
            note.partId = 0;
            realized.push_back(note);
            continue;
        }
        const auto rotationBars = std::clamp(static_cast<int>(std::lround(
            8.0 - plan.orchestrationLanguage.timbralMotion * 6.0)), 2, 8);
        const auto phrase = static_cast<int>(source.startBeat /
            std::max(4.0, plan.beatsPerBar * static_cast<double>(rotationBars)));
        const auto key = std::pair{phrase, source.voice};
        auto primaryIndex = candidates.front();
        const auto distributeByRegister = source.voice == VoiceId::HarmonicFoundation ||
                                          source.voice == VoiceId::HarmonicUpper;
        if (distributeByRegister && candidates.size() > 1) {
            auto bestCost = 1.0e9;
            for (const auto candidate : candidates) {
                const auto& assignment = assignments[candidate];
                const auto centre = (assignment.minimumPitch + assignment.maximumPitch) * 0.5;
                const auto registerCost = std::abs(fitPitch(source.pitch, assignment) - centre) *
                    (0.55 + plan.orchestrationLanguage.registerSeparation * 0.9);
                const auto cost = registerCost -
                                  assignment.prominence * 5.0 +
                                  mixedUnit(plan.seed, candidate * 313U + static_cast<std::uint64_t>(phrase)) * 2.0;
                if (cost < bestCost) { bestCost = cost; primaryIndex = candidate; }
            }
        } else if (const auto existing = foregroundByPhrase.find(key);
                   existing != foregroundByPhrase.end() &&
                   std::find(candidates.begin(), candidates.end(), existing->second) != candidates.end()) {
            primaryIndex = existing->second;
        } else {
            auto bestScore = -1.0;
            for (const auto candidate : candidates) {
                const auto score = assignments[candidate].prominence * 0.72 +
                    mixedUnit(plan.seed, static_cast<std::uint64_t>(phrase * 37 +
                        static_cast<int>(source.voice) * 11) + candidate * 101U) *
                    (0.18 + plan.orchestrationLanguage.foregroundRotation * 0.55);
                if (score > bestScore) { bestScore = score; primaryIndex = candidate; }
            }
            const auto melodicForeground = source.voice == VoiceId::Lead ||
                                             source.voice == VoiceId::Countermelody;
            const auto previous = previousForegroundOwner.find(source.voice);
            const auto run = foregroundOwnerRun.find(source.voice);
            if (melodicForeground && candidates.size() > 1 &&
                previous != previousForegroundOwner.end() &&
                run != foregroundOwnerRun.end() && run->second >= 2 &&
                primaryIndex == previous->second) {
                auto alternativeScore = -1.0;
                for (const auto candidate : candidates) {
                    if (candidate == previous->second) continue;
                    const auto score = assignments[candidate].prominence * 0.72 +
                        mixedUnit(plan.seed, static_cast<std::uint64_t>(phrase * 37 +
                            static_cast<int>(source.voice) * 11) + candidate * 101U) *
                        (0.18 + plan.orchestrationLanguage.foregroundRotation * 0.55);
                    if (score > alternativeScore) {
                        alternativeScore = score;
                        primaryIndex = candidate;
                    }
                }
            }
            foregroundByPhrase[key] = primaryIndex;
            if (melodicForeground) {
                if (previous != previousForegroundOwner.end() && previous->second == primaryIndex)
                    ++foregroundOwnerRun[source.voice];
                else {
                    previousForegroundOwner[source.voice] = primaryIndex;
                    foregroundOwnerRun[source.voice] = 1;
                }
            }
            ++report.foregroundChanges;
        }
        const auto& primary = assignments[primaryIndex];
        auto note = source;
        // GM percussion pitches describe articulations, not musical register. Octave fitting
        // a drum lane can silently turn a clave into a kick or a hat into a tom.
        note.pitch = isVoiceInFamily(source.voice, VoiceFamily::Rhythm) ||
                     source.voice == VoiceId::Transitions
            ? source.pitch : fitPitch(note.pitch, primary);
        note.partId = static_cast<std::uint16_t>(primaryIndex + 1);
        realized.push_back(note);
        ++report.notesAssigned;

        const auto climaxThreshold = 0.74 + plan.orchestrationLanguage.tuttiRarity * 0.16;
        const auto climax = section.energy > climaxThreshold && section.density > climaxThreshold - 0.10;
        const auto restraint = 0.30 + (1.0 - plan.orchestrationLanguage.doublingRestraint) * 0.70;
        const auto rarity = 0.42 + (1.0 - plan.orchestrationLanguage.tuttiRarity) * 0.58;
        const auto doubleProbability = primary.doubling * restraint * rarity;
        const auto allowDouble = climax && candidates.size() > 1 && primary.doubling > 0.05 &&
            mixedUnit(plan.seed, static_cast<std::uint64_t>(std::llround(source.startBeat * 16.0)) +
                               static_cast<std::uint64_t>(source.pitch * 131)) < doubleProbability;
        if (allowDouble && realized.size() < 32768) {
            const auto primaryPosition = static_cast<std::size_t>(
                std::distance(candidates.begin(), std::find(candidates.begin(), candidates.end(), primaryIndex)));
            const auto doubleIndex = candidates[(primaryPosition + 1) % candidates.size()];
            const auto& doubling = assignments[doubleIndex];
            auto doubled = source;
            doubled.pitch = isVoiceInFamily(source.voice, VoiceFamily::Rhythm) ||
                            source.voice == VoiceId::Transitions
                ? source.pitch : fitPitch(doubled.pitch, doubling);
            doubled.velocity = std::clamp(doubled.velocity - 8, 1, 127);
            doubled.partId = static_cast<std::uint16_t>(doubleIndex + 1);
            if (doubled.partId != note.partId || doubled.pitch != note.pitch) {
                realized.push_back(doubled);
                ++report.notesDoubled;
            }
        }
    }

    // Deep orchestration writes independent instrumental material after ownership has
    // been assigned. These are not unison clones: each eligible part follows its own
    // voice-led chord tone, rhythmic rate, register and rest policy.
    if (!plan.chordPalette.empty()) {
        std::vector<std::size_t> harmonicAssignments;
        for (std::size_t index = 0; index < assignments.size(); ++index) {
            const auto* definition = instrumentDefinition(assignments[index].instrumentId);
            if (definition != nullptr && definition->department == ScoreDepartment::Harmony)
                harmonicAssignments.push_back(index);
        }
        for (const auto& section : plan.sections) {
            const auto sectionIndex = static_cast<int>(&section - plan.sections.data());
            const auto explicitlyOwned = PerformanceScoreEngine::ownedVoicesForSection(
                plan.performanceScore, sectionIndex);
            const auto sectionStart = section.startBar * plan.beatsPerBar;
            const auto sectionEnd = std::min(pattern.lengthBeats,
                (section.startBar + section.bars) * plan.beatsPerBar);
            const auto desiredCast = std::clamp(static_cast<int>(std::lround(
                3.0 + section.density * (5.0 + plan.orchestrationLanguage.ensembleScale * 8.0))),
                3, 16);
            std::vector<std::size_t> cast;
            for (const auto index : harmonicAssignments) {
                const auto& assignment = assignments[index];
                if (!activeInSection(assignment, section)) continue;
                const auto score = assignment.prominence * 0.55 + assignment.activity * 0.30 +
                    mixedUnit(plan.seed, static_cast<std::uint64_t>(section.startBar * 977 + index * 41)) * 0.15;
                if (score >= 0.24) cast.push_back(index);
            }
            std::sort(cast.begin(), cast.end(), [&](auto left, auto right) {
                return assignments[left].prominence > assignments[right].prominence;
            });
            if (cast.size() > static_cast<std::size_t>(desiredCast)) cast.resize(desiredCast);

            for (std::size_t castOrdinal = 0; castOrdinal < cast.size(); ++castOrdinal) {
                const auto assignmentIndex = cast[castOrdinal];
                const auto& assignment = assignments[assignmentIndex];
                const auto sourceVoiceIndex = static_cast<std::size_t>(assignment.sourceVoice);
                if (sourceVoiceIndex < explicitlyOwned.size() && explicitlyOwned[sourceVoiceIndex])
                    continue;
                const auto function = resolvedFunction(assignment);
                auto existing = std::count_if(realized.begin(), realized.end(), [&](const auto& note) {
                    return note.partId == static_cast<std::uint16_t>(assignmentIndex + 1) &&
                           note.startBeat >= sectionStart && note.startBeat < sectionEnd;
                });
                const auto ownsIndependentLine = function == "counterpoint" || function == "color" ||
                                                 function == "transition";
                if (!ownsIndependentLine) continue;
                if (ownsIndependentLine && existing > 0) {
                    realized.erase(std::remove_if(realized.begin(), realized.end(), [&](const auto& note) {
                        return note.partId == static_cast<std::uint16_t>(assignmentIndex + 1) &&
                               note.startBeat >= sectionStart && note.startBeat < sectionEnd;
                    }), realized.end());
                    existing = 0;
                }
                const auto minimumMaterial = function == "counterpoint" ? section.bars * 2 :
                                             function == "color" ? std::max(1, section.bars / 2) :
                                             1;
                if (existing >= minimumMaterial) continue;

                const auto step = function == "counterpoint"
                    ? std::max(0.5, plan.beatsPerBar * (0.5 - plan.orchestrationLanguage.counterpointActivity * 0.25))
                    : function == "color" || function == "transition" ? plan.beatsPerBar * 2.0
                    : plan.beatsPerBar;
                auto previousPitch = std::clamp((assignment.minimumPitch + assignment.maximumPitch) / 2,
                                                assignment.minimumPitch, assignment.maximumPitch);
                auto ordinal = 0;
                for (auto beat = sectionStart; beat < sectionEnd && realized.size() < 32768; beat += step, ++ordinal) {
                    const auto restProbability = function == "color" ? 0.52 : function == "counterpoint" ? 0.24 : 0.14;
                    if (mixedUnit(plan.seed, static_cast<std::uint64_t>(assignmentIndex * 8191 +
                            section.startBar * 131 + ordinal)) < restProbability * (1.0 - section.density * 0.55))
                        continue;
                    const auto& chord = chordAt(plan, section, beat);
                    if (chord.pitchClasses.empty()) continue;
                    auto toneIndex = (castOrdinal + static_cast<std::size_t>(ordinal)) % chord.pitchClasses.size();
                    if (function == "counterpoint" && ordinal % 2 != 0)
                        toneIndex = (chord.pitchClasses.size() - 1 - toneIndex) % chord.pitchClasses.size();
                    const auto target = nearestChordPitch(chord.pitchClasses[toneIndex], previousPitch, assignment);
                    const auto durationScale = assignment.articulation == "staccato" ? 0.42 :
                                               assignment.articulation == "detached" ? 0.68 :
                                               assignment.articulation == "sustained" ? 0.96 : 0.84;
                    const auto duration = std::min(sectionEnd - beat, step * durationScale);
                    realized.push_back({beat, std::max(1.0 / 64.0, duration), target,
                        std::clamp(static_cast<int>(50 + section.energy * 38 + assignment.prominence * 18), 32, 112),
                        voiceDefinition(assignment.sourceVoice).midiChannel, assignment.sourceVoice,
                        static_cast<std::uint16_t>(assignmentIndex + 1)});
                    ++report.independentNotes;
                    previousPitch = target;
                    if (function == "counterpoint") ++report.contrapuntalParts;

                    const auto divisi = std::clamp(assignment.divisiVoices, 1, 4);
                    if (divisi > 1 && section.density > 0.58 && function != "counterpoint") {
                        for (auto division = 1; division < divisi && realized.size() < 32768; ++division) {
                            const auto pc = chord.pitchClasses[(toneIndex + static_cast<std::size_t>(division)) % chord.pitchClasses.size()];
                            const auto dividedPitch = nearestChordPitch(pc, target + division * 5, assignment);
                            if (dividedPitch == target) continue;
                            realized.push_back({beat, std::max(1.0 / 64.0, duration), dividedPitch,
                                std::clamp(static_cast<int>(44 + section.energy * 30), 28, 96),
                                voiceDefinition(assignment.sourceVoice).midiChannel, assignment.sourceVoice,
                                static_cast<std::uint16_t>(assignmentIndex + 1)});
                            ++report.divisiNotes;
                        }
                    }
                }
            }
        }
    }
    pattern.notes = std::move(realized);
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    // A repeated pitch on one DAW part must end before its retrigger. Standard MIDI cannot
    // represent overlapping ownership of the same channel/pitch pair unambiguously.
    std::map<std::tuple<std::uint16_t, int, int>, double> nextOnset;
    const auto electronicCore = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    for (auto iterator = pattern.notes.rbegin(); iterator != pattern.notes.rend(); ++iterator) {
        auto& note = *iterator;
        const auto key = std::tuple{note.partId, note.channel, note.pitch};
        if (const auto next = nextOnset.find(key); next != nextOnset.end() && note.endBeat() >= next->second)
            note.durationBeats = std::max(1.0 / 960.0, next->second - note.startBeat - 1.0 / 960.0);
        const auto maximumSustain = note.voice == VoiceId::Atmosphere
            ? plan.beatsPerBar * 2.0
            : (note.voice == VoiceId::HarmonicFoundation || note.voice == VoiceId::HarmonicUpper)
                ? plan.beatsPerBar * 1.05 : note.durationBeats;
        note.durationBeats = std::min(note.durationBeats, maximumSustain);
        if (electronicCore) {
            const auto roleMaximum = note.voice == VoiceId::HarmonicPulse ? plan.beatsPerBar * 0.25
                : note.voice == VoiceId::HarmonicFoundation
                    ? std::max(0.25, plan.beatsPerBar - 1.0 / 16.0)
                : note.voice == VoiceId::CoreDrums || note.voice == VoiceId::SnareClap ||
                  note.voice == VoiceId::ClosedHats || note.voice == VoiceId::OpenHatsShaker ? 0.25
                : note.voice == VoiceId::LowPercussion || note.voice == VoiceId::HighPercussion ? 0.50
                : note.voice == VoiceId::Transitions ? 1.0 : note.durationBeats;
            note.durationBeats = std::min(note.durationBeats, roleMaximum);
        }
        nextOnset[key] = note.startBeat;
    }
    // Orchestration critic: enforce playable chord sizes and keep the low register
    // transparent. This is deliberately post-realization so it reviews the MIDI that
    // the user actually exports, not merely the abstract plan.
    const auto beforeReview = pattern.notes.size();
    std::map<std::pair<std::uint16_t, long long>, int> simultaneous;
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (note.partId == 0 || note.partId > assignments.size()) return false;
        const auto& assignment = assignments[note.partId - 1];
        const auto* definition = instrumentDefinition(assignment.instrumentId);
        const auto limit = definition != nullptr && definition->polyphonic
            ? std::max(4, assignment.divisiVoices * 2) : std::max(1, assignment.divisiVoices);
        auto& count = simultaneous[{note.partId, std::llround(note.startBeat * 960.0)}];
        return ++count > limit;
    }), pattern.notes.end());
    report.balanceRemovals += beforeReview - pattern.notes.size();
    std::size_t lowComparisons{};
    std::size_t lowConflicts{};
    std::map<long long, std::vector<std::size_t>> lowByOnset;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        auto& upper = pattern.notes[index];
        if (upper.pitch >= 52 || upper.partId == 0) continue;
        auto& onset = lowByOnset[std::llround(upper.startBeat * 960.0)];
        for (const auto previous : onset) {
            const auto& lower = pattern.notes[previous];
            if (lower.partId == upper.partId) continue;
            ++lowComparisons;
            if (std::abs(lower.pitch - upper.pitch) >= 5) continue;
            const auto& assignment = assignments[upper.partId - 1];
            const auto voiceMaximum = voiceDefinition(upper.voice).maximumPitch;
            if (upper.pitch + 12 <= std::min(assignment.maximumPitch, voiceMaximum)) {
                upper.pitch += 12;
                ++report.registerRepairs;
            } else ++lowConflicts;
        }
        onset.push_back(index);
    }
    report.registerClarity = lowComparisons == 0 ? 1.0 :
        std::clamp(1.0 - static_cast<double>(lowConflicts) / lowComparisons, 0.0, 1.0);
    for (auto& note : pattern.notes) {
        if (note.partId == 0 || note.partId > assignments.size() ||
            isVoiceInFamily(note.voice, VoiceFamily::Rhythm) || note.voice == VoiceId::Transitions) continue;
        const auto& assignment = assignments[note.partId - 1];
        const auto& voice = voiceDefinition(note.voice);
        const auto minimum = std::max(assignment.minimumPitch, voice.minimumPitch);
        const auto maximum = std::min(assignment.maximumPitch, voice.maximumPitch);
        if (minimum > maximum) continue;
        const auto original = note.pitch;
        while (note.pitch < minimum && note.pitch + 12 <= maximum) note.pitch += 12;
        while (note.pitch > maximum && note.pitch - 12 >= minimum) note.pitch -= 12;
        note.pitch = std::clamp(note.pitch, minimum, maximum);
        if (note.pitch != original) ++report.registerRepairs;
    }
    const auto populatedParts = std::count_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
        return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) { return note.partId == part.id; });
    });
    report.familyBalance = pattern.parts.empty() ? 0.0 :
        static_cast<double>(populatedParts) / static_cast<double>(pattern.parts.size());
    return report;
}

std::size_t OrchestrationScore::enforcePublishedRegisters(Pattern& pattern) {
    std::size_t repaired{};
    for (auto& note : pattern.notes) {
        if (note.partId == 0 || isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.voice == VoiceId::Transitions) continue;
        const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& candidate) {
            return candidate.id == note.partId;
        });
        if (part == pattern.parts.end()) continue;
        const auto& voice = voiceDefinition(part->sourceVoice);
        const auto minimum = std::max(part->minimumPitch, voice.minimumPitch);
        const auto maximum = std::min(part->maximumPitch, voice.maximumPitch);
        if (minimum > maximum) continue;
        const auto original = note.pitch;
        while (note.pitch < minimum && note.pitch + 12 <= maximum) note.pitch += 12;
        while (note.pitch > maximum && note.pitch - 12 >= minimum) note.pitch -= 12;
        note.pitch = std::clamp(note.pitch, minimum, maximum);
        if (note.pitch != original) ++repaired;
    }
    return repaired;
}

void OrchestrationScore::applyPartExpression(Pattern& pattern, const SongPlan& plan,
                                             OrchestrationReport* report) {
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        const auto& instrument = plan.instruments[index];
        const auto partId = static_cast<std::uint16_t>(index + 1);
        const auto channel = voiceDefinition(instrument.sourceVoice).midiChannel;
        for (const auto& section : plan.sections) {
            const auto beat = section.startBar * plan.beatsPerBar;
            if (!activeInSection(instrument, section) || beat >= pattern.lengthBeats) continue;
            const auto base = std::clamp(static_cast<int>(38 + section.energy * 48 + instrument.prominence * 28), 1, 127);
            pattern.controls.push_back({beat, 11, base, channel, instrument.sourceVoice, partId});
            pattern.controls.push_back({beat, 1,
                instrument.articulation == "tremolo" || instrument.articulation == "swelling" ? 76 : 28,
                channel, instrument.sourceVoice, partId});
            pattern.controls.push_back({beat, 74,
                instrument.articulation == "staccato" || instrument.articulation == "pizzicato" ? 88 : 54,
                channel, instrument.sourceVoice, partId});
            if (report != nullptr) ++report->articulationChanges;
        }
    }
}

} // namespace pulso
