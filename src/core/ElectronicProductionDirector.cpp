#include "ElectronicProductionDirector.h"

#include "OrchestrationScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    plan.timbrePalette.material = "cohesive electronic low end, tactile transients, evolving synthesis and restrained spectral layers";
    plan.timbrePalette.space = "mono-compatible low end with short groove space and automated depth at transitions";
    plan.orchestrationLanguage.ensembleScale = std::min(plan.orchestrationLanguage.ensembleScale,
                                                         club ? 0.56 : 0.82);
    plan.orchestrationLanguage.counterpointActivity = std::min(plan.orchestrationLanguage.counterpointActivity, 0.40);
    plan.orchestrationLanguage.divisiDepth = std::min(plan.orchestrationLanguage.divisiDepth, 0.24);
    plan.orchestrationLanguage.tuttiRarity = std::max(plan.orchestrationLanguage.tuttiRarity, 0.90);
    plan.orchestrationLanguage.doublingRestraint = std::max(plan.orchestrationLanguage.doublingRestraint, 0.90);
    plan.orchestrationLanguage.hybridProduction = std::max(plan.orchestrationLanguage.hybridProduction,
                                                            club ? 0.92 : 0.84);
    if (!club) return;
    plan.instruments = electronicParts();

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
    report.lowEndCollisionsBefore = lowEndCollisions(pattern);
    report.kickOrnamentsRemoved = restrainKickOrnaments(pattern, plan);

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
        if (collision != kicks.end() && (++bassOrdinal % 4) != 0) {
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
        if (remove) ++report.phraseBreathsCreated;
        return remove;
    }), pattern.notes.end());
    report.phraseVariationsCreated = createPhraseVariations(pattern, beatsPerBar);
    report.rhythmNotesEvolved = evolveLiteralRhythm(pattern, beatsPerBar);
    report.harmonicBreathsCreated = createHarmonicPhraseBreaths(pattern, beatsPerBar);
    report.supportNotesRotated = rotatePeakSupport(pattern, plan);

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
        if (leadCount == 0 || counterCount == 0) continue;
        ++report.competingForegroundBars;
        const auto removeVoice = positiveModulo(bar / 2, 2) == 0 ? VoiceId::Countermelody : VoiceId::Lead;
        const auto before = pattern.notes.size();
        pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == removeVoice && note.startBeat >= start && note.startBeat < end;
        }), pattern.notes.end());
        report.foregroundNotesRemoved += before - pattern.notes.size();
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
}

} // namespace pulso
