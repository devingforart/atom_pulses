#include "ElectronicProductionDirector.h"

#include "OrchestrationScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <tuple>

namespace pulso {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const std::string& text, std::initializer_list<std::string_view> terms) {
    return std::any_of(terms.begin(), terms.end(), [&](const auto term) {
        return text.find(term) != std::string::npos;
    });
}

double clamp01(double value) { return std::clamp(std::isfinite(value) ? value : 0.5, 0.0, 1.0); }

InstrumentAssignment electronicPart(std::string id, std::string instrument, std::string name,
                                    VoiceId voice, std::string role, double activity,
                                    double prominence, std::string function,
                                    std::string device, std::string preset) {
    const auto* definition = instrumentDefinition(instrument);
    InstrumentAssignment result;
    result.id = std::move(id);
    result.instrumentId = std::move(instrument);
    result.name = std::move(name);
    result.sourceVoice = voice;
    result.role = std::move(role);
    result.minimumPitch = definition == nullptr ? voiceDefinition(voice).minimumPitch : definition->minimumPitch;
    result.maximumPitch = definition == nullptr ? voiceDefinition(voice).maximumPitch : definition->maximumPitch;
    result.activity = activity;
    result.prominence = prominence;
    result.doubling = 0.0;
    result.orchestralFunction = std::move(function);
    result.articulation = isVoiceInFamily(voice, VoiceFamily::Rhythm) ? "detached" : "natural";
    result.divisiVoices = 1;
    result.liveDevice = std::move(device);
    result.livePresetIntent = std::move(preset);
    return result;
}

std::vector<InstrumentAssignment> electronicParts() {
    std::vector<InstrumentAssignment> result;
    result.push_back(electronicPart("club_kick", "kick_drum", "Club Kick", VoiceId::CoreDrums,
        "Low-frequency pulse and physical anchor", .92, 1.0, "foundation", "Drum Rack", "tight club kick with controlled sub tail"));
    result.push_back(electronicPart("club_backbeat", "snare_clap", "Backbeat", VoiceId::SnareClap,
        "Backbeat, accent and sectional release", .66, .82, "body", "Drum Rack", "dry layered clap snare with short tail"));
    result.push_back(electronicPart("club_closed_hats", "hi_hats", "Closed Hats", VoiceId::ClosedHats,
        "High-frequency groove and changing subdivision", .68, .62, "body", "Drum Rack", "short detailed closed hats with tonal variation"));
    result.push_back(electronicPart("club_open_hats", "shakers", "Open Hats and Shaker", VoiceId::OpenHatsShaker,
        "Offbeat lift, shuffle and phrase breath", .54, .52, "extension", "Drum Rack", "open hats and shaker layer with multiple articulations"));
    result.push_back(electronicPart("club_low_percussion", "latin_percussion", "Low Percussion", VoiceId::LowPercussion,
        "Syncopated low-mid conversation", .46, .48, "counterpoint", "Drum Rack", "low conga tom and muted percussion one shots"));
    result.push_back(electronicPart("club_high_percussion", "orchestral_percussion", "High Percussion", VoiceId::HighPercussion,
        "Sparse metallic and skin responses", .42, .42, "color", "Drum Rack", "short rim metallic and hand percussion one shots"));
    result.push_back(electronicPart("club_sub", "sub_synth", "Sub", VoiceId::SubBass,
        "Fundamental low end with protected kick space", .76, .92, "foundation", "Operator", "clean mono sine sub with short release"));
    result.push_back(electronicPart("club_bass_groove", "electric_bass", "Bass Groove", VoiceId::MovementBass,
        "Syncopated bass response around the kick", .72, .82, "body", "Wavetable", "warm mono electronic bass with controlled transient"));
    result.push_back(electronicPart("club_chord_body", "poly_synth", "Chord Body", VoiceId::HarmonicFoundation,
        "Sparse chord body and harmonic identity", .46, .65, "body", "Wavetable", "warm restrained poly synth chord body"));
    result.push_back(electronicPart("club_stab", "poly_synth", "Chord Stab", VoiceId::HarmonicPulse,
        "Rhythmic harmonic punctuation", .48, .60, "color", "Drift", "short percussive chord stab with filter movement"));
    result.push_back(electronicPart("club_upper", "analog_pad", "Upper Air", VoiceId::HarmonicUpper,
        "Selective extension and air", .30, .38, "extension", "Wavetable", "thin high spectral pad leaving hook space"));
    result.push_back(electronicPart("club_hook", "lead_synth", "Primary Hook", VoiceId::Lead,
        "One memorable foreground identity", .52, .88, "body", "Meld", "expressive mono club hook with evolving timbre"));
    result.push_back(electronicPart("club_response", "lead_synth", "Hook Response", VoiceId::Countermelody,
        "Short answer in the hook's negative space", .28, .52, "counterpoint", "Drift", "compact filtered response pluck"));
    result.push_back(electronicPart("club_atmosphere", "ambient_texture", "Atmosphere", VoiceId::Atmosphere,
        "Depth, continuity and breakdown space", .34, .34, "transition", "Granulator III", "cohesive low-density electronic atmosphere"));
    result.push_back(electronicPart("club_transitions", "cymbals", "Transitions and FX", VoiceId::Transitions,
        "Signal structural arrivals without constant activity", .25, .44, "transition", "Drum Rack", "short impacts cymbals noise rises and reverses"));
    return result;
}

void mergeElectronicInfrastructure(SongPlan& plan) {
    auto required = electronicParts();
    if (plan.instruments.empty()) {
        plan.instruments = std::move(required);
        return;
    }
    for (auto& fallback : required) {
        const auto hardIdentity = fallback.sourceVoice == VoiceId::CoreDrums ||
            fallback.sourceVoice == VoiceId::SnareClap || fallback.sourceVoice == VoiceId::ClosedHats ||
            fallback.sourceVoice == VoiceId::SubBass;
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& authored) {
                return hardIdentity ? authored.instrumentId == fallback.instrumentId
                                    : authored.sourceVoice == fallback.sourceVoice;
            });
        if (owner == plan.instruments.end()) {
            // A structured AI cast is closed. An omitted lane is an intentional rest or an
            // authorship defect for the critic to repair; silently adding Upper Air or
            // generic High Percussion is exactly how unrelated parts accumulated before.
            if (plan.instrumentCastAuthored) continue;
            plan.instruments.push_back(std::move(fallback));
            continue;
        }
        // GPT owns the orchestration and timbre. The director supplies only missing club
        // infrastructure and strengthens playback metadata that the model left blank.
        if (owner->role.empty()) owner->role = fallback.role;
        if (owner->liveDevice.empty() || owner->liveDevice == "auto")
            owner->liveDevice = fallback.liveDevice;
        if (owner->livePresetIntent.empty()) owner->livePresetIntent = fallback.livePresetIntent;
    }
}

bool sectionNamed(const SongSection& section, std::initializer_list<std::string_view> names) {
    const auto text = lower(section.name + " " + section.function);
    return containsAny(text, names);
}

void addUnique(std::vector<VoiceId>& voices, VoiceId voice) {
    if (std::find(voices.begin(), voices.end(), voice) == voices.end()) voices.push_back(voice);
}

std::size_t lowEndCollisions(const Pattern& pattern) {
    std::vector<double> kicks;
    for (const auto& note : pattern.notes)
        if (note.voice == VoiceId::CoreDrums && (note.pitch == 35 || note.pitch == 36))
            kicks.push_back(note.startBeat);
    return static_cast<std::size_t>(std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (note.voice != VoiceId::SubBass && note.voice != VoiceId::MovementBass) return false;
        return std::any_of(kicks.begin(), kicks.end(), [&](double kick) {
            return std::abs(note.startBeat - kick) < 0.08;
        });
    }));
}

struct RhythmRun { std::size_t literal{}; std::size_t maximum{}; };

RhythmRun rhythmRuns(const Pattern& pattern, double beatsPerBar) {
    using Signature = std::vector<std::pair<int, int>>;
    std::map<std::pair<VoiceId, int>, Signature> signatures;
    for (const auto& note : pattern.notes) {
        if (!isVoiceInFamily(note.voice, VoiceFamily::Rhythm) || note.voice == VoiceId::CoreDrums) continue;
        const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        signatures[{note.voice, bar}].push_back({
            static_cast<int>(std::lround((note.startBeat - bar * beatsPerBar) * 16.0)), note.pitch});
    }
    RhythmRun result;
    const auto bars = static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar));
    for (std::size_t index = 0; index < static_cast<std::size_t>(VoiceId::Count); ++index) {
        const auto voice = static_cast<VoiceId>(index);
        if (!isVoiceInFamily(voice, VoiceFamily::Rhythm) || voice == VoiceId::CoreDrums) continue;
        Signature previous;
        std::size_t run{};
        for (auto bar = 0; bar < bars; ++bar) {
            auto current = signatures[{voice, bar}];
            std::sort(current.begin(), current.end());
            if (current.empty()) { previous.clear(); run = 0; continue; }
            if (current == previous) { ++result.literal; ++run; } else run = 1;
            result.maximum = std::max(result.maximum, run);
            previous = std::move(current);
        }
    }
    return result;
}

bool electronicCoreActive(const ProductionLanguage& language) {
    return language.electronicIntent >= 0.58 &&
           (language.domain == ProductionDomain::ClubElectronic ||
            language.domain == ProductionDomain::Hybrid);
}

std::size_t evolveLiteralRhythm(Pattern& pattern, double beatsPerBar) {
    using Signature = std::vector<std::pair<int, int>>;
    std::map<std::pair<VoiceId, int>, std::vector<std::size_t>> events;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (!isVoiceInFamily(note.voice, VoiceFamily::Rhythm) || note.voice == VoiceId::CoreDrums) continue;
        const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        events[{note.voice, bar}].push_back(index);
    }
    std::set<std::size_t> remove;
    const auto bars = static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar));
    for (std::size_t voiceIndex = 0; voiceIndex < static_cast<std::size_t>(VoiceId::Count); ++voiceIndex) {
        const auto voice = static_cast<VoiceId>(voiceIndex);
        if (!isVoiceInFamily(voice, VoiceFamily::Rhythm) || voice == VoiceId::CoreDrums) continue;
        Signature previous;
        std::size_t run{};
        for (auto bar = 0; bar < bars; ++bar) {
            const auto found = events.find({voice, bar});
            Signature current;
            if (found != events.end()) {
                for (const auto index : found->second) {
                    const auto& note = pattern.notes[index];
                    current.push_back({static_cast<int>(std::lround(
                        (note.startBeat - bar * beatsPerBar) * 16.0)), note.pitch});
                }
            }
            std::sort(current.begin(), current.end());
            if (current.empty()) { previous.clear(); run = 0; continue; }
            run = current == previous ? run + 1 : 1;
            if (run > 4 && found != events.end() && found->second.size() > 1) {
                // A deterministic phrase edit breaks the copied bar while preserving its
                // pulse and identity. The AI still authors the groove; this is a critic
                // constraint against ten-bar copy/paste percussion.
                const auto candidate = std::min_element(found->second.begin(), found->second.end(),
                    [&](const auto left, const auto right) {
                        const auto& a = pattern.notes[left];
                        const auto& b = pattern.notes[right];
                        if (a.velocity != b.velocity) return a.velocity < b.velocity;
                        return a.startBeat > b.startBeat;
                    });
                remove.insert(*candidate);
                previous.clear();
                run = 0;
            } else {
                previous = std::move(current);
            }
        }
    }
    if (remove.empty()) return 0;
    std::vector<NoteEvent> evolved;
    evolved.reserve(pattern.notes.size() - remove.size());
    for (std::size_t index = 0; index < pattern.notes.size(); ++index)
        if (!remove.contains(index)) evolved.push_back(pattern.notes[index]);
    pattern.notes = std::move(evolved);
    return remove.size();
}

const SongSection* sectionAtBeat(const SongPlan& plan, double beat) {
    const auto bar = static_cast<int>(std::floor(beat / plan.beatsPerBar));
    const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [&](const auto& section) {
        return bar >= section.startBar && bar < section.startBar + section.bars;
    });
    return found == plan.sections.end() ? nullptr : &*found;
}

bool explicitKickOrnament(const SongPlan& plan, double beat) {
    constexpr auto tolerance = 0.04;
    for (const auto& section : plan.sections) {
        const auto scale = plan.beatsPerBar / 4.0;
        for (const auto& gesture : section.rhythm.gestures) {
            const auto barStart = (section.startBar + gesture.barOffset) * plan.beatsPerBar;
            if (gesture.kind == RhythmGestureKind::DoubleKick &&
                std::abs(beat - (barStart + gesture.beat * scale)) < tolerance) return true;
            if (gesture.kind == RhythmGestureKind::PickupFill &&
                (std::abs(beat - (barStart + 3.50 * scale)) < tolerance ||
                 std::abs(beat - (barStart + 3.75 * scale)) < tolerance)) return true;
        }
    }
    return false;
}

std::size_t restrainKickOrnaments(Pattern& pattern, const SongPlan& plan) {
    std::map<int, std::vector<std::size_t>> candidates;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (note.voice != VoiceId::CoreDrums || (note.pitch != 35 && note.pitch != 36)) continue;
        const auto* section = sectionAtBeat(plan, note.startBeat);
        if (section == nullptr || section->rhythm.kickState != KickState::FourOnFloor) continue;
        const auto beatScale = plan.beatsPerBar / 4.0;
        const auto metric = note.startBeat / beatScale;
        if (std::abs(metric - std::round(metric)) < 0.04 || explicitKickOrnament(plan, note.startBeat)) continue;
        candidates[static_cast<int>(std::floor(note.startBeat / (plan.beatsPerBar * 8.0)))].push_back(index);
    }
    std::set<std::size_t> remove;
    for (auto& [phrase, indices] : candidates) {
        (void) phrase;
        if (indices.size() <= 1) continue;
        const auto keep = *std::max_element(indices.begin(), indices.end(), [&](auto left, auto right) {
            const auto& a = pattern.notes[left];
            const auto& b = pattern.notes[right];
            if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
            return a.velocity < b.velocity;
        });
        for (const auto index : indices) if (index != keep) remove.insert(index);
    }
    if (remove.empty()) return 0;
    std::vector<NoteEvent> retained;
    retained.reserve(pattern.notes.size() - remove.size());
    for (std::size_t index = 0; index < pattern.notes.size(); ++index)
        if (!remove.contains(index)) retained.push_back(pattern.notes[index]);
    pattern.notes = std::move(retained);
    return remove.size();
}

bool declaredFullSilence(const SongSection* section) {
    if (section == nullptr) return false;
    const auto text = lower(section->name + " " + section->function);
    if (containsAny(text, {"without full silence", "without complete silence",
                           "no full silence", "sin silencio total"})) return false;
    return containsAny(text, {"full silence", "complete silence", "silencio total"});
}

std::size_t maximumKicklessBars(const Pattern& pattern, const SongPlan& plan) {
    auto run = std::size_t{};
    auto maximum = std::size_t{};
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        const auto end = start + plan.beatsPerBar;
        const auto* section = sectionAtBeat(plan, start);
        if (declaredFullSilence(section)) { run = 0; continue; }
        const auto present = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::CoreDrums && (note.pitch == 35 || note.pitch == 36) &&
                   note.startBeat >= start && note.startBeat < end;
        });
        run = present ? 0 : run + 1;
        maximum = std::max(maximum, run);
    }
    return maximum;
}

std::size_t createMacroKickAnchors(Pattern& pattern, const SongPlan& plan) {
    if (plan.productionLanguage.domain != ProductionDomain::ClubElectronic ||
        plan.productionLanguage.clubFocus < 0.55 || plan.beatsPerBar <= 0.0) return 0;
    constexpr auto maximumGapBars = std::size_t{16};
    auto run = std::size_t{};
    auto created = std::size_t{};
    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        const auto end = start + plan.beatsPerBar;
        const auto* section = sectionAtBeat(plan, start);
        if (declaredFullSilence(section)) { run = 0; continue; }
        const auto present = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::CoreDrums && (note.pitch == 35 || note.pitch == 36) &&
                   note.startBeat >= start && note.startBeat < end;
        });
        if (present) { run = 0; continue; }
        if (++run <= maximumGapBars) continue;
        const auto scale = plan.beatsPerBar / 4.0;
        for (const auto quarter : {0.0, 1.0, 2.0, 3.0})
            pattern.notes.push_back({start + quarter * scale, 0.125, 36,
                quarter == 0.0 ? 101 : 94, 10, VoiceId::CoreDrums, 0, true});
        run = 0;
        ++created;
    }
    return created;
}

std::size_t createKickPhraseDevelopment(Pattern& pattern, const SongPlan& plan) {
    if (plan.beatsPerBar <= 0.0 || plan.productionLanguage.grooveEvolution < 0.42) return 0;
    constexpr std::array positions{0.75, 1.75, 2.75, 3.50};
    std::size_t created{};
    auto phraseOrdinal = 0;
    for (const auto& section : plan.sections) {
        if (section.rhythm.kickState != KickState::FourOnFloor) continue;
        for (auto phraseStart = 0; phraseStart < section.bars; phraseStart += 8, ++phraseOrdinal) {
            const auto phraseBars = std::min(8, section.bars - phraseStart);
            if (phraseBars < 4) continue;
            const auto phraseBeatStart = (section.startBar + phraseStart) * plan.beatsPerBar;
            const auto phraseBeatEnd = phraseBeatStart + phraseBars * plan.beatsPerBar;
            const auto establishedAnchors = std::count_if(pattern.notes.begin(), pattern.notes.end(),
                [&](const auto& note) {
                    if (note.voice != VoiceId::CoreDrums || note.pitch != 36 ||
                        note.startBeat < phraseBeatStart || note.startBeat >= phraseBeatEnd)
                        return false;
                    const auto metric = note.startBeat / (plan.beatsPerBar / 4.0);
                    return std::abs(metric - std::round(metric)) < 0.04;
                });
            if (establishedAnchors < phraseBars * 3) continue;
            const auto targetBar = phraseStart + phraseBars - 1;
            const auto authoredKickGesture = std::any_of(section.rhythm.gestures.begin(),
                section.rhythm.gestures.end(), [&](const auto& gesture) {
                    return gesture.barOffset == targetBar &&
                        (gesture.kind == RhythmGestureKind::DoubleKick ||
                         gesture.kind == RhythmGestureKind::PickupFill ||
                         gesture.kind == RhythmGestureKind::DropLastKick ||
                         gesture.kind == RhythmGestureKind::HalfBarMute ||
                         gesture.kind == RhythmGestureKind::FullBarMute);
                });
            if (authoredKickGesture) continue;
            // The plan's groove-evolution dimension controls how often the critic is
            // allowed to add one phrase punctuation. Stable styles keep every other
            // phrase literal; animated styles may develop each phrase.
            if (plan.productionLanguage.grooveEvolution < 0.72 && phraseOrdinal % 2 != 0)
                continue;
            const auto selector = positiveModulo(static_cast<int>(plan.seed & 0x7fffffffULL) +
                                                 phraseOrdinal * 3 + section.startBar,
                                                 static_cast<int>(positions.size()));
            const auto position = positions[static_cast<std::size_t>(selector)];
            const auto start = (section.startBar + targetBar) * plan.beatsPerBar +
                               position * plan.beatsPerBar / 4.0;
            const auto collision = std::any_of(pattern.notes.begin(), pattern.notes.end(),
                [&](const auto& note) {
                    return note.voice == VoiceId::CoreDrums &&
                           std::abs(note.startBeat - start) < 0.04;
                });
            if (collision || start >= pattern.lengthBeats) continue;
            pattern.notes.push_back({start, 0.055, 36,
                std::clamp(78 + positiveModulo(phraseOrdinal * 7, 16), 1, 127), 10,
                VoiceId::CoreDrums, 0, false});
            ++created;
        }
    }
    return created;
}

std::size_t createPhraseVariations(Pattern& pattern, double beatsPerBar) {
    std::map<std::pair<VoiceId, int>, std::vector<std::size_t>> byBar;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto voice = pattern.notes[index].voice;
        if (voice != VoiceId::ClosedHats && voice != VoiceId::OpenHatsShaker &&
            voice != VoiceId::LowPercussion && voice != VoiceId::HighPercussion) continue;
        const auto bar = static_cast<int>(std::floor(pattern.notes[index].startBeat / beatsPerBar));
        if (positiveModulo(bar, 8) != 5) continue;
        byBar[{voice, bar}].push_back(index);
    }
    std::size_t changed{};
    for (const auto& [owner, indices] : byBar) {
        if (indices.empty()) continue;
        const auto selected = *std::min_element(indices.begin(), indices.end(), [&](auto left, auto right) {
            return pattern.notes[left].velocity < pattern.notes[right].velocity;
        });
        auto& note = pattern.notes[selected];
        const auto barStart = owner.second * beatsPerBar;
        const auto direction = positiveModulo(owner.second / 8 + static_cast<int>(owner.first), 2) == 0 ? 0.25 : -0.25;
        auto target = std::clamp(note.startBeat + direction, barStart, barStart + beatsPerBar - 0.25);
        const auto collision = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& other) {
            return &other != &note && other.voice == note.voice && other.pitch == note.pitch &&
                   std::abs(other.startBeat - target) < 0.02;
        });
        if (collision) target = std::clamp(note.startBeat - direction, barStart, barStart + beatsPerBar - 0.25);
        if (std::abs(target - note.startBeat) > 0.02) { note.startBeat = target; ++changed; }
    }
    return changed;
}

std::pair<std::size_t, std::size_t> developRepeatedBassPhrases(
        Pattern& pattern, const SongPlan& plan) {
    if (plan.productionLanguage.domain != ProductionDomain::ClubElectronic ||
        plan.productionLanguage.grooveEvolution < 0.35) return {};
    using Signature = std::vector<std::tuple<int, int, int>>;
    const auto phraseBeats = plan.beatsPerBar * 8.0;
    const auto phrases = static_cast<int>(std::ceil(pattern.lengthBeats / phraseBeats));
    std::map<std::uint16_t, std::set<Signature>> seen;
    std::size_t developedPhrases{};
    std::size_t developedNotes{};
    for (const auto& part : pattern.parts) {
        if (part.sourceVoice != VoiceId::MovementBass) continue;
        for (auto phrase = 0; phrase < phrases; ++phrase) {
            const auto start = phrase * phraseBeats;
            const auto end = std::min(pattern.lengthBeats, start + phraseBeats);
            std::vector<std::size_t> indices;
            Signature signature;
            for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
                const auto& note = pattern.notes[index];
                if (note.partId != part.id || note.startBeat < start || note.startBeat >= end) continue;
                indices.push_back(index);
                signature.emplace_back(
                    static_cast<int>(std::lround((note.startBeat - start) * 4.0)),
                    note.pitch, static_cast<int>(std::lround(note.durationBeats * 8.0)));
            }
            std::sort(signature.begin(), signature.end());
            if (signature.empty() || seen[part.id].insert(signature).second) continue;
            const auto developmentStart = start + phraseBeats - plan.beatsPerBar * 2.0;
            std::vector<std::size_t> candidates;
            std::copy_if(indices.begin(), indices.end(), std::back_inserter(candidates), [&](auto index) {
                return pattern.notes[index].startBeat >= developmentStart;
            });
            std::sort(candidates.begin(), candidates.end(), [&](auto left, auto right) {
                const auto& a = pattern.notes[left];
                const auto& b = pattern.notes[right];
                if (a.velocity != b.velocity) return a.velocity < b.velocity;
                return a.startBeat > b.startBeat;
            });
            auto changed = false;
            for (const auto index : candidates) {
                auto& note = pattern.notes[index];
                const auto barStart = std::floor(note.startBeat / plan.beatsPerBar) * plan.beatsPerBar;
                const auto direction = positiveModulo(phrase + static_cast<int>(part.id), 2) == 0
                    ? 0.25 : -0.25;
                for (const auto offset : {direction, -direction}) {
                    const auto target = note.startBeat + offset;
                    if (target < barStart + 0.249 || target > barStart + plan.beatsPerBar - 0.249)
                        continue;
                    const auto occupied = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& other) {
                        if (&other == &note) return false;
                        if (other.voice == VoiceId::CoreDrums && (other.pitch == 35 || other.pitch == 36) &&
                            std::abs(other.startBeat - target) < 0.08) return true;
                        return other.partId == part.id && std::abs(other.startBeat - target) < 0.10;
                    });
                    if (occupied) continue;
                    note.startBeat = target;
                    note.durationBeats = std::clamp(note.durationBeats +
                        (offset > 0.0 ? -0.125 : 0.125), 0.125, 0.50);
                    changed = true;
                    ++developedNotes;
                    break;
                }
                if (changed) break;
            }
            if (changed) ++developedPhrases;
        }
    }
    return {developedPhrases, developedNotes};
}

std::size_t createHarmonicPhraseBreaths(Pattern& pattern, double beatsPerBar) {
    std::map<int, std::vector<std::size_t>> byBar;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (note.voice != VoiceId::HarmonicFoundation) continue;
        const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        if (positiveModulo(bar, 8) == 7) byBar[bar].push_back(index);
    }
    std::set<std::size_t> remove;
    for (const auto& [bar, indices] : byBar) {
        (void) bar;
        if (indices.empty()) continue;
        remove.insert(*std::max_element(indices.begin(), indices.end(), [&](auto left, auto right) {
            if (pattern.notes[left].pitch != pattern.notes[right].pitch)
                return pattern.notes[left].pitch < pattern.notes[right].pitch;
            return pattern.notes[left].velocity > pattern.notes[right].velocity;
        }));
    }
    if (remove.empty()) return 0;
    std::vector<NoteEvent> retained;
    retained.reserve(pattern.notes.size() - remove.size());
    for (std::size_t index = 0; index < pattern.notes.size(); ++index)
        if (!remove.contains(index)) retained.push_back(pattern.notes[index]);
    pattern.notes = std::move(retained);
    return remove.size();
}

std::size_t rotatePeakSupport(Pattern& pattern, const SongPlan& plan) {
    constexpr std::array rotations{
        std::array{VoiceId::HighPercussion, VoiceId::HarmonicUpper, VoiceId::Countermelody},
        std::array{VoiceId::LowPercussion, VoiceId::OpenHatsShaker, VoiceId::Atmosphere},
        std::array{VoiceId::ClosedHats, VoiceId::HarmonicPulse, VoiceId::Countermelody},
        std::array{VoiceId::OpenHatsShaker, VoiceId::HarmonicUpper, VoiceId::LowPercussion}};
    const auto before = pattern.notes.size();
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        const auto* section = sectionAtBeat(plan, note.startBeat);
        if (section == nullptr || section->bars < 16 || section->energy < 0.72 || section->density < 0.68)
            return false;
        const auto bar = static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar));
        const auto phrase = positiveModulo((bar - section->startBar) / 8, static_cast<int>(rotations.size()));
        const auto& muted = rotations[static_cast<std::size_t>(phrase)];
        return std::find(muted.begin(), muted.end(), note.voice) != muted.end();
    }), pattern.notes.end());
    return before - pattern.notes.size();
}

std::vector<int> scalePitchClasses(const SongPlan& plan) {
    constexpr std::array minor{0, 2, 3, 5, 7, 8, 10};
    constexpr std::array major{0, 2, 4, 5, 7, 9, 11};
    constexpr std::array dorian{0, 2, 3, 5, 7, 9, 10};
    constexpr std::array mixolydian{0, 2, 4, 5, 7, 9, 10};
    const auto* intervals = &minor;
    if (plan.scale == ScaleKind::Major) intervals = &major;
    else if (plan.scale == ScaleKind::Dorian) intervals = &dorian;
    else if (plan.scale == ScaleKind::Mixolydian) intervals = &mixolydian;
    std::vector<int> result;
    for (const auto interval : *intervals) result.push_back(positiveModulo(plan.rootPitchClass + interval, 12));
    return result;
}

int nearestScalePitch(int target, int minimum, int maximum, const std::vector<int>& scale) {
    auto best = std::clamp(target, minimum, maximum);
    auto distance = 1000;
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (std::find(scale.begin(), scale.end(), positiveModulo(pitch, 12)) == scale.end()) continue;
        const auto candidate = std::abs(pitch - target);
        if (candidate < distance) { distance = candidate; best = pitch; }
    }
    return best;
}

std::pair<std::size_t, std::size_t> reinforceThematicMemory(Pattern& pattern,
                                                            const SongPlan& plan) {
    const auto windowBeats = plan.beatsPerBar * 4.0;
    const auto windows = static_cast<int>(std::ceil(pattern.lengthBeats / windowBeats));
    std::vector<std::vector<NoteEvent>> material(static_cast<std::size_t>(windows));
    for (const auto& note : pattern.notes) {
        if (note.voice != VoiceId::Lead) continue;
        const auto window = static_cast<int>(std::floor(note.startBeat / windowBeats));
        if (window >= 0 && window < windows) material[static_cast<std::size_t>(window)].push_back(note);
    }
    const auto canonicalWindow = std::find_if(material.begin(), material.end(), [](const auto& notes) {
        return notes.size() >= 3 && notes.size() <= 16;
    });
    if (canonicalWindow == material.end()) return {};
    auto canonical = *canonicalWindow;
    std::sort(canonical.begin(), canonical.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    const auto sourceWindow = static_cast<int>(std::distance(material.begin(), canonicalWindow));
    const auto sourceStart = sourceWindow * windowBeats;
    const auto sourceAnchor = canonical.front().pitch;
    const auto scale = scalePitchClasses(plan);
    const auto& definition = voiceDefinition(VoiceId::Lead);
    std::set<int> replaceWindows;
    auto eligibleOrdinal = 0;
    for (auto window = sourceWindow + 1; window < windows; ++window) {
        if (material[static_cast<std::size_t>(window)].size() < 3) continue;
        ++eligibleOrdinal;
        if (eligibleOrdinal % 2 == 1) replaceWindows.insert(window);
    }
    if (replaceWindows.empty()) return {};
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (note.voice != VoiceId::Lead) return false;
        return replaceWindows.contains(static_cast<int>(std::floor(note.startBeat / windowBeats)));
    }), pattern.notes.end());
    std::size_t notesCreated{};
    for (const auto window : replaceWindows) {
        const auto targetStart = window * windowBeats;
        auto targetAnchor = material[static_cast<std::size_t>(window)].front().pitch;
        const auto transpose = targetAnchor - sourceAnchor;
        for (const auto& source : canonical) {
            const auto relative = source.startBeat - sourceStart;
            if (relative < -0.001 || relative >= windowBeats || targetStart + relative >= pattern.lengthBeats) continue;
            auto recalled = source;
            recalled.startBeat = targetStart + relative;
            recalled.pitch = nearestScalePitch(source.pitch + transpose,
                                                definition.minimumPitch, definition.maximumPitch, scale);
            recalled.velocity = std::clamp(source.velocity - 3 + positiveModulo(window, 3) * 3, 1, 127);
            recalled.partId = 0;
            pattern.notes.push_back(recalled);
            ++notesCreated;
        }
    }
    return {replaceWindows.size(), notesCreated};
}

struct ThematicAudit { std::size_t windows{}; std::size_t recurring{}; double ratio{}; };

ThematicAudit auditThematicMemory(const Pattern& pattern, double beatsPerBar) {
    using Signature = std::vector<std::pair<int, int>>;
    const auto windowBeats = beatsPerBar * 4.0;
    const auto windows = static_cast<int>(std::ceil(pattern.lengthBeats / windowBeats));
    std::map<Signature, std::size_t> occurrences;
    ThematicAudit result;
    for (auto window = 0; window < windows; ++window) {
        std::vector<NoteEvent> notes;
        const auto start = window * windowBeats;
        for (const auto& note : pattern.notes)
            if (note.voice == VoiceId::Lead && note.startBeat >= start && note.startBeat < start + windowBeats)
                notes.push_back(note);
        if (notes.size() < 3) continue;
        std::sort(notes.begin(), notes.end(), [](const auto& left, const auto& right) {
            return left.startBeat < right.startBeat;
        });
        Signature signature;
        for (std::size_t index = 0; index < notes.size(); ++index) {
            const auto onset = static_cast<int>(std::lround((notes[index].startBeat - start) * 4.0));
            const auto contour = index == 0 ? 0 :
                (notes[index].pitch > notes[index - 1].pitch ? 1 : notes[index].pitch < notes[index - 1].pitch ? -1 : 0);
            signature.push_back({onset, contour});
        }
        ++result.windows;
        auto& count = occurrences[signature];
        if (count++ > 0) ++result.recurring;
    }
    result.ratio = static_cast<double>(result.recurring) / std::max<std::size_t>(1, result.windows);
    return result;
}

std::size_t maximumRunForVoice(const Pattern& pattern, VoiceId voice, double beatsPerBar) {
    using Signature = std::vector<std::pair<int, int>>;
    Signature previous;
    std::size_t run{}, maximum{};
    const auto bars = static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar));
    for (auto bar = 0; bar < bars; ++bar) {
        Signature current;
        for (const auto& note : pattern.notes)
            if (note.voice == voice && note.startBeat >= bar * beatsPerBar && note.startBeat < (bar + 1) * beatsPerBar)
                current.push_back({static_cast<int>(std::lround((note.startBeat - bar * beatsPerBar) * 16.0)), note.pitch});
        std::sort(current.begin(), current.end());
        if (current.empty()) { previous.clear(); run = 0; continue; }
        run = current == previous ? run + 1 : 1;
        maximum = std::max(maximum, run);
        previous = std::move(current);
    }
    return maximum;
}

} // namespace

ProductionLanguage ElectronicProductionDirector::infer(std::string_view direction) {
    ProductionLanguage result;
    const auto text = lower(std::string(direction));
    const auto club = containsAny(text, {"club", "dancefloor", "dance floor", "boliche", "pista de baile",
        "house", "techno", "electronic", "electronica", "electrónica", "rave", "warehouse", "dj",
        "progressive", "trance", "garage", "breakbeat", "electro", "disco"});
    const auto orchestral = containsAny(text, {"orchestra", "orchestral", "symphony", "symphonic",
        "orquesta", "orquestal", "sinfonia", "sinfonía", "concerto", "concierto", "chamber ensemble"});
    if (club && !orchestral) result.domain = ProductionDomain::ClubElectronic;
    else if (club && orchestral) result.domain = ProductionDomain::Hybrid;
    else if (orchestral) result.domain = ProductionDomain::Orchestral;
    result.electronicIntent = club ? 0.92 : containsAny(text, {"synth", "analog", "machine"}) ? 0.68 : 0.24;
    result.clubFocus = club ? 0.88 : 0.18;
    result.lowEndInterlock = club ? 0.88 : 0.55;
    result.grooveEvolution = club ? 0.82 : 0.55;
    result.hookEconomy = containsAny(text, {"minimal", "hypnotic", "hipnot"}) ? 0.88 : club ? 0.78 : 0.62;
    result.automationMotion = club ? 0.82 : 0.52;
    result.djUtility = containsAny(text, {"dj", "club", "boliche", "dancefloor", "pista de baile"}) ? 0.92 : club ? 0.72 : 0.20;
    result.spectralRestraint = club ? 0.82 : 0.62;
    result.orchestralAllowance = orchestral ? (club ? 0.55 : 0.96) : 0.10;
    result.description = club ? "Electronic production grammar driven by low-end, groove, hook economy and sectional energy"
                              : orchestral ? "Orchestral production grammar driven by ensemble dialogue"
                                           : "Adaptive production direction";
    return result;
}

void ElectronicProductionDirector::normalizePlan(SongPlan& plan) {
    auto& language = plan.productionLanguage;
    for (auto* value : {&language.electronicIntent, &language.clubFocus, &language.lowEndInterlock,
                        &language.grooveEvolution, &language.hookEconomy, &language.automationMotion,
                        &language.djUtility, &language.spectralRestraint, &language.orchestralAllowance})
        *value = clamp01(*value);
    if (language.description.empty()) language.description = "Adaptive production direction";
    if (!electronicCoreActive(language)) return;

    const auto club = language.domain == ProductionDomain::ClubElectronic;
    plan.timbrePalette.acousticElectronicBalance = std::max(plan.timbrePalette.acousticElectronicBalance,
                                                             club ? 0.86 : 0.70);
    plan.timbrePalette.transientDefinition = std::max(plan.timbrePalette.transientDefinition, 0.70);
    plan.timbrePalette.cohesion = std::max(plan.timbrePalette.cohesion, 0.84);
    const auto electronicMaterial =
        "cohesive electronic low end, tactile transients, evolving synthesis and restrained spectral layers";
    if (plan.timbrePalette.material.find(electronicMaterial) == std::string::npos)
        plan.timbrePalette.material += "; " + std::string(electronicMaterial);
    const auto electronicSpace =
        "mono-compatible low end with short groove space and automated depth at transitions";
    if (plan.timbrePalette.space.find(electronicSpace) == std::string::npos)
        plan.timbrePalette.space += "; " + std::string(electronicSpace);
    plan.orchestrationLanguage.ensembleScale = std::min(plan.orchestrationLanguage.ensembleScale,
                                                         club ? 0.56 : 0.82);
    plan.orchestrationLanguage.counterpointActivity = std::min(plan.orchestrationLanguage.counterpointActivity, 0.40);
    plan.orchestrationLanguage.divisiDepth = std::min(plan.orchestrationLanguage.divisiDepth, 0.24);
    plan.orchestrationLanguage.tuttiRarity = std::max(plan.orchestrationLanguage.tuttiRarity, 0.90);
    plan.orchestrationLanguage.doublingRestraint = std::max(plan.orchestrationLanguage.doublingRestraint, 0.90);
    plan.orchestrationLanguage.hybridProduction = std::max(plan.orchestrationLanguage.hybridProduction,
                                                            club ? 0.92 : 0.84);
    if (!club) return;
    mergeElectronicInfrastructure(plan);

    for (auto& section : plan.sections) {
        const auto intro = sectionNamed(section, {"intro", "prologue", "entrada", "opening"});
        const auto breakdown = sectionNamed(section, {"break", "breakdown", "bajada", "suspend"});
        const auto outro = sectionNamed(section, {"outro", "coda", "exit", "salida"});
        const auto arrival = sectionNamed(section, {"arrival", "drop", "peak", "climax", "return", "summit"});
        std::vector<VoiceId> active;
        addUnique(active, VoiceId::Atmosphere);
        if (section.tension > 0.45 || intro || outro) addUnique(active, VoiceId::Transitions);
        if (section.energy > 0.18) addUnique(active, VoiceId::HarmonicPulse);
        if (section.density > 0.38 || breakdown) addUnique(active, VoiceId::HarmonicFoundation);
        if (section.energy > 0.28 && !breakdown) {
            addUnique(active, VoiceId::CoreDrums);
            addUnique(active, VoiceId::ClosedHats);
        }
        if (section.energy > 0.38 && !breakdown) {
            addUnique(active, VoiceId::SubBass);
            addUnique(active, VoiceId::MovementBass);
            addUnique(active, VoiceId::SnareClap);
            addUnique(active, VoiceId::OpenHatsShaker);
        }
        if (section.energy > 0.54 && !breakdown) addUnique(active, VoiceId::LowPercussion);
        if (section.energy > 0.66 && !breakdown) addUnique(active, VoiceId::HighPercussion);
        if (section.energy > 0.76 || breakdown) addUnique(active, VoiceId::HarmonicUpper);
        const auto phraseOwner = positiveModulo(section.motifVariant, 3);
        if (arrival || breakdown || phraseOwner != 0) addUnique(active, VoiceId::Lead);
        if (!breakdown && section.energy > 0.62 && phraseOwner == 0) addUnique(active, VoiceId::Countermelody);
        for (const auto& mutation : section.rhythm.mutations) {
            const auto voice = mutation.lane == RhythmLane::Kick ? VoiceId::CoreDrums
                : mutation.lane == RhythmLane::SnareClap ? VoiceId::SnareClap
                : mutation.lane == RhythmLane::ClosedHats ? VoiceId::ClosedHats
                : mutation.lane == RhythmLane::OpenHatsShaker ? VoiceId::OpenHatsShaker
                : mutation.lane == RhythmLane::LowPercussion ? VoiceId::LowPercussion
                                                             : VoiceId::HighPercussion;
            addUnique(active, voice);
        }
        section.activeVoices = std::move(active);
        const auto explicitContinuousKick = section.rhythm.continuity == KickContinuity::Required &&
                                            section.rhythm.kickState == KickState::FourOnFloor;
        if (explicitContinuousKick) {
            // An explicit user-authored macro contract (for example, constant quarter-note
            // kick) always wins over the director's default intro/breakdown subtraction.
        } else if (breakdown) {
            section.rhythm.kickState = KickState::Muted;
            section.rhythm.continuity = KickContinuity::Sectional;
        } else if (intro || outro) {
            section.rhythm.kickState = section.energy > 0.32 ? KickState::Reduced : KickState::Muted;
            section.rhythm.continuity = KickContinuity::Sectional;
        } else if (section.energy > 0.42) {
            section.rhythm.kickState = plan.rhythmLanguage.pulseStability > 0.54
                ? KickState::FourOnFloor : KickState::Sparse;
            section.rhythm.continuity = KickContinuity::Required;
        }
        section.rhythm.percussionDensity = std::min(section.rhythm.percussionDensity,
            0.42 + section.energy * 0.42);
        section.rhythm.swing = std::min(section.rhythm.swing, 0.16);
    }
}

ElectronicProductionReport ElectronicProductionDirector::shapePerformance(Pattern& pattern,
                                                                            const SongPlan& plan) {
    ElectronicProductionReport report;
    report.active = electronicCoreActive(plan.productionLanguage);
    if (!report.active) return report;
    const auto aiNarrative = plan.productionModeSource == "gpt_plan";
    report.lowEndCollisionsBefore = lowEndCollisions(pattern);
    report.kickOrnamentsRemoved = restrainKickOrnaments(pattern, plan);
    if (!aiNarrative)
        report.kickPhraseDevelopmentsCreated = createKickPhraseDevelopment(pattern, plan);
    report.maximumKicklessBarsBefore = maximumKicklessBars(pattern, plan);
    if (!aiNarrative)
        report.macroKickAnchorBarsCreated = createMacroKickAnchors(pattern, plan);
    report.maximumKicklessBarsAfter = maximumKicklessBars(pattern, plan);

    std::vector<double> kicks;
    for (const auto& note : pattern.notes)
        if (note.voice == VoiceId::CoreDrums && (note.pitch == 35 || note.pitch == 36))
            kicks.push_back(note.startBeat);
    std::sort(kicks.begin(), kicks.end());
    auto bassOrdinal = std::size_t{};
    for (auto& note : pattern.notes) {
        if (note.voice != VoiceId::SubBass && note.voice != VoiceId::MovementBass) continue;
        const auto collision = std::find_if(kicks.begin(), kicks.end(), [&](double kick) {
            return std::abs(note.startBeat - kick) < 0.08;
        });
        const auto aiNote = note.origin == NoteOrigin::AiAuthored ||
                            note.origin == NoteOrigin::AiTransformed;
        if (!aiNote && collision != kicks.end() && (++bassOrdinal % 4) != 0) {
            const auto originalEnd = note.endBeat();
            note.startBeat = std::min(pattern.lengthBeats - 0.0625, *collision + 0.25);
            note.durationBeats = std::max(0.0625, originalEnd - note.startBeat);
            ++report.bassAttacksMoved;
        }
        const auto nextKick = std::upper_bound(kicks.begin(), kicks.end(), note.startBeat + 0.02);
        if (nextKick != kicks.end() && note.endBeat() > *nextKick - 0.04 && *nextKick > note.startBeat + 0.10) {
            note.durationBeats = std::max(0.0625, *nextKick - note.startBeat - 0.04);
            ++report.bassReleasesTrimmed;
        }
    }

    const auto beatsPerBar = plan.beatsPerBar;
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (!isVoiceInFamily(note.voice, VoiceFamily::Rhythm) || note.voice == VoiceId::CoreDrums) return false;
        const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        const auto beat = note.startBeat - bar * beatsPerBar;
        const auto phraseEnd = positiveModulo(bar, 8) == 7;
        const auto halfPhrase = positiveModulo(bar, 8) == 3;
        const auto remove = (phraseEnd && beat >= beatsPerBar - 0.51) ||
            (halfPhrase && (note.voice == VoiceId::OpenHatsShaker || note.voice == VoiceId::HighPercussion) &&
             beat < 0.51);
        if (remove && note.origin != NoteOrigin::AiAuthored &&
            note.origin != NoteOrigin::AiTransformed) {
            ++report.phraseBreathsCreated;
            return true;
        }
        return false;
    }), pattern.notes.end());
    if (!aiNarrative) {
        report.phraseVariationsCreated = createPhraseVariations(pattern, beatsPerBar);
        report.rhythmNotesEvolved = evolveLiteralRhythm(pattern, beatsPerBar);
        const auto bassDevelopment = developRepeatedBassPhrases(pattern, plan);
        report.bassPhraseDevelopmentsCreated = bassDevelopment.first;
        report.bassNotesDeveloped = bassDevelopment.second;
        report.harmonicBreathsCreated = createHarmonicPhraseBreaths(pattern, beatsPerBar);
        report.supportNotesRotated = rotatePeakSupport(pattern, plan);
    }
    const auto bars = static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar));
    for (auto bar = 0; bar < bars; ++bar) {
        const auto start = bar * beatsPerBar;
        const auto end = std::min(pattern.lengthBeats, start + beatsPerBar);
        const auto leadCount = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::Lead && note.startBeat >= start && note.startBeat < end;
        });
        const auto counterCount = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == VoiceId::Countermelody && note.startBeat >= start && note.startBeat < end;
        });
        if (aiNarrative || leadCount == 0 || counterCount == 0) continue;
        ++report.competingForegroundBars;
        const auto removeVoice = positiveModulo(bar / 2, 2) == 0 ? VoiceId::Countermelody : VoiceId::Lead;
        const auto before = pattern.notes.size();
        pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == removeVoice && note.startBeat >= start && note.startBeat < end;
        }), pattern.notes.end());
        report.foregroundNotesRemoved += before - pattern.notes.size();
    }

    // Foreground arbitration must happen first. Otherwise it can delete pieces of the
    // recalled motif and leave a formally "repeated" idea unrecognisable in final MIDI.
    if (!aiNarrative) {
        const auto thematicRecall = reinforceThematicMemory(pattern, plan);
        report.thematicRecallWindowsCreated = thematicRecall.first;
        report.thematicRecallNotesCreated = thematicRecall.second;
    }

    constexpr std::array automatedVoices{VoiceId::MovementBass, VoiceId::HarmonicFoundation,
        VoiceId::HarmonicPulse, VoiceId::HarmonicUpper, VoiceId::Lead, VoiceId::Atmosphere};
    for (const auto& section : plan.sections) {
        const auto start = section.startBar * beatsPerBar;
        const auto end = std::min(pattern.lengthBeats, (section.startBar + section.bars) * beatsPerBar);
        for (const auto voice : automatedVoices) {
            if (std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) == section.activeVoices.end()) continue;
            const auto channel = voiceDefinition(voice).midiChannel;
            const auto base = std::clamp(static_cast<int>(30 + section.energy * 56), 16, 92);
            const auto peak = std::clamp(static_cast<int>(base + section.tension * 30), 24, 122);
            pattern.controls.push_back({start, 74, base, channel, voice});
            pattern.controls.push_back({start + (end - start) * 0.72, 74, peak, channel, voice});
            pattern.controls.push_back({std::max(start, end - 0.125), 74, std::max(18, base - 7), channel, voice});
            report.automationEventsAdded += 3;
        }
    }
    report.lowEndCollisionsAfter = lowEndCollisions(pattern);
    return report;
}

ElectronicProductionReport ElectronicProductionDirector::audit(const Pattern& pattern,
                                                                 const SongPlan& plan) {
    ElectronicProductionReport report;
    report.active = electronicCoreActive(plan.productionLanguage);
    if (!report.active) return report;
    report.lowEndCollisionsAfter = lowEndCollisions(pattern);
    const auto runs = rhythmRuns(pattern, plan.beatsPerBar);
    report.literalRhythmBars = runs.literal;
    report.maximumRhythmRun = runs.maximum;
    report.maximumHarmonicRun = maximumRunForVoice(
        pattern, VoiceId::HarmonicFoundation, plan.beatsPerBar);
    const auto thematic = auditThematicMemory(pattern, plan.beatsPerBar);
    report.thematicWindows = thematic.windows;
    report.recurringThematicWindows = thematic.recurring;
    report.thematicRecurrenceRatio = thematic.ratio;
    std::set<int> percussionPitches;
    for (const auto& note : pattern.notes)
        if (note.voice == VoiceId::LowPercussion || note.voice == VoiceId::HighPercussion ||
            note.voice == VoiceId::OpenHatsShaker) {
            percussionPitches.insert(note.pitch);
            ++report.percussionNotes;
        }
    report.percussionArticulations = percussionPitches.size();
    report.expectedEssentialInstruments = plan.timbrePalette.essentialInstrumentIds.size();
    for (const auto& essential : plan.timbrePalette.essentialInstrumentIds) {
        const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& candidate) {
            return candidate.catalogId == essential;
        });
        if (part != pattern.parts.end() && std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.partId == part->id;
            })) ++report.materializedEssentialInstruments;
    }
    std::size_t primaryKicks{}, ornamentKicks{};
    for (const auto& note : pattern.notes) {
        if (note.voice != VoiceId::CoreDrums || (note.pitch != 35 && note.pitch != 36)) continue;
        const auto* section = sectionAtBeat(plan, note.startBeat);
        if (section == nullptr || section->rhythm.kickState != KickState::FourOnFloor) continue;
        const auto metric = note.startBeat / (plan.beatsPerBar / 4.0);
        if (std::abs(metric - std::round(metric)) < 0.04) ++primaryKicks;
        else ++ornamentKicks;
    }
    report.kickOrnamentRatio = static_cast<double>(ornamentKicks) /
        std::max<std::size_t>(1, primaryKicks + ornamentKicks);
    for (double start = 0.0; start < pattern.lengthBeats; start += plan.beatsPerBar * 8.0) {
        std::set<VoiceId> active;
        for (const auto& note : pattern.notes)
            if (note.startBeat >= start && note.startBeat < start + plan.beatsPerBar * 8.0)
                active.insert(note.voice);
        report.peakActiveVoices = std::max(report.peakActiveVoices, active.size());
    }
    const auto bassNotes = std::count_if(pattern.notes.begin(), pattern.notes.end(), [](const auto& note) {
        return note.voice == VoiceId::SubBass || note.voice == VoiceId::MovementBass;
    });
    const auto collisionRatio = static_cast<double>(report.lowEndCollisionsAfter) /
                                std::max<std::size_t>(1, bassNotes);
    const auto instrumentMatch = pattern.parts.empty() ? 0.0 : static_cast<double>(std::count_if(
        pattern.parts.begin(), pattern.parts.end(), [](const auto& part) {
            return part.catalogId == "kick_drum" || part.catalogId == "snare_clap" ||
                   part.catalogId == "hi_hats" || part.catalogId == "shakers" ||
                   part.catalogId == "sub_synth" || part.catalogId == "electric_bass" ||
                   part.catalogId == "analog_pad" || part.catalogId == "poly_synth" ||
                   part.catalogId == "lead_synth" || part.catalogId == "ambient_texture";
        })) / pattern.parts.size();
    report.intentionMatch = std::clamp(instrumentMatch * 1.25, 0.0, 1.0);
    report.score = std::clamp(1.0 - collisionRatio * 0.32 -
        std::max(0.0, static_cast<double>(runs.maximum) - 4.0) * 0.025 -
        std::max(0.0, static_cast<double>(report.maximumHarmonicRun) - 8.0) * 0.018 -
        std::max(0.0, report.kickOrnamentRatio - 0.08) * 0.75 -
        (report.thematicWindows >= 6 ? std::max(0.0, 0.24 - report.thematicRecurrenceRatio) * 0.90 : 0.0) -
        (report.percussionNotes >= 12
            ? std::max(0.0, 3.0 - static_cast<double>(report.percussionArticulations)) * 0.04 : 0.0) -
        (report.expectedEssentialInstruments == 0 ? 0.0 :
            static_cast<double>(report.expectedEssentialInstruments - report.materializedEssentialInstruments) /
            report.expectedEssentialInstruments * 0.18) -
        std::max(0.0, static_cast<double>(report.peakActiveVoices) - 12.0) * 0.025 -
        std::max(0.0, 0.82 - report.intentionMatch) * 0.55, 0.0, 1.0);
    return report;
}

void ElectronicProductionDirector::stamp(Pattern& pattern,
                                           const ElectronicProductionReport& report) {
    if (!report.active) return;
    pattern.productionScore = std::clamp(pattern.productionScore * 0.68 + report.score * 0.32, 0.0, 1.0);
    if (report.maximumRhythmRun > 6)
        pattern.productionIssues.push_back("warning:electronic_loop_needs_evolution");
    if (report.maximumHarmonicRun > 8)
        pattern.productionIssues.push_back("warning:harmonic_body_needs_evolution");
    if (report.kickOrnamentRatio > 0.08)
        pattern.productionIssues.push_back("warning:kick_ornaments_overused");
    if (report.peakActiveVoices > 12)
        pattern.productionIssues.push_back("warning:electronic_peak_needs_rotation");
    if (report.intentionMatch < 0.82)
        pattern.productionIssues.push_back("warning:electronic_intention_mismatch");
    if (report.thematicWindows >= 6 && report.thematicRecurrenceRatio < 0.24) {
        pattern.productionIssues.push_back("warning:thematic_identity_needs_stronger_recall");
    }
    if (report.percussionNotes >= 12 && report.percussionArticulations < 3) {
        pattern.productionIssues.push_back("warning:percussion_articulation_is_sparse");
    }
    if (report.materializedEssentialInstruments < report.expectedEssentialInstruments) {
        pattern.productionIssues.push_back("warning:sound_world_instrument_not_materialized");
    }
    if (report.grooveRecallRatio < 0.50)
        pattern.productionIssues.push_back("warning:groove_identity_needs_stronger_recall");
    if (report.responseLineageRatio < 0.65)
        pattern.productionIssues.push_back("warning:response_is_not_derived_from_hook");
    if (report.musicalIdentityScore < 0.78)
        pattern.productionIssues.push_back("warning:musical_identity_gate_repaired_score");
    if (report.sparseStructuralWindowsRepaired > 0)
        pattern.productionIssues.push_back("warning:sparse_structure_received_continuity_repair");
    if (report.maximumKicklessBarsAfter > 16)
        pattern.productionIssues.push_back("warning:electronic_macro_pulse_is_missing");
}

} // namespace pulso
