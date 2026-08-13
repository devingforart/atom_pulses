#include "PerformanceExpression.h"

#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace pulso {
namespace {

template <typename Enum, std::size_t Size>
std::optional<Enum> fromKey(std::string_view key,
                            const std::array<std::pair<Enum, std::string_view>, Size>& values) noexcept {
    const auto found = std::find_if(values.begin(), values.end(), [key](const auto& value) {
        return value.second == key;
    });
    return found == values.end() ? std::nullopt : std::optional<Enum>{found->first};
}

constexpr std::array articulationKeys{
    std::pair{ArticulationStyle::Percussive, std::string_view{"percussive"}},
    std::pair{ArticulationStyle::Staccato, std::string_view{"staccato"}},
    std::pair{ArticulationStyle::Detached, std::string_view{"detached"}},
    std::pair{ArticulationStyle::Natural, std::string_view{"natural"}},
    std::pair{ArticulationStyle::Legato, std::string_view{"legato"}},
    std::pair{ArticulationStyle::Sustained, std::string_view{"sustained"}},
    std::pair{ArticulationStyle::Swelling, std::string_view{"swelling"}}
};
constexpr std::array dynamicKeys{
    std::pair{DynamicContour::Steady, std::string_view{"steady"}},
    std::pair{DynamicContour::PhraseArc, std::string_view{"phrase_arc"}},
    std::pair{DynamicContour::Crescendo, std::string_view{"crescendo"}},
    std::pair{DynamicContour::Decrescendo, std::string_view{"decrescendo"}},
    std::pair{DynamicContour::Swell, std::string_view{"swell"}},
    std::pair{DynamicContour::Pulsing, std::string_view{"pulsing"}}
};
constexpr std::array vibratoKeys{
    std::pair{VibratoStyle::None, std::string_view{"none"}},
    std::pair{VibratoStyle::LateSubtle, std::string_view{"late_subtle"}},
    std::pair{VibratoStyle::LateExpressive, std::string_view{"late_expressive"}},
    std::pair{VibratoStyle::ContinuousSubtle, std::string_view{"continuous_subtle"}}
};
constexpr std::array pitchKeys{
    std::pair{PitchGesture::Stable, std::string_view{"stable"}},
    std::pair{PitchGesture::Approach, std::string_view{"approach"}},
    std::pair{PitchGesture::GentleBends, std::string_view{"gentle_bends"}},
    std::pair{PitchGesture::Portamento, std::string_view{"portamento"}}
};

template <typename Enum, std::size_t Size>
std::string_view toKey(Enum value,
                       const std::array<std::pair<Enum, std::string_view>, Size>& values) noexcept {
    const auto found = std::find_if(values.begin(), values.end(), [value](const auto& item) {
        return item.first == value;
    });
    return found == values.end() ? values.front().second : found->second;
}

bool expressiveVoice(VoiceId voice) noexcept {
    return voice != VoiceId::Unspecified && voice != VoiceId::Count &&
           !isVoiceInFamily(voice, VoiceFamily::Rhythm);
}

bool monophonicPitchVoice(VoiceId voice) noexcept {
    return voice == VoiceId::Lead || voice == VoiceId::Countermelody ||
           voice == VoiceId::MovementBass || voice == VoiceId::SubBass;
}

std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double humanValue(std::uint64_t seed, const NoteEvent& note, std::size_t ordinal) noexcept {
    const auto value = mix(seed ^ (static_cast<std::uint64_t>(note.voice) + 1) * 0x9e3779b97f4a7c15ULL ^
                           static_cast<std::uint64_t>(note.pitch + 1) * 0xd1b54a32d192ed03ULL ^
                           static_cast<std::uint64_t>(ordinal + 1) * 0x94d049bb133111ebULL);
    return static_cast<double>(value & 0xffffu) / 32767.5 - 1.0;
}

double contourValue(DynamicContour contour, double position, double beat) noexcept {
    position = std::clamp(position, 0.0, 1.0);
    switch (contour) {
        case DynamicContour::Steady: return 0.05 * std::sin(position * 2.0 * std::numbers::pi);
        case DynamicContour::PhraseArc: return std::sin(position * std::numbers::pi) * 0.82 - 0.20;
        case DynamicContour::Crescendo: return position * 1.25 - 0.52;
        case DynamicContour::Decrescendo: return (1.0 - position) * 1.25 - 0.52;
        case DynamicContour::Swell: return std::pow(std::sin(position * std::numbers::pi), 2.0) * 1.25 - 0.42;
        case DynamicContour::Pulsing: return std::sin(beat * std::numbers::pi) * 0.34;
    }
    return 0.0;
}

const PlannedVoice* plannedVoice(const SongPlan& plan, VoiceId id) noexcept {
    const auto found = std::find_if(plan.voices.begin(), plan.voices.end(), [id](const auto& voice) {
        return voice.id == id;
    });
    return found == plan.voices.end() ? nullptr : &*found;
}

void addPitchBendRange(Pattern& pattern, VoiceId voice) {
    const auto channel = voiceDefinition(voice).midiChannel;
    // Registered Parameter Number 0: pitch-bend sensitivity = +/- 2 semitones.
    pattern.controls.push_back({0.0, 101, 0, channel, voice});
    pattern.controls.push_back({0.0, 100, 0, channel, voice});
    pattern.controls.push_back({0.0, 6, 2, channel, voice});
    pattern.controls.push_back({0.0, 38, 0, channel, voice});
    pattern.controls.push_back({0.0, 101, 127, channel, voice});
    pattern.controls.push_back({0.0, 100, 127, channel, voice});
}

void addPitchRamp(Pattern& pattern, VoiceId voice, double start, double end,
                  int from, int to, double songEnd) {
    if (end <= start || start >= songEnd) return;
    const auto points = std::clamp(static_cast<int>(std::ceil((end - start) * 32.0)) + 1, 5, 17);
    for (auto point = 0; point < points; ++point) {
        const auto position = static_cast<double>(point) / static_cast<double>(points - 1);
        pattern.expressions.push_back({std::clamp(start + (end - start) * position, 0.0,
                                                   std::max(0.0, songEnd - 1.0 / 64.0)),
            ExpressionEventType::PitchBend,
            std::clamp(static_cast<int>(std::lround(from + (to - from) * position)), 0, 16383),
            -1, voiceDefinition(voice).midiChannel, voice});
    }
}

} // namespace

PerformanceProfile defaultPerformanceProfile(VoiceId voice) noexcept {
    PerformanceProfile result;
    switch (voice) {
        case VoiceId::CoreDrums:
        case VoiceId::LowPercussion:
        case VoiceId::HighPercussion:
        case VoiceId::SnareClap:
        case VoiceId::ClosedHats:
        case VoiceId::OpenHatsShaker:
            result.articulation = ArticulationStyle::Percussive;
            result.dynamics = DynamicContour::PhraseArc;
            result.expressionDepth = 0.28;
            result.brightness = 0.58;
            result.humanization = voice == VoiceId::CoreDrums ? 0.08 : 0.42;
            break;
        case VoiceId::SubBass:
            result.articulation = ArticulationStyle::Natural;
            result.dynamics = DynamicContour::Steady;
            result.expressionDepth = 0.22;
            result.brightness = 0.28;
            result.humanization = 0.12;
            break;
        case VoiceId::MovementBass:
            result.articulation = ArticulationStyle::Detached;
            result.dynamics = DynamicContour::Pulsing;
            result.pitchGesture = PitchGesture::Approach;
            result.expressionDepth = 0.38;
            result.brightness = 0.46;
            result.humanization = 0.32;
            break;
        case VoiceId::HarmonicFoundation:
            result.articulation = ArticulationStyle::Sustained;
            result.dynamics = DynamicContour::Swell;
            result.expressionDepth = 0.58;
            result.brightness = 0.38;
            result.humanization = 0.16;
            result.sustainPedal = true;
            break;
        case VoiceId::HarmonicPulse:
            result.articulation = ArticulationStyle::Staccato;
            result.dynamics = DynamicContour::Pulsing;
            result.expressionDepth = 0.36;
            result.brightness = 0.62;
            result.humanization = 0.26;
            break;
        case VoiceId::HarmonicUpper:
            result.articulation = ArticulationStyle::Legato;
            result.dynamics = DynamicContour::PhraseArc;
            result.expressionDepth = 0.48;
            result.brightness = 0.68;
            result.humanization = 0.22;
            break;
        case VoiceId::Lead:
            result.articulation = ArticulationStyle::Legato;
            result.dynamics = DynamicContour::PhraseArc;
            result.vibrato = VibratoStyle::LateExpressive;
            result.pitchGesture = PitchGesture::GentleBends;
            result.expressionDepth = 0.64;
            result.brightness = 0.62;
            result.humanization = 0.52;
            break;
        case VoiceId::Countermelody:
            result.articulation = ArticulationStyle::Detached;
            result.dynamics = DynamicContour::Decrescendo;
            result.vibrato = VibratoStyle::LateSubtle;
            result.pitchGesture = PitchGesture::Approach;
            result.expressionDepth = 0.44;
            result.brightness = 0.56;
            result.humanization = 0.48;
            break;
        case VoiceId::Atmosphere:
            result.articulation = ArticulationStyle::Swelling;
            result.dynamics = DynamicContour::Swell;
            result.expressionDepth = 0.72;
            result.brightness = 0.52;
            result.humanization = 0.12;
            result.sustainPedal = true;
            break;
        case VoiceId::Transitions:
            result.articulation = ArticulationStyle::Swelling;
            result.dynamics = DynamicContour::Crescendo;
            result.expressionDepth = 0.82;
            result.brightness = 0.72;
            result.humanization = 0.08;
            break;
        case VoiceId::Count:
        case VoiceId::Unspecified: break;
    }
    return result;
}

std::string_view articulationStyleKey(ArticulationStyle value) noexcept { return toKey(value, articulationKeys); }
std::string_view dynamicContourKey(DynamicContour value) noexcept { return toKey(value, dynamicKeys); }
std::string_view vibratoStyleKey(VibratoStyle value) noexcept { return toKey(value, vibratoKeys); }
std::string_view pitchGestureKey(PitchGesture value) noexcept { return toKey(value, pitchKeys); }
std::optional<ArticulationStyle> articulationStyleFromKey(std::string_view key) noexcept { return fromKey(key, articulationKeys); }
std::optional<DynamicContour> dynamicContourFromKey(std::string_view key) noexcept { return fromKey(key, dynamicKeys); }
std::optional<VibratoStyle> vibratoStyleFromKey(std::string_view key) noexcept { return fromKey(key, vibratoKeys); }
std::optional<PitchGesture> pitchGestureFromKey(std::string_view key) noexcept { return fromKey(key, pitchKeys); }

void PerformanceExpression::apply(Pattern& pattern, const SongPlan& plan, bool shapeNotes) {
    if (pattern.notes.empty() || pattern.lengthBeats <= 0.0) return;

    // Idempotent replacement of PULSO-owned expressive controllers.
    constexpr std::array ownedControllers{1, 6, 11, 38, 64, 74, 100, 101};
    pattern.controls.erase(std::remove_if(pattern.controls.begin(), pattern.controls.end(), [](const auto& control) {
        return std::find(ownedControllers.begin(), ownedControllers.end(), control.controller) !=
               ownedControllers.end();
    }), pattern.controls.end());
    pattern.expressions.clear();

    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> bendRangeConfigured{};
    for (const auto& section : plan.sections) {
        const auto sectionStart = section.startBar * plan.beatsPerBar;
        const auto sectionEnd = std::min(pattern.lengthBeats,
            (section.startBar + section.bars) * plan.beatsPerBar);
        if (sectionEnd <= sectionStart) continue;

        for (const auto voice : section.activeVoices) {
            if (voice == VoiceId::Unspecified || voice == VoiceId::Count) continue;
            const auto* authored = plannedVoice(plan, voice);
            const auto profile = authored == nullptr ? defaultPerformanceProfile(voice) : authored->performance;
            const auto channel = voiceDefinition(voice).midiChannel;

            std::vector<std::size_t> voiceNotes;
            for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
                const auto& note = pattern.notes[index];
                if (note.voice == voice && note.startBeat >= sectionStart && note.startBeat < sectionEnd)
                    voiceNotes.push_back(index);
            }
            if (voiceNotes.empty()) continue;

            for (std::size_t ordinal = 0; ordinal < voiceNotes.size(); ++ordinal) {
                auto& note = pattern.notes[voiceNotes[ordinal]];
                const auto human = humanValue(plan.seed, note, ordinal) * profile.humanization;
                if (shapeNotes) {
                    const auto velocityScale = 1.0 + human * 0.075;
                    note.velocity = std::clamp(static_cast<int>(std::lround(note.velocity * velocityScale)), 1, 127);

                    auto durationScale = 1.0;
                    switch (profile.articulation) {
                        case ArticulationStyle::Percussive: durationScale = 0.88; break;
                        case ArticulationStyle::Staccato: durationScale = 0.46; break;
                        case ArticulationStyle::Detached: durationScale = 0.72; break;
                        case ArticulationStyle::Natural: durationScale = 0.94; break;
                        case ArticulationStyle::Legato: durationScale = 1.04; break;
                        case ArticulationStyle::Sustained: durationScale = 1.10; break;
                        case ArticulationStyle::Swelling: durationScale = 1.06; break;
                    }
                    note.durationBeats = std::max(1.0 / 64.0,
                        std::min(sectionEnd - note.startBeat,
                                 note.durationBeats * durationScale * (1.0 + human * 0.025)));
                }
                if (shapeNotes && monophonicPitchVoice(voice)) {
                    const auto next = std::find_if(voiceNotes.begin() + static_cast<std::ptrdiff_t>(ordinal + 1),
                        voiceNotes.end(), [&](std::size_t nextIndex) {
                            return pattern.notes[nextIndex].startBeat > note.startBeat + 0.0001;
                        });
                    if (next != voiceNotes.end()) {
                        const auto nextStart = pattern.notes[*next].startBeat;
                        const auto gap = profile.articulation == ArticulationStyle::Legato ? 1.0 / 128.0 : 1.0 / 64.0;
                        note.durationBeats = std::min(note.durationBeats,
                            std::max(1.0 / 64.0, nextStart - note.startBeat - gap));
                    }
                }
            }

            if (!expressiveVoice(voice)) continue;
            const auto depth = std::clamp(profile.expressionDepth, 0.0, 1.0);
            const auto curveStep = isVoiceInFamily(voice, VoiceFamily::Melodic) ? 0.5 : 1.0;
            const auto phraseBeats = std::max(plan.beatsPerBar, std::min(8.0 * plan.beatsPerBar,
                                                                         sectionEnd - sectionStart));
            for (auto beat = sectionStart; beat < sectionEnd; beat += curveStep) {
                const auto phrasePosition = std::fmod(beat - sectionStart, phraseBeats) / phraseBeats;
                const auto contour = contourValue(profile.dynamics, phrasePosition, beat);
                const auto expression = std::clamp(static_cast<int>(std::lround(
                    62.0 + section.energy * 38.0 + contour * depth * 34.0)), 18, 127);
                const auto brightness = std::clamp(static_cast<int>(std::lround(
                    profile.brightness * 82.0 + section.tension * 32.0 + contour * depth * 18.0)), 8, 127);
                pattern.controls.push_back({beat, 11, expression, channel, voice});
                pattern.controls.push_back({beat, 74, brightness, channel, voice});

                auto modulation = 0;
                if (profile.vibrato == VibratoStyle::ContinuousSubtle) modulation = 24;
                else if (profile.vibrato == VibratoStyle::LateSubtle && phrasePosition > 0.58) modulation = 32;
                else if (profile.vibrato == VibratoStyle::LateExpressive && phrasePosition > 0.52)
                    modulation = std::clamp(static_cast<int>(42 + section.tension * 32), 0, 90);
                pattern.controls.push_back({beat, 1, modulation, channel, voice});
                pattern.expressions.push_back({beat, ExpressionEventType::ChannelPressure,
                    std::clamp(static_cast<int>(std::lround(expression * (0.28 + depth * 0.38))), 0, 127),
                    -1, channel, voice});
            }

            if (profile.sustainPedal && (voice == VoiceId::HarmonicFoundation ||
                                          voice == VoiceId::HarmonicUpper ||
                                          voice == VoiceId::Atmosphere)) {
                // Pedal follows played phrases, never an entire section. Long section-wide
                // CC64 windows retained every released note and could accumulate nearly a
                // minute of harmony in a receiving instrument.
                constexpr auto maximumPedalBeats = 2.0;
                constexpr auto pedalTailBeats = 0.125;
                constexpr auto phraseGapBeats = 0.25;
                for (std::size_t first = 0; first < voiceNotes.size();) {
                    const auto pedalOn = pattern.notes[voiceNotes[first]].startBeat;
                    auto phraseEnd = pattern.notes[voiceNotes[first]].endBeat();
                    const auto hardEnd = std::min(sectionEnd - 1.0 / 32.0,
                                                  pedalOn + maximumPedalBeats);
                    auto next = first + 1;
                    while (next < voiceNotes.size()) {
                        const auto& candidate = pattern.notes[voiceNotes[next]];
                        if (candidate.startBeat >= hardEnd ||
                            candidate.startBeat > phraseEnd + phraseGapBeats) break;
                        phraseEnd = std::max(phraseEnd, candidate.endBeat());
                        ++next;
                    }
                    const auto pedalOff = std::min(hardEnd, phraseEnd + pedalTailBeats);
                    if (pedalOff > pedalOn + 1.0 / 64.0) {
                        pattern.controls.push_back({pedalOn, 64, 96, channel, voice});
                        pattern.controls.push_back({pedalOff, 64, 0, channel, voice});
                    }
                    while (next < voiceNotes.size() &&
                           pattern.notes[voiceNotes[next]].startBeat < pedalOff - 0.0001)
                        ++next;
                    first = std::max(first + 1, next);
                }
            }

            if (monophonicPitchVoice(voice) && profile.pitchGesture != PitchGesture::Stable) {
                const auto voiceIndex = static_cast<std::size_t>(voice);
                if (!bendRangeConfigured[voiceIndex]) {
                    addPitchBendRange(pattern, voice);
                    bendRangeConfigured[voiceIndex] = true;
                }
                auto previousPitch = pattern.notes[voiceNotes.front()].pitch;
                for (const auto noteIndex : voiceNotes) {
                    const auto& note = pattern.notes[noteIndex];
                    const auto gestureEnd = std::min(note.endBeat(), note.startBeat +
                        (profile.pitchGesture == PitchGesture::Portamento ? 0.38 : 0.16));
                    if (profile.pitchGesture == PitchGesture::Portamento && note.pitch != previousPitch) {
                        const auto semitones = std::clamp(previousPitch - note.pitch, -2, 2);
                        addPitchRamp(pattern, voice, note.startBeat, gestureEnd,
                                     8192 + semitones * 4096, 8192, pattern.lengthBeats);
                    } else if (profile.pitchGesture == PitchGesture::Approach) {
                        const auto direction = note.pitch >= previousPitch ? -1 : 1;
                        addPitchRamp(pattern, voice, note.startBeat, gestureEnd,
                                     8192 + direction * 768, 8192, pattern.lengthBeats);
                    } else if (profile.pitchGesture == PitchGesture::GentleBends &&
                               note.durationBeats >= 0.5) {
                        const auto bendStart = note.startBeat + note.durationBeats * 0.48;
                        const auto bendPeak = note.startBeat + note.durationBeats * 0.70;
                        const auto bendEnd = std::min(note.endBeat() - 1.0 / 64.0,
                                                      note.startBeat + note.durationBeats * 0.90);
                        const auto direction = (note.pitch + static_cast<int>(noteIndex)) % 2 == 0 ? 1 : -1;
                        addPitchRamp(pattern, voice, bendStart, bendPeak, 8192,
                                     8192 + direction * 520, pattern.lengthBeats);
                        addPitchRamp(pattern, voice, bendPeak, bendEnd, 8192 + direction * 520,
                                     8192, pattern.lengthBeats);
                    }
                    if (profile.vibrato != VibratoStyle::None && note.durationBeats >= 0.35) {
                        const auto pressureBeat = std::min(note.endBeat() - 1.0 / 64.0,
                            note.startBeat + note.durationBeats * 0.62);
                        pattern.expressions.push_back({pressureBeat, ExpressionEventType::PolyAftertouch,
                            profile.vibrato == VibratoStyle::LateExpressive ? 82 : 52,
                            note.pitch, channel, voice});
                    }
                    previousPitch = note.pitch;
                }
                pattern.expressions.push_back({std::max(sectionStart, sectionEnd - 1.0 / 64.0),
                    ExpressionEventType::PitchBend, 8192, -1, channel, voice});
            }
            pattern.controls.push_back({std::max(sectionStart, sectionEnd - 1.0 / 64.0),
                                        1, 0, channel, voice});
            pattern.expressions.push_back({std::max(sectionStart, sectionEnd - 1.0 / 64.0),
                ExpressionEventType::ChannelPressure, 0, -1, channel, voice});
        }
    }

    std::sort(pattern.controls.begin(), pattern.controls.end(), [](const auto& left, const auto& right) {
        if (left.beat != right.beat) return left.beat < right.beat;
        if (left.partId != right.partId) return left.partId < right.partId;
        if (left.channel != right.channel) return left.channel < right.channel;
        if (left.controller != right.controller) return left.controller < right.controller;
        return left.value < right.value;
    });
    pattern.controls.erase(std::unique(pattern.controls.begin(), pattern.controls.end(), [](const auto& a, const auto& b) {
        return std::abs(a.beat - b.beat) < 0.0001 && a.partId == b.partId && a.channel == b.channel &&
               a.controller == b.controller && a.value == b.value;
    }), pattern.controls.end());
    std::sort(pattern.expressions.begin(), pattern.expressions.end(), [](const auto& left, const auto& right) {
        if (left.beat != right.beat) return left.beat < right.beat;
        if (left.partId != right.partId) return left.partId < right.partId;
        if (left.channel != right.channel) return left.channel < right.channel;
        if (left.type != right.type) return left.type < right.type;
        return left.note < right.note;
    });
    pattern.expressions.erase(std::unique(pattern.expressions.begin(), pattern.expressions.end(), [](const auto& a, const auto& b) {
        return std::abs(a.beat - b.beat) < 0.0001 && a.partId == b.partId && a.type == b.type && a.channel == b.channel &&
               a.note == b.note && a.value == b.value;
    }), pattern.expressions.end());
    constexpr auto maximumExpressionEvents = std::size_t{65536};
    if (pattern.controls.size() > maximumExpressionEvents) pattern.controls.resize(maximumExpressionEvents);
    if (pattern.expressions.size() > maximumExpressionEvents) pattern.expressions.resize(maximumExpressionEvents);
}

void PerformanceExpression::applyIdeaDefaults(Pattern& pattern, double bpm, double beatsPerBar) {
    SongPlan plan;
    plan.totalBars = std::max(1, static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar)));
    plan.targetSeconds = std::max(30, static_cast<int>(std::ceil(pattern.lengthBeats * 60.0 / bpm)));
    plan.bpm = bpm;
    plan.beatsPerBar = beatsPerBar;
    plan.seed = pattern.seed;
    SongSection section;
    section.name = "Idea";
    section.bars = plan.totalBars;
    section.energy = 0.58;
    section.tension = 0.46;
    section.density = 0.55;
    std::array<bool, static_cast<std::size_t>(VoiceId::Count)> seen{};
    for (auto& note : pattern.notes) {
        const auto voice = note.voice == VoiceId::Unspecified ? inferVoiceFromChannel(note.channel) : note.voice;
        note.voice = voice;
        const auto index = static_cast<std::size_t>(voice);
        if (index >= seen.size() || seen[index]) continue;
        seen[index] = true;
        section.activeVoices.push_back(voice);
        const auto& definition = voiceDefinition(voice);
        PlannedVoice planned;
        planned.id = voice;
        planned.minimumPitch = definition.minimumPitch;
        planned.maximumPitch = definition.maximumPitch;
        planned.performance = defaultPerformanceProfile(voice);
        plan.voices.push_back(std::move(planned));
    }
    plan.sections.push_back(std::move(section));
    apply(pattern, plan);
}

} // namespace pulso
