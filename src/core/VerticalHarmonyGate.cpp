#include "VerticalHarmonyGate.h"

#include "Orchestration.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace pulso {
namespace {

struct Span { double start{}; double end{}; };

const InstrumentPart* partFor(const Pattern& pattern, const NoteEvent& note) {
    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
        return part.id == note.partId;
    });
    return found == pattern.parts.end() ? nullptr : &*found;
}

bool lowFoundation(const InstrumentPart* part, const NoteEvent& note) {
    if (note.voice == VoiceId::SubBass || note.voice == VoiceId::MovementBass) return true;
    if (part == nullptr) return false;
    return part->catalogId == "sub_synth" || part->catalogId == "electric_bass" ||
           part->catalogId == "contrabass" || part->catalogId == "tuba" ||
           part->catalogId == "bass_trombone";
}

bool duckableSupport(const InstrumentPart* part, const NoteEvent& note) {
    if (part == nullptr || part->department != ScoreDepartment::Harmony) return false;
    if (part->orchestralFunction == "counterpoint" || part->orchestralFunction == "color" ||
        part->orchestralFunction == "transition") return false;
    return !isVoiceInFamily(note.voice, VoiceFamily::Rhythm) && note.voice != VoiceId::Transitions;
}

bool harshLowInterval(const NoteEvent& low, const NoteEvent& support) {
    if (low.partId == support.partId || std::min(low.pitch, support.pitch) >= 55) return false;
    auto interval = positiveModulo(std::abs(low.pitch - support.pitch), 12);
    interval = std::min(interval, 12 - interval);
    if (interval != 1 && interval != 6) return false;
    return std::min(low.endBeat(), support.endBeat()) -
           std::max(low.startBeat, support.startBeat) >= 1.0 / 16.0 - 0.000001;
}

double durationFloor(const InstrumentPart* part) {
    if (part == nullptr) return 0.125;
    if (part->catalogId == "analog_pad" || part->catalogId == "ambient_texture") return 0.50;
    if (part->soundModel == InstrumentSoundModel::HighStrings ||
        part->soundModel == InstrumentSoundModel::MidStrings ||
        part->soundModel == InstrumentSoundModel::LowStrings ||
        part->soundModel == InstrumentSoundModel::Flute ||
        part->soundModel == InstrumentSoundModel::Oboe ||
        part->soundModel == InstrumentSoundModel::Clarinet ||
        part->soundModel == InstrumentSoundModel::Bassoon ||
        part->soundModel == InstrumentSoundModel::Horns ||
        part->soundModel == InstrumentSoundModel::Brass) return 0.25;
    return 0.125;
}

std::vector<Span> merged(std::vector<Span> spans) {
    std::sort(spans.begin(), spans.end(), [](const auto& left, const auto& right) {
        return left.start < right.start;
    });
    std::vector<Span> result;
    for (const auto& span : spans) {
        if (result.empty() || span.start > result.back().end + 0.000001)
            result.push_back(span);
        else
            result.back().end = std::max(result.back().end, span.end);
    }
    return result;
}

} // namespace

std::size_t VerticalHarmonyGate::audit(const Pattern& pattern) {
    std::size_t result{};
    for (std::size_t lowIndex = 0; lowIndex < pattern.notes.size(); ++lowIndex) {
        const auto& low = pattern.notes[lowIndex];
        const auto* lowPart = partFor(pattern, low);
        if (!lowFoundation(lowPart, low)) continue;
        for (std::size_t supportIndex = 0; supportIndex < pattern.notes.size(); ++supportIndex) {
            if (supportIndex == lowIndex) continue;
            const auto& support = pattern.notes[supportIndex];
            const auto* supportPart = partFor(pattern, support);
            if (!duckableSupport(supportPart, support) || lowFoundation(supportPart, support)) continue;
            if (harshLowInterval(low, support)) ++result;
        }
    }
    return result;
}

VerticalHarmonyReport VerticalHarmonyGate::enforce(Pattern& pattern) {
    VerticalHarmonyReport report;
    report.collisionsBefore = audit(pattern);
    if (report.collisionsBefore == 0) return report;

    std::map<std::size_t, std::vector<Span>> cuts;
    for (std::size_t lowIndex = 0; lowIndex < pattern.notes.size(); ++lowIndex) {
        const auto& low = pattern.notes[lowIndex];
        const auto* lowPart = partFor(pattern, low);
        if (!lowFoundation(lowPart, low)) continue;
        for (std::size_t supportIndex = 0; supportIndex < pattern.notes.size(); ++supportIndex) {
            if (supportIndex == lowIndex) continue;
            const auto& support = pattern.notes[supportIndex];
            const auto* supportPart = partFor(pattern, support);
            if (!duckableSupport(supportPart, support) || lowFoundation(supportPart, support) ||
                !harshLowInterval(low, support)) continue;
            cuts[supportIndex].push_back({std::max(low.startBeat, support.startBeat),
                                          std::min(low.endBeat(), support.endBeat())});
        }
    }

    std::vector<NoteEvent> result;
    result.reserve(pattern.notes.size() + cuts.size());
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto found = cuts.find(index);
        if (found == cuts.end()) { result.push_back(pattern.notes[index]); continue; }
        const auto& source = pattern.notes[index];
        const auto floor = durationFloor(partFor(pattern, source));
        auto cursor = source.startBeat;
        auto segments = merged(found->second);
        auto created = std::size_t{};
        for (const auto& cut : segments) {
            if (cut.start - cursor >= floor - 0.000001) {
                auto fragment = source;
                fragment.startBeat = cursor;
                fragment.durationBeats = cut.start - cursor;
                // The gate authors an exact 1/16-capable support rest. It is deliberate
                // publication timing, not humanisation or an accidental off-grid onset.
                fragment.authoredTiming = true;
                result.push_back(fragment);
                ++created;
            }
            cursor = std::max(cursor, cut.end);
        }
        if (source.endBeat() - cursor >= floor - 0.000001) {
            auto fragment = source;
            fragment.startBeat = cursor;
            fragment.durationBeats = source.endBeat() - cursor;
            fragment.authoredTiming = true;
            result.push_back(fragment);
            ++created;
        }
        ++report.supportNotesDucked;
        if (created > 1) report.continuationFragmentsCreated += created - 1;
    }
    pattern.notes = std::move(result);
    report.collisionsAfter = audit(pattern);
    report.score = report.collisionsBefore == 0 ? 1.0 :
        1.0 - static_cast<double>(report.collisionsAfter) / report.collisionsBefore;
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.partId != right.partId) return left.partId < right.partId;
        return left.pitch < right.pitch;
    });
    return report;
}

} // namespace pulso
