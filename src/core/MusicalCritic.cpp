#include "MusicalCritic.h"

#include "Orchestration.h"
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

bool aiAuthored(const NoteEvent& note) noexcept {
    return note.origin == NoteOrigin::AiAuthored || note.origin == NoteOrigin::AiTransformed;
}

using Signature = std::vector<std::tuple<int, int, int>>;

int barFor(const NoteEvent& note, const SongPlan& plan) noexcept {
    return std::clamp(static_cast<int>(std::floor(note.startBeat / plan.beatsPerBar)),
                      0, std::max(0, plan.totalBars - 1));
}

std::vector<Signature> melodicSignatures(const Pattern& pattern, const SongPlan& plan) {
    std::vector<Signature> result(static_cast<std::size_t>(plan.totalBars));
    for (const auto& note : pattern.notes) {
        if (!isVoiceInFamily(note.voice, VoiceFamily::Melodic)) continue;
        const auto bar = barFor(note, plan);
        const auto beat = note.startBeat - bar * plan.beatsPerBar;
        result[static_cast<std::size_t>(bar)].emplace_back(
            static_cast<int>(std::lround(beat * 8.0)), positiveModulo(note.pitch, 12),
            static_cast<int>(note.voice));
    }
    return result;
}

double clampScore(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

bool needsMonophonicSpace(VoiceId voice) noexcept {
    return isVoiceInFamily(voice, VoiceFamily::Melodic) ||
           voice == VoiceId::SubBass || voice == VoiceId::MovementBass ||
           voice == VoiceId::HarmonicUpper;
}

std::size_t varyLiteralRhythmRuns(Pattern& pattern, const SongPlan& plan) {
    std::vector<bool> remove(pattern.notes.size(), false);
    std::map<std::pair<VoiceId, int>, std::vector<std::size_t>> notesByVoiceBar;
    for (std::size_t index = 0; index < pattern.notes.size(); ++index) {
        const auto& note = pattern.notes[index];
        if (aiAuthored(note)) continue;
        if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) && note.voice != VoiceId::CoreDrums)
            notesByVoiceBar[{note.voice, barFor(note, plan)}].push_back(index);
    }
    std::size_t varied{};
    for (auto voiceIndex = std::size_t{};
         voiceIndex < static_cast<std::size_t>(VoiceId::Count); ++voiceIndex) {
        const auto voice = static_cast<VoiceId>(voiceIndex);
        if (!isVoiceInFamily(voice, VoiceFamily::Rhythm) || voice == VoiceId::CoreDrums)
            continue; // A requested four-on-the-floor foundation is allowed to be literal.
        Signature previous;
        auto run = 0;
        for (auto bar = 0; bar < plan.totalBars; ++bar) {
            Signature signature;
            const auto foundBar = notesByVoiceBar.find({voice, bar});
            static const std::vector<std::size_t> noIndices;
            const auto& indices = foundBar == notesByVoiceBar.end() ? noIndices : foundBar->second;
            for (const auto index : indices) {
                const auto& note = pattern.notes[index];
                signature.emplace_back(
                    static_cast<int>(std::lround((note.startBeat - bar * plan.beatsPerBar) * 96.0)),
                    note.pitch, static_cast<int>(std::lround(note.durationBeats * 96.0)));
            }
            std::sort(signature.begin(), signature.end());
            if (!signature.empty() && signature == previous) ++run;
            else run = signature.empty() ? 0 : 1;
            if (run >= 3 && !indices.empty()) {
                // Negative space is the safest style-independent mutation: it preserves
                // the grid, tuning and sound identity while breaking a copied bar.
                const auto selected = indices[(static_cast<std::size_t>(bar) + voiceIndex) % indices.size()];
                remove[selected] = true;
                const auto token = std::tuple{
                    static_cast<int>(std::lround((pattern.notes[selected].startBeat -
                        bar * plan.beatsPerBar) * 96.0)), pattern.notes[selected].pitch,
                    static_cast<int>(std::lround(pattern.notes[selected].durationBeats * 96.0))};
                if (const auto found = std::find(signature.begin(), signature.end(), token);
                    found != signature.end()) signature.erase(found);
                previous = std::move(signature);
                run = previous.empty() ? 0 : 1;
                ++varied;
            } else previous = std::move(signature);
        }
    }
    if (varied == 0) return 0;
    std::vector<NoteEvent> retained;
    retained.reserve(pattern.notes.size() - varied);
    for (std::size_t index = 0; index < pattern.notes.size(); ++index)
        if (!remove[index]) retained.push_back(pattern.notes[index]);
    pattern.notes = std::move(retained);
    return varied;
}

} // namespace

MusicalQualityReport MusicalCritic::review(const Pattern& pattern, const SongPlan& plan) {
    MusicalQualityReport report;
    if (pattern.notes.empty() || plan.totalBars <= 0) return report;

    const auto signatures = melodicSignatures(pattern, plan);
    auto melodicBars = std::size_t{};
    auto emptyMelodicBars = std::size_t{};
    for (std::size_t bar = 0; bar < signatures.size(); ++bar) {
        if (signatures[bar].empty()) {
            ++emptyMelodicBars;
            continue;
        }
        ++melodicBars;
        for (std::size_t previous = bar > 8 ? bar - 8 : 0; previous < bar; ++previous)
            if (!signatures[previous].empty() && signatures[previous] == signatures[bar]) {
                ++report.repeatedBars;
                break;
            }
    }
    const auto totalBars = static_cast<double>(std::max<std::size_t>(1, signatures.size()));
    const auto restRatio = emptyMelodicBars / totalBars;
    report.negativeSpace = clampScore(1.0 - std::abs(restRatio - 0.48) / 0.48);
    // Human musical memory lives between a copy and a stream of unrelated novelty.
    // Reward a recognisable recurrence band instead of the old "never repeat" bias.
    const auto recurrence = report.repeatedBars /
        static_cast<double>(std::max<std::size_t>(1, melodicBars));
    report.variation = clampScore(1.0 - std::abs(recurrence - 0.30) / 0.60);

    std::array<std::vector<const NoteEvent*>, static_cast<std::size_t>(VoiceId::Count)> byVoice;
    std::vector<int> velocities;
    std::vector<std::set<VoiceId>> voicesByBar(static_cast<std::size_t>(plan.totalBars));
    for (const auto& note : pattern.notes) {
        if (note.voice == VoiceId::Unspecified) continue;
        byVoice[static_cast<std::size_t>(note.voice)].push_back(&note);
        velocities.push_back(note.velocity);
        voicesByBar[static_cast<std::size_t>(barFor(note, plan))].insert(note.voice);
    }
    auto melodicComparisons = std::size_t{};
    for (const auto voice : {VoiceId::Lead, VoiceId::Countermelody, VoiceId::SubBass,
                             VoiceId::MovementBass}) {
        auto notes = byVoice[static_cast<std::size_t>(voice)];
        std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
            return left->startBeat < right->startBeat;
        });
        for (std::size_t index = 1; index < notes.size(); ++index) {
            ++melodicComparisons;
            if (std::abs(notes[index]->pitch - notes[index - 1]->pitch) > 12)
                ++report.excessiveLeaps;
        }
    }
    report.voiceIndependence = clampScore(1.0 - report.excessiveLeaps /
        static_cast<double>(std::max<std::size_t>(1, melodicComparisons)) * 2.0);

    const auto mean = std::accumulate(velocities.begin(), velocities.end(), 0.0) /
                      std::max<std::size_t>(1, velocities.size());
    auto variance = 0.0;
    for (const auto value : velocities) variance += (value - mean) * (value - mean);
    const auto deviation = std::sqrt(variance / std::max<std::size_t>(1, velocities.size()));
    report.dynamicShape = clampScore(deviation / 16.0);

    auto crowdedBars = std::size_t{};
    const auto voiceCeiling = plan.productionLanguage.domain == ProductionDomain::ClubElectronic ? 9 : 11;
    for (const auto& voices : voicesByBar)
        if (voices.size() > static_cast<std::size_t>(voiceCeiling)) ++crowdedBars;
    report.registerClarity = clampScore(1.0 - crowdedBars / totalBars);
    report.overall = report.negativeSpace * 0.22 + report.variation * 0.24 +
                     report.voiceIndependence * 0.20 + report.dynamicShape * 0.16 +
                     report.registerClarity * 0.18;
    return report;
}

MusicalQualityReport MusicalCritic::reviewAndRefine(Pattern& pattern, const SongPlan& plan) {
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.voice != right.voice) return left.voice < right.voice;
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        return left.pitch < right.pitch;
    });

    auto repaired = std::size_t{};
    for (std::size_t index = 1; index < pattern.notes.size(); ++index) {
        auto& previous = pattern.notes[index - 1];
        auto& current = pattern.notes[index];
        if (previous.voice != current.voice || !needsMonophonicSpace(current.voice)) continue;
        if (previous.endBeat() > current.startBeat - 0.025) {
            previous.durationBeats = std::max(0.06, current.startBeat - previous.startBeat - 0.025);
            ++repaired;
        }
    }

    std::map<std::pair<int, VoiceId>, int> onsets;
    auto removed = std::size_t{};
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
        if (note.authoredTiming || aiAuthored(note)) return false;
        if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) ||
            note.voice == VoiceId::Transitions || note.voice == VoiceId::Atmosphere)
            return false;
        const auto bar = barFor(note, plan);
        auto& count = onsets[{bar, note.voice}];
        const auto maximum = isVoiceInFamily(note.voice, VoiceFamily::Melodic) ? 4 :
                             note.voice == VoiceId::HarmonicPulse ? 8 : 6;
        if (count++ < maximum) return false;
        ++removed;
        return true;
    }), pattern.notes.end());

    const auto literalRhythmBarsVaried = varyLiteralRhythmRuns(pattern, plan);

    // Expression curves are part of the score, not random playback jitter. They let a receiving
    // instrument shape a phrase even when its note-on grid remains exact.
    for (const auto& section : plan.sections) {
        const auto sectionStart = section.startBar * plan.beatsPerBar;
        const auto sectionEnd = (section.startBar + section.bars) * plan.beatsPerBar;
        for (const auto voice : {VoiceId::SubBass, VoiceId::MovementBass, VoiceId::Lead,
                                 VoiceId::Countermelody, VoiceId::HarmonicFoundation,
                                 VoiceId::HarmonicUpper}) {
            if (std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) ==
                section.activeVoices.end()) continue;
            const auto channel = voiceDefinition(voice).midiChannel;
            const auto base = std::clamp(static_cast<int>(52 + section.energy * 52), 1, 127);
            pattern.controls.push_back({sectionStart, 11, std::max(1, base - 10), channel, voice});
            pattern.controls.push_back({sectionStart + (sectionEnd - sectionStart) * 0.58,
                                        11, std::min(127, base + 8), channel, voice});
            pattern.controls.push_back({std::max(sectionStart, sectionEnd - 0.125),
                                        11, std::max(1, base - 5), channel, voice});
        }
    }

    auto report = review(pattern, plan);
    report.overlapsRepaired = repaired;
    report.densityEventsRemoved = removed;
    report.literalRhythmBarsVaried = literalRhythmBarsVaried;
    return report;
}

} // namespace pulso
