#include "TonalContract.h"

#include "Scale.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

namespace pulso {
namespace {

bool pitchedVoice(VoiceId voice) noexcept {
    return voice != VoiceId::Unspecified && !isVoiceInFamily(voice, VoiceFamily::Rhythm) &&
           voice != VoiceId::Transitions;
}

bool harmonicVoice(VoiceId voice) noexcept {
    return voice == VoiceId::HarmonicFoundation || voice == VoiceId::HarmonicPulse ||
           voice == VoiceId::HarmonicUpper;
}

bool melodicVoice(VoiceId voice) noexcept {
    return voice == VoiceId::Lead || voice == VoiceId::Countermelody;
}

bool bassVoice(VoiceId voice) noexcept {
    return voice == VoiceId::SubBass || voice == VoiceId::MovementBass;
}

bool containsPitchClass(std::span<const int> pitchClasses, int pitch) {
    return std::any_of(pitchClasses.begin(), pitchClasses.end(), [pitch](int pitchClass) {
        return positiveModulo(pitchClass, 12) == positiveModulo(pitch, 12);
    });
}

int nearestAllowed(int pitch, std::span<const int> pitchClasses, int minimum, int maximum) {
    for (auto distance = 0; distance < 24; ++distance) {
        const auto down = pitch - distance;
        if (down >= minimum && containsPitchClass(pitchClasses, down)) return down;
        const auto up = pitch + distance;
        if (up <= maximum && containsPitchClass(pitchClasses, up)) return up;
    }
    return std::clamp(pitch, minimum, maximum);
}

std::vector<int> scalePitchClasses(int root, ScaleKind scale) {
    std::vector<int> result;
    for (const auto interval : intervalsFor(scale)) result.push_back(positiveModulo(root + interval, 12));
    return result;
}

bool strongMetricPosition(double beatInBar, double beatsPerBar) noexcept {
    if (beatInBar < 0.10) return true;
    const auto secondaryAccent = beatsPerBar * 0.5;
    return std::abs(beatInBar - secondaryAccent) < 0.09;
}

int collisionPriority(VoiceId voice) noexcept {
    switch (voice) {
        case VoiceId::SubBass: return 9;
        case VoiceId::HarmonicFoundation: return 8;
        case VoiceId::Lead: return 7;
        case VoiceId::MovementBass: return 6;
        case VoiceId::HarmonicPulse: return 5;
        case VoiceId::Countermelody: return 4;
        case VoiceId::HarmonicUpper: return 3;
        case VoiceId::Atmosphere: return 2;
        default: return 1;
    }
}

bool harshInterval(int leftPitch, int rightPitch) noexcept {
    const auto interval = positiveModulo(std::abs(leftPitch - rightPitch), 12);
    return interval == 1 || interval == 6 || interval == 11;
}

} // namespace

std::string canonicalKeyName(int rootPitchClass, ScaleKind scale) {
    constexpr std::array names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto mode = scale == ScaleKind::Major ? " major" : scale == ScaleKind::Dorian ? " dorian" :
                      scale == ScaleKind::Mixolydian ? " mixolydian" :
                      scale == ScaleKind::Chromatic ? " chromatic" : " minor";
    return std::string(names[static_cast<std::size_t>(positiveModulo(rootPitchClass, 12))]) + mode;
}

std::optional<std::pair<int, ScaleKind>> parseKeyName(std::string_view source) {
    auto text = std::string(source);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    text = text.substr(first);
    constexpr std::array<std::pair<std::string_view, int>, 17> roots{{
        {"c#", 1}, {"db", 1}, {"d#", 3}, {"eb", 3}, {"f#", 6}, {"gb", 6},
        {"g#", 8}, {"ab", 8}, {"a#", 10}, {"bb", 10}, {"c", 0}, {"d", 2},
        {"e", 4}, {"f", 5}, {"g", 7}, {"a", 9}, {"b", 11}
    }};
    auto root = -1;
    for (const auto& [name, pitchClass] : roots) {
        if (text.starts_with(name)) { root = pitchClass; break; }
    }
    if (root < 0) return std::nullopt;
    auto scale = ScaleKind::Major;
    if (text.find("minor") != std::string::npos || text.find(" min") != std::string::npos)
        scale = ScaleKind::Minor;
    else if (text.find("dorian") != std::string::npos) scale = ScaleKind::Dorian;
    else if (text.find("mixolydian") != std::string::npos) scale = ScaleKind::Mixolydian;
    return std::pair{root, scale};
}

void canonicalizeMotif(std::vector<int>& intervals, ScaleKind scale) {
    if (scale == ScaleKind::Chromatic) return;
    for (auto& interval : intervals) {
        if (isPitchClassInScale(interval, 0, scale)) continue;
        for (auto distance = 1; distance < 6; ++distance) {
            const auto down = interval - distance;
            const auto up = interval + distance;
            if (isPitchClassInScale(down, 0, scale)) { interval = down; break; }
            if (isPitchClassInScale(up, 0, scale)) { interval = up; break; }
        }
    }
}

TonalRepairReport repairTonalContract(Pattern& pattern, int rootPitchClass, ScaleKind scale,
                                      double beatsPerBar,
                                      std::span<const std::vector<int>> harmonyByBar,
                                      double maximumChromaticRatio) {
    TonalRepairReport report;
    if (pattern.notes.empty() || scale == ScaleKind::Chromatic || beatsPerBar <= 0.0) return report;
    const auto scalePitches = scalePitchClasses(rootPitchClass, scale);
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.pitch < right.pitch;
    });

    const auto maximumChromatic = std::max(1, static_cast<int>(std::lround(
        std::count_if(pattern.notes.begin(), pattern.notes.end(), [](const auto& note) {
            return melodicVoice(note.voice);
        }) * std::clamp(maximumChromaticRatio, 0.0, 0.15))));
    auto acceptedChromatic = 0;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        auto& note = pattern.notes[index];
        if (!pitchedVoice(note.voice)) continue;
        const auto bar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)), 0,
                                    std::max(0, static_cast<int>(harmonyByBar.size()) - 1));
        const auto beatInBar = note.startBeat - bar * beatsPerBar;
        const auto chord = harmonyByBar.empty() ? std::span<const int>(scalePitches) :
            std::span<const int>(harmonyByBar[static_cast<std::size_t>(bar)]);
        const auto inScale = isPitchClassInScale(note.pitch, rootPitchClass, scale);
        const auto strong = strongMetricPosition(beatInBar, beatsPerBar);
        auto intentionalChromatic = false;
        if (!inScale && melodicVoice(note.voice) && !strong && note.durationBeats <= 0.36 &&
            acceptedChromatic < maximumChromatic) {
            const NoteEvent* previous = nullptr;
            const NoteEvent* next = nullptr;
            for (auto before = index; before-- > 0;) {
                if (pattern.notes[before].voice == note.voice) { previous = &pattern.notes[before]; break; }
            }
            for (auto after = index + 1; after < pattern.notes.size(); ++after) {
                if (pattern.notes[after].voice == note.voice) { next = &pattern.notes[after]; break; }
            }
            intentionalChromatic = previous != nullptr && next != nullptr &&
                note.startBeat - previous->startBeat <= 1.5 && next->startBeat - note.startBeat <= 1.0 &&
                isPitchClassInScale(previous->pitch, rootPitchClass, scale) &&
                isPitchClassInScale(next->pitch, rootPitchClass, scale) &&
                std::abs(next->pitch - note.pitch) == 1;
        }
        if (intentionalChromatic) {
            ++acceptedChromatic;
            ++report.intentionalChromaticNotes;
            continue;
        }

        const auto requireChordTone = harmonicVoice(note.voice) ||
            ((bassVoice(note.voice) || melodicVoice(note.voice)) && strong);
        const auto valid = inScale && (!requireChordTone || containsPitchClass(chord, note.pitch));
        if (valid) continue;
        const auto& definition = voiceDefinition(note.voice);
        const auto repaired = nearestAllowed(note.pitch, requireChordTone ? chord : std::span<const int>(scalePitches),
                                             definition.minimumPitch, definition.maximumPitch);
        if (!inScale) ++report.outOfScaleRepaired;
        if (strong && requireChordTone) ++report.strongBeatRepaired;
        if (!inScale && !intentionalChromatic) ++report.unsupportedChromaticRepaired;
        note.pitch = repaired;
    }

    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        auto& note = pattern.notes[index];
        if (!pitchedVoice(note.voice)) continue;
        for (auto otherIndex = std::size_t{}; otherIndex < index; ++otherIndex) {
            auto& other = pattern.notes[otherIndex];
            if (!pitchedVoice(other.voice) || other.endBeat() <= note.startBeat + 0.02 ||
                other.startBeat > note.startBeat + 0.02 || !harshInterval(other.pitch, note.pitch)) continue;
            if (collisionPriority(note.voice) > collisionPriority(other.voice)) {
                if (other.startBeat + 0.05 < note.startBeat) {
                    other.durationBeats = std::max(0.03, note.startBeat - other.startBeat - 0.025);
                    ++report.verticalCollisionsRepaired;
                }
                continue;
            }
            if (collisionPriority(note.voice) == collisionPriority(other.voice) || note.durationBeats < 0.24)
                continue;
            const auto bar = std::clamp(static_cast<int>(note.startBeat / beatsPerBar), 0,
                                        std::max(0, static_cast<int>(harmonyByBar.size()) - 1));
            const auto allowed = harmonyByBar.empty() ? std::span<const int>(scalePitches) :
                std::span<const int>(harmonyByBar[static_cast<std::size_t>(bar)]);
            const auto& definition = voiceDefinition(note.voice);
            for (auto distance = 1; distance <= 4; ++distance) {
                const auto up = nearestAllowed(note.pitch + distance, allowed,
                                               definition.minimumPitch, definition.maximumPitch);
                const auto down = nearestAllowed(note.pitch - distance, allowed,
                                                 definition.minimumPitch, definition.maximumPitch);
                if (!harshInterval(other.pitch, up)) { note.pitch = up; ++report.verticalCollisionsRepaired; break; }
                if (!harshInterval(other.pitch, down)) { note.pitch = down; ++report.verticalCollisionsRepaired; break; }
            }
        }
    }

    if (!harmonyByBar.empty()) {
        for (auto& note : pattern.notes) {
            if (!harmonicVoice(note.voice) && !bassVoice(note.voice)) continue;
            const auto startBar = std::clamp(static_cast<int>(note.startBeat / beatsPerBar), 0,
                                             static_cast<int>(harmonyByBar.size()) - 1);
            for (auto bar = startBar + 1; bar < static_cast<int>(harmonyByBar.size()); ++bar) {
                const auto boundary = bar * beatsPerBar;
                if (boundary >= note.endBeat() - 0.01) break;
                if (harmonyByBar[static_cast<std::size_t>(bar)] ==
                    harmonyByBar[static_cast<std::size_t>(bar - 1)]) continue;
                if (!containsPitchClass(harmonyByBar[static_cast<std::size_t>(bar)], note.pitch)) {
                    note.durationBeats = std::max(0.03, boundary - note.startBeat - 0.025);
                    ++report.harmonicOverlapsTrimmed;
                    break;
                }
            }
        }
    }
    return report;
}

} // namespace pulso
