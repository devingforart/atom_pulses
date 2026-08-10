#include "RhythmEngine.h"

#include "PhraseDirector.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>

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
            double duration = 0.07) {
    if (beat < 0.0 || beat >= pattern.lengthBeats) return;
    pattern.notes.push_back({beat, duration, pitch, std::clamp(velocity, 1, 127), 10, voice});
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
    LaneInfo{RhythmLane::LowPercussion, VoiceId::LowPercussion, 45, 58},
    LaneInfo{RhythmLane::HighPercussion, VoiceId::HighPercussion, 75, 51}};

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
            const auto micro = info.lane == RhythmLane::Kick || info.lane == RhythmLane::SnareClap
                ? 0.0 : human(plan.seed, absoluteBar, static_cast<int>(info.lane), step) * 0.010;
            const auto swing = step % 2 == 1 ? section.rhythm.swing * stepDuration * 0.32 : 0.0;
            const auto velocity = static_cast<int>((info.velocity + (symbol == '2' ? 13 : 0) +
                human(plan.seed, absoluteBar, static_cast<int>(info.lane), step) * 4.0) * weight);
            addHit(pattern, barStart + step * stepDuration + micro + swing, info.pitch, velocity,
                   info.voice, info.lane == RhythmLane::OpenHatsShaker ? 0.14 : 0.055);
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
                       std::max(1, mutation.velocity - repeat * 5), info.voice, 0.035);
        }
    }
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
                constexpr std::array pitches{45, 47, 50, 43};
                for (std::size_t index = 0; index < pitches.size(); ++index)
                    addHit(pattern, barStart + (3.0 + index * 0.25) * beatScale, pitches[index],
                           67 + static_cast<int>(index) * 7, VoiceId::LowPercussion, 0.065);
            }
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
            const std::array positions = plan.grooveFamily == GrooveFamily::OrganicProgressive
                ? std::array{1.25, 2.75} : section.rhythm.syncopation > 0.58
                    ? std::array{1.50, 2.75} : std::array{1.50, 3.25};
            const auto count = section.rhythm.percussionDensity > 0.72 ? 2 : 1;
            for (auto index = 0; index < count; ++index)
                addHit(pattern, barStart + (positions[static_cast<std::size_t>(index)] +
                       human(plan.seed, absoluteBar, 4, index) * 0.018) * beatScale,
                       index == 0 ? 45 : 43, static_cast<int>((53 + section.energy * 25.0) * weight),
                       VoiceId::LowPercussion, 0.10);
        }
        if (drumsPresent && active(section, VoiceId::HighPercussion) &&
            section.rhythm.percussionDensity > 0.32) {
            const std::array positions{0.75, 2.75};
            for (auto index = 0; index < 2; ++index) {
                if (index == 1 && section.rhythm.percussionDensity < 0.55) continue;
                addHit(pattern, barStart + (positions[static_cast<std::size_t>(index)] +
                       human(plan.seed, absoluteBar, 5, index) * 0.022) * beatScale,
                       index == 0 ? 75 : 76, static_cast<int>((44 + section.energy * 22.0) * weight),
                       VoiceId::HighPercussion, 0.055);
            }
        }
        if (fill && active(section, VoiceId::LowPercussion)) {
            constexpr std::array pitches{45, 47, 50, 43};
            for (std::size_t index = 0; index < pitches.size(); ++index)
                addHit(pattern, barStart + (3.0 + index * 0.25) * beatScale, pitches[index],
                       67 + static_cast<int>(index) * 7, VoiceId::LowPercussion, 0.065);
        }
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

RhythmValidationReport RhythmEngine::enforceContract(Pattern& pattern, const SongPlan& plan) {
    RhythmValidationReport report;
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
