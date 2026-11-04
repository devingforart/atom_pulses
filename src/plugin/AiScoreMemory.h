#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace pulso::plugin::scoreMemory {

// Read-only evidence. Limits bound prompt cost, never change the composition.
inline juce::var evidence(const SongPlan& plan, const Pattern& midi,
    double begin, double end, const std::vector<std::size_t>& requested) {
    auto* root = new juce::DynamicObject();
    juce::var result(root);
    root->setProperty("kind", "accepted_score_memory_v1");
    root->setProperty("window_start", begin);
    root->setProperty("window_end", end);
    root->setProperty("note_format", "absoluteBeat,pitch,duration,velocity");
    root->setProperty("scope", "bounded evidence, not inferred motifs or aesthetic scores");
    if (!std::isfinite(begin) || !std::isfinite(end) || end <= begin || plan.beatsPerBar <= 0) {
        root->setProperty("invalid_window", true); return result;
    }
    const auto architecture = juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* arcs = architecture.getProperty("musical_arcs", {}).getArray();
    std::vector<std::size_t> selected;
    const auto add = [&](std::size_t owner) {
        if (owner < plan.instruments.size() &&
            std::find(selected.begin(), selected.end(), owner) == selected.end())
            selected.push_back(owner);
    };
    // Requested owners and named arc participants take priority over background.
    for (const auto owner : requested) add(owner);
    if (arcs) for (const auto& arc : *arcs) {
        const auto id = arc.getProperty("foreground_id", {}).toString();
        for (std::size_t i = 0; i < plan.instruments.size(); ++i)
            if (id == juce::String(plan.instruments[i].id)) add(i);
    }
    for (std::size_t i = 0; i < plan.instruments.size(); ++i) add(i);
    root->setProperty("omitted_owners", static_cast<int>(selected.size() > 16 ? selected.size() - 16 : 0));
    if (selected.size() > 16) selected.resize(16);
    juce::Array<juce::var> voices;
    int budget = 384;
    for (const auto owner : selected) {
        auto* voice = new juce::DynamicObject();
        voice->setProperty("owner", juce::String(plan.instruments[owner].id));
        std::vector<const NoteEvent*> history;
        for (const auto& note : midi.notes)
            if (note.partId == owner + 1 && note.startBeat < begin && note.durationBeats > 0)
                history.push_back(&note);
        std::sort(history.begin(), history.end(), [](auto a, auto b) {
            if (a->startBeat != b->startBeat) return a->startBeat < b->startBeat;
            return a->pitch < b->pitch;
        });
        voice->setProperty("history_notes", static_cast<int>(history.size()));
        juce::Array<juce::var> excerpts;
        std::set<double> usedStarts;
        const auto excerpt = [&](const char* label, double start, double stop) {
            if (!usedStarts.insert(start).second || stop <= start) return;
            auto* item = new juce::DynamicObject();
            item->setProperty("source", label); item->setProperty("from", start);
            item->setProperty("to", stop);
            juce::Array<juce::var> notes;
            int available = 0;
            for (const auto* n : history) if (n->startBeat < stop && n->endBeat() > start) {
                ++available;
                if (notes.size() >= 12 || budget <= 0) continue;
                notes.add(juce::Array<juce::var>{n->startBeat, n->pitch, n->durationBeats, n->velocity});
                --budget;
            }
            item->setProperty("notes", notes);
            item->setProperty("omitted_notes", available - notes.size());
            excerpts.add(juce::var(item));
        };
        if (!history.empty()) {
            // Select a previous authored arc, not a fabricated melodic template.
            double antecedent = -1, antecedentEnd = -1;
            if (arcs) for (const auto& arc : *arcs) {
                const auto start = static_cast<double>(arc.getProperty("start_bar", 0)) * plan.beatsPerBar;
                const auto stop = start + static_cast<double>(arc.getProperty("bars", 0)) * plan.beatsPerBar;
                if (arc.getProperty("foreground_id", {}).toString() == juce::String(plan.instruments[owner].id) &&
                    start < begin && start > antecedent) { antecedent = start; antecedentEnd = stop; }
            }
            if (antecedent >= 0)
                excerpt("planned_arc_opening_realized_notes", antecedent,
                    std::min({begin, antecedentEnd, antecedent + 4 * plan.beatsPerBar}));
            excerpt("first_actual_entry", history.front()->startBeat,
                std::min(begin, history.front()->startBeat + 2 * plan.beatsPerBar));
            excerpt("recent_context", std::max(0.0, begin - 2 * plan.beatsPerBar), begin);
        }
        voice->setProperty("excerpts", excerpts);
        voices.add(juce::var(voice));
    }
    root->setProperty("voices", voices);
    // Boundary sonority exposes cross-voice relations, including long carry-ins.
    juce::Array<juce::var> held;
    int heldCount = 0;
    for (const auto& n : midi.notes) if (n.startBeat <= begin && n.endBeat() > begin) {
        ++heldCount;
        if (held.size() >= 96) continue;
        held.add(juce::Array<juce::var>{static_cast<int>(n.partId), n.startBeat,
            n.pitch, n.durationBeats, n.velocity});
    }
    root->setProperty("sounding_at_window_start_partId_beat_pitch_duration_velocity", held);
    root->setProperty("omitted_sounding_notes", heldCount - held.size());
    root->setProperty("history_note_budget", 384);
    return result;
}

inline juce::String brief(const SongPlan& plan, const Pattern& midi,
    double begin, double end, const std::vector<std::size_t>& owners) {
    return "\nACCEPTED SCORE EVIDENCE (read-only): " +
        juce::JSON::toString(evidence(plan, midi, begin, end, owners), false) +
        "\nThese are actual notes, not a new motif prescription. Compare contour, onset "
        "relationships, note lengths and accents before recalling or answering a voice. "
        "An excerpt is NOT necessarily a complete phrase; omitted notes/owners are not "
        "silences. Consult the supplied current-window MIDI and harmonic map for the full "
        "local ensemble. Preserve accepted material; never copy an excerpt automatically "
        "or overwrite the past to satisfy a prose plan. Develop a recognizable relationship "
        "where the musical arc calls for it, and allow deliberate unchanged passages.\n";
}
} // namespace pulso::plugin::scoreMemory
