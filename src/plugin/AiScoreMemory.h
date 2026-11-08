#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace pulso::plugin::scoreMemory {

// Dedicated phrase evidence prevents a dense accompaniment from consuming the
// foreground's context. It describes authored MIDI, never scores or edits it.
inline juce::var sceneEvidence(const SongPlan& plan, const Pattern& midi,
    std::size_t sectionIndex, double beforeBeat) {
    juce::var result(new juce::DynamicObject());
    result.getDynamicObject()->setProperty("note_format", "absoluteBeat,pitch,duration,velocity");
    if (sectionIndex >= plan.sections.size() || !std::isfinite(beforeBeat)) return result;
    const auto architecture = juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* scenes = architecture.getProperty("ensemble_scenes", {}).getArray();
    juce::Array<juce::var> voices;
    const auto& section = plan.sections[sectionIndex];
    const double begin = section.startBar * plan.beatsPerBar;
    const double end = std::min(beforeBeat, double(section.startBar + section.bars) * plan.beatsPerBar);
    result.getDynamicObject()->setProperty("section_index", static_cast<int>(sectionIndex));
    result.getDynamicObject()->setProperty("observed_until_beat", end);
    if (scenes) for (const auto& scene : *scenes) {
        if (static_cast<int>(scene.getProperty("section_index", -1)) != static_cast<int>(sectionIndex)) continue;
        if (const auto* commitments = scene.getProperty("voices", {}).getArray())
            for (const auto& commitment : *commitments) {
                if (voices.size() >= 12) break;
                const auto id = commitment.getProperty("owner_id", {}).toString();
                for (std::size_t owner = 0; owner < plan.instruments.size(); ++owner) {
                    if (id != juce::String(plan.instruments[owner].id)) continue;
                    std::vector<const NoteEvent*> notes;
                    for (const auto& n : midi.notes)
                        if (n.partId == owner + 1 && n.startBeat < end && n.endBeat() > begin && n.durationBeats > 0)
                            notes.push_back(&n);
                    std::stable_sort(notes.begin(), notes.end(), [](auto a, auto b) {
                        return a->startBeat < b->startBeat;
                    });
                    juce::var voice(new juce::DynamicObject());
                    voice.getDynamicObject()->setProperty("owner", id);
                    voice.getDynamicObject()->setProperty("commitment", commitment);
                    juce::Array<juce::var> excerpt;
                    for (std::size_t i = 0; i < notes.size(); ++i) {
                        // Beginning AND continuation, rather than only the first attacks.
                        if (i >= 8 && notes.size() - i > 16) continue;
                        const auto& n = *notes[i];
                        excerpt.add(juce::Array<juce::var>{n.startBeat, n.pitch, n.durationBeats, n.velocity});
                    }
                    voice.getDynamicObject()->setProperty("opening_and_latest_notes", excerpt);
                    voice.getDynamicObject()->setProperty("omitted_notes", static_cast<int>(notes.size()) - excerpt.size());
                    // A returning voice may have rested for an entire scene. Keep
                    // its last actual utterance available instead of asking the AI
                    // to invent a response from its role label. Read-only, bounded,
                    // and explicit about omissions: this is not a detected phrase.
                    std::vector<const NoteEvent*> prior;
                    for (const auto& n : midi.notes)
                        if (n.partId == owner + 1 && n.startBeat < begin &&
                            n.startBeat < end && n.durationBeats > 0) prior.push_back(&n);
                    std::stable_sort(prior.begin(), prior.end(), [](auto a, auto b) {
                        return a->startBeat < b->startBeat;
                    });
                    juce::Array<juce::var> tail;
                    const auto first = prior.size() > 12 ? prior.size() - 12 : 0;
                    for (auto i = first; i < prior.size(); ++i) {
                        const auto& n = *prior[i];
                        tail.add(juce::Array<juce::var>{n.startBeat, n.pitch, n.durationBeats, n.velocity});
                    }
                    voice.getDynamicObject()->setProperty("latest_preceding_notes", tail);
                    voice.getDynamicObject()->setProperty("omitted_preceding_notes", static_cast<int>(first));
                    voices.add(voice);
                }
            }
    }
    result.getDynamicObject()->setProperty("voices", voices);
    result.getDynamicObject()->setProperty("scope", "bounded observed notes; omissions are not rests; no quality verdict");
    return result;
}

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

// Descriptive candidates for the AI critic, never an accept/reject score.
// Only named melodic-arc owners: chordal unisons must not be treated as solo errors.
inline juce::var articulationEvidence(const SongPlan& plan, const Pattern& midi) {
    juce::var result(new juce::DynamicObject());
    juce::Array<juce::var> examples;
    const auto architecture = juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* arcs = architecture.getProperty("musical_arcs", {}).getArray();
    std::set<std::string> ids;
    if (arcs) for (const auto& arc : *arcs)
        ids.insert(arc.getProperty("foreground_id", {}).toString().toStdString());
    int candidates = 0;
    for (std::size_t owner = 0; owner < plan.instruments.size(); ++owner) {
        if (!ids.contains(plan.instruments[owner].id)) continue;
        std::vector<const NoteEvent*> notes;
        for (const auto& n : midi.notes)
            if (n.partId == owner + 1 && n.durationBeats > 0) notes.push_back(&n);
        std::stable_sort(notes.begin(), notes.end(), [](auto a, auto b) {
            return a->startBeat < b->startBeat;
        });
        const auto add = [&](const char* kind, const NoteEvent& a, const NoteEvent& b) {
            ++candidates;
            if (examples.size() >= 32) return;
            juce::var item(new juce::DynamicObject());
            item.getDynamicObject()->setProperty("owner", juce::String(plan.instruments[owner].id));
            item.getDynamicObject()->setProperty("kind", kind);
            item.getDynamicObject()->setProperty("first", juce::Array<juce::var>{a.startBeat,a.pitch,a.durationBeats});
            item.getDynamicObject()->setProperty("next", juce::Array<juce::var>{b.startBeat,b.pitch,b.durationBeats});
            examples.add(item);
        };
        std::array<const NoteEvent*, 128> previousPitch{};
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const auto& n = *notes[i];
            if (n.pitch >= 0 && n.pitch < 128) {
                const auto* previous = previousPitch[n.pitch];
                if (previous && n.startBeat > previous->startBeat &&
                    n.startBeat - previous->startBeat <= .5 && previous->endBeat() > n.startBeat + .001)
                    add("close_same_pitch_overlap", *previous, n);
                previousPitch[n.pitch] = &n;
            }
            if (i > 0) {
                const auto& previous = *notes[i - 1];
                const auto gap = n.startBeat - previous.startBeat;
                if (gap > 0 && gap <= 2 && n.durationBeats <= 1 &&
                    std::abs(n.pitch - previous.pitch) >= 12)
                    add("exposed_short_register_leap", previous, n);
            }
        }
    }
    result.getDynamicObject()->setProperty("note_format", "absoluteBeat,pitch,duration");
    result.getDynamicObject()->setProperty("candidates", examples);
    result.getDynamicObject()->setProperty("omitted_candidates", candidates - examples.size());
    result.getDynamicObject()->setProperty("meaning", "observations for contextual musical judgment, not errors or repair instructions");
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
