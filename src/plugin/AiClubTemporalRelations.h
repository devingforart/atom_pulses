#pragma once
#include <limits>

namespace pulso::plugin::club::temporal {

// Guidance and context only: never schedules or changes MIDI events.
inline juce::String brief() {
    return "\nINTERLOCKED TEMPORAL DEVELOPMENT: Distinguish the short recurring pulse, "
        "a connected phrase/answer, and a longer withheld consequence. These are "
        "interacting musical timescales, not mandatory tracks or fixed event periods. "
        "Let a short answer occur inside a longer unresolved arc and a phrase continue "
        "across a section boundary when appropriate. Do not restart every voice's "
        "argument together. Stable hypnotic plateaus remain valuable.\n"
        "Use existing musical_arcs to make these relationships concrete: subject names "
        "the audible gesture and owner; continuation identifies what remains open and "
        "which earlier gesture changes meaning; arrival describes the consequence and "
        "destination; support_response identifies what another owner sustains, anticipates, "
        "answers or withholds. Refer to actual owners and absolute bar locations inside "
        "their permitted activity spans. Arcs may overlap or nest, but do not add arcs "
        "just to fill a template. Let their lengths fit the requested duration. An "
        "anticipation must foreshadow material that really arrives later, not an unrelated "
        "decoration. A return retains audible identity while completing or reinterpreting "
        "it. Preserve the global protagonist while allowing a secondary thought to lead.\n"
        "Choose a few consequential events, not perpetual novelty: an incomplete answer "
        "can become complete on its return; a familiar motif can acquire another harmonic "
        "meaning; a sustained upper voice can foreshadow a later register. These are "
        "possibilities, not a mandatory sequence. Tie selected changes to what was heard "
        "before and what follows. Some voices retain their argument while another changes. "
        "Independence does not require scattered syncopations; preserve a shared metric "
        "reference and intentional simultaneous arrivals. Use spacing, phrasing, register "
        "and harmony, not just more notes or track additions/removals. Complete the main "
        "outstanding promise in the final phrase rather than opening another question at "
        "the file boundary. A requested continuous loop instead needs an open musical wrap.\n";
}

inline juce::String context(const juce::var& root, double begin, double end,
                            double beatsPerBar) {
    const auto* arcs = root.getProperty("musical_arcs", {}).getArray();
    if (!arcs || end <= begin || beatsPerBar <= 0) return {};
    int previous = -1, next = -1;
    double previousEnd = -1, nextStart = std::numeric_limits<double>::max();
    for (int i = 0; i < arcs->size(); ++i) {
        const auto& arc = (*arcs)[i];
        const auto start = static_cast<double>(arc.getProperty("start_bar", 0)) * beatsPerBar;
        const auto stop = start + static_cast<double>(arc.getProperty("bars", 0)) * beatsPerBar;
        if (stop <= begin && stop > previousEnd) { previous = i; previousEnd = stop; }
        if (start >= end && start < nextStart) { next = i; nextStart = start; }
    }
    juce::String out;
    if (previous >= 0)
        out << "\nPRECEDING PLANNED ARGUMENT (memory, not instructions to replay): "
            << juce::JSON::toString((*arcs)[previous], true) << "\n";
    if (next >= 0)
        out << "\nUPCOMING PLANNED ARGUMENT (expectation, not permission to reveal it now): "
            << juce::JSON::toString((*arcs)[next], true) << "\n";
    out << "TEMPORAL CONTINUITY: Compare current arcs to actual accepted notes. Continue "
        "audible identities rather than restarting from prose descriptions. Preserve "
        "pending long-range questions while completing local answers when planned. "
        "Overlapping arcs do not require every owner to sound now. Preserve rests, "
        "cast activity bounds and groove. Do not resolve at every technical boundary, "
        "replay the preceding argument or reveal future material prematurely. Author "
        "only the current interval's notes.\n";
    return out;
}
} // namespace pulso::plugin::club::temporal
