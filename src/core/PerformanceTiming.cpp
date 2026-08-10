#include "PerformanceTiming.h"

#include "Orchestration.h"

#include <algorithm>
#include <cmath>

namespace pulso {
namespace {

std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double signedUnit(std::uint64_t seed, const NoteEvent& note, std::size_t ordinal) noexcept {
    const auto value = mix(seed ^ (static_cast<std::uint64_t>(note.voice) + 1) * 0x9e3779b97f4a7c15ULL ^
                           static_cast<std::uint64_t>(note.pitch + 1) * 0xd1b54a32d192ed03ULL ^
                           static_cast<std::uint64_t>(ordinal + 1) * 0x94d049bb133111ebULL);
    return static_cast<double>(value & 0xffffu) / 32767.5 - 1.0;
}

} // namespace

void quantizePatternTiming(Pattern& pattern, int stepsPerBeat) {
    const auto steps = std::clamp(stepsPerBeat, 1, 16);
    const auto grid = 1.0 / steps;
    const auto snap = [grid](double beat) { return std::round(beat / grid) * grid; };
    for (auto& note : pattern.notes) {
        const auto start = std::clamp(snap(note.startBeat), 0.0,
                                      std::max(0.0, pattern.lengthBeats - grid));
        const auto end = std::clamp(snap(note.endBeat()), start + grid, pattern.lengthBeats);
        note.startBeat = start;
        note.durationBeats = std::max(grid, end - start);
    }
    for (auto& control : pattern.controls)
        control.beat = std::clamp(snap(control.beat), 0.0, std::max(0.0, pattern.lengthBeats - grid));
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.pitch < right.pitch;
    });
    pattern.notes.erase(std::unique(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        return left.startBeat == right.startBeat && left.voice == right.voice && left.pitch == right.pitch;
    }), pattern.notes.end());
}

double performanceOffsetBeats(const NoteEvent& note, std::uint64_t seed,
                              std::size_t ordinal, double bpm) noexcept {
    const auto voice = note.voice == VoiceId::Unspecified ? inferVoiceFromChannel(note.channel) : note.voice;
    const auto random = signedUnit(seed, note, ordinal);
    auto milliseconds = 0.0;
    switch (voice) {
        case VoiceId::CoreDrums: return 0.0;
        case VoiceId::SnareClap: milliseconds = 2.5 + random * 0.8; break;
        case VoiceId::ClosedHats: {
            const auto sixteenth = static_cast<int>(std::llround(note.startBeat * 4.0));
            milliseconds = (sixteenth % 2 != 0 ? 5.0 : 0.0) + random * 1.0;
            break;
        }
        case VoiceId::OpenHatsShaker: milliseconds = 4.0 + random * 1.5; break;
        case VoiceId::LowPercussion: milliseconds = random * 3.0; break;
        case VoiceId::HighPercussion: milliseconds = random * 3.5; break;
        case VoiceId::SubBass: milliseconds = 1.0; break;
        case VoiceId::MovementBass: milliseconds = random * 2.0; break;
        case VoiceId::HarmonicFoundation: return 0.0;
        case VoiceId::HarmonicPulse: milliseconds = random * 2.0; break;
        case VoiceId::HarmonicUpper: milliseconds = 1.0 + random; break;
        case VoiceId::Lead: milliseconds = random * 4.0; break;
        case VoiceId::Countermelody: milliseconds = random * 4.0; break;
        case VoiceId::Atmosphere: milliseconds = random * 2.0; break;
        case VoiceId::Transitions: return 0.0;
        case VoiceId::Unspecified:
        case VoiceId::Count: return 0.0;
    }
    return milliseconds * std::max(1.0, bpm) / 60000.0;
}

} // namespace pulso
