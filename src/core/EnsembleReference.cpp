#include "EnsembleReference.h"

#include "SongComposer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace pulso {
namespace {

int priorityFor(const SongPlan& plan, const InstrumentAssignment& part) {
    if (part.id == plan.narrativeSpine.protagonistInstrumentId) return 0;
    if (part.role.find("primary_chord_bed") != std::string::npos) return 1;
    if (part.sourceVoice == VoiceId::SubBass || part.sourceVoice == VoiceId::MovementBass)
        return 2;
    return 3;
}

std::vector<const NoteEvent*> landmarks(const std::vector<NoteEvent>& notes,
                                        std::size_t limit) {
    std::vector<const NoteEvent*> result;
    if (notes.empty() || limit == 0) return result;
    const auto count = std::min(notes.size(), limit);
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto selected = count == 1 ? 0 : index * (notes.size() - 1) / (count - 1);
        result.push_back(&notes[selected]);
    }
    return result;
}

} // namespace

std::string EnsembleReference::summarize(const SongPlan& plan,
                                          const PerformanceScore& accepted,
                                          const std::set<std::string>& excludedInstrumentIds,
                                          std::size_t maximumEvents) {
    if (accepted.empty() || maximumEvents == 0) return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    auto emitted = std::size_t{};
    const auto sectionBudget = std::max<std::size_t>(1,
        maximumEvents / std::max<std::size_t>(1, plan.sections.size()));
    for (std::size_t sectionIndex = 0;
         sectionIndex < plan.sections.size() && emitted < maximumEvents; ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        if (sectionBeats <= 0.0) continue;
        Pattern rendered;
        rendered.lengthBeats = sectionBeats;
        PerformanceScoreEngine::replaceChunk(rendered, accepted,
            static_cast<int>(sectionIndex), 0.0, sectionBeats, plan.instruments);
        if (rendered.notes.empty()) continue;
        std::map<std::uint16_t, std::vector<NoteEvent>> byPart;
        for (auto note : rendered.notes) {
            if (note.partId == 0 || note.partId > plan.instruments.size()) continue;
            const auto& instrument = plan.instruments[note.partId - 1];
            if (excludedInstrumentIds.contains(instrument.id)) continue;
            note.startBeat += section.startBar * plan.beatsPerBar;
            byPart[note.partId].push_back(std::move(note));
        }
        if (byPart.empty()) continue;
        out << "SECTION " << sectionIndex << " " << section.name
            << " start_bar=" << section.startBar << "\n";
        // The principal melody, harmony and low-end are the common temporal spine.
        // Other owners receive fewer landmarks, preserving a bounded prompt even
        // for large casts and long songs.
        auto emittedInSection = std::size_t{};
        for (auto priority = 0; priority < 4; ++priority) {
            for (auto& [partId, notes] : byPart) {
                const auto& instrument = plan.instruments[partId - 1];
                if (priorityFor(plan, instrument) != priority || emitted >= maximumEvents ||
                    emittedInSection >= sectionBudget) continue;
                std::sort(notes.begin(), notes.end(), [](const auto& left, const auto& right) {
                    if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
                    return left.pitch < right.pitch;
                });
                const auto ownerQuota = priority == 0 ? std::size_t{8} :
                    priority == 1 ? std::size_t{8} : priority == 2 ? std::size_t{6} :
                    std::size_t{3};
                const auto quota = std::min({maximumEvents - emitted,
                    sectionBudget - emittedInSection, ownerQuota});
                const auto chosen = landmarks(notes, quota);
                if (chosen.empty()) continue;
                out << "  " << instrument.id << " role=" << instrument.role << " events=";
                for (const auto* note : chosen) {
                    out << "[" << note->startBeat << "," << note->pitch << ","
                        << note->durationBeats << "]";
                    ++emitted;
                    ++emittedInSection;
                }
                out << "\n";
            }
        }
    }
    return emitted == 0 ? std::string{} : out.str();
}

std::string EnsembleReference::harmonicLedger(
    const SongPlan& plan, const PerformanceScore& accepted,
    const std::set<std::string>& excludedInstrumentIds, std::size_t maximumGroups) {
    if (accepted.empty() || maximumGroups == 0) return {};
    struct ChordGroup {
        std::uint16_t partId{};
        long long start{};
        long long end{};
        std::vector<int> pitches;
    };
    struct SectionGroups {
        std::size_t index{};
        std::vector<ChordGroup> groups;
    };
    std::vector<SectionGroups> bySection;
    bySection.reserve(plan.sections.size());
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        if (sectionBeats <= 0.0) continue;
        Pattern rendered;
        rendered.lengthBeats = sectionBeats;
        PerformanceScoreEngine::replaceChunk(rendered, accepted,
            static_cast<int>(sectionIndex), 0.0, sectionBeats, plan.instruments);
        using GroupKey = std::tuple<std::uint16_t, long long, long long>;
        std::map<GroupKey, std::vector<int>> groups;
        for (const auto& note : rendered.notes) {
            if (note.partId == 0 || note.partId > plan.instruments.size()) continue;
            const auto& instrument = plan.instruments[note.partId - 1];
            if (excludedInstrumentIds.contains(instrument.id)) continue;
            if (instrument.role.find("primary_chord_bed") == std::string::npos &&
                instrument.sourceVoice != VoiceId::HarmonicFoundation &&
                instrument.sourceVoice != VoiceId::SubBass &&
                instrument.sourceVoice != VoiceId::MovementBass &&
                instrument.id != plan.narrativeSpine.protagonistInstrumentId) continue;
            const auto start = note.startBeat + section.startBar * plan.beatsPerBar;
            const auto end = start + note.durationBeats;
            groups[{note.partId, std::llround(start * 1000.0),
                    std::llround(end * 1000.0)}].push_back(note.pitch);
        }
        if (groups.empty()) continue;
        SectionGroups sectionGroups;
        sectionGroups.index = sectionIndex;
        for (auto& [key, pitches] : groups) {
            std::sort(pitches.begin(), pitches.end());
            pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
            const auto [partId, start, end] = key;
            sectionGroups.groups.push_back({partId, start, end, std::move(pitches)});
        }
        std::stable_sort(sectionGroups.groups.begin(), sectionGroups.groups.end(),
            [&](const auto& left, const auto& right) {
                if (left.start != right.start) return left.start < right.start;
                return priorityFor(plan, plan.instruments[left.partId - 1]) <
                    priorityFor(plan, plan.instruments[right.partId - 1]);
            });
        bySection.push_back(std::move(sectionGroups));
    }
    if (bySection.empty()) return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    std::size_t emitted = 0;
    for (std::size_t sectionPosition = 0; sectionPosition < bySection.size() &&
         emitted < maximumGroups; ++sectionPosition) {
        const auto& sectionGroups = bySection[sectionPosition];
        const auto remainingSections = bySection.size() - sectionPosition;
        const auto quota = std::min(sectionGroups.groups.size(),
            std::max<std::size_t>(1, (maximumGroups - emitted) / remainingSections));
        out << "SECTION " << sectionGroups.index << " "
            << plan.sections[sectionGroups.index].name << "\n";
        for (std::size_t i = 0; i < quota; ++i) {
            const auto selected = quota == 1 ? 0 :
                i * (sectionGroups.groups.size() - 1) / (quota - 1);
            const auto& group = sectionGroups.groups[selected];
            out << "  " << plan.instruments[group.partId - 1].id << " ["
                << group.start / 1000.0 << "-" << group.end / 1000.0 << ":";
            for (std::size_t j = 0; j < group.pitches.size(); ++j) {
                if (j) out << ',';
                out << group.pitches[j];
            }
            out << "]\n";
            ++emitted;
        }
    }
    if (emitted == maximumGroups)
        out << "SAMPLED " << emitted << " complete attack/release groups across "
            << bySection.size() << " nonempty sections; unlisted attacks are not silence.\n";
    return out.str();
}

std::string EnsembleReference::verticalSnapshots(
    const SongPlan& plan, const PerformanceScore& accepted,
    const std::set<std::string>& excludedInstrumentIds,
    std::size_t maximumSnapshots) {
    if (accepted.empty() || maximumSnapshots == 0) return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    auto emitted = std::size_t{};
    for (std::size_t sectionIndex = 0;
         sectionIndex < plan.sections.size() && emitted < maximumSnapshots;
         ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        if (sectionBeats <= 0.0) continue;
        Pattern rendered;
        rendered.lengthBeats = sectionBeats;
        PerformanceScoreEngine::replaceChunk(rendered, accepted,
            static_cast<int>(sectionIndex), 0.0, sectionBeats,
            plan.instruments);
        std::vector<double> boundaries{0.0};
        for (const auto& event : section.harmonicEvents) {
            const auto beat = event.barOffset * plan.beatsPerBar + event.beatOffset;
            if (beat >= 0.0 && beat < sectionBeats - .001)
                boundaries.push_back(beat);
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
            [](const auto left, const auto right) {
                return std::abs(left - right) < .001;
            }), boundaries.end());
        const auto remainingSections = plan.sections.size() - sectionIndex;
        const auto quota = std::min(boundaries.size(),
            std::max<std::size_t>(1,
                (maximumSnapshots - emitted) / remainingSections));
        if (quota == 0) continue;
        out << "SECTION " << sectionIndex << " " << section.name << "\n";
        for (std::size_t selected = 0; selected < quota; ++selected) {
            const auto index = quota == 1 ? 0 :
                selected * (boundaries.size() - 1) / (quota - 1);
            const auto localBeat = boundaries[index];
            const auto probe = localBeat + .01;
            std::map<std::uint16_t, std::vector<int>> sounding;
            for (const auto& note : rendered.notes) {
                if (note.partId == 0 || note.partId > plan.instruments.size() ||
                    note.startBeat > probe || note.endBeat() <= probe) continue;
                const auto& owner = plan.instruments[note.partId - 1];
                if (excludedInstrumentIds.contains(owner.id) ||
                    isVoiceInFamily(owner.sourceVoice, VoiceFamily::Rhythm)) continue;
                sounding[note.partId].push_back(note.pitch);
            }
            out << "  beat=" << section.startBar * plan.beatsPerBar + localBeat;
            if (sounding.empty()) out << " accepted_pitched_silence";
            for (auto& [partId, pitches] : sounding) {
                std::sort(pitches.begin(), pitches.end());
                pitches.erase(std::unique(pitches.begin(), pitches.end()),
                    pitches.end());
                out << " " << plan.instruments[partId - 1].id << "=";
                for (std::size_t pitch = 0; pitch < pitches.size(); ++pitch) {
                    if (pitch) out << ',';
                    out << pitches[pitch];
                }
            }
            out << "\n";
            ++emitted;
        }
    }
    return emitted == 0 ? std::string{} : out.str();
}

} // namespace pulso
