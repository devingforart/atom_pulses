#include "TonalContract.h"

#include "Scale.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

namespace pulso {
namespace {

constexpr double timingTolerance = 0.02;
constexpr double minimumDuration = 1.0 / 64.0;

bool pitchedVoice(VoiceId voice) noexcept {
    return voice != VoiceId::Unspecified && !isVoiceInFamily(voice, VoiceFamily::Rhythm) &&
           voice != VoiceId::Transitions;
}

bool harmonicVoice(VoiceId voice) noexcept;

const InstrumentPart* playbackPart(const Pattern& pattern, const NoteEvent& note) noexcept {
    if (note.partId == 0) return nullptr;
    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
        return part.id == note.partId;
    });
    return found == pattern.parts.end() ? nullptr : &*found;
}

bool pitchedNote(const Pattern& pattern, const NoteEvent& note) noexcept {
    if (pitchedVoice(note.voice)) return true;
    // A transition voice is only non-tonal when Live receives a percussion/FX part.
    // If orchestration assigns it to a chromatic texture or synth, its MIDI pitch is
    // audible and must obey the same tonal contract as every other instrument.
    const auto* part = playbackPart(pattern, note);
    return note.voice == VoiceId::Transitions && part != nullptr &&
           part->department != ScoreDepartment::Rhythm;
}

bool harmonicNote(const Pattern& pattern, const NoteEvent& note) noexcept {
    return harmonicVoice(note.voice) ||
           (note.voice == VoiceId::Transitions && pitchedNote(pattern, note));
}

std::pair<int, int> playbackRange(const Pattern& pattern, const NoteEvent& note) noexcept {
    if (const auto* part = playbackPart(pattern, note))
        return {part->minimumPitch, part->maximumPitch};
    const auto& definition = voiceDefinition(note.voice);
    return {definition.minimumPitch, definition.maximumPitch};
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

bool sustainedVoice(VoiceId voice) noexcept {
    return harmonicVoice(voice) || bassVoice(voice) || voice == VoiceId::Atmosphere;
}

bool containsPitchClass(std::span<const int> pitchClasses, int pitch) {
    return std::any_of(pitchClasses.begin(), pitchClasses.end(), [pitch](int pitchClass) {
        return positiveModulo(pitchClass, 12) == positiveModulo(pitch, 12);
    });
}

int nearestAllowed(int pitch, std::span<const int> pitchClasses, int minimum, int maximum) {
    for (auto distance = 0; distance < 36; ++distance) {
        const auto down = pitch - distance;
        if (down >= minimum && containsPitchClass(pitchClasses, down)) return down;
        const auto up = pitch + distance;
        if (up <= maximum && containsPitchClass(pitchClasses, up)) return up;
    }
    return std::clamp(pitch, minimum, maximum);
}

std::vector<int> scalePitchClasses(int root, ScaleKind scale) {
    std::vector<int> result;
    for (const auto interval : intervalsFor(scale))
        result.push_back(positiveModulo(root + interval, 12));
    return result;
}

const HarmonicWindow* windowAt(std::span<const HarmonicWindow> windows, double beat) {
    if (windows.empty()) return nullptr;
    const auto after = std::upper_bound(windows.begin(), windows.end(), beat + timingTolerance,
        [](double position, const HarmonicWindow& window) {
            return position < window.startBeat;
        });
    if (after == windows.begin()) return nullptr;
    return &*std::prev(after);
}

bool strongMetricPosition(double beat, double beatsPerBar,
                          const HarmonicWindow* window) noexcept {
    const auto beatInBar = beat - std::floor(beat / beatsPerBar) * beatsPerBar;
    if (beatInBar < 0.10 || std::abs(beatInBar - beatsPerBar * 0.5) < 0.09) return true;
    return window != nullptr && std::abs(beat - window->startBeat) < 0.09;
}

int collisionPriority(VoiceId voice) noexcept {
    switch (voice) {
        case VoiceId::SubBass: return 10;
        case VoiceId::HarmonicFoundation: return 9;
        case VoiceId::Lead: return 8;
        case VoiceId::MovementBass: return 7;
        case VoiceId::HarmonicPulse: return 6;
        case VoiceId::Countermelody: return 5;
        case VoiceId::HarmonicUpper: return 4;
        case VoiceId::Atmosphere: return 3;
        default: return 1;
    }
}

bool harshInterval(int leftPitch, int rightPitch) noexcept {
    const auto interval = positiveModulo(std::abs(leftPitch - rightPitch), 12);
    return interval == 1 || interval == 6 || interval == 11;
}

bool intentionalVerticalColour(const NoteEvent& left, const NoteEvent& right,
                               const HarmonicWindow* window, TonalPolicy policy) noexcept {
    if (policy == TonalPolicy::Consolidated) return false;
    if (window == nullptr ||
        !containsPitchClass(window->pitchClasses, left.pitch) ||
        !containsPitchClass(window->pitchClasses, right.pitch)) return false;
    const auto distance = std::abs(left.pitch - right.pitch);
    const auto interval = positiveModulo(distance, 12);
    const auto structuralColour = window->function == HarmonicFunction::Dominant ||
                                  window->function == HarmonicFunction::Chromatic ||
                                  window->function == HarmonicFunction::Colour;
    if (bassVoice(left.voice) || bassVoice(right.voice)) {
        if (interval == 6) return structuralColour && distance >= 18;
        return (interval == 1 || interval == 11) && structuralColour &&
               window->tension >= 0.65 && distance >= 24;
    }
    const auto compatibleVoices = [](VoiceId voice) {
        return harmonicVoice(voice) || melodicVoice(voice) || voice == VoiceId::Atmosphere;
    };
    if (!compatibleVoices(left.voice) || !compatibleVoices(right.voice)) return false;
    if (window->voicing == VoicingStrategy::Cluster)
        return policy == TonalPolicy::Free &&
               window->tension >= 0.45 && left.pitch >= 55 && right.pitch >= 55;
    if (policy == TonalPolicy::Expanded) {
        // Expanded harmony permits a registered tritone, but not unresolved
        // semitone clusters merely because the plan labelled them as colour.
        return interval == 6 && structuralColour && distance >= 12;
    }
    if (interval == 6 && structuralColour) return distance >= 6;
    if ((interval == 1 || interval == 11) && structuralColour)
        return window->tension >= 0.60 && std::min(left.pitch, right.pitch) >= 55 &&
               distance >= 12;
    return window->voicing == VoicingStrategy::Quartal && window->tension >= 0.72 &&
           interval == 6 && std::min(left.pitch, right.pitch) >= 55;
}

bool overlaps(const NoteEvent& left, const NoteEvent& right) noexcept {
    return left.startBeat < right.endBeat() - timingTolerance &&
           right.startBeat < left.endBeat() - timingTolerance;
}

void addIssue(TonalAuditReport& report, double beat, VoiceId voice, int pitch,
              std::string kind, VoiceId otherVoice = VoiceId::Unspecified,
              int otherPitch = 0) {
    if (report.issues.size() < 16)
        report.issues.push_back({beat, voice, otherVoice, pitch, otherPitch, std::move(kind)});
}

std::vector<HarmonicWindow> legacyWindows(std::span<const std::vector<int>> harmonyByBar,
                                          double beatsPerBar, int rootPitchClass,
                                          ScaleKind scale, double lengthBeats) {
    std::vector<HarmonicWindow> result;
    const auto fallback = scalePitchClasses(rootPitchClass, scale);
    const auto bars = std::max<std::size_t>(1, harmonyByBar.size());
    result.reserve(bars);
    for (std::size_t bar = 0; bar < bars; ++bar) {
        auto pitches = harmonyByBar.empty() ? fallback : harmonyByBar[bar];
        if (pitches.empty()) pitches = fallback;
        result.push_back({bar * beatsPerBar,
                          std::min(lengthBeats, (bar + 1) * beatsPerBar),
                          rootPitchClass, rootPitchClass, std::move(pitches),
                          HarmonicFunction::Tonic, VoicingStrategy::Mixed, 0.25,
                          "legacy", "Legacy harmony"});
    }
    if (!result.empty()) result.back().endBeat = std::max(result.back().endBeat, lengthBeats);
    return result;
}

bool validPassingTone(const Pattern& pattern, std::size_t index, int rootPitchClass,
                      ScaleKind scale, double maximumDuration) {
    const auto& note = pattern.notes[index];
    if (!melodicVoice(note.voice) || note.durationBeats > maximumDuration) return false;
    const NoteEvent* previous = nullptr;
    const NoteEvent* next = nullptr;
    for (auto before = index; before-- > 0;)
        if (pattern.notes[before].voice == note.voice) { previous = &pattern.notes[before]; break; }
    for (auto after = index + 1; after < pattern.notes.size(); ++after)
        if (pattern.notes[after].voice == note.voice) { next = &pattern.notes[after]; break; }
    if (previous == nullptr || next == nullptr) return false;
    const auto into = note.pitch - previous->pitch;
    const auto out = next->pitch - note.pitch;
    const auto stepwise = std::abs(into) >= 1 && std::abs(into) <= 2 &&
                          std::abs(out) >= 1 && std::abs(out) <= 2;
    const auto resolves = (into > 0 && out > 0) || (into < 0 && out < 0) ||
                          previous->pitch == next->pitch;
    return stepwise && resolves &&
           note.startBeat - previous->startBeat <= 1.5 &&
           next->startBeat - note.startBeat <= 1.0 &&
           isPitchClassInScale(previous->pitch, rootPitchClass, scale) &&
           isPitchClassInScale(next->pitch, rootPitchClass, scale) &&
           std::abs(next->pitch - note.pitch) == 1;
}

bool validResolvedLeadingTone(const Pattern& pattern, std::size_t index, int rootPitchClass,
                              ScaleKind scale, double beatsPerBar,
                              std::span<const HarmonicWindow> harmony) {
    if (scale != ScaleKind::Minor || index >= pattern.notes.size()) return false;
    const auto& note = pattern.notes[index];
    if (!melodicVoice(note.voice) || note.durationBeats > 0.50 ||
        positiveModulo(note.pitch, 12) != positiveModulo(rootPitchClass - 1, 12)) return false;
    const auto* window = windowAt(harmony, note.startBeat);
    if (window == nullptr || (window->function != HarmonicFunction::Dominant &&
        window->function != HarmonicFunction::Transitional && window->tension < 0.65)) return false;
    const NoteEvent* next = nullptr;
    for (auto after = index + 1; after < pattern.notes.size(); ++after) {
        const auto& candidate = pattern.notes[after];
        if (candidate.voice == note.voice && candidate.partId == note.partId) {
            next = &candidate;
            break;
        }
    }
    if (next == nullptr || next->startBeat - note.startBeat > std::max(1.0, beatsPerBar * 0.25))
        return false;
    return next->pitch - note.pitch == 1 &&
           positiveModulo(next->pitch, 12) == positiveModulo(rootPitchClass, 12);
}

bool intentionalPassingCollision(const Pattern& pattern, std::size_t passingIndex,
                                 std::size_t otherIndex, int rootPitchClass, ScaleKind scale,
                                 double beatsPerBar,
                                 std::span<const HarmonicWindow> harmony) {
    const auto& passing = pattern.notes[passingIndex];
    const auto& other = pattern.notes[otherIndex];
    const auto* window = windowAt(harmony, passing.startBeat);
    return melodicVoice(passing.voice) && !bassVoice(other.voice) &&
           !strongMetricPosition(passing.startBeat, beatsPerBar, window) &&
           passing.durationBeats <= 0.36 && validPassingTone(pattern, passingIndex,
                                                             rootPitchClass, scale, 0.36) &&
           positiveModulo(std::abs(passing.pitch - other.pitch), 12) != 6;
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
    for (const auto& [name, pitchClass] : roots)
        if (text.starts_with(name)) { root = pitchClass; break; }
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

TonalAuditReport auditTonalContract(const Pattern& pattern, int rootPitchClass, ScaleKind scale,
                                    double beatsPerBar,
                                    std::span<const HarmonicWindow> harmony,
                                    TonalPolicy policy) {
    TonalAuditReport report;
    if (beatsPerBar <= 0.0) return report;
    for (std::size_t noteIndex = 0; noteIndex < pattern.notes.size(); ++noteIndex) {
        const auto& note = pattern.notes[noteIndex];
        if (!pitchedNote(pattern, note)) continue;
        ++report.pitchedNotes;
        const auto* window = windowAt(harmony, note.startBeat);
        const auto chord = window == nullptr ? std::span<const int>{} :
            std::span<const int>(window->pitchClasses);
        const auto strong = strongMetricPosition(note.startBeat, beatsPerBar, window);
        const auto inChord = containsPitchClass(chord, note.pitch);
        const auto inScale = scale == ScaleKind::Chromatic ||
            isPitchClassInScale(note.pitch, rootPitchClass, scale);
        const auto resolvedLeadingTone = validResolvedLeadingTone(
            pattern, noteIndex, rootPitchClass, scale, beatsPerBar, harmony);
        const auto structural = harmonicNote(pattern, note) ||
            ((bassVoice(note.voice) || melodicVoice(note.voice)) && strong);
        if (!resolvedLeadingTone && structural &&
            (!inChord || (policy == TonalPolicy::Consolidated && !inScale))) {
            ++report.strongNonChordNotes;
            addIssue(report, note.startBeat, note.voice, note.pitch, "strong_non_chord");
        } else if (!resolvedLeadingTone && !inScale && (policy == TonalPolicy::Consolidated ||
                   (!inChord && !validPassingTone(pattern, noteIndex, rootPitchClass, scale, 0.36)))) {
            ++report.unsupportedChromaticNotes;
            addIssue(report, note.startBeat, note.voice, note.pitch, "unsupported_chromatic");
        }
        if (sustainedVoice(note.voice) || harmonicNote(pattern, note)) {
            for (const auto& next : harmony) {
                if (next.startBeat <= note.startBeat + timingTolerance ||
                    next.startBeat >= note.endBeat() - timingTolerance) continue;
                if (!containsPitchClass(next.pitchClasses, note.pitch)) {
                    ++report.invalidSustains;
                    addIssue(report, next.startBeat, note.voice, note.pitch, "invalid_sustain");
                    break;
                }
            }
        }
    }
    for (std::size_t leftIndex = 0; leftIndex < pattern.notes.size(); ++leftIndex) {
        const auto& left = pattern.notes[leftIndex];
        if (!pitchedNote(pattern, left)) continue;
        for (auto rightIndex = leftIndex + 1; rightIndex < pattern.notes.size(); ++rightIndex) {
            const auto& right = pattern.notes[rightIndex];
            if (right.startBeat >= left.endBeat() - timingTolerance) break;
            if (!pitchedNote(pattern, right) || !overlaps(left, right) ||
                !harshInterval(left.pitch, right.pitch)) continue;
            const auto conflictBeat = std::max(left.startBeat, right.startBeat);
            if (intentionalVerticalColour(left, right, windowAt(harmony, conflictBeat), policy)) {
                ++report.intentionalClusters;
                continue;
            }
            if (intentionalPassingCollision(pattern, leftIndex, rightIndex, rootPitchClass, scale,
                                            beatsPerBar, harmony) ||
                intentionalPassingCollision(pattern, rightIndex, leftIndex, rootPitchClass, scale,
                                            beatsPerBar, harmony))
                continue;
            ++report.unintendedHarshOverlaps;
            addIssue(report, conflictBeat, right.voice, right.pitch, "harsh_overlap",
                     left.voice, left.pitch);
        }
    }
    return report;
}

TonalRepairReport repairTonalContract(Pattern& pattern, int rootPitchClass, ScaleKind scale,
                                      double beatsPerBar,
                                      std::span<const HarmonicWindow> harmony,
                                      double maximumChromaticRatio,
                                      TonalPolicy policy) {
    TonalRepairReport report;
    if (pattern.notes.empty() || beatsPerBar <= 0.0) return report;
    const auto scalePitches = scalePitchClasses(rootPitchClass, scale);
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.pitch < right.pitch;
    });
    report.before = auditTonalContract(pattern, rootPitchClass, scale, beatsPerBar, harmony, policy);

    const auto maximumChromatic = std::max(1, static_cast<int>(std::lround(
        std::count_if(pattern.notes.begin(), pattern.notes.end(), [](const auto& note) {
            return melodicVoice(note.voice);
        }) * std::clamp(maximumChromaticRatio, 0.0,
            policy == TonalPolicy::Consolidated ? 0.02 : policy == TonalPolicy::Expanded ? 0.06 : 0.15))));
    auto acceptedChromatic = 0;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        auto& note = pattern.notes[index];
        if (!pitchedNote(pattern, note)) continue;
        const auto* window = windowAt(harmony, note.startBeat);
        auto allowed = window == nullptr || window->pitchClasses.empty()
            ? std::span<const int>(scalePitches) : std::span<const int>(window->pitchClasses);
        const auto inScale = scale == ScaleKind::Chromatic ||
            isPitchClassInScale(note.pitch, rootPitchClass, scale);
        const auto inChord = containsPitchClass(allowed, note.pitch);
        const auto strong = strongMetricPosition(note.startBeat, beatsPerBar, window);
        const auto resolvedLeadingTone = validResolvedLeadingTone(
            pattern, index, rootPitchClass, scale, beatsPerBar, harmony);
        const auto passingDuration = policy == TonalPolicy::Consolidated ? 0.25 : 0.36;
        if (resolvedLeadingTone && acceptedChromatic < maximumChromatic) {
            ++acceptedChromatic;
            ++report.intentionalChromaticNotes;
            continue;
        }
        if (policy != TonalPolicy::Consolidated && policy != TonalPolicy::Free && !inScale && !strong && acceptedChromatic < maximumChromatic &&
            validPassingTone(pattern, index, rootPitchClass, scale, passingDuration)) {
            ++acceptedChromatic;
            ++report.intentionalChromaticNotes;
            continue;
        }
        const auto requireChordTone = harmonicNote(pattern, note) ||
            ((bassVoice(note.voice) || melodicVoice(note.voice)) && strong);
        const auto acceptedStructural = requireChordTone && inChord &&
            (policy != TonalPolicy::Consolidated || inScale);
        const auto acceptedDecorative = !requireChordTone &&
            (inScale || (policy != TonalPolicy::Consolidated && inChord));
        if (acceptedStructural || acceptedDecorative || policy == TonalPolicy::Free) continue;
        const auto [minimumPitch, maximumPitch] = playbackRange(pattern, note);
        std::vector<int> consolidatedChordPitches;
        if (policy == TonalPolicy::Consolidated && requireChordTone) {
            for (const auto pitchClass : allowed)
                if (containsPitchClass(scalePitches, pitchClass))
                    consolidatedChordPitches.push_back(positiveModulo(pitchClass, 12));
        }
        const auto repairPitches = !consolidatedChordPitches.empty()
            ? std::span<const int>(consolidatedChordPitches)
            : policy == TonalPolicy::Consolidated
                ? std::span<const int>(scalePitches)
                : requireChordTone ? allowed : std::span<const int>(scalePitches);
        note.pitch = nearestAllowed(note.pitch, repairPitches, minimumPitch, maximumPitch);
        if (!inScale) ++report.outOfScaleRepaired;
        if (strong && requireChordTone) ++report.strongBeatRepaired;
        if (!inScale && !inChord) ++report.unsupportedChromaticRepaired;
    }

    for (auto& note : pattern.notes) {
        if (!sustainedVoice(note.voice) && !harmonicNote(pattern, note)) continue;
        for (const auto& next : harmony) {
            if (next.startBeat <= note.startBeat + timingTolerance ||
                next.startBeat >= note.endBeat() - timingTolerance) continue;
            if (!containsPitchClass(next.pitchClasses, note.pitch)) {
                note.durationBeats = std::max(minimumDuration,
                    next.startBeat - note.startBeat - minimumDuration);
                ++report.harmonicOverlapsTrimmed;
                ++report.exactBoundaryTrims;
                break;
            }
        }
    }

    std::vector<bool> removed(pattern.notes.size());
    for (auto pass = 0; pass < 4; ++pass) {
        auto changed = false;
        for (std::size_t leftIndex = 0; leftIndex < pattern.notes.size(); ++leftIndex) {
            if (removed[leftIndex] || !pitchedNote(pattern, pattern.notes[leftIndex])) continue;
            for (auto rightIndex = leftIndex + 1; rightIndex < pattern.notes.size(); ++rightIndex) {
                if (pattern.notes[rightIndex].startBeat >=
                    pattern.notes[leftIndex].endBeat() - timingTolerance) break;
                if (removed[rightIndex] || !pitchedNote(pattern, pattern.notes[rightIndex])) continue;
                auto& left = pattern.notes[leftIndex];
                auto& right = pattern.notes[rightIndex];
                if (!overlaps(left, right) || !harshInterval(left.pitch, right.pitch)) continue;
                const auto conflictBeat = std::max(left.startBeat, right.startBeat);
                const auto* window = windowAt(harmony, conflictBeat);
                if (intentionalVerticalColour(left, right, window, policy)) {
                    ++report.intentionalClusters;
                    continue;
                }
                if (intentionalPassingCollision(pattern, leftIndex, rightIndex, rootPitchClass, scale,
                                                beatsPerBar, harmony) ||
                    intentionalPassingCollision(pattern, rightIndex, leftIndex, rootPitchClass, scale,
                                                beatsPerBar, harmony))
                    continue;
                // Articulation may lengthen an earlier note over a later attack. In that case
                // the musical boundary wins regardless of voice priority; pitch repair would
                // change the composition when a precise release is sufficient.
                if (left.startBeat + 0.05 < right.startBeat) {
                    const auto duration = right.startBeat - left.startBeat - minimumDuration;
                    if (duration >= minimumDuration) {
                        left.durationBeats = duration;
                        ++report.verticalCollisionsRepaired;
                        ++report.exactBoundaryTrims;
                        changed = true;
                        continue;
                    }
                }
                auto targetIndex = collisionPriority(left.voice) < collisionPriority(right.voice)
                    ? leftIndex : rightIndex;
                if (collisionPriority(left.voice) == collisionPriority(right.voice)) {
                    targetIndex = left.velocity <= right.velocity ? leftIndex : rightIndex;
                }
                auto& target = pattern.notes[targetIndex];
                if (target.startBeat + 0.05 < conflictBeat) {
                    const auto duration = conflictBeat - target.startBeat - minimumDuration;
                    if (duration >= minimumDuration) {
                        target.durationBeats = duration;
                        ++report.verticalCollisionsRepaired;
                        changed = true;
                        continue;
                    }
                }
                const auto* targetWindow = windowAt(harmony, target.startBeat);
                const auto allowed = targetWindow == nullptr || targetWindow->pitchClasses.empty()
                    ? std::span<const int>(scalePitches) : std::span<const int>(targetWindow->pitchClasses);
                const auto [minimumPitch, maximumPitch] = playbackRange(pattern, target);
                auto replacement = -1;
                auto bestDistance = std::numeric_limits<int>::max();
                for (auto candidate = minimumPitch; candidate <= maximumPitch; ++candidate) {
                    if (!containsPitchClass(allowed, candidate)) continue;
                    auto safe = true;
                    for (std::size_t check = 0; check < pattern.notes.size(); ++check) {
                        if (pattern.notes[check].startBeat >= target.endBeat() - timingTolerance) break;
                        if (check == targetIndex || removed[check] ||
                            collisionPriority(pattern.notes[check].voice) < collisionPriority(target.voice) ||
                            !overlaps(target, pattern.notes[check])) continue;
                        if (harshInterval(candidate, pattern.notes[check].pitch)) { safe = false; break; }
                    }
                    const auto distance = std::abs(candidate - target.pitch);
                    if (safe && distance < bestDistance) { replacement = candidate; bestDistance = distance; }
                }
                if (replacement >= 0 && replacement != target.pitch) {
                    target.pitch = replacement;
                    ++report.notesRetunedForVoicing;
                } else {
                    removed[targetIndex] = true;
                    ++report.notesRemoved;
                }
                ++report.verticalCollisionsRepaired;
                changed = true;
            }
        }
        if (!changed) break;
    }
    if (std::any_of(removed.begin(), removed.end(), [](bool value) { return value; })) {
        std::vector<NoteEvent> kept;
        kept.reserve(pattern.notes.size() - static_cast<std::size_t>(report.notesRemoved));
        for (std::size_t index = 0; index < pattern.notes.size(); ++index)
            if (!removed[index]) kept.push_back(pattern.notes[index]);
        pattern.notes = std::move(kept);
    }
    report.after = auditTonalContract(pattern, rootPitchClass, scale, beatsPerBar, harmony, policy);
    return report;
}

TonalRepairReport repairTonalContract(Pattern& pattern, int rootPitchClass, ScaleKind scale,
                                      double beatsPerBar,
                                      std::span<const std::vector<int>> harmonyByBar,
                                      double maximumChromaticRatio,
                                      TonalPolicy policy) {
    const auto windows = legacyWindows(harmonyByBar, beatsPerBar, rootPitchClass, scale,
                                       pattern.lengthBeats);
    return repairTonalContract(pattern, rootPitchClass, scale, beatsPerBar, windows,
                               maximumChromaticRatio, policy);
}

} // namespace pulso
