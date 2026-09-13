#include "AttentionDirector.h"

#include "HarmonyPlan.h"
#include "Scale.h"
#include "SongComposer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pulso {
namespace {

bool electronic(const SongPlan& plan) noexcept {
    return plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool contains(std::string_view text, std::string_view token) noexcept {
    return text.find(token) != std::string_view::npos;
}

bool foundation(const InstrumentPart& part) {
    if (part.department != ScoreDepartment::Harmony) return false;
    const auto identity = lower(part.catalogId + " " + part.name + " " + part.role + " " +
                                part.orchestralFunction);
    return part.sourceVoice == VoiceId::HarmonicFoundation ||
        part.orchestralFunction == "foundation" || part.orchestralFunction == "body" ||
        contains(identity, "pad") || contains(identity, "drone") ||
        contains(identity, "chord body") || contains(identity, "harmonic floor");
}

bool contractedFloor(const InstrumentPart& part, const SongPlan& plan) {
    // Synthetic/legacy plans may not carry the assignment table; retain the Pattern
    // role as a compatibility fallback. Production AI plans always take the exact
    // ordered assignment path below.
    if (part.id == 0 || part.id > plan.instruments.size())
        return part.sourceVoice != VoiceId::SubBass &&
            part.sourceVoice != VoiceId::MovementBass && foundation(part);
    const auto& assignment = plan.instruments[part.id - 1];
    if (assignment.sourceVoice != VoiceId::HarmonicFoundation &&
        assignment.sourceVoice != VoiceId::HarmonicUpper &&
        assignment.sourceVoice != VoiceId::Atmosphere) return false;
    const auto role = lower(assignment.role);
    return assignment.orchestralFunction == "foundation" ||
        assignment.orchestralFunction == "body" ||
        assignment.instrumentId == "analog_pad" || assignment.instrumentId == "granular_pad" ||
        assignment.instrumentId == "spectral_drone" || contains(role, "pad") ||
        contains(role, "chord body");
}

bool foreground(const InstrumentPart& part) noexcept {
    return part.department == ScoreDepartment::Melody || part.sourceVoice == VoiceId::Lead ||
           part.sourceVoice == VoiceId::Countermelody;
}

bool lowEnd(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::SubBass || part.sourceVoice == VoiceId::MovementBass ||
           part.soundModel == InstrumentSoundModel::SubSynth ||
           part.soundModel == InstrumentSoundModel::ElectricBass;
}

bool corePulse(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::CoreDrums || part.soundModel == InstrumentSoundModel::Kick;
}

bool texture(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::Atmosphere || part.sourceVoice == VoiceId::Transitions ||
           part.soundModel == InstrumentSoundModel::Texture;
}

bool phraseVoice(const InstrumentPart& part) {
    const auto identity = lower(part.catalogId + " " + part.name + " " + part.role);
    return foreground(part) || part.sourceVoice == VoiceId::HarmonicPulse ||
           contains(identity, "arp") || contains(identity, "sequence") ||
           contains(identity, "pulse");
}

bool eventPart(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::Transitions || part.orchestralFunction == "transition" ||
           part.soundModel == InstrumentSoundModel::Cymbal;
}

const SongSection* sectionAt(const SongPlan& plan, double beat) noexcept {
    const SongSection* result = plan.sections.empty() ? nullptr : &plan.sections.front();
    for (const auto& section : plan.sections) {
        if (beat + .001 < section.startBar * plan.beatsPerBar) break;
        result = &section;
    }
    return result;
}

bool requiredCorePulseAt(const SongPlan& plan, double beat) noexcept {
    const auto* section = sectionAt(plan, beat);
    return section != nullptr && section->rhythm.continuity == KickContinuity::Required &&
           section->rhythm.kickState == KickState::FourOnFloor;
}

const HarmonicChord& chordAt(const SongPlan& plan, double beat) {
    static const HarmonicChord fallback{"attention_tonic", "Tonic", 0, 0, {0, 3, 7}};
    if (plan.chordPalette.empty()) return fallback;
    const auto* section = sectionAt(plan, beat);
    if (section == nullptr || section->harmonicEvents.empty()) return plan.chordPalette.front();
    const auto localBeat = beat - section->startBar * plan.beatsPerBar;
    const HarmonicEvent* event = &section->harmonicEvents.front();
    for (const auto& candidate : section->harmonicEvents) {
        const auto onset = candidate.barOffset * plan.beatsPerBar + candidate.beatOffset;
        if (onset <= localBeat + .001) event = &candidate;
    }
    const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
        [&](const auto& candidate) { return candidate.id == event->chordId; });
    return chord == plan.chordPalette.end() ? plan.chordPalette.front() : *chord;
}

int nearestPitch(int pitchClass, int target, const InstrumentPart& part) noexcept {
    auto result = std::clamp(target, part.minimumPitch, part.maximumPitch);
    auto distance = 1000;
    for (auto pitch = part.minimumPitch; pitch <= part.maximumPitch; ++pitch) {
        if (positiveModulo(pitch, 12) != positiveModulo(pitchClass, 12)) continue;
        const auto candidate = std::abs(pitch - target);
        if (candidate < distance) {
            result = pitch;
            distance = candidate;
        }
    }
    return result;
}

const InstrumentPart* partFor(const Pattern& pattern, std::uint16_t id) noexcept {
    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(),
        [&](const auto& part) { return part.id == id; });
    return found == pattern.parts.end() ? nullptr : &*found;
}

std::set<std::uint16_t> activeParts(const Pattern& pattern, double start, double end) {
    std::set<std::uint16_t> result;
    for (const auto& note : pattern.notes)
        if (note.partId != 0 && note.startBeat < end - .001 && note.endBeat() > start + .001)
            result.insert(note.partId);
    return result;
}

std::size_t activeFloorLayers(const Pattern& pattern, const SongPlan& plan,
                              double start, double end) {
    std::set<std::uint16_t> result;
    for (const auto& note : pattern.notes) {
        if (note.partId == 0 || note.startBeat >= end - .001 || note.endBeat() <= start + .001) continue;
        const auto* part = partFor(pattern, note.partId);
        if (part != nullptr && contractedFloor(*part, plan)) result.insert(note.partId);
    }
    return result.size();
}

std::size_t partBudget(const SongPlan& plan, double beat) noexcept {
    const auto* section = sectionAt(plan, beat);
    const auto energy = section == nullptr ? .5 : section->energy;
    const auto density = section == nullptr ? .5 : section->density;
    const auto combined = energy * .62 + density * .38;
    if (combined < .28) return 5;
    if (combined < .50) return 6;
    if (combined < .70) return 7;
    if (combined < .86) return 8;
    return 9;
}

double priority(const InstrumentPart& part, std::size_t window,
                std::size_t previousSelections) noexcept {
    auto score = part.prominence * 4.0;
    if (foundation(part)) score += 3.0;
    if (foreground(part)) score += 2.4;
    if (corePulse(part)) score += 2.2;
    if (lowEnd(part)) score += 1.8;
    if (texture(part)) score += .45;
    if (eventPart(part)) score -= .35;
    const auto rotation = static_cast<double>((part.id * 37U + window * 17U) % 101U) / 100.0;
    score += rotation * .85 + 1.1 / static_cast<double>(previousSelections + 1);
    return score;
}

std::set<int> structuralBreathBars(const SongPlan& plan) {
    std::set<int> result;
    auto previous = -32;
    for (std::size_t index = 1; index < plan.sections.size(); ++index) {
        const auto& before = plan.sections[index - 1];
        const auto& after = plan.sections[index];
        const auto boundary = after.startBar - 1;
        const auto contrast = std::abs(after.energy - before.energy) >= .12 ||
                              std::abs(after.tension - before.tension) >= .16;
        if (boundary >= 3 && boundary - previous >= 16 && (contrast || index % 2 == 0)) {
            result.insert(boundary);
            previous = boundary;
        }
    }
    return result;
}

struct ActivitySummary {
    std::size_t overcrowded{};
    std::size_t peak{};
    double average{};
};

ActivitySummary activity(const Pattern& pattern, const SongPlan& plan) {
    ActivitySummary result;
    const auto bars = std::max(1, static_cast<int>(std::ceil(
        pattern.lengthBeats / std::max(1.0, plan.beatsPerBar))));
    auto total = std::size_t{};
    for (auto bar = 0; bar < bars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        const auto count = activeParts(pattern, start, start + plan.beatsPerBar).size();
        total += count;
        result.peak = std::max(result.peak, count);
        if (count > partBudget(plan, start)) ++result.overcrowded;
    }
    result.average = static_cast<double>(total) / static_cast<double>(bars);
    return result;
}

double floorCoverage(const Pattern& pattern, const SongPlan& plan) {
    const auto bars = std::max(1, static_cast<int>(std::ceil(
        pattern.lengthBeats / std::max(1.0, plan.beatsPerBar))));
    auto covered = 0;
    for (auto bar = 0; bar < bars; ++bar) {
        const auto start = bar * plan.beatsPerBar;
        if (activeFloorLayers(pattern, plan, start, start + plan.beatsPerBar) >= 2) ++covered;
    }
    return static_cast<double>(covered) / static_cast<double>(bars);
}

void retainInterval(NoteEvent source, double start, double end,
                    std::vector<NoteEvent>& destination,
                    AttentionDirectionReport& report) {
    if (end - start < .03125) return;
    const auto changed = std::abs(start - source.startBeat) > .001 ||
                         std::abs(end - source.endBeat()) > .001;
    source.startBeat = start;
    source.durationBeats = end - start;
    if (changed) {
        if (source.origin == NoteOrigin::AiAuthored || source.origin == NoteOrigin::PlanDerived)
            source.origin = NoteOrigin::AiTransformed;
        ++report.notesTrimmed;
    }
    destination.push_back(source);
}

} // namespace

AttentionDirectionReport AttentionDirector::audit(const Pattern& pattern,
                                                   const SongPlan& plan) {
    AttentionDirectionReport report;
    report.active = electronic(plan) && !pattern.parts.empty() && !pattern.notes.empty();
    if (!report.active) return report;
    const auto summary = activity(pattern, plan);
    report.windows = static_cast<std::size_t>(std::ceil(
        pattern.lengthBeats / std::max(1.0, plan.beatsPerBar * 4.0)));
    report.overcrowdedBarsAfter = summary.overcrowded;
    report.peakActivePartsAfter = summary.peak;
    report.averageActivePartsAfter = summary.average;
    report.harmonicFloorCoverageAfter = floorCoverage(pattern, plan);
    return report;
}

AttentionDirectionReport AttentionDirector::shape(Pattern& pattern, const SongPlan& plan) {
    auto report = audit(pattern, plan);
    if (!report.active) return report;
    const auto before = activity(pattern, plan);
    report.overcrowdedBarsBefore = before.overcrowded;
    report.peakActivePartsBefore = before.peak;
    report.averageActivePartsBefore = before.average;
    report.harmonicFloorCoverageBefore = floorCoverage(pattern, plan);

    const auto beatsPerBar = std::max(1.0, plan.beatsPerBar);
    const auto bars = std::max(1, static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar)));
    const auto windowBars = 4;
    const auto windows = static_cast<std::size_t>((bars + windowBars - 1) / windowBars);
    report.windows = windows;
    const auto persistentCongestion = before.overcrowded >=
        std::max<std::size_t>(2, static_cast<std::size_t>(bars) / 8);
    const auto breathBars = persistentCongestion ? structuralBreathBars(plan) : std::set<int>{};
    auto lowEndBreathBars = breathBars;
    if (persistentCongestion) {
        for (auto bar = 15; bar + 1 < bars; bar += 16)
            lowEndBreathBars.insert(bar);
    }
    report.structuralBreathBars = lowEndBreathBars.size();

    std::map<std::uint16_t, std::size_t> totalNotes;
    for (const auto& note : pattern.notes) if (note.partId != 0) ++totalNotes[note.partId];
    std::vector<std::set<std::uint16_t>> allowed(static_cast<std::size_t>(bars));
    std::map<std::uint16_t, std::size_t> selections;
    auto primarySpeakerId = std::uint16_t{};
    auto primarySpeakerProminence = -1.0;
    for (const auto& part : pattern.parts) {
        if (part.sourceVoice == VoiceId::Lead && part.prominence > primarySpeakerProminence) {
            primarySpeakerId = part.id;
            primarySpeakerProminence = part.prominence;
        }
    }
    if (!plan.narrativeSpine.protagonistInstrumentId.empty()) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index)
            if (plan.instruments[index].id == plan.narrativeSpine.protagonistInstrumentId) {
                const auto expected = static_cast<std::uint16_t>(index + 1);
                if (partFor(pattern, expected) != nullptr) primarySpeakerId = expected;
                break;
            }
    }
    const auto protagonist = [&](const InstrumentPart& part) {
        return part.id != 0 && part.id == primarySpeakerId;
    };

    // A call-response lane is subordinate speech. It may quote a clue, but it cannot
    // out-talk the protagonist or occupy the same bars as a second lead for most of
    // the form. This pass only subtracts existing notes and distributes the retained
    // answer evenly; it never composes a replacement.
    if (primarySpeakerId != 0) {
        std::set<std::uint16_t> answerIds;
        for (const auto& part : pattern.parts)
            if (part.lineRelationship == "call_response") answerIds.insert(part.id);
        const auto primaryNotes = std::count_if(pattern.notes.begin(), pattern.notes.end(),
            [&](const auto& note) { return note.partId == primarySpeakerId; });
        if (!answerIds.empty() && primaryNotes >= 6) {
            std::set<int> primaryBars;
            std::set<int> answerBars;
            for (const auto& note : pattern.notes) {
                const auto bar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)),
                                            0, bars - 1);
                if (note.partId == primarySpeakerId) primaryBars.insert(bar);
                if (answerIds.contains(note.partId)) answerBars.insert(bar);
            }
            std::vector<int> overlaps;
            std::set_intersection(primaryBars.begin(), primaryBars.end(), answerBars.begin(), answerBars.end(),
                                  std::back_inserter(overlaps));
            const auto permittedOverlap = std::max<std::size_t>(1,
                static_cast<std::size_t>(std::ceil(primaryBars.size() * .15)));
            std::set<int> retainedOverlap;
            if (!overlaps.empty()) {
                for (std::size_t slot = 0; slot < std::min(permittedOverlap, overlaps.size()); ++slot) {
                    const auto index = std::min(overlaps.size() - 1,
                        slot * overlaps.size() / std::min(permittedOverlap, overlaps.size()));
                    retainedOverlap.insert(overlaps[index]);
                }
            }
            const auto beforeOverlap = pattern.notes.size();
            pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                if (!answerIds.contains(note.partId)) return false;
                const auto bar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)),
                                            0, bars - 1);
                return primaryBars.contains(bar) && !retainedOverlap.contains(bar);
            }), pattern.notes.end());
            report.notesRemoved += beforeOverlap - pattern.notes.size();

            std::vector<std::size_t> answerNotes;
            for (std::size_t index = 0; index < pattern.notes.size(); ++index)
                if (answerIds.contains(pattern.notes[index].partId)) answerNotes.push_back(index);
            const auto answerLimit = std::max<std::size_t>(6,
                static_cast<std::size_t>(std::ceil(primaryNotes * .60)));
            if (answerNotes.size() > answerLimit) {
                std::set<std::size_t> keep;
                for (std::size_t slot = 0; slot < answerLimit; ++slot)
                    keep.insert(slot * answerNotes.size() / answerLimit);
                const auto beforeLimit = pattern.notes.size();
                auto ordinal = std::size_t{};
                pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                    if (!answerIds.contains(note.partId)) return false;
                    const auto current = ordinal++;
                    return !keep.contains(current);
                }), pattern.notes.end());
                report.notesRemoved += beforeLimit - pattern.notes.size();
            }
        }
    }

    for (std::size_t window = 0; window < windows; ++window) {
        const auto firstBar = static_cast<int>(window) * windowBars;
        const auto lastBar = std::min(bars, firstBar + windowBars);
        const auto start = firstBar * beatsPerBar;
        const auto end = std::min(pattern.lengthBeats, lastBar * beatsPerBar);
        const auto active = activeParts(pattern, start, end);
        const auto budget = partBudget(plan, start);
        std::set<std::uint16_t> chosen;

        const auto chooseBest = [&](auto predicate, std::size_t maximum) {
            std::vector<const InstrumentPart*> candidates;
            for (const auto id : active) {
                const auto* part = partFor(pattern, id);
                if (part != nullptr && predicate(*part) && !chosen.contains(id))
                    candidates.push_back(part);
            }
            std::stable_sort(candidates.begin(), candidates.end(), [&](const auto* left, const auto* right) {
                const auto leftScore = priority(*left, window, selections[left->id]) +
                    (protagonist(*left) ? 100.0 : 0.0);
                const auto rightScore = priority(*right, window, selections[right->id]) +
                    (protagonist(*right) ? 100.0 : 0.0);
                return leftScore > rightScore;
            });
            for (const auto* candidate : candidates) {
                if (maximum-- == 0 || chosen.size() >= budget) break;
                chosen.insert(candidate->id);
            }
        };

        if (!persistentCongestion) {
            chosen = active;
        } else {
            chooseBest([&](const auto& part) { return contractedFloor(part, plan); }, 2);
            chooseBest([](const auto& part) { return foreground(part); },
                       partBudget(plan, start) >= 10 ? 2 : 1);
            // One declared protagonist owns thematic memory. Other Lead-labelled
            // instruments compete for foreground like any orchestral colour; protecting
            // all of them was precisely how one motif remained split across many tracks.
            for (const auto id : active)
                if (const auto* part = partFor(pattern, id);
                    part != nullptr && protagonist(*part))
                    chosen.insert(id);
            chooseBest([](const auto& part) { return corePulse(part); }, 1);
            chooseBest([](const auto& part) { return lowEnd(part); }, 1);
            chooseBest([](const auto& part) { return texture(part); }, 1);
            chooseBest([](const auto& part) {
                return part.department == ScoreDepartment::Rhythm && !corePulse(part);
            }, 3);
            // Explicit mutations are compositional events, not background density. Keep
            // their lane audible in the exact window in which GPT scheduled the gesture.
            for (const auto& section : plan.sections) {
                const auto sectionStart = section.startBar * beatsPerBar;
                const auto sectionEnd = (section.startBar + section.bars) * beatsPerBar;
                if (sectionEnd <= start + .001 || sectionStart >= end - .001) continue;
                for (const auto& mutation : section.rhythm.mutations) {
                    const auto voice = mutation.lane == RhythmLane::Kick ? VoiceId::CoreDrums
                        : mutation.lane == RhythmLane::SnareClap ? VoiceId::SnareClap
                        : mutation.lane == RhythmLane::ClosedHats ? VoiceId::ClosedHats
                        : mutation.lane == RhythmLane::OpenHatsShaker ? VoiceId::OpenHatsShaker
                        : mutation.lane == RhythmLane::LowPercussion ? VoiceId::LowPercussion
                                                                    : VoiceId::HighPercussion;
                    for (const auto id : active)
                        if (const auto* part = partFor(pattern, id);
                            part != nullptr && part->sourceVoice == voice)
                            chosen.insert(id);
                }
            }
            chooseBest([](const auto&) { return true; }, budget);

            // Token gestures cannot create overcrowding and must not disappear merely because
            // they are rare. They represent authored transitions, cymbal gates and thresholds.
            for (const auto id : active) {
                const auto* part = partFor(pattern, id);
                if (part != nullptr && (eventPart(*part) || totalNotes[id] < 6)) chosen.insert(id);
            }
        }
        // The four-bar window decides orchestral rotation, but subtraction happens at
        // the exact bar. A rare one-note colour elsewhere in the window must not make
        // an entire authored phrase disappear.
        for (auto bar = firstBar; bar < lastBar; ++bar) {
            const auto barStart = bar * beatsPerBar;
            const auto barActive = activeParts(pattern, barStart,
                std::min(pattern.lengthBeats, barStart + beatsPerBar));
            const auto barBudget = partBudget(plan, barStart);
            if (!persistentCongestion || barActive.size() <= barBudget) {
                allowed[static_cast<std::size_t>(bar)] = barActive;
                continue;
            }
            auto& exact = allowed[static_cast<std::size_t>(bar)];
            for (const auto id : chosen)
                if (barActive.contains(id)) exact.insert(id);
            std::vector<const InstrumentPart*> remainder;
            for (const auto id : barActive)
                if (!exact.contains(id))
                    if (const auto* part = partFor(pattern, id)) remainder.push_back(part);
            std::stable_sort(remainder.begin(), remainder.end(), [&](const auto* left, const auto* right) {
                return priority(*left, window, selections[left->id]) +
                           (protagonist(*left) ? 100.0 : 0.0) >
                       priority(*right, window, selections[right->id]) +
                           (protagonist(*right) ? 100.0 : 0.0);
            });
            for (const auto* part : remainder) {
                if (exact.size() >= barBudget) break;
                exact.insert(part->id);
            }
        }
        for (const auto id : chosen) ++selections[id];
    }

    // A structural breath is a one-bar subtraction before a consequential boundary.
    // One floor owner remains, while foreground, transition and pulse take turns instead
    // of the whole ensemble sustaining through the change.
    for (const auto bar : breathBars) {
        if (bar < 0 || bar >= bars) continue;
        auto candidates = allowed[static_cast<std::size_t>(bar)];
        std::vector<const InstrumentPart*> ranked;
        for (const auto id : candidates)
            if (const auto* part = partFor(pattern, id)) ranked.push_back(part);
        std::stable_sort(ranked.begin(), ranked.end(), [&](const auto* left, const auto* right) {
            auto leftScore = left->prominence + (foundation(*left) ? 4.0 : 0.0) +
                (foreground(*left) ? 2.0 : 0.0) + (texture(*left) ? 1.0 : 0.0);
            auto rightScore = right->prominence + (foundation(*right) ? 4.0 : 0.0) +
                (foreground(*right) ? 2.0 : 0.0) + (texture(*right) ? 1.0 : 0.0);
            return leftScore > rightScore;
        });
        std::set<std::uint16_t> reduced;
        auto floorKept = false;
        auto foregroundKept = false;
        if (requiredCorePulseAt(plan, bar * beatsPerBar))
            for (const auto* part : ranked)
                if (corePulse(*part)) reduced.insert(part->id);
        for (const auto* part : ranked)
            if (protagonist(*part)) {
                reduced.insert(part->id);
                foregroundKept = true;
            }
        for (const auto* part : ranked) {
            if (reduced.contains(part->id)) continue;
            if (eventPart(*part)) { reduced.insert(part->id); continue; }
            if (contractedFloor(*part, plan) && !floorKept) {
                reduced.insert(part->id); floorKept = true; continue;
            }
            if (foreground(*part) && !foregroundKept) {
                reduced.insert(part->id); foregroundKept = true; continue;
            }
            if (reduced.size() < 4 && !corePulse(*part) && !lowEnd(*part)) reduced.insert(part->id);
        }
        allowed[static_cast<std::size_t>(bar)] = std::move(reduced);
    }

    // Low-end continuity needs punctuation as much as melody. A one-bar withdrawal at
    // phrase endings makes the return of the sub perceptible and prevents an anchor
    // from covering virtually the entire song. Harmonic memory may remain above it.
    for (const auto bar : lowEndBreathBars) {
        if (bar < 0 || bar >= bars) continue;
        auto& exact = allowed[static_cast<std::size_t>(bar)];
        for (auto iterator = exact.begin(); iterator != exact.end();) {
            const auto* part = partFor(pattern, *iterator);
            if (part != nullptr && lowEnd(*part)) iterator = exact.erase(iterator);
            else ++iterator;
        }
    }

    const auto original = pattern.notes;
    std::vector<NoteEvent> shaped;
    shaped.reserve(original.size());
    for (const auto& note : original) {
        if (note.partId == 0 || partFor(pattern, note.partId) == nullptr) {
            shaped.push_back(note);
            continue;
        }
        const auto firstBar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)), 0, bars - 1);
        const auto lastBar = std::clamp(static_cast<int>(std::floor(
            std::max(note.startBeat, note.endBeat() - .001) / beatsPerBar)), 0, bars - 1);
        auto fragmentStart = -1.0;
        auto fragmentEnd = -1.0;
        auto retained = false;
        for (auto bar = firstBar; bar <= lastBar; ++bar) {
            const auto barStart = bar * beatsPerBar;
            const auto barEnd = std::min(pattern.lengthBeats, barStart + beatsPerBar);
            const auto start = std::max(note.startBeat, barStart);
            const auto end = std::min(note.endBeat(), barEnd);
            const auto keep = allowed[static_cast<std::size_t>(bar)].contains(note.partId);
            if (keep && end > start + .001) {
                retained = true;
                if (fragmentStart < 0.0) fragmentStart = start;
                fragmentEnd = end;
            } else if (fragmentStart >= 0.0) {
                retainInterval(note, fragmentStart, fragmentEnd, shaped, report);
                fragmentStart = fragmentEnd = -1.0;
            }
        }
        if (fragmentStart >= 0.0) retainInterval(note, fragmentStart, fragmentEnd, shaped, report);
        if (!retained) ++report.notesRemoved;
    }
    pattern.notes = std::move(shaped);

    // Phrase-scale silence: dense speakers and arpeggiators release half a beat before
    // alternating phrase boundaries. Existing rests are left untouched.
    std::map<std::pair<std::uint16_t, int>, std::size_t> phraseOnsets;
    for (const auto& note : pattern.notes)
        if (note.partId != 0)
            ++phraseOnsets[{note.partId,
                static_cast<int>(std::floor(note.startBeat / (beatsPerBar * 4.0)))}];
    std::vector<NoteEvent> breathed;
    breathed.reserve(pattern.notes.size());
    for (const auto& note : pattern.notes) {
        const auto* part = partFor(pattern, note.partId);
        if (!persistentCongestion || part == nullptr || !phraseVoice(*part) ||
            part->sourceVoice == VoiceId::Lead || protagonist(*part) ||
            totalNotes[note.partId] < 12) {
            breathed.push_back(note);
            continue;
        }
        const auto bar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)), 0, bars - 1);
        const auto phraseIndex = bar / 4;
        if (phraseOnsets[{note.partId, phraseIndex}] < 6) {
            breathed.push_back(note);
            continue;
        }
        const auto breathBar = phraseIndex * 4 + (part->id % 2 == 0 ? 1 : 3);
        const auto breathEnd = std::min(pattern.lengthBeats, (breathBar + 1) * beatsPerBar);
        const auto breathStart = breathEnd - std::min(.5, beatsPerBar * .125);
        if (bar != breathBar || note.endBeat() <= breathStart + .001 || note.startBeat >= breathEnd - .001) {
            breathed.push_back(note);
            continue;
        }
        if (note.startBeat < breathStart - .03125) {
            retainInterval(note, note.startBeat, breathStart - .03125, breathed, report);
            ++report.phraseBreathsCreated;
        } else {
            ++report.notesRemoved;
        }
    }
    pattern.notes = std::move(breathed);

    // The floor is continuous as a group, not as one eternal pad. Two owners rotate
    // every four bars; structural breath bars intentionally reduce to one. New sustains
    // are chord tones selected from the AI-authored harmonic plan and marked PlanDerived.
    std::vector<const InstrumentPart*> floors;
    // Use exactly the same ordered floor contract as ElectronicCompositionFabric's
    // publication audit. Filling a fifth attractive pad while the audited first four
    // remain absent created reassuring local metrics but a genuinely incomplete floor.
    for (const auto& part : pattern.parts)
        if (contractedFloor(part, plan)) floors.push_back(&part);
    if (floors.size() > 4) floors.resize(4);
    std::map<std::uint16_t, int> previousPitch;
    for (const auto* part : floors) previousPitch[part->id] = (part->minimumPitch + part->maximumPitch) / 2;
    for (auto bar = 0; bar < bars && floors.size() >= 2; ++bar) {
        const auto start = bar * beatsPerBar;
        const auto end = std::min(pattern.lengthBeats, start + beatsPerBar);
        const auto desired = breathBars.contains(bar) ? std::size_t{1} : std::size_t{2};
        std::set<std::uint16_t> sounding;
        for (const auto* part : floors)
            if (std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
                    return note.partId == part->id && note.startBeat < end - .001 &&
                           note.endBeat() > start + .001;
                })) sounding.insert(part->id);
        const auto& chord = chordAt(plan, start);
        const auto tones = chord.pitchClasses.empty()
            ? std::vector<int>{plan.rootPitchClass, positiveModulo(plan.rootPitchClass + 7, 12)}
            : chord.pitchClasses;
        for (std::size_t attempt = 0; attempt < floors.size() * 2 && sounding.size() < desired; ++attempt) {
            const auto ordinal = (static_cast<std::size_t>(bar / 4) + attempt) % floors.size();
            const auto* part = floors[ordinal];
            if (sounding.contains(part->id)) continue;
            const auto tone = tones[(ordinal + static_cast<std::size_t>(bar / 4)) % tones.size()];
            const auto pitch = nearestPitch(tone, previousPitch[part->id], *part);
            pattern.notes.push_back({start, std::max(.0625, end - start - .03125), pitch,
                std::clamp(static_cast<int>(42 + part->prominence * 24.0), 34, 72),
                voiceDefinition(part->sourceVoice).midiChannel, part->sourceVoice, part->id,
                true, NoteOrigin::PlanDerived, 0x4154544eu});
            previousPitch[part->id] = pitch;
            sounding.insert(part->id);
            ++report.floorNotesCreated;
        }
    }

    // Floor completion changes the density it is supposed to protect. Recompute the
    // exact bar budget after those sustains exist and subtract the least important
    // background owners until the final score—not the pre-floor draft—fits. Two floor
    // layers, the protagonist and contractual pulse remain protected.
    std::vector<std::set<std::uint16_t>> finalAllowed(static_cast<std::size_t>(bars));
    auto needsFinalPass = false;
    for (auto bar = 0; bar < bars; ++bar) {
        const auto start = bar * beatsPerBar;
        auto active = activeParts(pattern, start, std::min(pattern.lengthBeats, start + beatsPerBar));
        const auto budget = partBudget(plan, start);
        auto floorCount = std::count_if(active.begin(), active.end(), [&](auto id) {
            const auto* part = partFor(pattern, id);
            return part != nullptr && contractedFloor(*part, plan);
        });
        while (active.size() > budget) {
            std::vector<const InstrumentPart*> removable;
            for (const auto id : active) {
                const auto* part = partFor(pattern, id);
                if (part == nullptr || protagonist(*part) || eventPart(*part) ||
                    (corePulse(*part) && requiredCorePulseAt(plan, start))) continue;
                if (contractedFloor(*part, plan) && floorCount <= 2) continue;
                removable.push_back(part);
            }
            if (removable.empty()) break;
            const auto victim = *std::min_element(removable.begin(), removable.end(),
                [&](const auto* left, const auto* right) {
                    return priority(*left, static_cast<std::size_t>(bar / 4), selections[left->id]) <
                           priority(*right, static_cast<std::size_t>(bar / 4), selections[right->id]);
                });
            if (contractedFloor(*victim, plan)) --floorCount;
            active.erase(victim->id);
            needsFinalPass = true;
        }
        finalAllowed[static_cast<std::size_t>(bar)] = std::move(active);
    }
    if (needsFinalPass) {
        const auto postFloor = pattern.notes;
        std::vector<NoteEvent> converged;
        converged.reserve(postFloor.size());
        for (const auto& note : postFloor) {
            if (note.partId == 0 || partFor(pattern, note.partId) == nullptr) {
                converged.push_back(note);
                continue;
            }
            const auto firstBar = std::clamp(static_cast<int>(std::floor(note.startBeat / beatsPerBar)),
                                             0, bars - 1);
            const auto lastBar = std::clamp(static_cast<int>(std::floor(
                std::max(note.startBeat, note.endBeat() - .001) / beatsPerBar)), 0, bars - 1);
            auto fragmentStart = -1.0;
            auto fragmentEnd = -1.0;
            auto retained = false;
            for (auto bar = firstBar; bar <= lastBar; ++bar) {
                const auto barStart = bar * beatsPerBar;
                const auto barEnd = std::min(pattern.lengthBeats, barStart + beatsPerBar);
                const auto start = std::max(note.startBeat, barStart);
                const auto end = std::min(note.endBeat(), barEnd);
                const auto keep = finalAllowed[static_cast<std::size_t>(bar)].contains(note.partId);
                if (keep && end > start + .001) {
                    retained = true;
                    if (fragmentStart < 0.0) fragmentStart = start;
                    fragmentEnd = end;
                } else if (fragmentStart >= 0.0) {
                    retainInterval(note, fragmentStart, fragmentEnd, converged, report);
                    fragmentStart = fragmentEnd = -1.0;
                }
            }
            if (fragmentStart >= 0.0)
                retainInterval(note, fragmentStart, fragmentEnd, converged, report);
            if (!retained) ++report.notesRemoved;
        }
        pattern.notes = std::move(converged);
    }

    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    const auto after = activity(pattern, plan);
    report.overcrowdedBarsAfter = after.overcrowded;
    report.peakActivePartsAfter = after.peak;
    report.averageActivePartsAfter = after.average;
    report.harmonicFloorCoverageAfter = floorCoverage(pattern, plan);
    return report;
}

} // namespace pulso
