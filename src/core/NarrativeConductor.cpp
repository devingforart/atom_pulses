#include "NarrativeConductor.h"

#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace pulso {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& text, const char* token) {
    return lower(text).find(token) != std::string::npos;
}

const InstrumentAssignment* assignmentFor(const SongPlan& plan, std::uint16_t partId) {
    if (partId == 0 || partId > plan.instruments.size()) return nullptr;
    return &plan.instruments[partId - 1];
}

const ConductorDecision* decisionFor(const SongPlan& plan, std::uint16_t partId) {
    const auto* assignment = assignmentFor(plan, partId);
    if (assignment == nullptr) return nullptr;
    const auto found = std::find_if(plan.conductor.decisions.begin(), plan.conductor.decisions.end(),
        [&](const auto& decision) { return decision.instrumentId == assignment->id; });
    return found == plan.conductor.decisions.end() ? nullptr : &*found;
}

bool isArpeggio(const Pattern& pattern, const SongPlan& plan, std::uint16_t partId) {
    const auto* assignment = assignmentFor(plan, partId);
    const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(),
        [&](const auto& item) { return item.id == partId; });
    const auto description = (assignment == nullptr ? std::string{} :
        assignment->role + " " + assignment->orchestralFunction + " " + assignment->name +
        " " + assignment->livePresetIntent) + (part == pattern.parts.end() ? std::string{} :
        " " + part->role + " " + part->orchestralFunction + " " + part->name);
    return contains(description, "arp") || contains(description, "ostinato") ||
           contains(description, "sequenc") || contains(description, "hypnotic pulse");
}

bool isFoundation(const SongPlan& plan, std::uint16_t partId) {
    const auto* assignment = assignmentFor(plan, partId);
    if (assignment == nullptr) return false;
    return assignment->sourceVoice == VoiceId::SubBass ||
           assignment->sourceVoice == VoiceId::MovementBass ||
           assignment->sourceVoice == VoiceId::HarmonicFoundation ||
           contains(assignment->orchestralFunction, "foundation") ||
           contains(assignment->role, "bass") || contains(assignment->role, "floor");
}

int partPriority(const Pattern& pattern, const SongPlan& plan, std::uint16_t partId) {
    auto score = 50;
    if (const auto* decision = decisionFor(plan, partId)) {
        score = std::clamp(decision->priority, 0, 100);
        if (decision->disposition == ConductorDisposition::Retire) score -= 35;
        if (decision->cadenceVoice) score += 15;
    }
    const auto* assignment = assignmentFor(plan, partId);
    if (assignment != nullptr) {
        if (assignment->id == plan.narrativeSpine.protagonistInstrumentId) score += 40;
        if (isFoundation(plan, partId)) score += 18;
        if (assignment->sourceVoice == VoiceId::Lead) score += 16;
        if (assignment->sourceVoice == VoiceId::Atmosphere) score -= 4;
        if (contains(assignment->orchestralFunction, "transition") ||
            contains(assignment->orchestralFunction, "color")) score -= 12;
        score += static_cast<int>(std::lround(assignment->prominence * 8.0));
    }
    if (isArpeggio(pattern, plan, partId)) score -= 18;
    const auto authored = std::count_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        return note.partId == partId &&
            (note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed);
    });
    if (authored > 0) score += 5;
    return score;
}

std::size_t peakParts(const Pattern& pattern) {
    std::size_t peak{};
    for (auto beat = 0.0; beat < pattern.lengthBeats; beat += 1.0) {
        std::set<std::uint16_t> active;
        for (const auto& note : pattern.notes)
            if (note.partId != 0 && note.startBeat < beat + 1.0 && note.endBeat() > beat)
                active.insert(note.partId);
        peak = std::max(peak, active.size());
    }
    return peak;
}

void applyArpeggioEconomy(Pattern& pattern, const SongPlan& plan,
                          NarrativeConductorReport& report) {
    std::set<std::uint16_t> arpParts;
    for (const auto& part : pattern.parts)
        if (isArpeggio(pattern, plan, part.id)) arpParts.insert(part.id);
    if (arpParts.empty() || pattern.notes.empty()) return;
    const auto initialArp = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [&](const auto& note) { return arpParts.contains(note.partId); });
    report.arpeggioShareBefore = static_cast<double>(initialArp) / pattern.notes.size();

    const auto bars = std::max(1, plan.totalBars);
    const auto maximumBars = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(
        bars * std::clamp(plan.conductor.maximumArpeggioActiveBarRatio, 0.10, 0.80))));
    std::vector<std::pair<double, int>> rankedBars;
    for (auto bar = 0; bar < bars; ++bar) {
        auto active = false;
        auto authored = false;
        for (const auto& note : pattern.notes) {
            if (!arpParts.contains(note.partId)) continue;
            if (static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar)) != bar) continue;
            active = true;
            authored = authored || note.origin == NoteOrigin::AiAuthored;
        }
        if (!active) continue;
        auto energy = 0.5;
        for (const auto& section : plan.sections)
            if (bar >= section.startBar && bar < section.startBar + section.bars) {
                energy = section.energy;
                break;
            }
        // Eight-bar breathing alternation is only a tie-breaker: authored material and
        // the structural energy curve remain more important than a mechanical pattern.
        rankedBars.emplace_back((authored ? 4.0 : 0.0) + energy +
            (((bar / 8) % 2) == 0 ? 0.16 : 0.0), bar);
    }
    std::stable_sort(rankedBars.begin(), rankedBars.end(), std::greater<>());
    std::set<int> keptBars;
    for (std::size_t index = 0; index < std::min(maximumBars, rankedBars.size()); ++index)
        keptBars.insert(rankedBars[index].second);

    const auto beforeBars = pattern.notes.size();
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (!arpParts.contains(note.partId) || note.origin == NoteOrigin::AiAuthored) return false;
        const auto bar = std::clamp(static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar)), 0, bars - 1);
        return !keptBars.contains(bar);
    }), pattern.notes.end());
    report.arpeggioNotesRemoved += beforeBars - pattern.notes.size();

    const auto nonArp = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [&](const auto& note) { return !arpParts.contains(note.partId); });
    const auto share = std::clamp(plan.conductor.maximumArpeggioNoteShare, 0.05, 0.35);
    const auto maximumArp = static_cast<std::size_t>(std::floor(
        share * static_cast<double>(nonArp) / std::max(0.01, 1.0 - share)));
    std::vector<std::size_t> removable;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (arpParts.contains(note.partId) && note.origin != NoteOrigin::AiAuthored)
            removable.push_back(index);
    }
    const auto currentArp = static_cast<std::size_t>(std::count_if(
        pattern.notes.begin(), pattern.notes.end(),
        [&](const auto& note) { return arpParts.contains(note.partId); }));
    if (currentArp > maximumArp) {
        std::stable_sort(removable.begin(), removable.end(), [&](auto left, auto right) {
            const auto rank = [](NoteOrigin origin) {
                if (origin == NoteOrigin::Procedural) return 0;
                if (origin == NoteOrigin::PlanDerived) return 1;
                if (origin == NoteOrigin::LocalContinuity || origin == NoteOrigin::LocalRepair) return 2;
                return 3;
            };
            return rank(pattern.notes[left].origin) < rank(pattern.notes[right].origin);
        });
        const auto removeCount = std::min(currentArp - maximumArp, removable.size());
        std::set<std::size_t> remove(removable.begin(), removable.begin() +
            static_cast<std::ptrdiff_t>(removeCount));
        std::vector<NoteEvent> retained;
        retained.reserve(pattern.notes.size() - remove.size());
        for (std::size_t index = 0; index < pattern.notes.size(); ++index)
            if (!remove.contains(index)) retained.push_back(pattern.notes[index]);
        pattern.notes = std::move(retained);
        report.arpeggioNotesRemoved += removeCount;
    }
    const auto finalArp = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [&](const auto& note) { return arpParts.contains(note.partId); });
    report.arpeggioShareAfter = pattern.notes.empty() ? 0.0 :
        static_cast<double>(finalArp) / pattern.notes.size();
}

void applyDensityEconomy(Pattern& pattern, const SongPlan& plan,
                         NarrativeConductorReport& report) {
    const auto bars = std::max(1, plan.totalBars);
    const auto maximum = std::clamp<std::size_t>(plan.conductor.maximumSimultaneousParts, 4, 12);
    std::set<std::pair<int, std::uint16_t>> silenced;
    for (auto bar = 0; bar < bars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        const auto end = start + plan.beatsPerBar;
        std::set<std::uint16_t> active;
        for (const auto& note : pattern.notes)
            if (note.partId != 0 && note.startBeat < end && note.endBeat() > start)
                active.insert(note.partId);
        auto sectionIndex = -1;
        for (std::size_t index = 0; index < plan.sections.size(); ++index)
            if (bar >= plan.sections[index].startBar &&
                bar < plan.sections[index].startBar + plan.sections[index].bars) {
                sectionIndex = static_cast<int>(index);
                break;
            }
        for (const auto partId : active) {
            const auto* decision = decisionFor(plan, partId);
            if (decision == nullptr) continue;
            const auto cadenceWindow = decision->cadenceVoice &&
                bar >= bars - std::max(1, plan.conductor.cadenceBars);
            const auto outsideEditorialForm = !decision->activeSectionIndices.empty() &&
                std::find(decision->activeSectionIndices.begin(), decision->activeSectionIndices.end(),
                          sectionIndex) == decision->activeSectionIndices.end();
            if (decision->disposition == ConductorDisposition::Retire ||
                (outsideEditorialForm && !cadenceWindow))
                silenced.emplace(bar, partId);
        }
        for (auto it = active.begin(); it != active.end();) {
            if (silenced.contains({bar, *it})) it = active.erase(it);
            else ++it;
        }
        if (active.size() <= maximum) continue;
        std::vector<std::uint16_t> ranked(active.begin(), active.end());
        std::stable_sort(ranked.begin(), ranked.end(), [&](auto left, auto right) {
            const auto lp = partPriority(pattern, plan, left);
            const auto rp = partPriority(pattern, plan, right);
            if (lp != rp) return lp > rp;
            // Rotate equal-priority support instead of always sacrificing one color.
            return ((left + static_cast<std::uint16_t>(bar / 4)) % 17) <
                   ((right + static_cast<std::uint16_t>(bar / 4)) % 17);
        });
        for (std::size_t index = maximum; index < ranked.size(); ++index)
            silenced.emplace(bar, ranked[index]);
    }
    if (silenced.empty()) return;
    std::vector<NoteEvent> retained;
    retained.reserve(pattern.notes.size());
    for (auto note : pattern.notes) {
        const auto firstBar = std::clamp(static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar)), 0, bars - 1);
        const auto lastBar = std::clamp(static_cast<int>(std::floor(
            std::max(note.startBeat, note.endBeat() - 0.001) / plan.beatsPerBar)), 0, bars - 1);
        if (silenced.contains({firstBar, note.partId})) {
            ++report.densityNotesRemoved;
            continue;
        }
        for (auto bar = firstBar + 1; bar <= lastBar; ++bar) {
            if (!silenced.contains({bar, note.partId})) continue;
            note.durationBeats = std::max(0.05, bar * plan.beatsPerBar - note.startBeat - 0.01);
            break;
        }
        retained.push_back(note);
    }
    pattern.notes = std::move(retained);
}

void repairMelodicLeaps(Pattern& pattern, const SongPlan& plan,
                        NarrativeConductorReport& report) {
    const auto maximumLeap = std::clamp(plan.conductor.maximumMelodicLeap, 3, 9);
    std::map<std::uint16_t, std::vector<NoteEvent*>> byPart;
    for (auto& note : pattern.notes) {
        const auto* assignment = assignmentFor(plan, note.partId);
        if (assignment == nullptr) continue;
        if (assignment->sourceVoice == VoiceId::Lead ||
            assignment->sourceVoice == VoiceId::Countermelody ||
            assignment->sourceVoice == VoiceId::HarmonicUpper)
            byPart[note.partId].push_back(&note);
    }
    for (auto& [partId, notes] : byPart) {
        std::stable_sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            if (left->startBeat != right->startBeat) return left->startBeat < right->startBeat;
            return left->pitch < right->pitch;
        });
        const auto* assignment = assignmentFor(plan, partId);
        for (std::size_t index = 1; index < notes.size(); ++index) {
            auto* previous = notes[index - 1];
            auto* current = notes[index];
            if (std::abs(current->startBeat - previous->startBeat) < 0.01 ||
                current->startBeat - previous->endBeat() > plan.beatsPerBar * 0.75) continue;
            if (std::abs(current->pitch - previous->pitch) <= maximumLeap) continue;
            auto best = current->pitch;
            auto bestCost = 100000;
            for (auto candidate = std::max(0, assignment->minimumPitch);
                 candidate <= std::min(127, assignment->maximumPitch); ++candidate) {
                if (!isPitchClassInScale(candidate % 12, plan.rootPitchClass, plan.scale)) continue;
                const auto distance = std::abs(candidate - previous->pitch);
                if (distance > maximumLeap) continue;
                const auto cost = std::abs(candidate - current->pitch) * 2 + distance;
                if (cost < bestCost) { bestCost = cost; best = candidate; }
            }
            if (best == current->pitch) continue;
            current->pitch = best;
            if (current->origin != NoteOrigin::AiAuthored) current->origin = NoteOrigin::AiTransformed;
            ++report.melodicLeapsRevoiced;
        }
    }
}

void resolveCadence(Pattern& pattern, const SongPlan& plan,
                    NarrativeConductorReport& report) {
    const auto cadenceStart = std::max(0.0, pattern.lengthBeats -
        std::max(4, plan.conductor.cadenceBars) * plan.beatsPerBar);
    std::uint16_t protagonist{};
    for (std::size_t index = 0; index < plan.instruments.size(); ++index)
        if (plan.instruments[index].id == plan.narrativeSpine.protagonistInstrumentId)
            protagonist = static_cast<std::uint16_t>(index + 1);
    if (protagonist == 0)
        for (std::size_t index = 0; index < plan.instruments.size(); ++index)
            if (plan.instruments[index].sourceVoice == VoiceId::Lead) {
                protagonist = static_cast<std::uint16_t>(index + 1); break;
            }
    std::uint16_t bass{};
    auto lowest = 128;
    for (const auto& note : pattern.notes) {
        if (note.startBeat < cadenceStart || !isFoundation(plan, note.partId)) continue;
        if (note.pitch < lowest) { lowest = note.pitch; bass = note.partId; }
    }
    auto closePart = [&](std::uint16_t partId) {
        if (partId == 0) return false;
        auto found = pattern.notes.end();
        for (auto it = pattern.notes.begin(); it != pattern.notes.end(); ++it)
            if (it->partId == partId && it->startBeat >= cadenceStart &&
                (found == pattern.notes.end() || it->endBeat() > found->endBeat())) found = it;
        if (found == pattern.notes.end()) return false;
        const auto* assignment = assignmentFor(plan, partId);
        const auto target = pitchClassToMidi(plan.rootPitchClass, found->pitch / 12 - 1,
            assignment == nullptr ? 0 : assignment->minimumPitch,
            assignment == nullptr ? 127 : assignment->maximumPitch);
        if (found->pitch != target) {
            found->pitch = target;
            found->origin = NoteOrigin::AiTransformed;
            ++report.cadenceNotesRevoiced;
        }
        return found->pitch % 12 == plan.rootPitchClass;
    };
    const auto bassResolved = closePart(bass);
    const auto protagonistResolved = closePart(protagonist);
    report.cadenceResolved = bassResolved && protagonistResolved;
}

} // namespace

NarrativeConductorReport NarrativeConductor::enforce(Pattern& pattern, const SongPlan& plan) {
    NarrativeConductorReport report;
    report.peakPartsBefore = peakParts(pattern);
    if (!plan.conductor.enabled) {
        report.peakPartsAfter = report.peakPartsBefore;
        return report;
    }
    applyArpeggioEconomy(pattern, plan, report);
    applyDensityEconomy(pattern, plan, report);
    repairMelodicLeaps(pattern, plan, report);
    resolveCadence(pattern, plan, report);
    std::stable_sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    report.peakPartsAfter = peakParts(pattern);
    return report;
}

} // namespace pulso
