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
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    std::size_t emitted = 0;
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
        out << "SECTION " << sectionIndex << " " << section.name << "\n";
        for (auto& [key, pitches] : groups) {
            if (emitted >= maximumGroups) {
                out << "TRUNCATED after " << maximumGroups
                    << " complete attack/release groups; use the local MIDI audit for omitted events.\n";
                return out.str();
            }
            std::sort(pitches.begin(), pitches.end());
            pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
            const auto [partId, start, end] = key;
            out << "  " << plan.instruments[partId - 1].id << " ["
                << start / 1000.0 << "-" << end / 1000.0 << ":";
            for (std::size_t i = 0; i < pitches.size(); ++i) {
                if (i) out << ',';
                out << pitches[i];
            }
            out << "]\n";
            ++emitted;
        }
    }
    return emitted == 0 ? std::string{} : out.str();
}

} // namespace pulso
