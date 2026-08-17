#include "SongComposer.h"

#include "Random.h"
#include "HarmonyEngine.h"
#include "ElectronicProductionDirector.h"
#include "MusicalCritic.h"
#include "OrchestrationScore.h"
#include "PhraseDirector.h"
#include "PhraseComposer.h"
#include "RhythmEngine.h"
#include "Scale.h"
#include "TonalContract.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace pulso {
namespace {

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

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

void materializeNamedSoundWorld(SongPlan& plan) {
    const auto text = lowerText(plan.timbrePalette.description + " " + plan.timbrePalette.material);
    constexpr std::array references{
        std::pair{"alto-flute", "alto_flute"}, std::pair{"alto flute", "alto_flute"},
        std::pair{"felt piano", "piano"}, std::pair{"electric bass", "electric_bass"},
        std::pair{"bass clarinet", "bass_clarinet"}, std::pair{"english horn", "english_horn"},
        std::pair{"french horn", "french_horns"}, std::pair{"chamber strings", "chamber_strings"},
        std::pair{"string ensemble", "string_ensemble"}, std::pair{"woodwind", "woodwind_ensemble"},
        std::pair{"flute", "flute"}, std::pair{"piano", "piano"},
        std::pair{"violin", "violin_1"}, std::pair{"viola", "viola"},
        std::pair{"cello", "cello"}, std::pair{"clarinet", "clarinet"},
        std::pair{"oboe", "oboe"}, std::pair{"bassoon", "bassoon"},
        std::pair{"trumpet", "trumpets"}, std::pair{"trombone", "trombones"},
        std::pair{"choir", "choir"}, std::pair{"harp", "harp"},
        std::pair{"marimba", "marimba"}, std::pair{"vibraphone", "vibraphone"},
        std::pair{"guitar", "guitar"}, std::pair{"analog pad", "analog_pad"},
        std::pair{"synth lead", "lead_synth"}, std::pair{"sub bass", "sub_synth"}
    };
    plan.timbrePalette.essentialInstrumentIds.clear();
    std::vector<std::string> claimedTerms;
    for (const auto& [term, id] : references) {
        if (text.find(term) == std::string::npos ||
            std::any_of(claimedTerms.begin(), claimedTerms.end(), [&](const auto& claimed) {
                return std::string(term).find(claimed) != std::string::npos ||
                       claimed.find(term) != std::string::npos;
            })) continue;
        claimedTerms.emplace_back(term);
        if (std::find(plan.timbrePalette.essentialInstrumentIds.begin(),
                      plan.timbrePalette.essentialInstrumentIds.end(), id) ==
            plan.timbrePalette.essentialInstrumentIds.end())
            plan.timbrePalette.essentialInstrumentIds.emplace_back(id);
        const auto existing = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& instrument) { return instrument.instrumentId == id; });
        if (existing != plan.instruments.end()) {
            existing->activity = std::max(existing->activity, 0.82);
            existing->prominence = std::max(existing->prominence, 0.78);
            continue;
        }
        const auto* definition = instrumentDefinition(id);
        if (definition == nullptr || plan.instruments.size() >= 48 || plan.instrumentCastAuthored)
            continue;
        InstrumentAssignment addition;
        addition.id = "world_" + std::string(id);
        addition.instrumentId = id;
        addition.name = "Essential " + std::string(definition->name);
        addition.sourceVoice = definition->preferredVoice;
        addition.role = "Required identity explicitly named by the AI sound world";
        addition.minimumPitch = definition->minimumPitch;
        addition.maximumPitch = definition->maximumPitch;
        addition.activity = 0.92;
        addition.prominence = 0.88;
        addition.doubling = 0.08;
        addition.orchestralFunction = definition->department == ScoreDepartment::Melody
            ? "counterpoint" : "color";
        addition.articulation = "natural";
        addition.divisiVoices = 1;
        addition.liveDevice = definition->department == ScoreDepartment::Rhythm
            ? "Drum Rack" : "Instrument Rack";
        addition.livePresetIntent = std::string(term) + " matching the shared sound world";
        plan.instruments.push_back(std::move(addition));
    }
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
    for (auto& expression : source.expressions) {
        expression.beat += beatOffset;
        if (expression.beat < songLength) destination.expressions.push_back(expression);
    }
    for (auto& marker : source.markers) {
        marker.beat += beatOffset;
        if (marker.beat < songLength) destination.markers.push_back(std::move(marker));
    }
}

struct ContinuityRepair {
    std::size_t windowsRepaired{};
    double longestBefore{};
    double longestAfter{};
};

double longestGlobalSilence(const Pattern& song) {
    auto notes = song.notes;
    std::sort(notes.begin(), notes.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    auto soundingUntil = 0.0;
    auto longest = 0.0;
    for (const auto& note : notes) {
        longest = std::max(longest, note.startBeat - soundingUntil);
        soundingUntil = std::max(soundingUntil, std::min(song.lengthBeats, note.endBeat()));
    }
    return std::max(longest, song.lengthBeats - soundingUntil);
}

bool declaredFullSilence(const SongSection& section) {
    const auto text = lowerText(section.function + " " + section.motifTreatment + " " +
                                section.harmonicDirection);
    if (text.find("without full silence") != std::string::npos ||
        text.find("without complete silence") != std::string::npos ||
        text.find("no full silence") != std::string::npos ||
        text.find("sin silencio total") != std::string::npos) return false;
    return text.find("full silence") != std::string::npos ||
           text.find("complete silence") != std::string::npos ||
           text.find("silencio total") != std::string::npos;
}

const SongSection* sectionAtBeat(const SongPlan& plan, double beat) {
    const auto bar = static_cast<int>(std::floor(beat / plan.beatsPerBar));
    const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
        return bar >= section.startBar && bar < section.startBar + section.bars;
    });
    return found == plan.sections.end() ? nullptr : &*found;
}

ContinuityRepair repairUnintendedGlobalSilence(Pattern& song, const SongPlan& plan,
                                                std::span<const HarmonicWindow> harmony) {
    ContinuityRepair report;
    report.longestBefore = longestGlobalSilence(song);
    const auto electronic = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    auto notes = song.notes;
    std::sort(notes.begin(), notes.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    std::vector<std::pair<double, double>> gaps;
    auto soundingUntil = 0.0;
    for (const auto& note : notes) {
        const auto* section = sectionAtBeat(plan, (soundingUntil + note.startBeat) * 0.5);
        const auto maximumSilence = !electronic ? plan.beatsPerBar * 2.0 :
            section != nullptr && declaredFullSilence(*section) ? plan.beatsPerBar * 2.0 :
            section != nullptr && section->energy < 0.28 ? plan.beatsPerBar :
            plan.beatsPerBar * 0.5;
        if (note.startBeat - soundingUntil > maximumSilence + 0.000001)
            gaps.emplace_back(soundingUntil, note.startBeat);
        soundingUntil = std::max(soundingUntil, std::min(song.lengthBeats, note.endBeat()));
    }
    if (song.lengthBeats > soundingUntil) {
        const auto* section = sectionAtBeat(plan, (soundingUntil + song.lengthBeats) * 0.5);
        const auto maximumSilence = !electronic ? plan.beatsPerBar * 2.0 :
            section != nullptr && declaredFullSilence(*section) ? plan.beatsPerBar * 2.0 :
            section != nullptr && section->energy < 0.28 ? plan.beatsPerBar :
            plan.beatsPerBar * 0.5;
        if (song.lengthBeats - soundingUntil > maximumSilence + 0.000001)
            gaps.emplace_back(soundingUntil, song.lengthBeats);
    }

    const InstrumentPart* continuityPart = nullptr;
    for (const auto& part : song.parts) {
        if (part.sourceVoice == VoiceId::Atmosphere) {
            continuityPart = &part;
            break;
        }
        if (continuityPart == nullptr && part.department == ScoreDepartment::Harmony)
            continuityPart = &part;
    }
    const auto continuityVoice = continuityPart == nullptr ? VoiceId::Atmosphere
                                                            : continuityPart->sourceVoice;
    const auto& voice = voiceDefinition(continuityVoice);
    const auto minimumPitch = continuityPart == nullptr ? voice.minimumPitch
        : std::max(voice.minimumPitch, continuityPart->minimumPitch);
    const auto maximumPitch = continuityPart == nullptr ? voice.maximumPitch
        : std::min(voice.maximumPitch, continuityPart->maximumPitch);
    const auto partId = continuityPart == nullptr ? std::uint16_t{} : continuityPart->id;
    for (const auto& [start, end] : gaps) {
        const auto* section = sectionAtBeat(plan, (start + end) * 0.5);
        if (section != nullptr && declaredFullSilence(*section)) continue;
        const auto maximumSilence = !electronic ? plan.beatsPerBar * 2.0 :
            section != nullptr && section->energy < 0.28 ? plan.beatsPerBar :
            plan.beatsPerBar * 0.5;
        ++report.windowsRepaired;
        for (auto beat = start + maximumSilence * 0.75; beat < end;
             beat += maximumSilence * 0.75) {
            auto pitchClass = 0;
            if (!harmony.empty()) {
                const auto found = std::find_if(harmony.begin(), harmony.end(), [&](const auto& window) {
                    return beat >= window.startBeat && beat < window.endBeat;
                });
                pitchClass = (found == harmony.end() ? harmony.back() : *found).rootPitchClass;
            }
            song.notes.push_back({beat, std::min(maximumSilence * 0.25, end - beat),
                nearestPitchClass(pitchClass, 60, minimumPitch, maximumPitch),
                38, voice.midiChannel, continuityVoice, partId, false,
                NoteOrigin::LocalContinuity});
        }
    }
    report.longestAfter = longestGlobalSilence(song);
    return report;
}

struct EarlyRhythmRepair { std::size_t notesCreated{}; };

EarlyRhythmRepair repairEarlyClubRhythm(Pattern& song, const SongPlan& plan) {
    EarlyRhythmRepair report;
    const auto club = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid) &&
        (plan.productionLanguage.clubFocus >= 0.55 || plan.productionLanguage.djUtility >= 0.48);
    if (!club || plan.beatsPerBar <= 0.0) return report;
    const auto horizon = std::min(song.lengthBeats, plan.beatsPerBar * 16.0);
    if (std::any_of(song.notes.begin(), song.notes.end(), [&](const auto& note) {
            return note.startBeat < horizon &&
                   (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
                    note.voice == VoiceId::Transitions);
        })) return report;
    const auto part = std::find_if(song.parts.begin(), song.parts.end(), [](const auto& candidate) {
        return candidate.department == ScoreDepartment::Rhythm &&
               candidate.orchestralFunction != "transition" &&
               (candidate.sourceVoice == VoiceId::ClosedHats ||
                candidate.sourceVoice == VoiceId::OpenHatsShaker);
    });
    if (part == song.parts.end()) return report;
    const auto startBar = std::min(8, std::max(0, static_cast<int>(horizon / plan.beatsPerBar) - 8));
    for (auto bar = startBar; bar < 16 && (bar + 0.625) * plan.beatsPerBar < horizon; bar += 2) {
        song.notes.push_back({bar * plan.beatsPerBar + plan.beatsPerBar * 0.625, 0.125,
            part->sourceVoice == VoiceId::ClosedHats ? 42 : 46,
            34 + (bar % 4) * 3, voiceDefinition(part->sourceVoice).midiChannel,
            part->sourceVoice, part->id, false, NoteOrigin::LocalContinuity});
        ++report.notesCreated;
    }
    return report;
}

struct StructuralContinuityRepair {
    std::size_t windowsRepaired{};
    std::size_t notesCreated{};
};

const InstrumentPart* partForVoice(const Pattern& song, VoiceId voice,
                                   ScoreDepartment fallbackDepartment,
                                   std::span<const std::uint16_t> excluded = {}) {
    const auto available = [&](const InstrumentPart& part) {
        return std::find(excluded.begin(), excluded.end(), part.id) == excluded.end();
    };
    const auto exact = std::find_if(song.parts.begin(), song.parts.end(), [&](const auto& part) {
        return part.sourceVoice == voice && available(part);
    });
    if (exact != song.parts.end()) return &*exact;
    const auto fallback = std::find_if(song.parts.begin(), song.parts.end(), [&](const auto& part) {
        return part.department == fallbackDepartment && available(part);
    });
    return fallback == song.parts.end() ? nullptr : &*fallback;
}

const HarmonicWindow* harmonyAt(std::span<const HarmonicWindow> harmony, double beat) {
    if (harmony.empty()) return nullptr;
    const auto found = std::find_if(harmony.begin(), harmony.end(), [&](const auto& window) {
        return beat >= window.startBeat && beat < window.endBeat;
    });
    return found == harmony.end() ? &harmony.back() : &*found;
}

StructuralContinuityRepair repairSparseStructuralWindows(
    Pattern& song, const SongPlan& plan, std::span<const HarmonicWindow> harmony) {
    StructuralContinuityRepair report;
    const auto electronic = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    if (!electronic || plan.beatsPerBar <= 0.0) return report;
    const auto windowBeats = plan.beatsPerBar * 8.0;
    for (double start = 0.0; start + windowBeats <= song.lengthBeats + 0.001;
         start += windowBeats) {
        std::set<std::uint16_t> activeParts;
        std::set<VoiceId> activeUnassigned;
        for (const auto& note : song.notes) {
            if (note.startBeat >= start + windowBeats || note.endBeat() <= start ||
                isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
                note.voice == VoiceId::Transitions) continue;
            if (note.partId != 0) activeParts.insert(note.partId);
            else activeUnassigned.insert(note.voice);
        }
        const auto activeLayers = activeParts.size() + activeUnassigned.size();
        if (activeLayers > 1) continue;

        std::vector<std::uint16_t> excluded(activeParts.begin(), activeParts.end());
        const auto* harmonicPart = partForVoice(song, VoiceId::HarmonicFoundation,
                                                ScoreDepartment::Harmony, excluded);
        if (harmonicPart != nullptr) excluded.push_back(harmonicPart->id);
        const auto* melodicPart = partForVoice(song, VoiceId::Lead,
                                               ScoreDepartment::Melody, excluded);
        if (melodicPart == nullptr)
            melodicPart = partForVoice(song, VoiceId::Countermelody,
                                       ScoreDepartment::Melody, excluded);
        if (harmonicPart == nullptr || melodicPart == nullptr) continue;

        ++report.windowsRepaired;
        // A quiet harmonic breath every two bars keeps the tonal argument alive without
        // turning a breakdown back into a drop.
        for (auto barOffset = 0; barOffset < 8; barOffset += 2) {
            const auto beat = start + barOffset * plan.beatsPerBar;
            const auto* window = harmonyAt(harmony, beat);
            const auto pitchClass = window == nullptr ? plan.rootPitchClass
                                                       : window->rootPitchClass;
            const auto pitch = nearestPitchClass(pitchClass, 55,
                harmonicPart->minimumPitch, harmonicPart->maximumPitch);
            song.notes.push_back({beat, plan.beatsPerBar * 1.50, pitch,
                30 + barOffset, voiceDefinition(harmonicPart->sourceVoice).midiChannel,
                harmonicPart->sourceVoice, harmonicPart->id, false,
                NoteOrigin::LocalContinuity});
            ++report.notesCreated;
        }
        // Two sparse statements recall the global DNA. Register and chord repair remain
        // downstream, so the fragment adapts to the current harmony instead of pasting a
        // foreign phrase into the breakdown.
        const auto motifSize = std::min<std::size_t>(3, plan.motifIntervals.size());
        for (const auto statementBar : {1, 5}) {
            for (std::size_t index = 0; index < motifSize; ++index) {
                const auto beat = start + statementBar * plan.beatsPerBar +
                                  (1.0 + static_cast<double>(index) * 0.75);
                const auto pitchClass = positiveModulo(plan.rootPitchClass +
                    plan.motifIntervals[index], 12);
                const auto pitch = nearestPitchClass(pitchClass, 72,
                    melodicPart->minimumPitch, melodicPart->maximumPitch);
                song.notes.push_back({beat, index + 1 == motifSize ? 0.75 : 0.50,
                    pitch, 39 + static_cast<int>(index) * 5,
                    voiceDefinition(melodicPart->sourceVoice).midiChannel,
                    melodicPart->sourceVoice, melodicPart->id, false,
                    NoteOrigin::LocalContinuity});
                ++report.notesCreated;
            }
        }
    }
    return report;
}

struct ForegroundContinuityRepair {
    std::size_t windowsRepaired{};
    std::size_t notesCreated{};
};

ForegroundContinuityRepair repairExtendedForegroundAbsence(
    Pattern& song, const SongPlan& plan, std::span<const HarmonicWindow> harmony) {
    ForegroundContinuityRepair report;
    const auto electronic = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    if (!electronic || plan.beatsPerBar <= 0.0) return report;

    std::vector<const InstrumentPart*> melodicParts;
    for (const auto& part : song.parts)
        if (part.department == ScoreDepartment::Melody)
            melodicParts.push_back(&part);
    if (melodicParts.empty()) return report;

    const auto windowBeats = plan.beatsPerBar * 8.0;
    auto silentWindows = 0;
    auto windowIndex = 0;
    for (double start = 0.0; start + windowBeats <= song.lengthBeats + 0.001;
         start += windowBeats, ++windowIndex) {
        const auto melodic = std::any_of(song.notes.begin(), song.notes.end(), [&](const auto& note) {
            if (note.startBeat >= start + windowBeats || note.endBeat() <= start) return false;
            const auto part = std::find_if(song.parts.begin(), song.parts.end(), [&](const auto& candidate) {
                return candidate.id == note.partId;
            });
            return (part != song.parts.end() && part->department == ScoreDepartment::Melody) ||
                   note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody;
        });
        if (melodic) { silentWindows = 0; continue; }
        if (++silentWindows < 2) continue;

        // Preserve one complete eight-bar reduction. In the following window a different
        // foreground instrument recalls only the motif, so the arrangement breathes
        // without becoming a 32-second bed with no narrative voice.
        const auto ownerIndex = static_cast<std::size_t>(positiveModulo(
            windowIndex + static_cast<int>(plan.seed % melodicParts.size()),
            static_cast<int>(melodicParts.size())));
        const auto* owner = melodicParts[ownerIndex];
        const auto motifSize = std::min<std::size_t>(4, plan.motifIntervals.size());
        for (std::size_t index = 0; index < motifSize; ++index) {
            const auto beat = start + plan.beatsPerBar + 0.75 + index * 0.75;
            const auto* window = harmonyAt(harmony, beat);
            const auto centre = window == nullptr ? plan.rootPitchClass : window->rootPitchClass;
            const auto pitchClass = positiveModulo(centre + plan.motifIntervals[index], 12);
            const auto pitch = nearestPitchClass(pitchClass,
                (owner->minimumPitch + owner->maximumPitch) / 2,
                owner->minimumPitch, owner->maximumPitch);
            song.notes.push_back({beat, index + 1 == motifSize ? 0.75 : 0.50, pitch,
                43 + static_cast<int>(index) * 5,
                voiceDefinition(owner->sourceVoice).midiChannel,
                owner->sourceVoice, owner->id, false, NoteOrigin::LocalContinuity});
            ++report.notesCreated;
        }
        ++report.windowsRepaired;
        silentWindows = 0;
    }
    return report;
}

struct AudibleDurationRepair {
    std::size_t durationsRepaired{};
    std::size_t notesRemoved{};
};

double audibleDurationFloor(const Pattern& song, const NoteEvent& note) {
    const auto found = std::find_if(song.parts.begin(), song.parts.end(), [&](const auto& part) {
        return part.id == note.partId;
    });
    const auto catalog = found == song.parts.end() ? std::string{} : found->catalogId;
    if (catalog == "analog_pad" || catalog == "ambient_texture") return 0.50;
    if (catalog == "violin_1" || catalog == "violin_2" || catalog == "viola" ||
        catalog == "cello" || catalog == "contrabass" || catalog == "string_ensemble" ||
        catalog == "chamber_strings" || catalog == "flute" || catalog == "piccolo" ||
        catalog == "alto_flute" || catalog == "oboe" || catalog == "english_horn" ||
        catalog == "clarinet" || catalog == "bass_clarinet" || catalog == "bassoon" ||
        catalog == "contrabassoon" || catalog == "woodwind_ensemble" ||
        catalog == "french_horns" || catalog == "trumpets" || catalog == "trombones" ||
        catalog == "bass_trombone" || catalog == "tuba" || catalog == "brass_ensemble" ||
        catalog == "choir") return 0.25;
    if (catalog == "sub_synth" || catalog == "electric_bass" || catalog == "piano" ||
        catalog == "harp" || catalog == "guitar" || catalog == "marimba" ||
        catalog == "vibraphone" || catalog == "celesta" || catalog == "tubular_bells" ||
        catalog == "lead_synth" || catalog == "poly_synth") return 0.125;
    if (note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody) return 0.125;
    if (isVoiceInFamily(note.voice, VoiceFamily::Harmony) ||
        note.voice == VoiceId::SubBass || note.voice == VoiceId::MovementBass)
        return 0.125;
    return 1.0 / 960.0;
}

AudibleDurationRepair enforceAudibleDurations(
    Pattern& song, std::span<const HarmonicWindow> harmony) {
    AudibleDurationRepair report;
    using Key = std::tuple<std::uint16_t, int, int>;
    std::map<Key, std::vector<std::size_t>> notesByKey;
    for (std::size_t index = 0; index < song.notes.size(); ++index) {
        const auto& note = song.notes[index];
        if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.voice == VoiceId::Transitions) continue;
        notesByKey[{note.partId, note.channel, note.pitch}].push_back(index);
    }
    std::vector<bool> remove(song.notes.size(), false);
    for (auto& [key, indices] : notesByKey) {
        std::sort(indices.begin(), indices.end(), [&](auto left, auto right) {
            return song.notes[left].startBeat < song.notes[right].startBeat;
        });
        for (std::size_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
            auto& note = song.notes[indices[ordinal]];
            const auto floor = audibleDurationFloor(song, note);
            auto boundary = song.lengthBeats;
            if (const auto* window = harmonyAt(harmony, note.startBeat); window != nullptr)
                boundary = std::min(boundary, window->endBeat);
            if (ordinal + 1 < indices.size())
                boundary = std::min(boundary, song.notes[indices[ordinal + 1]].startBeat);
            const auto available = boundary - note.startBeat - 1.0 / 960.0;
            if (available + 0.000001 < floor) {
                remove[indices[ordinal]] = true;
                ++report.notesRemoved;
            } else if (note.durationBeats + 0.000001 < floor) {
                note.durationBeats = floor;
                ++report.durationsRepaired;
            } else if (note.durationBeats > available) {
                note.durationBeats = available;
                ++report.durationsRepaired;
            }
        }
    }
    auto index = std::size_t{};
    song.notes.erase(std::remove_if(song.notes.begin(), song.notes.end(), [&](const auto&) {
        return remove[index++];
    }), song.notes.end());
    return report;
}

std::size_t repairSamePitchOverlaps(Pattern& song) {
    std::sort(song.notes.begin(), song.notes.end(), [](const auto& left, const auto& right) {
        if (left.partId != right.partId) return left.partId < right.partId;
        if (left.channel != right.channel) return left.channel < right.channel;
        if (left.pitch != right.pitch) return left.pitch < right.pitch;
        return left.startBeat < right.startBeat;
    });
    std::size_t repaired{};
    std::vector<NoteEvent> resolved;
    resolved.reserve(song.notes.size());
    for (const auto& note : song.notes) {
        if (!resolved.empty()) {
            auto& previous = resolved.back();
            const auto sameKey = previous.partId == note.partId && previous.channel == note.channel &&
                                 previous.pitch == note.pitch;
            if (sameKey && std::abs(previous.startBeat - note.startBeat) < 0.0001) {
                previous.durationBeats = std::max(previous.durationBeats, note.durationBeats);
                previous.velocity = std::max(previous.velocity, note.velocity);
                ++repaired;
                continue;
            }
            if (sameKey && previous.endBeat() >= note.startBeat - 0.0001) {
                previous.durationBeats = std::max(1.0 / 960.0,
                    note.startBeat - previous.startBeat - 1.0 / 960.0);
                ++repaired;
            }
        }
        resolved.push_back(note);
    }
    song.notes = std::move(resolved);
    return repaired;
}

bool containsCaseInsensitive(const std::string& text, const std::string& needle) {
    auto lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lower.find(needle) != std::string::npos;
}

std::vector<int> homeScalePitchClasses(int rootPitchClass, ScaleKind scale) {
    std::vector<int> result;
    for (const auto interval : intervalsFor(scale))
        result.push_back(positiveModulo(rootPitchClass + interval, 12));
    return result;
}

int nearestScalePitchClass(int pitchClass, std::span<const int> scalePitches) {
    for (auto distance = 0; distance < 7; ++distance) {
        const auto down = positiveModulo(pitchClass - distance, 12);
        if (std::find(scalePitches.begin(), scalePitches.end(), down) != scalePitches.end()) return down;
        const auto up = positiveModulo(pitchClass + distance, 12);
        if (std::find(scalePitches.begin(), scalePitches.end(), up) != scalePitches.end()) return up;
    }
    return scalePitches.empty() ? positiveModulo(pitchClass, 12) : scalePitches.front();
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
        result.back().performance = defaultPerformanceProfile(definition.id);
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

std::uint64_t textIdentity(const std::string& text) noexcept {
    auto hash = std::uint64_t{1469598103934665603ULL};
    for (const auto character : text) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

RhythmMotif createOpenRhythmMotif(const std::string& direction, std::uint64_t seed,
                                  int variant) {
    RhythmMotif motif;
    motif.id = std::string(1, static_cast<char>('A' + variant));
    motif.bars = 2;
    motif.stepsPerBar = 16;
    const auto length = motif.bars * motif.stepsPerBar;
    motif.kick.assign(static_cast<std::size_t>(length), '0');
    motif.snareClap.assign(static_cast<std::size_t>(length), '0');
    motif.closedHats.assign(static_cast<std::size_t>(length), '0');
    motif.openHatsShaker.assign(static_cast<std::size_t>(length), '0');
    motif.lowPercussion.assign(static_cast<std::size_t>(length), '0');
    motif.highPercussion.assign(static_cast<std::size_t>(length), '0');
    Random random(seed ^ textIdentity(direction) ^
                  (static_cast<std::uint64_t>(variant + 1) * 0xd1b54a32d192ed03ULL));
    const auto rotation = random.range(0, 3);
    const auto backbeatA = random.chance(0.5) ? 4 : 6;
    const auto backbeatB = random.chance(0.5) ? 12 : 14;
    for (auto step = 0; step < length; ++step) {
        const auto local = step % 16;
        const auto index = static_cast<std::size_t>(step);
        const auto downbeat = local % 4 == rotation;
        if ((step == 0 || downbeat) ? random.chance(0.52 + variant * 0.08)
                                    : random.chance(0.08 + variant * 0.025))
            motif.kick[index] = local == rotation ? '2' : '1';
        if (local == backbeatA || local == backbeatB)
            motif.snareClap[index] = random.chance(0.82) ? '2' : '1';
        else if (random.chance(0.035 + variant * 0.018))
            motif.snareClap[index] = '1';
        if ((local + variant) % (variant == 1 ? 2 : 4) == 0 || random.chance(0.18))
            motif.closedHats[index] = random.chance(0.20) ? '2' : '1';
        if ((local + rotation) % 8 == 6 && random.chance(0.72)) motif.openHatsShaker[index] = '1';
        if (random.chance(0.08 + variant * 0.025)) motif.lowPercussion[index] = '1';
        if (random.chance(0.10 + variant * 0.03)) motif.highPercussion[index] = '1';
    }
    motif.kick.front() = '2';
    const auto ornamentCount = 2 + random.range(0, 4);
    constexpr std::array instruments{RhythmInstrument::Sidestick, RhythmInstrument::TomLow,
        RhythmInstrument::TomMid, RhythmInstrument::TomHigh, RhythmInstrument::Ride,
        RhythmInstrument::Crash, RhythmInstrument::Shaker, RhythmInstrument::Tambourine,
        RhythmInstrument::Cowbell, RhythmInstrument::CongaLow, RhythmInstrument::CongaHigh};
    for (auto index = 0; index < ornamentCount; ++index)
        motif.ornaments.push_back({random.range(0, length - 1),
            instruments[static_cast<std::size_t>(random.range(0, static_cast<int>(instruments.size()) - 1))],
            random.range(42, 88), random.chance(0.22) ? 1.5 : 0.5});
    return motif;
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

        if (isVoiceInFamily(note.voice, VoiceFamily::Melodic))
            note.durationBeats = std::max(0.04, note.durationBeats * (0.95 + human * 0.035));
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
    plan.totalBars = phraseAlignedBars(static_cast<int>(std::lround(
        plan.targetSeconds * plan.bpm / 60.0 / plan.beatsPerBar)));
    plan.seed = seed;
    plan.rootPitchClass = positiveModulo(rootPitchClass, 12);
    plan.scale = scale;
    plan.harmonicLanguage.tonalPolicy = tonalPolicyForDirection(direction);
    plan.title = direction.empty() ? "Longform Idea" : direction.substr(0, 48);
    plan.key = std::array{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
                   [static_cast<std::size_t>(plan.rootPitchClass)] +
               std::string(plan.scale == ScaleKind::Major ? " major" : " minor");
    plan.summary = "A complete thematic arc with recurring material, contrast, climax and resolution.";
    plan.productionLanguage = ElectronicProductionDirector::infer(direction);
    plan.productionModeSource = "local_inference";

    Random random(seed ^ 0x534F4E47504C414EULL);
    const auto third = plan.scale == ScaleKind::Major ? 4 : 3;
    plan.motifIntervals = {0, third, 7, 5, third, random.range(0, 1) == 0 ? 10 : 12};
    Random harmonyRandom(seed ^ textIdentity(direction) ^ 0x4841524d4f4e59ULL);
    const auto makeChord = [&](std::string id, std::string label, int rootOffset,
                               std::initializer_list<int> intervals, HarmonicFunction function,
                               VoicingStrategy voicing, double tension) {
        HarmonicChord chord;
        chord.id = std::move(id);
        chord.label = std::move(label);
        chord.rootPitchClass = positiveModulo(plan.rootPitchClass + rootOffset, 12);
        chord.bassPitchClass = chord.rootPitchClass;
        for (const auto interval : intervals)
            chord.pitchClasses.push_back(positiveModulo(chord.rootPitchClass + interval, 12));
        chord.function = function;
        chord.voicing = voicing;
        chord.tension = tension;
        return chord;
    };
    const auto minor = plan.scale != ScaleKind::Major;
    plan.chordPalette = {
        makeChord("home", "Home with colour", 0,
                  {0, minor ? 3 : 4, 7, minor ? 10 : 11, harmonyRandom.chance(0.5) ? 2 : 9},
                  HarmonicFunction::Tonic, VoicingStrategy::Open, 0.14),
        makeChord("departure", "Open departure", harmonyRandom.chance(0.5) ? 5 : 2,
                  {0, minor ? 4 : 3, 7, 10, harmonyRandom.chance(0.5) ? 2 : 9},
                  HarmonicFunction::Predominant, VoicingStrategy::Drop2, 0.42),
        makeChord("shadow", "Borrowed shadow", minor ? harmonyRandom.range(1, 8) : harmonyRandom.range(1, 6),
                  {0, harmonyRandom.chance(0.5) ? 3 : 4, 7, harmonyRandom.chance(0.5) ? 10 : 11},
                  HarmonicFunction::Chromatic, VoicingStrategy::Mixed, 0.56),
        makeChord("suspension", "Suspended field", harmonyRandom.chance(0.72) ? 7 : 11,
                  {0, 5, 7, 10, harmonyRandom.chance(0.5) ? 2 : 9},
                  HarmonicFunction::Dominant, VoicingStrategy::Quartal, 0.76),
        makeChord("colour", "Ambiguous colour", minor ? harmonyRandom.range(3, 10) : harmonyRandom.range(2, 9),
                  {0, 2, harmonyRandom.chance(0.5) ? 6 : 7, 9},
                  HarmonicFunction::Colour, VoicingStrategy::Open, 0.48),
        makeChord("pedal", "Tonic pedal tension", 2, {0, 3, 7, 10},
                  HarmonicFunction::Pedal, VoicingStrategy::Cluster, 0.64),
        makeChord("threshold", "Chromatic threshold", harmonyRandom.chance(0.5) ? 1 : 6,
                  {0, 4, 7, harmonyRandom.chance(0.5) ? 10 : 11},
                  HarmonicFunction::Transitional, VoicingStrategy::Shell, 0.82),
        makeChord("arrival", "Final arrival", 0, {0, minor ? 3 : 4, 7, minor ? 10 : 11},
                  HarmonicFunction::Tonic, VoicingStrategy::Close, 0.08)
    };
    plan.harmonicLanguage.description = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
        ? "Consolidated tonal narrative with functional diatonic colour"
        : "Expanded harmonic narrative explicitly requested by the direction";
    plan.harmonicLanguage.tonalGravity = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
        ? 0.78 + harmonyRandom.unit() * 0.18 : 0.38 + harmonyRandom.unit() * 0.52;
    plan.harmonicLanguage.modalFluidity = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
        ? 0.02 + harmonyRandom.unit() * 0.06 : 0.10 + harmonyRandom.unit() * 0.70;
    plan.harmonicLanguage.chromaticism = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
        ? 0.0 : 0.08 + harmonyRandom.unit() * 0.58;
    plan.harmonicLanguage.extensionRichness = 0.28 + harmonyRandom.unit() * 0.65;
    plan.harmonicLanguage.inversionMotion = 0.20 + harmonyRandom.unit() * 0.68;
    plan.harmonicLanguage.voiceLeadingSmoothness = 0.45 + harmonyRandom.unit() * 0.50;
    plan.harmonicLanguage.harmonicRhythmActivity = 0.18 + harmonyRandom.unit() * 0.66;
    plan.harmonicLanguage.pedalToneAffinity = 0.08 + harmonyRandom.unit() * 0.62;
    plan.harmonicLanguage.ambiguity = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
        ? 0.04 + harmonyRandom.unit() * 0.10 : 0.12 + harmonyRandom.unit() * 0.66;
    plan.harmonicLanguage.cadenceStrength = 0.38 + harmonyRandom.unit() * 0.56;
    plan.voices = defaultVoicePlan();
    Random rhythmRandom(seed ^ textIdentity(direction) ^ 0x525954484dULL);
    plan.rhythmLanguage.description = "Open local rhythm derived from the creative direction and composition DNA";
    plan.rhythmLanguage.pulseStability = 0.35 + rhythmRandom.unit() * 0.50;
    plan.rhythmLanguage.backbeatGravity = 0.25 + rhythmRandom.unit() * 0.65;
    plan.rhythmLanguage.syncopation = 0.18 + rhythmRandom.unit() * 0.70;
    plan.rhythmLanguage.ghostDensity = 0.05 + rhythmRandom.unit() * 0.36;
    plan.rhythmLanguage.velocityContrast = 0.30 + rhythmRandom.unit() * 0.58;
    plan.rhythmLanguage.timingFreedom = 0.04 + rhythmRandom.unit() * 0.34;
    plan.rhythmLanguage.orchestrationMotion = 0.20 + rhythmRandom.unit() * 0.68;
    plan.rhythmLanguage.silenceBias = 0.12 + rhythmRandom.unit() * 0.52;
    plan.rhythmLanguage.callResponse = 0.18 + rhythmRandom.unit() * 0.70;
    Random timbreRandom(seed ^ textIdentity(direction) ^ 0x54494d425245ULL);
    plan.timbrePalette.description = "Cohesive sound world serving: " + direction;
    plan.timbrePalette.material = "A shared material continuum shaped by the composition DNA";
    plan.timbrePalette.space = "Layered depth with deliberate foreground rotation";
    plan.timbrePalette.warmth = 0.30 + timbreRandom.unit() * 0.60;
    plan.timbrePalette.brightness = 0.25 + timbreRandom.unit() * 0.65;
    plan.timbrePalette.transientDefinition = 0.32 + timbreRandom.unit() * 0.62;
    plan.timbrePalette.acousticElectronicBalance = 0.18 + timbreRandom.unit() * 0.72;
    plan.timbrePalette.cohesion = 0.72 + timbreRandom.unit() * 0.24;
    plan.timbrePalette.contrast = 0.30 + timbreRandom.unit() * 0.60;
    for (auto variant = 0; variant < 3; ++variant)
        plan.rhythmMotifs.push_back(createOpenRhythmMotif(direction, seed, variant));

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
        auto& harmonicSection = plan.sections.back();
        harmonicSection.tonalCenterPitchClass = positiveModulo(
            plan.rootPitchClass + (index > selectedFormIndices.size() / 2 && item.tension > 0.68
                ? (harmonyRandom.chance(0.5) ? 5 : 7) : 0), 12);
        harmonicSection.modeHint = plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated
            ? (minor ? "consolidated minor" : "consolidated major")
            : (minor ? "minor with requested harmonic expansion"
                     : "major with requested harmonic expansion");
        const auto harmonicStride = std::clamp(4 - static_cast<int>(std::lround(
            plan.harmonicLanguage.harmonicRhythmActivity * 3.0)), 1, 4);
        for (auto harmonicBar = 0; harmonicBar < bars; harmonicBar += harmonicStride) {
            const auto phrase = harmonicBar / harmonicStride;
            auto paletteIndex = positiveModulo(phrase * 2 + static_cast<int>(index) +
                harmonyRandom.range(0, 2), static_cast<int>(plan.chordPalette.size()) - 1);
            if (harmonicBar == 0 && index == 0) paletteIndex = 0;
            if (harmonicBar + harmonicStride >= bars)
                paletteIndex = index + 1 == selectedFormIndices.size() ? 7 : 3;
            harmonicSection.harmonicEvents.push_back({harmonicBar, 0.0,
                plan.chordPalette[static_cast<std::size_t>(paletteIndex)].id,
                std::clamp(0.35 + item.tension * 0.45, 0.0, 1.0),
                "Develop the section's harmonic argument"});
            if (item.tension > 0.72 && harmonicStride > 1 && harmonicBar + 1 < bars &&
                harmonyRandom.chance(0.32))
                harmonicSection.harmonicEvents.push_back({harmonicBar, plan.beatsPerBar * 0.5,
                    plan.chordPalette[static_cast<std::size_t>((paletteIndex + 1) %
                        static_cast<int>(plan.chordPalette.size()))].id, 0.72,
                    "Create an internal harmonic turn"});
        }
        rhythm.authored = true;
        rhythm.motifId = plan.rhythmMotifs[static_cast<std::size_t>(item.motif %
            static_cast<int>(plan.rhythmMotifs.size()))].id;
        rhythm.percussionDensity = item.density;
        rhythm.syncopation = std::clamp(0.22 + item.tension * 0.42, 0.0, 1.0);
        rhythm.swing = std::clamp(0.03 + plan.rhythmLanguage.timingFreedom * 0.20, 0.0, 0.24);
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
    normalizePlan(plan);
    return plan;
}

int SongComposer::phraseAlignedBars(int rawBars) noexcept {
    rawBars = std::clamp(rawBars, 8, 512);
    if (rawBars < 32) return rawBars;
    const auto lower = std::max(8, rawBars / 8 * 8);
    const auto upper = std::min(512, lower + 8);
    // Ties deliberately choose the shorter form: a clean cadence is preferable to an
    // isolated tail bar added only to satisfy a wall-clock rounding artefact.
    return rawBars - lower <= upper - rawBars ? lower : upper;
}

void SongComposer::normalizePlan(SongPlan& plan) {
    plan.targetSeconds = std::clamp(plan.targetSeconds, 30, 1800);
    plan.bpm = std::clamp(plan.bpm, 30.0, 300.0);
    plan.beatsPerBar = std::clamp(plan.beatsPerBar, 2.0, 12.0);
    plan.totalBars = phraseAlignedBars(plan.totalBars);
    plan.rootPitchClass = positiveModulo(plan.rootPitchClass, 12);
    ElectronicProductionDirector::normalizePlan(plan);
    if (plan.motifIntervals.size() < 3) plan.motifIntervals = {0, 3, 7, 5};
    for (auto& value : plan.motifIntervals) value = std::clamp(value, -24, 24);
    canonicalizeMotif(plan.motifIntervals, plan.scale);
    plan.key = canonicalKeyName(plan.rootPitchClass, plan.scale);
    if (plan.chordPalette.size() < 2) {
        plan.chordPalette = {{"home", "Home", plan.rootPitchClass, plan.rootPitchClass,
                              {plan.rootPitchClass, positiveModulo(plan.rootPitchClass + 3, 12),
                               positiveModulo(plan.rootPitchClass + 7, 12)},
                              HarmonicFunction::Tonic, VoicingStrategy::Open, 0.15},
                             {"motion", "Motion", positiveModulo(plan.rootPitchClass + 7, 12),
                              positiveModulo(plan.rootPitchClass + 7, 12),
                              {positiveModulo(plan.rootPitchClass + 7, 12),
                               positiveModulo(plan.rootPitchClass + 11, 12),
                               positiveModulo(plan.rootPitchClass + 2, 12)},
                              HarmonicFunction::Dominant, VoicingStrategy::Mixed, 0.70}};
    }
    if (plan.chordPalette.size() > 24) plan.chordPalette.resize(24);
    if (plan.harmonicLanguage.description.empty())
        plan.harmonicLanguage.description = "Open harmonic narrative";
    if (plan.harmonicLanguage.description.size() > 320)
        plan.harmonicLanguage.description.resize(320);
    for (auto* value : {&plan.harmonicLanguage.tonalGravity,
                        &plan.harmonicLanguage.modalFluidity,
                        &plan.harmonicLanguage.chromaticism,
                        &plan.harmonicLanguage.extensionRichness,
                        &plan.harmonicLanguage.inversionMotion,
                        &plan.harmonicLanguage.voiceLeadingSmoothness,
                        &plan.harmonicLanguage.harmonicRhythmActivity,
                        &plan.harmonicLanguage.pedalToneAffinity,
                        &plan.harmonicLanguage.ambiguity,
                        &plan.harmonicLanguage.cadenceStrength})
        *value = std::clamp(std::isfinite(*value) ? *value : 0.5, 0.0, 1.0);
    if (plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated) {
        plan.harmonicLanguage.tonalGravity = std::max(0.72, plan.harmonicLanguage.tonalGravity);
        plan.harmonicLanguage.modalFluidity = std::min(0.10, plan.harmonicLanguage.modalFluidity);
        plan.harmonicLanguage.chromaticism = std::min(0.02, plan.harmonicLanguage.chromaticism);
        plan.harmonicLanguage.ambiguity = std::min(0.16, plan.harmonicLanguage.ambiguity);
    } else if (plan.harmonicLanguage.tonalPolicy == TonalPolicy::Expanded) {
        plan.harmonicLanguage.chromaticism = std::min(0.35, plan.harmonicLanguage.chromaticism);
        plan.harmonicLanguage.ambiguity = std::min(0.42, plan.harmonicLanguage.ambiguity);
    }
    const auto homeScale = homeScalePitchClasses(plan.rootPitchClass, plan.scale);
    std::vector<std::string> chordIds;
    for (std::size_t index = 0; index < plan.chordPalette.size(); ++index) {
        auto& chord = plan.chordPalette[index];
        if (chord.id.empty()) chord.id = "H" + std::to_string(index + 1);
        if (std::find(chordIds.begin(), chordIds.end(), chord.id) != chordIds.end())
            chord.id += "_" + std::to_string(index + 1);
        chordIds.push_back(chord.id);
        if (chord.label.empty()) chord.label = chord.id;
        if (chord.label.size() > 96) chord.label.resize(96);
        chord.rootPitchClass = positiveModulo(chord.rootPitchClass, 12);
        chord.bassPitchClass = positiveModulo(chord.bassPitchClass, 12);
        for (auto& pitchClass : chord.pitchClasses) pitchClass = positiveModulo(pitchClass, 12);
        std::vector<int> uniquePitchClasses;
        for (const auto pitchClass : chord.pitchClasses)
            if (std::find(uniquePitchClasses.begin(), uniquePitchClasses.end(), pitchClass) ==
                uniquePitchClasses.end())
                uniquePitchClasses.push_back(pitchClass);
        chord.pitchClasses = std::move(uniquePitchClasses);
        if (plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated) {
            chord.rootPitchClass = nearestScalePitchClass(chord.rootPitchClass, homeScale);
            chord.bassPitchClass = nearestScalePitchClass(chord.bassPitchClass, homeScale);
            chord.pitchClasses.erase(std::remove_if(chord.pitchClasses.begin(), chord.pitchClasses.end(),
                [&](int pitchClass) {
                    return std::find(homeScale.begin(), homeScale.end(), pitchClass) == homeScale.end();
                }), chord.pitchClasses.end());
            if (std::find(chord.pitchClasses.begin(), chord.pitchClasses.end(), chord.rootPitchClass) ==
                chord.pitchClasses.end())
                chord.pitchClasses.insert(chord.pitchClasses.begin(), chord.rootPitchClass);
            if (chord.pitchClasses.size() < 3) {
                const auto degree = std::find(homeScale.begin(), homeScale.end(), chord.rootPitchClass);
                const auto degreeIndex = degree == homeScale.end() ? 0 :
                    static_cast<std::size_t>(std::distance(homeScale.begin(), degree));
                for (const auto offset : {2U, 4U}) {
                    const auto tone = homeScale[(degreeIndex + offset) % homeScale.size()];
                    if (std::find(chord.pitchClasses.begin(), chord.pitchClasses.end(), tone) == chord.pitchClasses.end())
                        chord.pitchClasses.push_back(tone);
                }
            }
            if (chord.function == HarmonicFunction::Chromatic || chord.function == HarmonicFunction::Colour)
                chord.function = HarmonicFunction::Modal;
            if (chord.voicing == VoicingStrategy::Cluster || chord.voicing == VoicingStrategy::Quartal)
                chord.voicing = VoicingStrategy::Open;
            chord.tension = std::min(chord.tension, 0.72);
        }
        if (chord.pitchClasses.size() < 2)
            chord.pitchClasses = {chord.rootPitchClass, positiveModulo(chord.rootPitchClass + 7, 12)};
        if (chord.pitchClasses.size() > 8) chord.pitchClasses.resize(8);
        chord.tension = std::clamp(std::isfinite(chord.tension) ? chord.tension : 0.5, 0.0, 1.0);
    }
    if (plan.voices.empty()) plan.voices = defaultVoicePlan();
    if (plan.instruments.empty()) plan.instruments = defaultOrchestrationAssignments();
    materializeNamedSoundWorld(plan);
    if (plan.instruments.size() > 48) plan.instruments.resize(48);
    if (plan.orchestrationLanguage.description.empty())
        plan.orchestrationLanguage.description = "Evolving chamber-to-tutti orchestration";
    if (plan.orchestrationLanguage.description.size() > 320)
        plan.orchestrationLanguage.description.resize(320);
    for (auto* value : {&plan.orchestrationLanguage.ensembleScale,
                        &plan.orchestrationLanguage.timbralMotion,
                        &plan.orchestrationLanguage.foregroundRotation,
                        &plan.orchestrationLanguage.doublingRestraint,
                        &plan.orchestrationLanguage.registerSeparation,
                        &plan.orchestrationLanguage.chamberContrast,
                        &plan.orchestrationLanguage.tuttiRarity,
                        &plan.orchestrationLanguage.harmonicDepth,
                        &plan.orchestrationLanguage.counterpointActivity,
                        &plan.orchestrationLanguage.divisiDepth,
                        &plan.orchestrationLanguage.articulationContrast,
                        &plan.orchestrationLanguage.familyDialogue,
                        &plan.orchestrationLanguage.hybridProduction})
        *value = std::clamp(std::isfinite(*value) ? *value : 0.5, 0.0, 1.0);
    if (plan.timbrePalette.description.empty())
        plan.timbrePalette.description = "coherent, dimensional and natural";
    if (plan.timbrePalette.material.empty())
        plan.timbrePalette.material = "warm organic core with restrained electronic detail";
    if (plan.timbrePalette.space.empty())
        plan.timbrePalette.space = "deep foreground-to-background stage";
    for (auto* value : {&plan.timbrePalette.warmth, &plan.timbrePalette.brightness,
                        &plan.timbrePalette.transientDefinition,
                        &plan.timbrePalette.acousticElectronicBalance,
                        &plan.timbrePalette.cohesion, &plan.timbrePalette.contrast})
        *value = std::clamp(std::isfinite(*value) ? *value : 0.5, 0.0, 1.0);
    std::vector<std::string> instrumentIds;
    std::vector<std::string> instrumentNames;
    for (std::size_t index = 0; index < plan.instruments.size(); ++index) {
        auto& instrument = plan.instruments[index];
        const auto* definition = instrumentDefinition(instrument.instrumentId);
        if (definition == nullptr) {
            definition = &instrumentCatalog()[index % instrumentCatalog().size()];
            instrument.instrumentId = std::string(definition->id);
        }
        if (instrument.id.empty()) instrument.id = "part_" + std::to_string(index + 1);
        if (std::find(instrumentIds.begin(), instrumentIds.end(), instrument.id) != instrumentIds.end())
            instrument.id += "_" + std::to_string(index + 1);
        instrumentIds.push_back(instrument.id);
        if (instrument.name.empty()) instrument.name = std::string(definition->name);
        if (instrument.name.size() > 96) instrument.name.resize(96);
        const auto baseName = instrument.name;
        auto nameSuffix = 2;
        while (std::find(instrumentNames.begin(), instrumentNames.end(), instrument.name) != instrumentNames.end())
            instrument.name = baseName + " " + std::to_string(nameSuffix++);
        instrumentNames.push_back(instrument.name);
        if (instrument.role.size() > 180) instrument.role.resize(180);
        if (instrument.sourceVoice == VoiceId::Unspecified || instrument.sourceVoice == VoiceId::Count)
            instrument.sourceVoice = definition->preferredVoice;
        const auto sourceFamily = voiceDefinition(instrument.sourceVoice).family;
        const auto melodicCounterpoint = sourceFamily == VoiceFamily::Melodic &&
                                           instrument.orchestralFunction == "counterpoint";
        const auto incompatible = (definition->department == ScoreDepartment::Rhythm &&
                                   instrument.sourceVoice != definition->preferredVoice) ||
                                  (definition->department == ScoreDepartment::Melody &&
                                   sourceFamily != VoiceFamily::Melodic) ||
                                  (definition->department == ScoreDepartment::Harmony &&
                                   (sourceFamily == VoiceFamily::Rhythm ||
                                    (sourceFamily == VoiceFamily::Melodic && !melodicCounterpoint)));
        if (incompatible) instrument.sourceVoice = definition->preferredVoice;
        instrument.minimumPitch = std::clamp(instrument.minimumPitch,
            definition->minimumPitch, definition->maximumPitch);
        instrument.maximumPitch = std::clamp(instrument.maximumPitch,
            instrument.minimumPitch, definition->maximumPitch);
        // This is an octave displacement, never a chromatic transposition. Accept old
        // persisted values defensively, but snap them to an octave so orchestration
        // cannot silently move a tonal line by an arbitrary number of semitones.
        instrument.octaveShift = std::clamp(
            static_cast<int>(std::lround(instrument.octaveShift / 12.0)) * 12, -24, 24);
        instrument.activity = std::clamp(std::isfinite(instrument.activity) ? instrument.activity : 0.5, 0.0, 1.0);
        instrument.prominence = std::clamp(std::isfinite(instrument.prominence) ? instrument.prominence : 0.5, 0.0, 1.0);
        instrument.doubling = std::clamp(std::isfinite(instrument.doubling) ? instrument.doubling : 0.2, 0.0, 1.0);
        constexpr std::array functions{"foundation", "body", "extension", "counterpoint", "color", "transition"};
        if (std::find(functions.begin(), functions.end(), instrument.orchestralFunction) == functions.end())
            instrument.orchestralFunction = "body";
        constexpr std::array articulations{"natural", "legato", "staccato", "detached", "sustained",
                                            "swelling", "tremolo", "pizzicato", "ostinato"};
        if (std::find(articulations.begin(), articulations.end(), instrument.articulation) == articulations.end())
            instrument.articulation = "natural";
        instrument.divisiVoices = std::clamp(instrument.divisiVoices, 1, 4);
        constexpr std::array liveDevices{"auto", "Drum Rack", "Instrument Rack", "Simpler", "Sampler",
                                         "Drift", "Meld", "Wavetable", "Operator", "Analog", "Electric",
                                         "Tension", "Collision", "Granulator III"};
        if (std::find(liveDevices.begin(), liveDevices.end(), instrument.liveDevice) == liveDevices.end())
            instrument.liveDevice = "auto";
        if (instrument.livePresetIntent.empty()) instrument.livePresetIntent = instrument.instrumentId;
        if (instrument.livePresetIntent.size() > 96) instrument.livePresetIntent.resize(96);
    }
    const auto departmentCount = [&](ScoreDepartment department) {
        return std::count_if(plan.instruments.begin(), plan.instruments.end(), [&](const auto& instrument) {
            const auto* definition = instrumentDefinition(instrument.instrumentId);
            return definition != nullptr && definition->department == department;
        });
    };
    auto orchestralDefaults = defaultOrchestrationAssignments();
    const auto ensureDepartment = [&](ScoreDepartment department, int minimum) {
        auto count = departmentCount(department);
        for (const auto& candidate : orchestralDefaults) {
            if (count >= minimum || plan.instruments.size() >= 48) break;
            const auto* definition = instrumentDefinition(candidate.instrumentId);
            if (definition == nullptr || definition->department != department ||
                std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& existing) {
                    return existing.instrumentId == candidate.instrumentId;
                })) continue;
            auto addition = candidate;
            addition.id = "auto_" + addition.instrumentId;
            plan.instruments.push_back(std::move(addition));
            ++count;
        }
    };
    const auto electronicCast = plan.productionLanguage.electronicIntent >= 0.58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    if (!electronicCast) {
        ensureDepartment(ScoreDepartment::Rhythm, 3);
        ensureDepartment(ScoreDepartment::Harmony, plan.orchestrationLanguage.harmonicDepth >= 0.62 ? 9 : 6);
        ensureDepartment(ScoreDepartment::Melody, plan.orchestrationLanguage.familyDialogue >= 0.62 ? 4 : 3);
    }
    const auto voiceDefaultsForInstruments = defaultVoicePlan();
    const auto closedAuthoredCast = electronicCast && plan.instrumentCastAuthored;
    for (auto& instrument : plan.instruments)
        if (std::none_of(plan.voices.begin(), plan.voices.end(), [&](const auto& voice) {
                return voice.id == instrument.sourceVoice;
            })) {
            const auto* instrumentType = instrumentDefinition(instrument.instrumentId);
            const auto compatible = [&](VoiceId voice) {
                if (instrumentType == nullptr) return true;
                const auto family = voiceDefinition(voice).family;
                if (instrumentType->department == ScoreDepartment::Rhythm)
                    return family == VoiceFamily::Rhythm;
                if (instrumentType->department == ScoreDepartment::Melody)
                    return family == VoiceFamily::Melodic;
                return family != VoiceFamily::Rhythm && family != VoiceFamily::Melodic;
            };
            const auto authoredSource = closedAuthoredCast ? std::find_if(plan.voices.begin(), plan.voices.end(),
                [&](const auto& voice) { return compatible(voice.id); }) : plan.voices.end();
            if (authoredSource != plan.voices.end()) {
                instrument.sourceVoice = authoredSource->id;
                continue;
            }
            const auto source = std::find_if(voiceDefaultsForInstruments.begin(),
                voiceDefaultsForInstruments.end(), [&](const auto& voice) {
                    return voice.id == instrument.sourceVoice;
                });
            if (source != voiceDefaultsForInstruments.end()) {
                auto addition = *source;
                const auto authoredCompatible = std::find_if(plan.voices.begin(), plan.voices.end(),
                    [&](const auto& voice) { return compatible(voice.id) && voice.performance.authored; });
                if (authoredCompatible != plan.voices.end()) {
                    addition.performance = authoredCompatible->performance;
                    addition.performance.authored = true;
                    addition.performance.intent = "Derived part articulation from AI family direction: " +
                                                    authoredCompatible->performance.intent;
                }
                plan.voices.push_back(std::move(addition));
            }
        }
    // Every execution role in the score needs a concrete instrumental owner. Without this
    // invariant, inactive assignments leak back out as anonymous legacy MIDI tracks.
    for (const auto& voice : plan.voices) {
        if (closedAuthoredCast) continue;
        if (plan.instruments.size() >= 48 ||
            std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& instrument) {
                return instrument.sourceVoice == voice.id;
            })) continue;
        const auto definition = std::find_if(instrumentCatalog().begin(), instrumentCatalog().end(),
            [&](const auto& candidate) { return candidate.preferredVoice == voice.id; });
        if (definition == instrumentCatalog().end()) continue;
        auto name = std::string(definition->name);
        const auto baseName = name;
        auto suffix = 2;
        while (std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& instrument) {
            return instrument.name == name;
        })) name = baseName + " " + std::to_string(suffix++);
        plan.instruments.push_back({"coverage_" + std::string(voiceDefinition(voice.id).key),
            std::string(definition->id), std::move(name), voice.id, "Dedicated orchestral owner",
            definition->minimumPitch, definition->maximumPitch, 0, 0.62,
            definition->weight * 0.68, 0.10, {}});
    }
    if (plan.rhythmLanguage.description.empty())
        plan.rhythmLanguage.description = "Open rhythmic conversation";
    if (plan.rhythmLanguage.description.size() > 240)
        plan.rhythmLanguage.description.resize(240);
    for (auto* value : {&plan.rhythmLanguage.pulseStability,
                        &plan.rhythmLanguage.backbeatGravity,
                        &plan.rhythmLanguage.syncopation,
                        &plan.rhythmLanguage.ghostDensity,
                        &plan.rhythmLanguage.velocityContrast,
                        &plan.rhythmLanguage.timingFreedom,
                        &plan.rhythmLanguage.orchestrationMotion,
                        &plan.rhythmLanguage.silenceBias,
                        &plan.rhythmLanguage.callResponse})
        *value = std::clamp(std::isfinite(*value) ? *value : 0.5, 0.0, 1.0);
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
        if (motif.ornaments.size() > 48) motif.ornaments.resize(48);
        for (auto& ornament : motif.ornaments) {
            ornament.step = std::clamp(ornament.step, 0, std::max(0, static_cast<int>(expected) - 1));
            ornament.velocity = std::clamp(ornament.velocity, 1, 127);
            ornament.durationSteps = std::clamp(
                std::isfinite(ornament.durationSteps) ? ornament.durationSteps : 0.5, 0.10, 8.0);
        }
    }
    const auto defaults = defaultVoicePlan();
    for (const auto rhythmVoice : {VoiceId::SnareClap, VoiceId::ClosedHats, VoiceId::OpenHatsShaker})
        if (std::none_of(plan.voices.begin(), plan.voices.end(), [rhythmVoice](const auto& voice) {
                return voice.id == rhythmVoice;
            })) {
            auto addition = *std::find_if(defaults.begin(), defaults.end(), [rhythmVoice](const auto& voice) {
                return voice.id == rhythmVoice;
            });
            // A structured AI score may omit a technical drum lane while still authoring the
            // complete rhythmic language. Preserve that intelligence instead of injecting an
            // unrelated generic performance profile.
            const auto authoredRhythm = std::find_if(plan.voices.begin(), plan.voices.end(), [](const auto& voice) {
                return isVoiceInFamily(voice.id, VoiceFamily::Rhythm) && voice.performance.authored;
            });
            if (authoredRhythm != plan.voices.end()) {
                addition.performance = authoredRhythm->performance;
                addition.performance.authored = true;
                addition.performance.intent = "Derived lane articulation from AI rhythm direction: " +
                                                authoredRhythm->performance.intent;
            }
            plan.voices.push_back(std::move(addition));
        }
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
        if (!voice.performance.authored)
            voice.performance = defaultPerformanceProfile(voice.id);
        voice.performance.expressionDepth = std::clamp(voice.performance.expressionDepth, 0.0, 1.0);
        voice.performance.brightness = std::clamp(voice.performance.brightness, 0.0, 1.0);
        voice.performance.humanization = std::clamp(voice.performance.humanization, 0.0, 1.0);
        const auto pitchEligible = voice.id == VoiceId::SubBass || voice.id == VoiceId::MovementBass ||
                                   voice.id == VoiceId::Lead || voice.id == VoiceId::Countermelody;
        if (!pitchEligible) voice.performance.pitchGesture = PitchGesture::Stable;
        if (isVoiceInFamily(voice.id, VoiceFamily::Rhythm)) {
            voice.performance.vibrato = VibratoStyle::None;
            voice.performance.sustainPedal = false;
        }
        const auto pedalEligible = voice.id == VoiceId::HarmonicFoundation ||
                                   voice.id == VoiceId::HarmonicUpper ||
                                   voice.id == VoiceId::Atmosphere;
        if (!pedalEligible) voice.performance.sustainPedal = false;
        if (voice.performance.intent.size() > 240) voice.performance.intent.resize(240);
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
        section.tonalCenterPitchClass = positiveModulo(section.tonalCenterPitchClass, 12);
        if (plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated)
            section.tonalCenterPitchClass = plan.rootPitchClass;
        if (section.modeHint.empty()) section.modeHint = "open";
        if (section.modeHint.size() > 96) section.modeHint.resize(96);
        if (section.harmonicEvents.empty()) {
            section.tonalCenterPitchClass = plan.rootPitchClass;
            section.harmonicEvents.push_back({0, 0.0, plan.chordPalette.front().id, 0.5,
                                              "Establish harmonic ground"});
        }
        if (section.harmonicEvents.size() > 64) section.harmonicEvents.resize(64);
        for (auto& event : section.harmonicEvents) {
            event.barOffset = std::clamp(event.barOffset, 0, section.bars - 1);
            event.beatOffset = std::clamp(std::isfinite(event.beatOffset) ? event.beatOffset : 0.0,
                                          0.0, plan.beatsPerBar - 0.05);
            if (std::find(chordIds.begin(), chordIds.end(), event.chordId) == chordIds.end())
                event.chordId = plan.chordPalette.front().id;
            event.emphasis = std::clamp(std::isfinite(event.emphasis) ? event.emphasis : 0.5,
                                        0.0, 1.0);
            if (event.purpose.size() > 180) event.purpose.resize(180);
        }
        std::sort(section.harmonicEvents.begin(), section.harmonicEvents.end(), [](const auto& left,
                                                                                   const auto& right) {
            return left.barOffset != right.barOffset ? left.barOffset < right.barOffset
                                                     : left.beatOffset < right.beatOffset;
        });
        section.harmonicEvents.erase(std::unique(section.harmonicEvents.begin(),
            section.harmonicEvents.end(), [](const auto& left, const auto& right) {
                return left.barOffset == right.barOffset &&
                       std::abs(left.beatOffset - right.beatOffset) < 0.01;
            }), section.harmonicEvents.end());
        if (section.harmonicEvents.front().barOffset != 0 ||
            section.harmonicEvents.front().beatOffset > 0.001)
            section.harmonicEvents.insert(section.harmonicEvents.begin(),
                {0, 0.0, plan.chordPalette.front().id, 0.45, "Establish section harmony"});
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
    // An AI-authored electronic cast is closed: an execution voice without a named
    // instrument is incomplete authorship, not permission to inject a generic cello or
    // a multi-articulation GM percussion section. Counterpoint guitars and synths remain
    // valid melodic owners because their source voice was preserved above.
    if (closedAuthoredCast) {
        std::set<VoiceId> owners;
        for (const auto& instrument : plan.instruments) owners.insert(instrument.sourceVoice);
        std::set<VoiceId> implicit;
        for (const auto& voice : plan.voices)
            if (!owners.contains(voice.id)) implicit.insert(voice.id);
        plan.implicitVoicesPruned = implicit.size();
        plan.voices.erase(std::remove_if(plan.voices.begin(), plan.voices.end(), [&](const auto& voice) {
            return implicit.contains(voice.id);
        }), plan.voices.end());
        for (auto& section : plan.sections)
            section.activeVoices.erase(std::remove_if(section.activeVoices.begin(),
                section.activeVoices.end(), [&](VoiceId voice) { return implicit.contains(voice); }),
                section.activeVoices.end());
        for (auto& cell : plan.performanceScore.cells) {
            const auto before = cell.notes.size();
            cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                return implicit.contains(note.voice);
            }), cell.notes.end());
            plan.implicitPerformanceNotesPruned += before - cell.notes.size();
            cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(),
                [&](const auto& event) { return implicit.contains(event.voice); }), cell.controls.end());
            cell.ownedVoices.erase(std::remove_if(cell.ownedVoices.begin(), cell.ownedVoices.end(),
                [&](VoiceId voice) { return implicit.contains(voice); }), cell.ownedVoices.end());
        }
        for (auto& placement : plan.performanceScore.placements)
            placement.voiceMap.erase(std::remove_if(placement.voiceMap.begin(),
                placement.voiceMap.end(), [&](const auto& mapping) {
                    return implicit.contains(mapping.from) || implicit.contains(mapping.to);
                }), placement.voiceMap.end());
    }
    for (auto& instrument : plan.instruments) {
        instrument.activeSections.erase(std::remove_if(instrument.activeSections.begin(),
            instrument.activeSections.end(), [&](const auto& name) {
                return std::none_of(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
                    return section.name == name;
                });
            }), instrument.activeSections.end());
        std::sort(instrument.activeSections.begin(), instrument.activeSections.end());
        instrument.activeSections.erase(std::unique(instrument.activeSections.begin(),
            instrument.activeSections.end()), instrument.activeSections.end());
        if (!instrument.activeSections.empty()) {
            for (auto& section : plan.sections)
                if (std::find(instrument.activeSections.begin(), instrument.activeSections.end(), section.name) !=
                        instrument.activeSections.end() &&
                    std::find(section.activeVoices.begin(), section.activeVoices.end(), instrument.sourceVoice) ==
                        section.activeVoices.end())
                    section.activeVoices.push_back(instrument.sourceVoice);
        } else if (std::none_of(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
                       return std::find(section.activeVoices.begin(), section.activeVoices.end(),
                                        instrument.sourceVoice) != section.activeVoices.end();
                   })) {
            const auto selected = std::max_element(plan.sections.begin(), plan.sections.end(),
                [](const auto& left, const auto& right) { return left.energy < right.energy; });
            if (selected != plan.sections.end()) selected->activeVoices.push_back(instrument.sourceVoice);
        }
    }
    std::vector<double> sectionLengths;
    sectionLengths.reserve(plan.sections.size());
    for (const auto& section : plan.sections)
        sectionLengths.push_back(section.bars * plan.beatsPerBar);
    PerformanceScoreEngine::normalize(plan.performanceScore, plan.sections.size(), sectionLengths);
}

Pattern SongComposer::render(const SongPlan& sourcePlan, const GenerationContext& foundation,
                             const ProgressCallback& progress,
                             CompositionRenderReport* renderReport) const {
    auto plan = sourcePlan;
    normalizePlan(plan);
    Pattern song;
    song.lengthBeats = plan.totalBars * plan.beatsPerBar;
    song.seed = plan.seed;
    song.soundWorld = plan.timbrePalette.description + "; " + plan.timbrePalette.material +
                      "; " + plan.timbrePalette.space;
    song.soundWarmth = plan.timbrePalette.warmth;
    song.soundBrightness = plan.timbrePalette.brightness;
    song.acousticElectronicBalance = plan.timbrePalette.acousticElectronicBalance;
    song.productionDomain = plan.productionLanguage.domain == ProductionDomain::ClubElectronic
        ? "club_electronic" : plan.productionLanguage.domain == ProductionDomain::Hybrid
        ? "hybrid" : plan.productionLanguage.domain == ProductionDomain::Orchestral
        ? "orchestral" : "adaptive";
    song.productionModeSource = plan.productionModeSource;
    Generator generator;
    std::size_t workUnits = 0;
    for (const auto& section : plan.sections)
        workUnits += static_cast<std::size_t>((section.bars + 15) / 16);
    auto completed = std::size_t{};
    auto globalChunk = std::uint64_t{};
    HarmonyState harmonyState;
    PhrasePerformanceState phraseState;
    std::vector<std::vector<int>> songHarmony(static_cast<std::size_t>(plan.totalBars));
    std::vector<HarmonicWindow> harmonicWindows;

    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto directions = PhraseDirector::create(plan, section);
        const auto sectionHarmony = HarmonyEngine::composeSection(plan, section, directions,
                                                                   harmonyState);
        for (auto localBar = 0; localBar < section.bars; ++localBar) {
            const auto absoluteBar = section.startBar + localBar;
            if (absoluteBar >= 0 && absoluteBar < plan.totalBars) {
                auto& pitchClasses = songHarmony[static_cast<std::size_t>(absoluteBar)];
                for (const auto& moment : sectionHarmony[static_cast<std::size_t>(localBar)]) {
                    pitchClasses.insert(pitchClasses.end(), moment.pitchClasses.begin(),
                                        moment.pitchClasses.end());
                    pitchClasses.push_back(moment.bassPitchClass);
                }
                std::sort(pitchClasses.begin(), pitchClasses.end());
                pitchClasses.erase(std::unique(pitchClasses.begin(), pitchClasses.end()),
                                   pitchClasses.end());
                for (const auto& moment : sectionHarmony[static_cast<std::size_t>(localBar)]) {
                    const auto startBeat = absoluteBar * plan.beatsPerBar + moment.beatOffset;
                    harmonicWindows.push_back({startBeat,
                        std::min(song.lengthBeats, startBeat + moment.durationBeats),
                        moment.rootPitchClass, moment.bassPitchClass, moment.pitchClasses,
                        moment.function, moment.voicingStrategy, moment.tension,
                        moment.chordId, moment.label});
                }
            }
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
                return note.voice != VoiceId::Unspecified &&
                       (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
                        isVoiceInFamily(note.voice, VoiceFamily::Bass) ||
                        isVoiceInFamily(note.voice, VoiceFamily::Harmony) ||
                        isVoiceInFamily(note.voice, VoiceFamily::Melodic));
            }), chunk.notes.end());

            PhraseComposer::renderBassVoices(chunk, plan, section, directions, sectionHarmony,
                                              sectionBar, chunkBars, phraseState);
            PhraseComposer::renderMelodicVoices(chunk, plan, section, directions, sectionHarmony,
                                                 sectionBar, chunkBars, phraseState);

            for (auto bar = 0; bar < chunkBars; ++bar) {
                const auto localBar = sectionBar + bar;
                const auto& direction = directions[static_cast<std::size_t>(localBar)];
                HarmonyEngine::renderBar(chunk, plan, section, direction,
                    sectionHarmony[static_cast<std::size_t>(localBar)], bar, chunkBars - bar);
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
            // Explicit AI-authored voices replace procedural material only after the local
            // fallback has completed. Their rests, phrasing and dynamics remain intentional.
            PerformanceScoreEngine::replaceChunk(chunk, plan.performanceScore,
                static_cast<int>(sectionIndex), sectionBar * plan.beatsPerBar,
                chunkBars * plan.beatsPerBar);

            appendShifted(song, std::move(chunk), offset, song.lengthBeats);
            sectionBar += chunkBars;
            ++completed;
            if (progress) progress(completed, workUnits, section);
        }
    }

    std::sort(harmonicWindows.begin(), harmonicWindows.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    harmonicWindows.erase(std::unique(harmonicWindows.begin(), harmonicWindows.end(), [](const auto& left,
                                                                                         const auto& right) {
        return std::abs(left.startBeat - right.startBeat) < 0.001;
    }), harmonicWindows.end());
    for (std::size_t index = 0; index < harmonicWindows.size(); ++index) {
        const auto nextStart = index + 1 < harmonicWindows.size()
            ? harmonicWindows[index + 1].startBeat : song.lengthBeats;
        harmonicWindows[index].endBeat = std::max(harmonicWindows[index].startBeat + 0.01,
                                                   nextStart);
    }

    [[maybe_unused]] const auto rhythmReport = RhythmEngine::enforceContract(song, plan);
    const auto tonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, harmonicWindows, 0.035,
        plan.harmonicLanguage.tonalPolicy);
    const auto qualityReport = MusicalCritic::reviewAndRefine(song, plan);
    [[maybe_unused]] const auto structuralTonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, harmonicWindows, 0.035,
        plan.harmonicLanguage.tonalPolicy);
    for (auto& note : song.notes) {
        if (note.voice == VoiceId::Unspecified ||
            isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.voice == VoiceId::Transitions) continue;
        const auto& definition = voiceDefinition(note.voice);
        if (note.pitch < definition.minimumPitch || note.pitch > definition.maximumPitch)
            note.pitch = nearestPitchClass(positiveModulo(note.pitch, 12), note.pitch,
                                           definition.minimumPitch, definition.maximumPitch);
    }

    // Articulation is part of the rendered score and may extend releases. Validate that
    // actual performed MIDI, then rebuild note-bound expression against the final notes.
    PerformanceExpression::apply(song, plan, true);
    const auto finalTonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, harmonicWindows, 0.035,
        plan.harmonicLanguage.tonalPolicy);
    if (renderReport != nullptr) {
        renderReport->firstTonalPass = tonalReport;
        renderReport->finalTonalPass = finalTonalReport;
        renderReport->musical = qualityReport;
        renderReport->harmonicWindows = harmonicWindows.size();
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
    const auto electronicShaping = ElectronicProductionDirector::shapePerformance(song, plan);
    auto orchestrationReport = OrchestrationScore::realize(song, plan);
    const auto earlyRhythm = repairEarlyClubRhythm(song, plan);
    const auto musicalIdentity = MusicalIdentityGate::enforce(song, plan);
    const auto foregroundContinuity = repairExtendedForegroundAbsence(
        song, plan, harmonicWindows);
    const auto structuralContinuity = repairSparseStructuralWindows(
        song, plan, harmonicWindows);
    // Continuity must be the final structural stage. Earlier anchors can legitimately be
    // removed by active-section orchestration, which would recreate long empty windows in
    // the MIDI actually delivered to Live.
    const auto continuityReport = repairUnintendedGlobalSilence(song, plan, harmonicWindows);
    // Continuity operates on realized parts. Re-assert the two physical contracts it can
    // otherwise bypass: upper parts remain upper, and every late percussion note carries a
    // concrete GM articulation rather than generic pitch 36.
    const auto latePercussion = RhythmEngine::enforceSemanticArticulations(song, plan);
    orchestrationReport.registerRepairs += OrchestrationScore::enforcePublishedRegisters(song);
    // Orchestration is a pitch-producing stage (register fitting and divisi). The tonal
    // contract therefore has to validate the realized score, not merely its source voices.
    const auto orchestratedTonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, harmonicWindows, 0.035,
        plan.harmonicLanguage.tonalPolicy);
    // Tonal repair may converge two independently voiced pitches. Restore unambiguous
    // MIDI note ownership after that last pitch-producing operation.
    [[maybe_unused]] const auto finalOverlapRepairs = repairSamePitchOverlaps(song);
    [[maybe_unused]] const auto metricRepairs = ProductionPolish::enforceMetricContract(song);
    // Snapping two independently voiced attacks can make their release boundaries
    // coincide again; ownership is therefore converged once more on published timing.
    [[maybe_unused]] const auto metricOverlapRepairs = repairSamePitchOverlaps(song);
    // Legal scale tones can still form a low semitone, major seventh/minor ninth or
    // tritone when independent parts overlap. Duck sustained support around the actual
    // low-foundation event before expression is bound to the final note fragments.
    auto verticalHarmony = VerticalHarmonyGate::enforce(song);
    [[maybe_unused]] const auto verticalOverlapRepairs = repairSamePitchOverlaps(song);
    // Instrument realization and vertical phrasing may transpose or split a line. Rebind
    // expression only after every final pitch, doubling and continuation is known.
    PerformanceExpression::apply(song, plan, false);
    OrchestrationScore::applyPartExpression(song, plan, &orchestrationReport);
    // Metric publication and articulation-aware release shaping are pitch-neutral, but can
    // move a note-off across a harmonic boundary. Audit and repair the exact MIDI that Live
    // receives, after those operations rather than trusting the earlier abstract score.
    const auto publishedTonalReport = repairTonalContract(
        song, plan.rootPitchClass, plan.scale, plan.beatsPerBar, harmonicWindows, 0.035,
        plan.harmonicLanguage.tonalPolicy);
    const auto audibleDurationReport = enforceAudibleDurations(song, harmonicWindows);
    [[maybe_unused]] const auto publishedOverlapRepairs = repairSamePitchOverlaps(song);
    // Release repair can legitimately lengthen a note after the first vertical pass.
    // Re-audit the exact publication boundary so the production gate never discovers a
    // collision that was introduced by its own playability repair.
    const auto finalVerticalHarmony = VerticalHarmonyGate::enforce(song);
    verticalHarmony.collisionsBefore += finalVerticalHarmony.collisionsBefore;
    verticalHarmony.collisionsAfter = finalVerticalHarmony.collisionsAfter;
    verticalHarmony.supportNotesDucked += finalVerticalHarmony.supportNotesDucked;
    verticalHarmony.continuationFragmentsCreated +=
        finalVerticalHarmony.continuationFragmentsCreated;
    verticalHarmony.score = verticalHarmony.collisionsBefore == 0 ? 1.0 :
        1.0 - static_cast<double>(verticalHarmony.collisionsAfter) /
              static_cast<double>(verticalHarmony.collisionsBefore);
    [[maybe_unused]] const auto finalVerticalOverlapRepairs = repairSamePitchOverlaps(song);
    const auto expressionReport = ProductionPolish::compactExpression(song);
    const auto productionReport = ProductionPolish::audit(
        song, publishedTonalReport.after, plan.beatsPerBar, orchestrationReport.registerClarity,
        orchestrationReport.familyBalance);
    ProductionPolish::stamp(song, productionReport);
    auto electronicReport = ElectronicProductionDirector::audit(song, plan);
    electronicReport.lowEndCollisionsBefore = electronicShaping.lowEndCollisionsBefore;
    electronicReport.bassAttacksMoved = electronicShaping.bassAttacksMoved;
    electronicReport.bassReleasesTrimmed = electronicShaping.bassReleasesTrimmed;
    electronicReport.phraseBreathsCreated = electronicShaping.phraseBreathsCreated;
    electronicReport.rhythmNotesEvolved = electronicShaping.rhythmNotesEvolved;
    electronicReport.phraseVariationsCreated = electronicShaping.phraseVariationsCreated;
    electronicReport.bassPhraseDevelopmentsCreated = electronicShaping.bassPhraseDevelopmentsCreated;
    electronicReport.bassNotesDeveloped = electronicShaping.bassNotesDeveloped;
    electronicReport.kickOrnamentsRemoved = electronicShaping.kickOrnamentsRemoved;
    electronicReport.harmonicBreathsCreated = electronicShaping.harmonicBreathsCreated;
    electronicReport.supportNotesRotated = electronicShaping.supportNotesRotated;
    electronicReport.thematicRecallWindowsCreated = electronicShaping.thematicRecallWindowsCreated;
    electronicReport.thematicRecallNotesCreated = electronicShaping.thematicRecallNotesCreated;
    electronicReport.kickPhraseDevelopmentsCreated = electronicShaping.kickPhraseDevelopmentsCreated;
    electronicReport.macroKickAnchorBarsCreated = electronicShaping.macroKickAnchorBarsCreated;
    electronicReport.maximumKicklessBarsBefore = electronicShaping.maximumKicklessBarsBefore;
    electronicReport.maximumKicklessBarsAfter = electronicShaping.maximumKicklessBarsAfter;
    electronicReport.latePercussionArticulationRepairs = static_cast<std::size_t>(
        latePercussion.semanticPitchRepairs + latePercussion.articulationDiversifications);
    electronicReport.sparseStructuralWindowsRepaired = structuralContinuity.windowsRepaired;
    electronicReport.continuityNotesCreated = structuralContinuity.notesCreated;
    electronicReport.foregroundNotesRemoved = electronicShaping.foregroundNotesRemoved;
    electronicReport.automationEventsAdded = electronicShaping.automationEventsAdded;
    electronicReport.earlyRhythmNotesCreated = earlyRhythm.notesCreated;
    electronicReport.musicalIdentityScore = musicalIdentity.score;
    electronicReport.grooveRecallRatio = musicalIdentity.grooveRecallRatio;
    electronicReport.responseLineageRatio = musicalIdentity.responseLineageRatio;
    electronicReport.transitionNotesRemoved = musicalIdentity.transitionNotesRemoved;
    electronicReport.score = std::clamp(electronicReport.score * 0.72 +
        musicalIdentity.score * 0.28, 0.0, 1.0);
    ElectronicProductionDirector::stamp(song, electronicReport);
    song.electronicProductionAudited = electronicReport.active;
    song.electronicProductionScore = electronicReport.active ? electronicReport.score : 0.0;
    const auto narrativeReport = NarrativeScoreGate::audit(song, plan);
    NarrativeScoreGate::stamp(song, narrativeReport);
    if (renderReport != nullptr) {
        renderReport->orchestration = orchestrationReport;
        renderReport->finalTonalPass = publishedTonalReport;
        renderReport->unintendedSilenceWindowsRepaired = continuityReport.windowsRepaired;
        renderReport->sparseStructuralWindowsRepaired = structuralContinuity.windowsRepaired;
        renderReport->structuralContinuityNotesCreated = structuralContinuity.notesCreated;
        renderReport->extendedForegroundWindowsRepaired = foregroundContinuity.windowsRepaired;
        renderReport->foregroundContinuityNotesCreated = foregroundContinuity.notesCreated;
        renderReport->earlyRhythmNotesCreated = earlyRhythm.notesCreated;
        renderReport->audibleDurationRepairs = audibleDurationReport.durationsRepaired;
        renderReport->inaudibleNotesRemoved = audibleDurationReport.notesRemoved;
        renderReport->longestGlobalSilenceBefore = continuityReport.longestBefore;
        renderReport->longestGlobalSilenceAfter = continuityReport.longestAfter;
        renderReport->expression = expressionReport;
        renderReport->production = productionReport;
        renderReport->production.ready = song.productionReady;
        renderReport->electronicProduction = electronicReport;
        renderReport->musicalIdentity = musicalIdentity;
        renderReport->narrative = narrativeReport;
        renderReport->verticalHarmony = verticalHarmony;
    }
    std::sort(song.controls.begin(), song.controls.end(), [](const auto& left, const auto& right) {
        if (left.beat != right.beat) return left.beat < right.beat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.controller < right.controller;
    });
    return song;
}

} // namespace pulso
