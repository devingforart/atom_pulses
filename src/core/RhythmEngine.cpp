#include "RhythmEngine.h"

#include "PhraseDirector.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <span>

namespace pulso {
namespace {

bool active(const SongSection& section, VoiceId voice) {
    return std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) !=
           section.activeVoices.end();
}

const RhythmGesture* gestureAt(const SongSection& section, int bar, RhythmGestureKind kind) {
    const auto found = std::find_if(section.rhythm.gestures.begin(), section.rhythm.gestures.end(),
        [=](const auto& gesture) { return gesture.barOffset == bar && gesture.kind == kind; });
    return found == section.rhythm.gestures.end() ? nullptr : &*found;
}

bool suppressKickAt(const SongSection& section, int bar, double quarterBeat) {
    if (gestureAt(section, bar, RhythmGestureKind::FullBarMute) != nullptr) return true;
    if (gestureAt(section, bar, RhythmGestureKind::HalfBarMute) != nullptr && quarterBeat >= 2.0) return true;
    return gestureAt(section, bar, RhythmGestureKind::DropLastKick) != nullptr && quarterBeat >= 2.99;
}

double human(std::uint64_t seed, int absoluteBar, int lane, int ordinal) noexcept {
    auto value = seed ^ (static_cast<std::uint64_t>(absoluteBar + 1) * 0x9e3779b97f4a7c15ULL) ^
                 (static_cast<std::uint64_t>(lane + 1) * 0xd1b54a32d192ed03ULL) ^
                 (static_cast<std::uint64_t>(ordinal + 1) * 0x94d049bb133111ebULL);
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    return static_cast<double>(value & 0xffffu) / 32767.5 - 1.0;
}

void addHit(Pattern& pattern, double beat, int pitch, int velocity, VoiceId voice,
            double duration = 0.07, bool authoredTiming = false) {
    if (beat < 0.0 || beat >= pattern.lengthBeats) return;
    pattern.notes.push_back({beat, duration, pitch, std::clamp(velocity, 1, 127), 10, voice, 0,
                             authoredTiming});
}

std::vector<double> kickQuarterBeats(KickState state) {
    if (state == KickState::FourOnFloor) return {0.0, 1.0, 2.0, 3.0};
    if (state == KickState::Sparse) return {0.0, 2.0};
    if (state == KickState::Reduced) return {0.0};
    return {};
}

const SongSection* sectionForBar(const SongPlan& plan, int bar) {
    const auto found = std::find_if(plan.sections.begin(), plan.sections.end(), [bar](const auto& section) {
        return bar >= section.startBar && bar < section.startBar + section.bars;
    });
    return found == plan.sections.end() ? nullptr : &*found;
}

bool kickNear(const Pattern& pattern, double beat) {
    return std::any_of(pattern.notes.begin(), pattern.notes.end(), [beat](const auto& note) {
        return note.voice == VoiceId::CoreDrums && note.pitch == 36 &&
               std::abs(note.startBeat - beat) < 0.045;
    });
}

template <std::size_t Size>
bool containsPitch(const std::array<int, Size>& pitches, int pitch) noexcept {
    return std::find(pitches.begin(), pitches.end(), pitch) != pitches.end();
}

int canonicalPercussionPitch(VoiceId voice, int pitch, std::size_t ordinal) noexcept {
    constexpr std::array kicks{35, 36};
    constexpr std::array backbeats{37, 38, 39, 40};
    constexpr std::array closedHats{42, 44};
    constexpr std::array openTops{46, 58, 70};
    constexpr std::array lowPercussion{41, 43, 45, 47, 48, 50, 60, 61, 64, 65, 66};
    constexpr std::array highPercussion{49, 51, 53, 54, 56, 62, 63, 67, 68, 69, 70,
                                        75, 76, 77, 80, 81};
    constexpr std::array transitions{49, 51, 52, 55, 57};
    switch (voice) {
        case VoiceId::CoreDrums: return containsPitch(kicks, pitch) ? pitch : 36;
        case VoiceId::SnareClap: return containsPitch(backbeats, pitch) ? pitch : 39;
        case VoiceId::ClosedHats: return containsPitch(closedHats, pitch) ? pitch : 42;
        case VoiceId::OpenHatsShaker:
            return containsPitch(openTops, pitch) ? pitch : openTops[ordinal % openTops.size()];
        case VoiceId::LowPercussion:
            return containsPitch(lowPercussion, pitch) ? pitch : lowPercussion[ordinal % lowPercussion.size()];
        case VoiceId::HighPercussion:
            return containsPitch(highPercussion, pitch) ? pitch : highPercussion[ordinal % highPercussion.size()];
        case VoiceId::Transitions:
            return containsPitch(transitions, pitch) ? pitch : transitions[ordinal % transitions.size()];
        default: return pitch;
    }
}

int diversifyPercussion(Pattern& pattern, VoiceId voice, std::span<const int> palette,
                        std::size_t minimumNotes, std::size_t minimumArticulations) {
    std::vector<std::size_t> indices;
    std::set<int> pitches;
    std::map<int, std::size_t> counts;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        if (pattern.notes[index].voice != voice) continue;
        indices.push_back(index);
        pitches.insert(pattern.notes[index].pitch);
        ++counts[pattern.notes[index].pitch];
    }
    if (indices.size() < minimumNotes || palette.empty()) return 0;
    auto changed = 0;
    if (!counts.empty()) {
        const auto dominant = std::max_element(counts.begin(), counts.end(),
            [](const auto& left, const auto& right) { return left.second < right.second; })->first;
        const auto maximumDominance = std::max<std::size_t>(1, indices.size() * 2 / 3);
        auto excess = counts[dominant] > maximumDominance
            ? counts[dominant] - maximumDominance : 0;
        std::vector<std::size_t> dominantIndices;
        for (const auto index : indices)
            if (pattern.notes[index].pitch == dominant) dominantIndices.push_back(index);
        const auto changesNeeded = excess;
        for (std::size_t change = 0; change < changesNeeded && !dominantIndices.empty(); ++change) {
            const auto position = std::min(dominantIndices.size() - 1,
                (change * dominantIndices.size() + dominantIndices.size() / 2) /
                std::max<std::size_t>(1, changesNeeded));
            auto& note = pattern.notes[dominantIndices[position]];
            auto target = palette[(change + 1) % palette.size()];
            if (target == dominant) target = palette[(change + 2) % palette.size()];
            if (target == dominant) continue;
            note.pitch = target;
            pitches.insert(target);
            --excess;
            ++changed;
        }
    }
    if (pitches.size() >= minimumArticulations) return changed;
    for (std::size_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
        // Stable cells retain their main articulation; phrase answers receive a contrasting
        // physically valid GM articulation. This changes timbre without inventing attacks.
        if (ordinal % 4 != 3 && ordinal % 11 != 7) continue;
        auto& note = pattern.notes[indices[ordinal]];
        const auto target = palette[(ordinal / 4) % palette.size()];
        if (note.pitch != target) { note.pitch = target; pitches.insert(target); ++changed; }
        if (pitches.size() >= minimumArticulations) break;
    }
    return changed;
}

const RhythmMotif* motifFor(const SongPlan& plan, const SongSection& section) {
    const auto found = std::find_if(plan.rhythmMotifs.begin(), plan.rhythmMotifs.end(), [&](const auto& motif) {
        return motif.id == section.rhythm.motifId;
    });
    return found == plan.rhythmMotifs.end() ? nullptr : &*found;
}

struct LaneInfo { RhythmLane lane; VoiceId voice; int pitch; int velocity; };
constexpr std::array laneInfo{
    LaneInfo{RhythmLane::Kick, VoiceId::CoreDrums, 36, 98},
    LaneInfo{RhythmLane::SnareClap, VoiceId::SnareClap, 39, 82},
    LaneInfo{RhythmLane::ClosedHats, VoiceId::ClosedHats, 42, 54},
    LaneInfo{RhythmLane::OpenHatsShaker, VoiceId::OpenHatsShaker, 46, 62},
    LaneInfo{RhythmLane::LowPercussion, VoiceId::LowPercussion, 64, 58},
    LaneInfo{RhythmLane::HighPercussion, VoiceId::HighPercussion, 75, 51}};

int evolvingLanePitch(RhythmLane lane, int absoluteBar, int step, int stepsPerBar,
                      int fallback) noexcept {
    // A rhythmic motif owns onset DNA, not one immutable sample for the whole song.
    // Rotate only at phrase-scale boundaries so the gesture remains recognisable while
    // Live receives honest GM articulations that can be resolved to distinct one-shots.
    const auto phrase = std::max(0, absoluteBar) / 4;
    const auto secondHalf = step >= std::max(1, stepsPerBar / 2) ? 1 : 0;
    if (lane == RhythmLane::ClosedHats)
        return positiveModulo(absoluteBar, 8) == 7 && secondHalf != 0 ? 44 : 42;
    if (lane == RhythmLane::LowPercussion) {
        constexpr std::array palette{64, 62, 63, 61}; // low/muted/open conga, low bongo
        return palette[static_cast<std::size_t>(positiveModulo(
            phrase + secondHalf, static_cast<int>(palette.size())))];
    }
    if (lane == RhythmLane::HighPercussion) {
        constexpr std::array palette{75, 54, 56, 63, 51}; // clave, tambourine, cowbell, conga, ride
        return palette[static_cast<std::size_t>(positiveModulo(
            phrase + secondHalf, static_cast<int>(palette.size())))];
    }
    return fallback;
}

const std::string& maskFor(const RhythmMotif& motif, RhythmLane lane) {
    switch (lane) {
        case RhythmLane::Kick: return motif.kick;
        case RhythmLane::SnareClap: return motif.snareClap;
        case RhythmLane::ClosedHats: return motif.closedHats;
        case RhythmLane::OpenHatsShaker: return motif.openHatsShaker;
        case RhythmLane::LowPercussion: return motif.lowPercussion;
        case RhythmLane::HighPercussion: return motif.highPercussion;
    }
    return motif.kick;
}

const LaneInfo& infoFor(RhythmLane lane) {
    return *std::find_if(laneInfo.begin(), laneInfo.end(), [lane](const auto& info) {
        return info.lane == lane;
    });
}

LaneInfo infoFor(RhythmInstrument instrument) {
    switch (instrument) {
        case RhythmInstrument::KickDeep: return {RhythmLane::Kick, VoiceId::CoreDrums, 36, 98};
        case RhythmInstrument::KickAlt: return {RhythmLane::Kick, VoiceId::CoreDrums, 35, 91};
        case RhythmInstrument::Snare: return {RhythmLane::SnareClap, VoiceId::SnareClap, 38, 84};
        case RhythmInstrument::Sidestick: return {RhythmLane::SnareClap, VoiceId::SnareClap, 37, 68};
        case RhythmInstrument::Clap: return {RhythmLane::SnareClap, VoiceId::SnareClap, 39, 82};
        case RhythmInstrument::TomLow: return {RhythmLane::LowPercussion, VoiceId::LowPercussion, 45, 75};
        case RhythmInstrument::TomMid: return {RhythmLane::LowPercussion, VoiceId::LowPercussion, 47, 72};
        case RhythmInstrument::TomHigh: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 50, 70};
        case RhythmInstrument::ClosedHat: return {RhythmLane::ClosedHats, VoiceId::ClosedHats, 42, 57};
        case RhythmInstrument::PedalHat: return {RhythmLane::ClosedHats, VoiceId::ClosedHats, 44, 53};
        case RhythmInstrument::OpenHat: return {RhythmLane::OpenHatsShaker, VoiceId::OpenHatsShaker, 46, 64};
        case RhythmInstrument::Ride: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 51, 66};
        case RhythmInstrument::Crash: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 49, 82};
        case RhythmInstrument::Shaker: return {RhythmLane::OpenHatsShaker, VoiceId::OpenHatsShaker, 70, 48};
        case RhythmInstrument::Tambourine: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 54, 62};
        case RhythmInstrument::Cowbell: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 56, 66};
        case RhythmInstrument::CongaLow: return {RhythmLane::LowPercussion, VoiceId::LowPercussion, 64, 67};
        case RhythmInstrument::CongaHigh: return {RhythmLane::HighPercussion, VoiceId::HighPercussion, 63, 65};
    }
    return infoFor(RhythmLane::HighPercussion);
}

bool renderMotifBar(Pattern& pattern, const SongPlan& plan, const SongSection& section,
                    const RhythmMotif& motif, int localBar, int absoluteBar,
                    double barStart, double weight) {
    const auto expected = motif.bars * motif.stepsPerBar;
    if (motif.bars < 1 || motif.stepsPerBar < 1) return false;
    for (const auto& info : laneInfo)
        if (static_cast<int>(maskFor(motif, info.lane).size()) != expected) return false;

    const auto motifBar = localBar % motif.bars;
    const auto stepDuration = plan.beatsPerBar / motif.stepsPerBar;
    for (const auto& info : laneInfo) {
        if (!active(section, info.voice)) continue;
        if (info.lane == RhythmLane::Kick && section.rhythm.kickState == KickState::Muted) continue;
        const auto& mask = maskFor(motif, info.lane);
        auto kickOrdinal = 0;
        for (auto step = 0; step < motif.stepsPerBar; ++step) {
            const auto symbol = mask[static_cast<std::size_t>(motifBar * motif.stepsPerBar + step)];
            if (symbol != '1' && symbol != '2') continue;
            if (info.lane == RhythmLane::Kick) {
                const auto ordinal = kickOrdinal++;
                if (section.rhythm.kickState == KickState::Reduced && ordinal > 0) continue;
                if (section.rhythm.kickState == KickState::Sparse && ordinal % 2 != 0) continue;
            }
            const auto quarter = 4.0 * step / motif.stepsPerBar;
            if (info.lane == RhythmLane::Kick && suppressKickAt(section, localBar, quarter)) continue;
            const auto timingRange = 0.002 + plan.rhythmLanguage.timingFreedom * 0.014;
            const auto micro = info.lane == RhythmLane::Kick ? 0.0
                : human(plan.seed, absoluteBar, static_cast<int>(info.lane), step) * timingRange;
            const auto swing = step % 2 == 1 ? section.rhythm.swing * stepDuration * 0.32 : 0.0;
            const auto contrast = plan.rhythmLanguage.velocityContrast;
            const auto backbeat = info.lane == RhythmLane::SnareClap &&
                (std::abs(quarter - 1.0) < 0.05 || std::abs(quarter - 3.0) < 0.05)
                ? plan.rhythmLanguage.backbeatGravity * 15.0 : 0.0;
            const auto offbeat = step % 4 != 0 ? plan.rhythmLanguage.syncopation * 5.0 : 0.0;
            const auto responseShape = (info.lane == RhythmLane::LowPercussion ||
                                        info.lane == RhythmLane::HighPercussion)
                ? ((motifBar + (info.lane == RhythmLane::HighPercussion ? 1 : 0)) % 2 == 0 ? 1.0 : -1.0) *
                    plan.rhythmLanguage.callResponse * 5.0 : 0.0;
            const auto humanRange = info.lane == RhythmLane::Kick
                ? (1.0 - plan.rhythmLanguage.pulseStability) * 7.0
                : 2.0 + contrast * 6.0;
            const auto pulseAnchor = info.lane == RhythmLane::Kick && step % 4 == 0
                ? plan.rhythmLanguage.pulseStability * 7.0 : 0.0;
            const auto velocity = static_cast<int>((info.velocity - (symbol == '1' ? contrast * 7.0 : 0.0) +
                (symbol == '2' ? 8.0 + contrast * 13.0 : 0.0) + backbeat + offbeat + responseShape +
                pulseAnchor + human(plan.seed, absoluteBar, static_cast<int>(info.lane), step) * humanRange) * weight);
            const auto pitch = evolvingLanePitch(info.lane, absoluteBar, step,
                                                  motif.stepsPerBar, info.pitch);
            addHit(pattern, barStart + step * stepDuration + micro + swing, pitch, velocity,
                   info.voice, info.lane == RhythmLane::OpenHatsShaker ? 0.14 : 0.055);
        }
    }

    for (const auto& ornament : motif.ornaments) {
        if (ornament.step / motif.stepsPerBar != motifBar) continue;
        const auto info = infoFor(ornament.instrument);
        if (!active(section, info.voice)) continue;
        if (info.voice == VoiceId::CoreDrums && section.rhythm.kickState == KickState::Muted) continue;
        const auto localStep = ornament.step % motif.stepsPerBar;
        const auto motion = plan.rhythmLanguage.orchestrationMotion;
        const auto shapedVelocity = std::clamp(static_cast<int>(std::lround(
            ornament.velocity * weight + human(plan.seed, absoluteBar, 12, localStep) * (3.0 + motion * 8.0))), 1, 127);
        addHit(pattern, barStart + localStep * stepDuration, info.pitch, shapedVelocity, info.voice,
               std::max(0.025, ornament.durationSteps * stepDuration));
    }

    if (plan.rhythmLanguage.ghostDensity > 0.02 && active(section, VoiceId::SnareClap)) {
        for (auto step = 1; step < motif.stepsPerBar; ++step) {
            const auto chance = (human(plan.seed ^ 0x47484f5354ULL, absoluteBar, 9, step) + 1.0) * 0.5;
            const auto effectiveGhostDensity = plan.rhythmLanguage.ghostDensity *
                (1.0 - plan.rhythmLanguage.silenceBias * 0.72);
            if (chance > effectiveGhostDensity * 0.42) continue;
            const auto beat = barStart + step * stepDuration;
            const auto collision = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                return note.voice == VoiceId::SnareClap && std::abs(note.startBeat - beat) < stepDuration * 0.25;
            });
            if (!collision)
                addHit(pattern, beat, 38, std::clamp(static_cast<int>(24 +
                    plan.rhythmLanguage.velocityContrast * 18.0), 1, 55), VoiceId::SnareClap, 0.035);
        }
    }
    return true;
}

void applyMutations(Pattern& pattern, const SongPlan& plan, const SongSection& section,
                    int localBar, double barStart, int stepsPerBar) {
    const auto stepDuration = plan.beatsPerBar / std::max(1, stepsPerBar);
    for (const auto& mutation : section.rhythm.mutations) {
        if (mutation.barOffset != localBar) continue;
        const auto& info = infoFor(mutation.lane);
        if (!active(section, info.voice)) continue;
        if (mutation.lane == RhythmLane::Kick &&
            (section.rhythm.kickState == KickState::Muted ||
             suppressKickAt(section, localBar, 4.0 * mutation.step / std::max(1, stepsPerBar))))
            continue;
        const auto target = barStart + mutation.step * stepDuration;
        const auto nearTarget = [&](const NoteEvent& note) {
            return note.voice == info.voice && std::abs(note.startBeat - target) < stepDuration * 0.45;
        };
        auto found = std::find_if(pattern.notes.begin(), pattern.notes.end(), nearTarget);
        if (mutation.kind == RhythmMutationKind::Remove) {
            pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), nearTarget),
                                pattern.notes.end());
        } else if (mutation.kind == RhythmMutationKind::Add) {
            addHit(pattern, target, info.pitch, mutation.velocity, info.voice);
        } else if (mutation.kind == RhythmMutationKind::Shift && found != pattern.notes.end()) {
            found->startBeat = std::clamp(found->startBeat + mutation.amount * stepDuration,
                                          barStart, barStart + plan.beatsPerBar - 0.02);
        } else if (mutation.kind == RhythmMutationKind::Velocity && found != pattern.notes.end()) {
            found->velocity = mutation.velocity;
        } else if (mutation.kind == RhythmMutationKind::Ratchet) {
            if (found == pattern.notes.end()) addHit(pattern, target, info.pitch, mutation.velocity, info.voice);
            const auto count = std::clamp(mutation.amount, 2, 4);
            for (auto repeat = 1; repeat < count; ++repeat)
                addHit(pattern, target + repeat * stepDuration / count, info.pitch,
                       std::max(1, mutation.velocity - repeat * 5), info.voice, 0.035, true);
        }
    }
}

void applyPhraseWindow(Pattern& pattern, const BarDirection& direction,
                       double barStart, double beatsPerBar) {
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (!isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.startBeat < barStart || note.startBeat >= barStart + beatsPerBar)
            return false;
        const auto& instruction = direction.forVoice(note.voice);
        if (instruction.participation == Participation::Silent || instruction.maximumOnsets <= 0)
            return true;
        const auto beatInBar = note.startBeat - barStart;
        return beatInBar + 0.0001 < instruction.entryBeat ||
               beatInBar >= instruction.exitBeat - 0.0001;
    }), pattern.notes.end());
}

} // namespace

void RhythmEngine::renderChunk(Pattern& pattern, const SongPlan& plan, const SongSection& section,
                               const std::vector<BarDirection>& directions,
                               int sectionBar, int chunkBars) {
    const auto beatScale = plan.beatsPerBar / 4.0;
    for (auto bar = 0; bar < chunkBars; ++bar) {
        const auto localBar = sectionBar + bar;
        if (localBar < 0 || localBar >= section.bars) continue;
        const auto absoluteBar = section.startBar + localBar;
        const auto barStart = bar * plan.beatsPerBar;
        const auto expression = directions[static_cast<std::size_t>(localBar)]
                                    .forVoice(VoiceId::CoreDrums).expression;
        const auto weight = std::clamp(0.72 + expression * 0.34, 0.65, 1.08);
        const auto fill = gestureAt(section, localBar, RhythmGestureKind::PercussionFill) != nullptr;
        const auto fullKickMute = gestureAt(section, localBar, RhythmGestureKind::FullBarMute) != nullptr;

        if (const auto* motif = motifFor(plan, section);
            motif != nullptr && renderMotifBar(pattern, plan, section, *motif, localBar,
                                               absoluteBar, barStart, weight)) {
            applyMutations(pattern, plan, section, localBar, barStart, motif->stepsPerBar);
            if (!fullKickMute && active(section, VoiceId::CoreDrums)) {
                if (const auto* gesture = gestureAt(section, localBar, RhythmGestureKind::DoubleKick))
                    addHit(pattern, barStart + std::clamp(gesture->beat, 0.25, 3.95) * beatScale,
                           36, static_cast<int>((82 + gesture->intensity * 18.0) * weight),
                           VoiceId::CoreDrums, 0.055);
                if (gestureAt(section, localBar, RhythmGestureKind::PickupFill) != nullptr) {
                    addHit(pattern, barStart + 3.50 * beatScale, 36, static_cast<int>(88 * weight), VoiceId::CoreDrums, 0.055);
                    addHit(pattern, barStart + 3.75 * beatScale, 36, static_cast<int>(98 * weight), VoiceId::CoreDrums, 0.055);
                }
            }
            if (fill && active(section, VoiceId::LowPercussion)) {
                constexpr std::array pitches{64, 63, 62, 61};
                for (std::size_t index = 0; index < pitches.size(); ++index)
                    addHit(pattern, barStart + (3.0 + index * 0.25) * beatScale, pitches[index],
                           67 + static_cast<int>(index) * 7, VoiceId::LowPercussion, 0.065);
            }
            applyPhraseWindow(pattern, directions[static_cast<std::size_t>(localBar)],
                              barStart, plan.beatsPerBar);
            continue;
        }

        if (active(section, VoiceId::CoreDrums)) {
            auto ordinal = 0;
            for (const auto quarter : kickQuarterBeats(section.rhythm.kickState)) {
                if (suppressKickAt(section, localBar, quarter)) continue;
                const auto accent = quarter == 0.0 ? 8 : quarter == 2.0 ? 3 : 0;
                addHit(pattern, barStart + quarter * beatScale, 36,
                       static_cast<int>((96 + accent + human(plan.seed, absoluteBar, 0, ordinal++) * 3.0) * weight),
                       VoiceId::CoreDrums);
            }
            if (const auto* gesture = fullKickMute ? nullptr :
                    gestureAt(section, localBar, RhythmGestureKind::DoubleKick)) {
                const auto position = std::clamp(gesture->beat, 0.25, 3.95);
                addHit(pattern, barStart + position * beatScale, 36,
                       static_cast<int>((82 + gesture->intensity * 18.0) * weight), VoiceId::CoreDrums, 0.055);
            }
            if (!fullKickMute && gestureAt(section, localBar, RhythmGestureKind::PickupFill) != nullptr) {
                addHit(pattern, barStart + 3.50 * beatScale, 36, static_cast<int>(88 * weight), VoiceId::CoreDrums, 0.055);
                addHit(pattern, barStart + 3.75 * beatScale, 36, static_cast<int>(98 * weight), VoiceId::CoreDrums, 0.055);
            }
        }

        const auto drumsPresent = section.rhythm.kickState != KickState::Muted ||
                                  section.rhythm.percussionDensity > 0.28;
        if (drumsPresent && active(section, VoiceId::SnareClap)) {
            for (const auto quarter : {1.0, 3.0})
                if (!fill || quarter < 3.0)
                    addHit(pattern, barStart + quarter * beatScale, section.energy > 0.68 ? 39 : 38,
                           static_cast<int>((78 + section.energy * 24.0) * weight), VoiceId::SnareClap, 0.075);
        }

        if (drumsPresent && active(section, VoiceId::ClosedHats)) {
            const auto dense = section.rhythm.percussionDensity +
                               section.rhythm.syncopation * 0.12 > 0.68;
            const auto steps = dense ? 16 : 8;
            for (auto step = 0; step < steps; ++step) {
                if (fill && step >= steps - 2) continue;
                if (!dense && (step + absoluteBar) % 4 == 3) continue;
                if (dense && step % 2 == 1 && (step + absoluteBar * 3) % 7 == 0) continue;
                const auto quarter = 4.0 * step / steps;
                const auto swing = step % 2 == 1 ? section.rhythm.swing * 0.32 : 0.0;
                const auto offset = human(plan.seed, absoluteBar, 2, step) * 0.010 + swing;
                addHit(pattern, barStart + (quarter + offset) * beatScale, 42,
                       static_cast<int>((50 + (step % 4 == 2 ? 13 : 0) +
                                         human(plan.seed, absoluteBar, 2, step) * 6.0) * weight),
                       VoiceId::ClosedHats, 0.04);
            }
        }

        if (drumsPresent && active(section, VoiceId::OpenHatsShaker)) {
            if (section.rhythm.percussionDensity > 0.46)
                for (auto quarter = 0; quarter < 4; ++quarter)
                    if ((quarter + absoluteBar) % 3 != 1 || section.energy > 0.72)
                        addHit(pattern, barStart + (quarter + 0.5 + section.rhythm.swing * 0.18) * beatScale,
                               46, static_cast<int>((58 + section.energy * 18.0) * weight),
                               VoiceId::OpenHatsShaker, 0.16);
            if (section.rhythm.percussionDensity > 0.62)
                for (auto step = 1; step < 16; step += 2)
                    if ((step + absoluteBar) % 5 != 0)
                        addHit(pattern, barStart + (step * 0.25 + section.rhythm.swing * 0.24) * beatScale,
                               70, static_cast<int>((39 + (step % 4 == 3 ? 9 : 0)) * weight),
                               VoiceId::OpenHatsShaker, 0.035);
        }

        if (drumsPresent && active(section, VoiceId::LowPercussion) &&
            section.rhythm.percussionDensity > 0.38) {
            const std::array positions{
                1.0 + plan.rhythmLanguage.syncopation * 0.75,
                3.25 - plan.rhythmLanguage.callResponse * 0.75};
            const auto count = section.rhythm.percussionDensity > 0.72 ? 2 : 1;
            for (auto index = 0; index < count; ++index)
                addHit(pattern, barStart + (positions[static_cast<std::size_t>(index)] +
                       human(plan.seed, absoluteBar, 4, index) * 0.018) * beatScale,
                       index == 0 ? 64 : 62, static_cast<int>((53 + section.energy * 25.0) * weight),
                       VoiceId::LowPercussion, 0.10);
        }
        if (drumsPresent && active(section, VoiceId::HighPercussion) &&
            section.rhythm.percussionDensity > 0.32) {
            const std::array positions{0.75, 2.75};
            for (auto index = 0; index < 2; ++index) {
                if (index == 1 && section.rhythm.percussionDensity < 0.55) continue;
                addHit(pattern, barStart + (positions[static_cast<std::size_t>(index)] +
                       human(plan.seed, absoluteBar, 5, index) * 0.022) * beatScale,
                       evolvingLanePitch(RhythmLane::HighPercussion, absoluteBar,
                           index == 0 ? 0 : 8, 16, 75),
                       static_cast<int>((44 + section.energy * 22.0) * weight),
                       VoiceId::HighPercussion, 0.055);
            }
        }
        if (fill && active(section, VoiceId::LowPercussion)) {
            constexpr std::array pitches{64, 63, 62, 61};
            for (std::size_t index = 0; index < pitches.size(); ++index)
                addHit(pattern, barStart + (3.0 + index * 0.25) * beatScale, pitches[index],
                       67 + static_cast<int>(index) * 7, VoiceId::LowPercussion, 0.065);
        }
        applyPhraseWindow(pattern, directions[static_cast<std::size_t>(localBar)],
                          barStart, plan.beatsPerBar);
    }
}

void RhythmEngine::coordinateBassWithKick(Pattern& pattern, const SongSection& section,
                                          double beatsPerBar) {
    if (section.rhythm.kickState != KickState::FourOnFloor || beatsPerBar <= 0.0) return;
    const auto scale = beatsPerBar / 4.0;
    for (auto& note : pattern.notes) {
        if (note.voice != VoiceId::SubBass && note.voice != VoiceId::MovementBass) continue;
        const auto collision = std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& kick) {
            return kick.voice == VoiceId::CoreDrums && kick.pitch == 36 &&
                   std::abs(kick.startBeat - note.startBeat) < 0.045;
        });
        if (!collision) continue;
        const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
        const auto barEnd = (bar + 1) * beatsPerBar;
        const auto displacement = note.voice == VoiceId::SubBass ? 0.125 * scale : 0.50 * scale;
        note.startBeat = std::min(barEnd - 0.08, note.startBeat + displacement);
        note.durationBeats = std::min(note.durationBeats,
            (note.voice == VoiceId::SubBass ? 0.76 : 0.42) * scale);
    }
}

RhythmValidationReport RhythmEngine::enforceSemanticArticulations(Pattern& pattern,
                                                                  const SongPlan& plan) {
    RhythmValidationReport report;
    const auto latinLowLane = std::any_of(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& instrument) {
            return instrument.sourceVoice == VoiceId::LowPercussion &&
                   instrument.instrumentId == "latin_percussion";
        });
    constexpr std::array latinLowPitches{60, 61, 62, 63, 64, 65, 66};
    std::array<std::size_t, static_cast<std::size_t>(VoiceId::Count)> ordinals{};
    for (auto& note : pattern.notes) {
        if (!isVoiceInFamily(note.voice, VoiceFamily::Rhythm) && note.voice != VoiceId::Transitions) continue;
        auto& ordinal = ordinals[static_cast<std::size_t>(note.voice)];
        auto repaired = canonicalPercussionPitch(note.voice, note.pitch, ordinal++);
        // The AI may request a tom ensemble through its own orchestral assignment. A lane
        // explicitly realised as Latin percussion, however, must not silently accumulate
        // floor toms and become a different instrument family while retaining a conga role.
        if (latinLowLane && note.voice == VoiceId::LowPercussion &&
            !containsPitch(latinLowPitches, repaired))
            repaired = latinLowPitches[(ordinal - 1) % latinLowPitches.size()];
        if (repaired != note.pitch) { note.pitch = repaired; ++report.semanticPitchRepairs; }
    }
    constexpr std::array openPalette{46, 70, 58};
    constexpr std::array lowPalette{64, 62, 63, 61};
    constexpr std::array highPalette{75, 54, 56, 63, 51};
    report.articulationDiversifications += diversifyPercussion(
        pattern, VoiceId::OpenHatsShaker, openPalette, 16, 2);
    report.articulationDiversifications += diversifyPercussion(
        pattern, VoiceId::LowPercussion, lowPalette, 12, 2);
    report.articulationDiversifications += diversifyPercussion(
        pattern, VoiceId::HighPercussion, highPalette, 12, 3);
    return report;
}

RhythmValidationReport RhythmEngine::enforceContract(Pattern& pattern, const SongPlan& plan) {
    auto report = enforceSemanticArticulations(pattern, plan);
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (note.voice != VoiceId::CoreDrums || note.pitch != 36) return false;
        const auto bar = static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar));
        const auto* section = sectionForBar(plan, bar);
        if (section == nullptr || section->rhythm.kickState != KickState::Muted) return false;
        const auto localBar = bar - section->startBar;
        const auto pickup = gestureAt(*section, localBar, RhythmGestureKind::PickupFill) != nullptr;
        if (pickup && note.startBeat - bar * plan.beatsPerBar >= plan.beatsPerBar * 0.75) return false;
        ++report.forbiddenKicksRemoved;
        return true;
    }), pattern.notes.end());

    for (auto bar = 0; bar < plan.totalBars; ++bar) {
        const auto* section = sectionForBar(plan, bar);
        if (section == nullptr || section->rhythm.kickState != KickState::FourOnFloor ||
            section->rhythm.continuity != KickContinuity::Required) continue;
        const auto localBar = bar - section->startBar;
        const auto scale = plan.beatsPerBar / 4.0;
        for (const auto quarter : {0.0, 1.0, 2.0, 3.0}) {
            if (suppressKickAt(*section, localBar, quarter)) continue;
            const auto beat = bar * plan.beatsPerBar + quarter * scale;
            if (!kickNear(pattern, beat)) {
                addHit(pattern, beat, 36, quarter == 0.0 ? 106 : 98, VoiceId::CoreDrums);
                ++report.mandatoryKicksRestored;
            }
        }
    }

    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.voice != right.voice) return left.voice < right.voice;
        return left.pitch < right.pitch;
    });
    const auto before = pattern.notes.size();
    pattern.notes.erase(std::unique(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        return std::abs(left.startBeat - right.startBeat) < 0.012 && left.voice == right.voice &&
               left.pitch == right.pitch;
    }), pattern.notes.end());
    report.duplicateHitsRemoved = static_cast<int>(before - pattern.notes.size());
    return report;
}

} // namespace pulso
