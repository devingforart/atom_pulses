#pragma once
#include <algorithm>

namespace pulso::plugin::club::delivery {

// AI-authored phrase itinerary + read-only realization evidence. No MIDI edits.
inline void extend(juce::var& arc) {
    arc.getProperty("required", {}).getArray()->add("phrase_route");
    arc.getProperty("properties", {}).getDynamicObject()->setProperty("phrase_route",
        juce::JSON::parse(R"json({"type":"array","minItems":1,"maxItems":6,"items":{
          "type":"object","additionalProperties":false,
          "required":["from_bar","bars","action","material","consequence","material_source"],"properties":{
            "from_bar":{"type":"integer","minimum":0},"bars":{"type":"integer","minimum":1},
            "action":{"type":"string","enum":["statement","continuation","arrival","breath"]},
            "material_source":{"type":"string","enum":["phrase_reference","response_reference","free_continuation","none"]},
            "material":{"type":"string"},"consequence":{"type":"string"}
          }}})json"));
}

inline juce::String planning() {
    return "\nPHRASE DELIVERY: Separate the time spent actually speaking from the longer "
        "expectation left by that speech. An eighteen-bar dramatic debt is NOT necessarily "
        "an eighteen-bar melodic sentence. For each musical_arc author phrase_route, a "
        "compact sequence of contiguous spans covering its bars. from_bar is ARC-LOCAL, "
        "zero based; bars is the span length. Decide the lengths musically, not by writer "
        "windows. Each span says statement/continuation/arrival/breath, actual material "
        "derived from the references, and what its consequence prepares. material_source "
        "identifies the opening phrase, its concrete response, a free continuation or none. "
        "Do not allocate four bars merely to three ascending cue notes unless that economy "
        "is the intended audible identity. Choose natural sentence lengths; the references "
        "now allow up to eight bars of genuine writing. Longer spans can contain related "
        "statements and internal breaths, not just one note at each boundary. Do not "
        "stretch reference timing to occupy the arc or repeat it mechanically. For a "
        "response_reference span actually develop its written answering material, adapting "
        "to the current harmony. Never select a null reference as a source. A breath must "
        "name what remains audible or pending; it is not an excuse to omit an unwritten "
        "continuation. Use few spans, not a prescribed recurring structure. A complete "
        "statement can include internal rests and expressive held notes. A continuation "
        "must develop actual contour/rhythm toward something, not describe an energy level. "
        "Where a voice is assigned a developed thought, reserve enough of its permitted "
        "activity to say it, not only an entry cue and a final long note. Do not require "
        "continuous soloing: a deliberately sparse or hypnotic work can use long breaths "
        "with a concrete musical reason. Keep all route spans within the arc and permitted "
        "cast activity. The route is a composing intention, never a note-density quota.\n";
}

inline bool validate(const juce::var& arc, juce::String& error) {
    if (!arc.hasProperty("phrase_route")) return true; // Historical checkpoints.
    const auto* route = arc.getProperty("phrase_route", {}).getArray();
    if (!route || route->isEmpty() || route->size() > 6) {
        error = "Club phrase route is missing or too large"; return false;
    }
    int cursor = 0;
    const auto arcBars = static_cast<int>(arc.getProperty("bars", 0));
    for (const auto& span : *route) {
        if (span.hasProperty("material_source")) {
            const auto source = span.getProperty("material_source", {}).toString();
            if (!(source == "phrase_reference" || source == "response_reference" ||
                  source == "free_continuation" || source == "none") ||
                ((source == "phrase_reference" || source == "response_reference") &&
                 !arc.getProperty(juce::Identifier(source), {}).isObject())) {
                error = "Club phrase route selects an unavailable reference"; return false;
            }
        }
        const auto from = span.getProperty("from_bar", {});
        const auto bars = span.getProperty("bars", {});
        const auto action = span.getProperty("action", {}).toString();
        if (!(from.isInt() || from.isInt64()) || !(bars.isInt() || bars.isInt64()) ||
            static_cast<int>(from) != cursor || static_cast<int>(bars) <= 0 ||
            static_cast<int>(bars) > arcBars - cursor ||
            !(action == "statement" || action == "continuation" || action == "arrival" || action == "breath") ||
            span.getProperty("material", {}).toString().trim().isEmpty() ||
            span.getProperty("consequence", {}).toString().trim().isEmpty()) {
            error = "Club phrase route has invalid spans or missing musical intention"; return false;
        }
        cursor += static_cast<int>(bars);
    }
    if (cursor != arcBars) { error = "Club phrase route does not cover its arc"; return false; }
    return true;
}

inline juce::String context(const SongPlan& plan, const Pattern& accepted,
    double begin, double end, const std::vector<std::size_t>& owners) {
    if (end <= begin || plan.beatsPerBar <= 0) return {};
    const auto root = juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* arcs = root.getProperty("musical_arcs", {}).getArray();
    if (!arcs) return {};
    juce::String out;
    for (const auto& arc : *arcs) {
        const auto start = static_cast<double>(arc.getProperty("start_bar", 0)) * plan.beatsPerBar;
        const auto finish = start + static_cast<double>(arc.getProperty("bars", 0)) * plan.beatsPerBar;
        if (start >= end || finish <= begin) continue;
        const auto id = arc.getProperty("foreground_id", {}).toString();
        const auto owner = std::find_if(owners.begin(), owners.end(), [&](auto i) {
            return i < plan.instruments.size() && juce::String(plan.instruments[i].id) == id;
        });
        if (owner == owners.end()) continue;
        const auto* route = arc.getProperty("phrase_route", {}).getArray();
        if (!route) continue;
        out << "\nPHRASE DELIVERY EVIDENCE owner=" << id << " (absolute quarter-note beats):\n";
        for (const auto& span : *route) {
            const auto a = start + static_cast<double>(span.getProperty("from_bar", 0)) * plan.beatsPerBar;
            const auto b = a + static_cast<double>(span.getProperty("bars", 0)) * plan.beatsPerBar;
            int attacks = 0; double latestRelease = a;
            for (const auto& note : accepted.notes)
                if (note.partId == *owner + 1 && note.startBeat < begin && note.startBeat < b && note.endBeat() > a) {
                    if (note.startBeat >= a) ++attacks;
                    latestRelease = std::max(latestRelease, std::min(b, note.endBeat()));
                }
            out << "[" << a << "," << b << ") " << span.getProperty("action", {}).toString()
                << " source=" << span.getProperty("material_source", "legacy").toString()
                << " | " << span.getProperty("material", {}).toString()
                << " | consequence=" << span.getProperty("consequence", {}).toString();
            if (a < begin) out << " | prior_attacks=" << attacks << " latest_release=" << latestRelease;
            out << (a < end && b > begin ? " | WRITE_CURRENT_INTERSECTION\n" : b <= begin ? " | PAST_IMMUTABLE\n" : " | FUTURE_DO_NOT_WRITE\n");
        }
    }
    if (out.isNotEmpty()) out << "Continue what was ACTUALLY heard, not a fiction that the prose was already played. "
        "Past counts describe evidence, not quality or a quota. If a prior statement was sparse, "
        "make its currently planned continuation intelligible, without cramming missed notes "
        "into this window, rewriting the past, or advancing the future arrival. Realize each "
        "current span's argument with connected notes and internal breaths, not isolated cues "
        "at span boundaries. Keep the groove and other owners intelligible.\n";
    return out;
}
} // namespace pulso::plugin::club::delivery
