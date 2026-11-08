#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <algorithm>

namespace pulso::plugin::arrivalReview {
// Narrative scores deliberately are not a prerequisite for asking the critic.
inline bool mayCompare(bool technicalIntegrity, bool earlierScenesPreserved, bool required) {
    return technicalIntegrity && earlierScenesPreserved && required;
}
inline juce::String schema() {
    return R"json({"type":"object","additionalProperties":false,
      "required":["prefer_candidate","original_goal_realized","candidate_goal_realized",
        "existing_arrival_preserved","evidence","reason"],"properties":{
        "prefer_candidate":{"type":"boolean"},"original_goal_realized":{"type":"boolean"},
        "candidate_goal_realized":{"type":"boolean"},"existing_arrival_preserved":{"type":"boolean"},
        "evidence":{"type":"string"},"reason":{"type":"string"}}})json";
}
// Fail closed on an unavailable/ambiguous verdict: keep the accepted score.
// No local pitch target, tonic rule, duration quota or MIDI modification.
inline bool preferred(const juce::var& verdict) {
    for (const auto* key : {"prefer_candidate", "original_goal_realized", "candidate_goal_realized", "existing_arrival_preserved"})
        if (!verdict.getProperty(key, {}).isBool()) return false;
    if (verdict.getProperty("evidence", {}).toString().trim().isEmpty() ||
        verdict.getProperty("reason", {}).toString().trim().isEmpty()) return false;
    return static_cast<bool>(verdict.getProperty("prefer_candidate", false)) &&
        static_cast<bool>(verdict.getProperty("candidate_goal_realized", false)) &&
        (!static_cast<bool>(verdict.getProperty("original_goal_realized", false)) ||
         static_cast<bool>(verdict.getProperty("existing_arrival_preserved", false)));
}
inline bool originalValidated(const juce::var& verdict) {
    for (const auto* key : {"prefer_candidate", "original_goal_realized", "candidate_goal_realized", "existing_arrival_preserved"})
        if (!verdict.getProperty(key, {}).isBool()) return false;
    return !static_cast<bool>(verdict.getProperty("prefer_candidate", true)) &&
        static_cast<bool>(verdict.getProperty("original_goal_realized", false)) &&
        verdict.getProperty("evidence", {}).toString().trim().isNotEmpty() &&
        verdict.getProperty("reason", {}).toString().trim().isNotEmpty();
}
inline juce::String context(const SongPlan& plan, const Pattern& midi) {
    juce::String out = "\nARRIVAL CONTRACT: Judge actual melodic destination separately from "
        "tonal accompaniment. A final chord on home harmony does not prove that the "
        "speaker completed its promised phrase. The destination may be modal, open, "
        "a non-tonic chord tone or a resolved earlier arrival followed by coda. Do not "
        "invent a tonic requirement. Read the AI-authored scene/arc intention, exact "
        "last pitches and their harmonic support. Preserve a successful arrival and "
        "its preparation unless actual contextual evidence shows it is wrong. More "
        "notes, a longer solo or a better aggregate score is not an improvement.\n";
    const auto root = juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    out << "PLANNED SCENES: " << juce::JSON::toString(root.getProperty("ensemble_scenes", {}), true)
        << "\nPLANNED ARCS: " << juce::JSON::toString(root.getProperty("musical_arcs", {}), true);
    for (const auto& chord : plan.chordPalette) {
        out << "\nCHORD " << juce::String(chord.id) << " pitch_classes=";
        for (const auto pitch : chord.pitchClasses) out << pitch << ",";
    }
    if (!plan.sections.empty()) {
        const auto& section = plan.sections.back();
        for (const auto& event : section.harmonicEvents)
            out << "\nFINAL HARMONY beat=" << section.startBar * plan.beatsPerBar +
                event.barOffset * plan.beatsPerBar + event.beatOffset << " chord=" << juce::String(event.chordId);
    }
    std::vector<const NoteEvent*> subject;
    for (const auto& note : midi.notes)
        if (note.partId > 0 && note.partId <= plan.instruments.size() &&
            plan.instruments[note.partId - 1].id == plan.narrativeSpine.protagonistInstrumentId)
            subject.push_back(&note);
    std::stable_sort(subject.begin(), subject.end(), [](auto a, auto b) { return a->startBeat < b->startBeat; });
    out << "\nACTUAL SUBJECT FIRST ENTRY (not necessarily song start): ";
    for (std::size_t i = 0; i < std::min(subject.size(), std::size_t{12}); ++i)
        out << subject[i]->startBeat << ":" << subject[i]->pitch << "/" << subject[i]->durationBeats << " ";
    return out + "\n";
}
}
