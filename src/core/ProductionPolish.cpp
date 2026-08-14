#include "ProductionPolish.h"

#include "Orchestration.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace pulso {
namespace {

struct Span { double start{}; double end{}; };

using EventOwner = std::pair<std::uint16_t, VoiceId>;

std::map<EventOwner, std::vector<Span>> soundingSpans(const Pattern& pattern, double gap) {
    std::map<EventOwner, std::vector<Span>> result;
    for (const auto& note : pattern.notes) {
        result[{note.partId, note.voice}].push_back({note.startBeat, note.endBeat()});
        if (note.partId != 0)
            result[{static_cast<std::uint16_t>(0), note.voice}].push_back({note.startBeat, note.endBeat()});
    }
    for (auto& [owner, spans] : result) {
        (void) owner;
        std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        std::vector<Span> merged;
        for (const auto& span : spans) {
            if (merged.empty() || span.start > merged.back().end + gap)
                merged.push_back(span);
            else
                merged.back().end = std::max(merged.back().end, span.end);
        }
        spans = std::move(merged);
    }
    return result;
}

template <typename Event>
const std::vector<Span>* spansFor(const std::map<EventOwner, std::vector<Span>>& spans,
                                  const Event& event) {
    if (const auto exact = spans.find({event.partId, event.voice}); exact != spans.end())
        return &exact->second;
    if (event.partId == 0) {
        if (const auto voice = spans.find({static_cast<std::uint16_t>(0), event.voice});
            voice != spans.end()) return &voice->second;
    }
    return nullptr;
}

template <typename Event, typename Key, typename Value>
void reduceCurves(std::vector<Event>& events, const std::map<EventOwner, std::vector<Span>>& spans,
                  std::size_t maximum, Key key, Value value, std::size_t& silentRemoved) {
    std::sort(events.begin(), events.end(), [&](const auto& a, const auto& b) {
        if (key(a) != key(b)) return key(a) < key(b);
        return a.beat < b.beat;
    });
    std::vector<Event> reduced;
    for (std::size_t first = 0; first < events.size();) {
        auto last = first + 1;
        while (last < events.size() && key(events[last]) == key(events[first])) ++last;
        const auto* active = spansFor(spans, events[first]);
        if (active == nullptr) {
            silentRemoved += last - first;
            first = last;
            continue;
        }
        std::size_t retainedInStream{};
        for (const auto& phrase : *active) {
            std::vector<Event> audible;
            for (auto index = first; index < last; ++index)
                if (events[index].beat >= phrase.start - 0.001 &&
                    events[index].beat <= phrase.end + 0.125)
                    audible.push_back(events[index]);
            if (audible.empty()) continue;
            retainedInStream += audible.size();
            std::vector<Event> meaningful;
            meaningful.push_back(audible.front());
            for (std::size_t index = 1; index + 1 < audible.size(); ++index) {
                const auto timeChanged = audible[index].beat - meaningful.back().beat >= 1.0;
                const auto valueChanged = std::abs(value(audible[index]) - value(meaningful.back())) >= 5;
                if (timeChanged && valueChanged) meaningful.push_back(audible[index]);
            }
            if (audible.size() > 1 && audible.back().beat > meaningful.back().beat + 0.001)
                meaningful.push_back(audible.back());
            if (meaningful.size() > maximum) {
                std::vector<Event> sampled;
                sampled.reserve(maximum);
                for (std::size_t index = 0; index < maximum; ++index) {
                    const auto source = index * (meaningful.size() - 1) / (maximum - 1);
                    sampled.push_back(meaningful[source]);
                }
                meaningful = std::move(sampled);
            }
            reduced.insert(reduced.end(), meaningful.begin(), meaningful.end());
        }
        silentRemoved += (last - first) - std::min(last - first, retainedInStream);
        first = last;
    }
    events = std::move(reduced);
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
}

} // namespace

std::size_t ProductionPolish::enforceMetricContract(Pattern& pattern, int onsetStepsPerBeat,
                                                    int releaseStepsPerBeat) {
    const auto onset = 1.0 / std::max(1, onsetStepsPerBeat);
    const auto release = 1.0 / std::max(1, releaseStepsPerBeat);
    std::size_t repaired{};
    for (auto& note : pattern.notes) {
        if (note.authoredTiming) continue;
        const auto start = std::round(note.startBeat / onset) * onset;
        const auto end = std::round(note.endBeat() / release) * release;
        if (std::abs(start - note.startBeat) > 0.000001 || std::abs(end - note.endBeat()) > 0.000001) ++repaired;
        note.startBeat = std::clamp(start, 0.0, std::max(0.0, pattern.lengthBeats - release));
        note.durationBeats = std::max(release, std::min(pattern.lengthBeats, end) - note.startBeat);
    }
    return repaired;
}

ExpressionCompactionReport ProductionPolish::compactExpression(Pattern& pattern, double phraseGapBeats,
                                                               std::size_t maxPointsPerPhrase) {
    ExpressionCompactionReport report{pattern.controls.size(), 0, pattern.expressions.size(), 0, 0};
    const auto spans = soundingSpans(pattern, phraseGapBeats);
    auto setup = std::vector<ControlEvent>{};
    auto curves = std::vector<ControlEvent>{};
    for (const auto& event : pattern.controls) {
        if ((event.controller == 6 || event.controller == 38 || event.controller == 100 ||
             event.controller == 101) && event.beat <= 0.001)
            setup.push_back(event);
        else curves.push_back(event);
    }
    reduceCurves(curves, spans, std::max<std::size_t>(2, maxPointsPerPhrase),
        [](const auto& e) { return std::tuple{e.partId, e.voice, e.channel, e.controller}; },
        [](const auto& e) { return e.value; }, report.silentEventsRemoved);
    setup.insert(setup.end(), curves.begin(), curves.end());
    pattern.controls = std::move(setup);
    reduceCurves(pattern.expressions, spans, std::max<std::size_t>(2, maxPointsPerPhrase),
        [](const auto& e) { return std::tuple{e.partId, e.voice, e.channel, e.type, e.note}; },
        [](const auto& e) { return e.value; }, report.silentEventsRemoved);
    report.controlsAfter = pattern.controls.size();
    report.expressionsAfter = pattern.expressions.size();
    return report;
}

ProductionAuditReport ProductionPolish::audit(const Pattern& pattern, const TonalAuditReport& tonal,
                                               double beatsPerBar, double registerClarity,
                                               double familyBalance) {
    ProductionAuditReport report;
    report.unsupportedChromaticNotes = tonal.unsupportedChromaticNotes;
    report.strongNonChordNotes = tonal.strongNonChordNotes;
    report.invalidSustains = tonal.invalidSustains;
    report.unintendedHarshOverlaps = tonal.unintendedHarshOverlaps;
    report.registerClarity = std::clamp(registerClarity, 0.0, 1.0);
    report.familyBalance = std::clamp(familyBalance, 0.0, 1.0);
    using RhythmSignature = std::vector<std::tuple<int, int, int>>;
    std::map<std::pair<VoiceId, int>, RhythmSignature> rhythmByBar;
    for (const auto& note : pattern.notes) {
        if (!note.authoredTiming && std::abs(note.startBeat * 4.0 - std::round(note.startBeat * 4.0)) > 0.00001)
            ++report.metricViolations;
        if (note.durationBeats <= 0.0 || note.endBeat() > pattern.lengthBeats + 0.001) ++report.unsafeDurations;
        if (note.partId != 0 && std::none_of(pattern.parts.begin(), pattern.parts.end(),
            [&](const auto& part) { return part.id == note.partId; })) ++report.orphanEvents;
        if (isVoiceInFamily(note.voice, VoiceFamily::Rhythm) && note.voice != VoiceId::CoreDrums) {
            const auto bar = static_cast<int>(std::floor(note.startBeat / beatsPerBar));
            rhythmByBar[{note.voice, bar}].emplace_back(
                static_cast<int>(std::lround((note.startBeat - bar * beatsPerBar) * 96.0)),
                note.pitch, static_cast<int>(std::lround(note.durationBeats * 96.0)));
        }
    }
    auto rhythmBars = std::size_t{};
    for (auto voiceIndex = std::size_t{}; voiceIndex < static_cast<std::size_t>(VoiceId::Count); ++voiceIndex) {
        const auto voice = static_cast<VoiceId>(voiceIndex);
        if (!isVoiceInFamily(voice, VoiceFamily::Rhythm) || voice == VoiceId::CoreDrums) continue;
        RhythmSignature previous;
        auto run = std::size_t{};
        const auto bars = static_cast<int>(std::ceil(pattern.lengthBeats / beatsPerBar));
        for (auto bar = 0; bar < bars; ++bar) {
            auto signature = rhythmByBar[{voice, bar}];
            std::sort(signature.begin(), signature.end());
            if (signature.empty()) { previous.clear(); run = 0; continue; }
            ++rhythmBars;
            if (signature == previous) { ++run; ++report.literalRhythmBars; }
            else run = 1;
            report.maximumRhythmRun = std::max(report.maximumRhythmRun, run);
            previous = std::move(signature);
        }
    }
    report.rhythmRepeatRatio = static_cast<double>(report.literalRhythmBars) /
        std::max<std::size_t>(1, rhythmBars);
    report.expressionEvents = pattern.controls.size() + pattern.expressions.size();
    report.expressionEventsPerNote = static_cast<double>(report.expressionEvents) /
        std::max<std::size_t>(1, pattern.notes.size());
    // Publication is blocked only by corrupt/unsafe MIDI or unresolved tonal collisions.
    // Musical-quality observations are valuable critic feedback, not grounds for deleting a
    // complete song. A scale tone over a chord, a repeated house ostinato, a chamber section
    // with fewer active families or a dense-but-valid expression curve can all be intentional.
    const auto blocking = tonal.unsupportedChromaticNotes + tonal.invalidSustains +
        tonal.unintendedHarshOverlaps + report.metricViolations + report.unsafeDurations +
        report.orphanEvents;
    const auto nonChordRatio = static_cast<double>(tonal.strongNonChordNotes) /
        std::max(1, tonal.pitchedNotes);
    report.score = std::clamp(1.0 - std::min(0.48, blocking * 0.08) - nonChordRatio * 0.18 -
        report.rhythmRepeatRatio * 0.12 - std::max(0.0, 0.70 - report.registerClarity) * 0.55 -
        std::max(0.0, 0.45 - report.familyBalance) * 0.35 -
        std::max(0.0, report.expressionEventsPerNote - 8.0) * 0.01, 0.0, 1.0);
    report.ready = blocking == 0;
    return report;
}

void ProductionPolish::stamp(Pattern& pattern, const ProductionAuditReport& report) {
    pattern.productionAuditPerformed = true;
    pattern.productionReady = report.ready;
    pattern.productionScore = report.score;
    pattern.productionIssues.clear();
    if (report.metricViolations) pattern.productionIssues.push_back("metric_contract");
    if (report.unsafeDurations) pattern.productionIssues.push_back("unsafe_note_duration");
    if (report.orphanEvents) pattern.productionIssues.push_back("orphan_part_event");
    if (report.unsupportedChromaticNotes) pattern.productionIssues.push_back("unsupported_chromatic_note");
    if (report.invalidSustains) pattern.productionIssues.push_back("invalid_harmonic_sustain");
    if (report.unintendedHarshOverlaps) pattern.productionIssues.push_back("unintended_harsh_overlap");
    if (report.strongNonChordNotes) pattern.productionIssues.push_back("warning:non_chord_tension");
    if (report.maximumRhythmRun > 4) pattern.productionIssues.push_back("warning:repeated_rhythm_run");
    if (report.registerClarity < 0.70) pattern.productionIssues.push_back("warning:register_clarity");
    if (report.familyBalance < 0.45) pattern.productionIssues.push_back("warning:family_balance");
    if (report.expressionEventsPerNote > 12.0) pattern.productionIssues.push_back("warning:expression_density");
    if (!report.ready && pattern.productionIssues.empty()) pattern.productionIssues.push_back("unknown_integrity_failure");
}

} // namespace pulso
