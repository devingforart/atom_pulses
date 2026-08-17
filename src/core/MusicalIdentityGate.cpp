#include "MusicalIdentityGate.h"

#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <vector>

namespace pulso {
namespace {

bool electronic(const SongPlan& plan) noexcept {
    return plan.productionLanguage.electronicIntent >= 0.58 &&
           (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
            plan.productionLanguage.domain == ProductionDomain::Hybrid);
}

const InstrumentPart* partFor(const Pattern& pattern, const NoteEvent& note) noexcept {
    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
        return part.id == note.partId;
    });
    return found == pattern.parts.end() ? nullptr : &*found;
}

bool transitionNote(const Pattern& pattern, const NoteEvent& note) noexcept {
    const auto* part = partFor(pattern, note);
    return note.voice == VoiceId::Transitions ||
           (part != nullptr && part->department == ScoreDepartment::Rhythm &&
            part->orchestralFunction == "transition");
}

bool stableGrooveNote(const Pattern& pattern, const NoteEvent& note) noexcept {
    if (note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed)
        return false;
    const auto* part = partFor(pattern, note);
    if (part == nullptr || part->department != ScoreDepartment::Rhythm ||
        part->orchestralFunction == "transition") return false;
    return note.voice == VoiceId::SnareClap || note.voice == VoiceId::ClosedHats ||
           note.voice == VoiceId::OpenHatsShaker || note.voice == VoiceId::LowPercussion ||
           note.voice == VoiceId::HighPercussion;
}

std::vector<double> structuralBoundaries(const SongPlan& plan) {
    std::vector<double> result;
    for (const auto& section : plan.sections) {
        if (section.startBar > 0) result.push_back(section.startBar * plan.beatsPerBar);
        result.push_back((section.startBar + section.bars) * plan.beatsPerBar);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end(), [](double left, double right) {
        return std::abs(left - right) < 0.001;
    }), result.end());
    return result;
}

std::size_t restrainTransitions(Pattern& pattern, const SongPlan& plan,
                                MusicalIdentityReport& report) {
    const auto boundaries = structuralBoundaries(plan);
    using Group = std::pair<std::uint16_t, long long>;
    std::map<Group, std::vector<std::size_t>> candidates;
    std::vector<bool> remove(pattern.notes.size(), false);
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (!transitionNote(pattern, note)) continue;
        ++report.transitionNotesBefore;
        auto selected = boundaries.end();
        auto bestDistance = 1.0e9;
        for (auto boundary = boundaries.begin(); boundary != boundaries.end(); ++boundary) {
            // A reverse may begin one bar before an arrival; an impact may land just after it.
            if (note.startBeat < *boundary - plan.beatsPerBar - 0.001 ||
                note.startBeat > *boundary + 0.26) continue;
            const auto distance = std::abs(note.startBeat - *boundary);
            if (distance < bestDistance) { bestDistance = distance; selected = boundary; }
        }
        if (selected == boundaries.end()) {
            remove[index] = true;
            continue;
        }
        candidates[{note.partId, std::llround(*selected * 960.0)}].push_back(index);
    }

    constexpr std::array transitionPitches{49, 52, 55, 57};
    for (auto& [group, indices] : candidates) {
        (void) group;
        ++report.transitionBoundaries;
        std::sort(indices.begin(), indices.end(), [&](auto left, auto right) {
            const auto& a = pattern.notes[left];
            const auto& b = pattern.notes[right];
            const auto aScore = a.durationBeats * 12.0 + a.velocity / 127.0;
            const auto bScore = b.durationBeats * 12.0 + b.velocity / 127.0;
            return aScore > bScore;
        });
        for (std::size_t ordinal = 0; ordinal < indices.size(); ++ordinal) {
            if (ordinal >= 2) { remove[indices[ordinal]] = true; continue; }
            auto& note = pattern.notes[indices[ordinal]];
            if (std::find(transitionPitches.begin(), transitionPitches.end(), note.pitch) ==
                transitionPitches.end())
                note.pitch = transitionPitches[ordinal % transitionPitches.size()];
            note.durationBeats = std::max(note.durationBeats, ordinal == 0 ? 0.50 : 0.25);
        }
    }
    std::vector<NoteEvent> retained;
    retained.reserve(pattern.notes.size());
    for (std::size_t index = 0; index < pattern.notes.size(); ++index)
        if (!remove[index]) retained.push_back(pattern.notes[index]);
    pattern.notes = std::move(retained);
    report.transitionNotesAfter = std::count_if(pattern.notes.begin(), pattern.notes.end(),
        [&](const auto& note) { return transitionNote(pattern, note); });
    report.transitionNotesRemoved = report.transitionNotesBefore - report.transitionNotesAfter;
    return report.transitionNotesRemoved;
}

void stabilizeGroove(Pattern& pattern, const SongPlan& plan, MusicalIdentityReport& report) {
    const auto phraseBeats = plan.beatsPerBar * 8.0;
    const auto stableBeats = plan.beatsPerBar * 6.0;
    for (const auto& section : plan.sections) {
        if (section.bars < 16 || section.energy < 0.30) continue;
        for (auto phrase = 8; phrase + 6 <= section.bars; phrase += 16) {
            const auto sourceStart = (section.startBar + phrase - 8) * plan.beatsPerBar;
            const auto targetStart = (section.startBar + phrase) * plan.beatsPerBar;
            std::set<std::uint16_t> parts;
            for (const auto& note : pattern.notes)
                if (stableGrooveNote(pattern, note) && note.startBeat >= sourceStart &&
                    note.startBeat < sourceStart + stableBeats) parts.insert(note.partId);
            for (const auto partId : parts) {
                std::vector<NoteEvent> source;
                std::vector<NoteEvent> target;
                for (const auto& note : pattern.notes) {
                    if (note.partId != partId || !stableGrooveNote(pattern, note)) continue;
                    if (note.startBeat >= sourceStart && note.startBeat < sourceStart + stableBeats)
                        source.push_back(note);
                    if (note.startBeat >= targetStart && note.startBeat < targetStart + stableBeats)
                        target.push_back(note);
                }
                if (source.size() < 3 || target.size() < 3) continue;
                ++report.groovePhrasePairs;
                const auto sourceMean = std::accumulate(source.begin(), source.end(), 0.0,
                    [](double sum, const auto& note) { return sum + note.velocity; }) / source.size();
                const auto targetMean = std::accumulate(target.begin(), target.end(), 0.0,
                    [](double sum, const auto& note) { return sum + note.velocity; }) / target.size();
                const auto velocityDelta = std::clamp(targetMean - sourceMean, -8.0, 8.0);
                const auto before = pattern.notes.size();
                pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                    [&](const auto& note) {
                        return note.partId == partId && stableGrooveNote(pattern, note) &&
                               note.startBeat >= targetStart && note.startBeat < targetStart + stableBeats;
                    }), pattern.notes.end());
                report.grooveNotesReplaced += before - pattern.notes.size();
                for (auto note : source) {
                    note.startBeat += phraseBeats;
                    note.velocity = std::clamp(static_cast<int>(std::lround(
                        note.velocity + velocityDelta)), 1, 127);
                    pattern.notes.push_back(note);
                }
                ++report.grooveRecallPhrases;
            }
        }
    }
    report.grooveRecallRatio = report.groovePhrasePairs == 0 ? 1.0 :
        static_cast<double>(report.grooveRecallPhrases) / report.groovePhrasePairs;
}

void developGroovePhraseEndings(Pattern& pattern, const SongPlan& plan,
                                MusicalIdentityReport& report) {
    if (plan.beatsPerBar <= 0.0) return;
    constexpr std::array snare{37, 38, 39, 40};
    constexpr std::array hats{42, 44};
    constexpr std::array tops{46, 58, 70};
    constexpr std::array low{61, 62, 63, 64, 65, 66};
    constexpr std::array high{51, 54, 56, 63, 75};
    const auto windowBeats = plan.beatsPerBar * 8.0;
    const auto developmentStart = plan.beatsPerBar * 6.0;
    std::map<std::pair<std::uint16_t, int>, std::vector<std::size_t>> candidates;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (!stableGrooveNote(pattern, note)) continue;
        const auto window = static_cast<int>(std::floor(note.startBeat / windowBeats));
        const auto local = note.startBeat - window * windowBeats;
        if (local >= developmentStart && local < windowBeats)
            candidates[{note.partId, window}].push_back(index);
    }
    for (auto& [key, indices] : candidates) {
        if (indices.empty()) continue;
        const auto selectedOrdinal = static_cast<std::size_t>(positiveModulo(
            key.second * 7 + static_cast<int>(key.first) * 3, static_cast<int>(indices.size())));
        auto& note = pattern.notes[indices[selectedOrdinal]];
        const auto mutate = [&](const auto& palette) {
            auto target = palette[static_cast<std::size_t>(positiveModulo(
                key.second + static_cast<int>(key.first) + static_cast<int>(selectedOrdinal),
                static_cast<int>(palette.size())))];
            if (target == note.pitch && palette.size() > 1)
                target = palette[(static_cast<std::size_t>(positiveModulo(
                    key.second + static_cast<int>(key.first), static_cast<int>(palette.size()))) + 1) %
                    palette.size()];
            if (target != note.pitch) { note.pitch = target; ++report.grooveDevelopmentNotes; }
        };
        if (note.voice == VoiceId::SnareClap) mutate(snare);
        else if (note.voice == VoiceId::ClosedHats) mutate(hats);
        else if (note.voice == VoiceId::OpenHatsShaker) mutate(tops);
        else if (note.voice == VoiceId::LowPercussion) mutate(low);
        else if (note.voice == VoiceId::HighPercussion) mutate(high);
        ++report.groovePhraseDevelopments;
    }
}

int nearestScalePitch(int target, int minimum, int maximum, int root, ScaleKind scale) {
    auto result = std::clamp(target, minimum, maximum);
    auto best = 1000;
    for (auto pitch = minimum; pitch <= maximum; ++pitch) {
        if (!isPitchClassInScale(pitch, root, scale)) continue;
        const auto distance = std::abs(pitch - target);
        if (distance < best) { best = distance; result = pitch; }
    }
    return result;
}

void bindResponses(Pattern& pattern, const SongPlan& plan, MusicalIdentityReport& report) {
    std::vector<NoteEvent> lead;
    for (const auto& note : pattern.notes)
        if (note.voice == VoiceId::Lead) lead.push_back(note);
    std::sort(lead.begin(), lead.end(), [](const auto& left, const auto& right) {
        return left.startBeat < right.startBeat;
    });
    if (lead.size() < 4) return;
    const auto anchor = lead.front().pitch;
    std::array<int, 4> intervals{};
    for (std::size_t index = 0; index < intervals.size(); ++index)
        intervals[index] = lead[index].pitch - anchor;

    const auto windowBeats = plan.beatsPerBar * 8.0;
    std::set<std::uint16_t> linkedParts;
    auto windowIndex = std::uint64_t{};
    for (double start = 0.0; start < pattern.lengthBeats;
         start += windowBeats, ++windowIndex) {
        std::map<std::uint16_t, std::vector<std::size_t>> responsesByPart;
        for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
            const auto& note = pattern.notes[index];
            if (note.voice == VoiceId::Countermelody &&
                note.origin != NoteOrigin::AiAuthored && note.origin != NoteOrigin::AiTransformed &&
                note.startBeat >= start &&
                note.startBeat < start + windowBeats)
                responsesByPart[note.partId].push_back(index);
        }
        for (auto& [partId, response] : responsesByPart) {
            if (response.size() < 3) continue;
            ++report.responsePhrases;
            linkedParts.insert(partId);
            std::sort(response.begin(), response.end(), [&](auto left, auto right) {
                return pattern.notes[left].startBeat < pattern.notes[right].startBeat;
            });
            const auto first = pattern.notes[response.front()];
            const auto* part = partFor(pattern, first);
            const auto minimum = part == nullptr ? voiceDefinition(VoiceId::Countermelody).minimumPitch
                                                  : part->minimumPitch;
            const auto maximum = part == nullptr ? voiceDefinition(VoiceId::Countermelody).maximumPitch
                                                  : part->maximumPitch;
            const auto variant = (plan.seed + partId * 17ULL + windowIndex * 13ULL) % 4ULL;
            const auto count = std::min(response.size(), intervals.size());
            for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
                auto transformed = intervals[ordinal];
                if (variant == 1) transformed = -intervals[ordinal];
                else if (variant == 2)
                    transformed = intervals[intervals.size() - 1 - ordinal] - intervals.back();
                else if (variant == 3)
                    transformed = -(intervals[intervals.size() - 1 - ordinal] - intervals.back());
                auto& note = pattern.notes[response[ordinal]];
                const auto target = nearestScalePitch(first.pitch + transformed, minimum, maximum,
                                                       plan.rootPitchClass, plan.scale);
                if (target != note.pitch) { note.pitch = target; ++report.responseNotesRetuned; }
            }
            ++report.derivedResponsePhrases;
        }
    }
    report.responseParts = linkedParts.size();
    report.responseLineageRatio = report.responsePhrases == 0 ? 1.0 :
        static_cast<double>(report.derivedResponsePhrases) / report.responsePhrases;
}

void authorPercussionDurations(Pattern& pattern, MusicalIdentityReport& report) {
    for (auto& note : pattern.notes) {
        const auto* part = partFor(pattern, note);
        const auto rhythm = isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.voice == VoiceId::Transitions ||
            (part != nullptr && part->department == ScoreDepartment::Rhythm);
        if (!rhythm) continue;
        const auto floor = transitionNote(pattern, note) ? 0.25 : 0.125;
        if (note.durationBeats + 0.000001 < floor) {
            note.durationBeats = floor;
            ++report.percussionDurationsAuthored;
        }
    }
}

} // namespace

MusicalIdentityReport MusicalIdentityGate::enforce(Pattern& pattern, const SongPlan& plan) {
    MusicalIdentityReport report;
    report.active = electronic(plan);
    if (!report.active || pattern.notes.empty() || plan.beatsPerBar <= 0.0) return report;
    restrainTransitions(pattern, plan, report);
    stabilizeGroove(pattern, plan, report);
    developGroovePhraseEndings(pattern, plan, report);
    bindResponses(pattern, plan, report);
    authorPercussionDurations(pattern, report);
    report.transitionRestraint = report.transitionNotesAfter == 0 ? 1.0 :
        std::clamp(static_cast<double>(report.transitionBoundaries * 2) /
            report.transitionNotesAfter, 0.0, 1.0);
    report.score = std::clamp(report.grooveRecallRatio * 0.36 +
                              report.responseLineageRatio * 0.36 +
                              report.transitionRestraint * 0.28, 0.0, 1.0);
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    return report;
}

} // namespace pulso
