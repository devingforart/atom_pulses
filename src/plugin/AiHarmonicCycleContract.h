#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <cmath>

namespace pulso::plugin::harmonic_cycle {
inline void shortPassageSchema(juce::DynamicObject& sections,int bars,bool club) {
    if(!club||bars<1||bars>16) return;
    sections.setProperty("minItems",1);
    sections.setProperty("maxItems",2);
    auto props=sections.getProperty("items").getProperty("properties",{});
    props.getProperty("name",{}).getDynamicObject()->setProperty("minLength",1);
    props.getProperty("harmonic_events",{}).getDynamicObject()->setProperty("minItems",1);
}
inline juce::String planningBrief() {
    return "\nHARMONIC SCHEDULE: Choose harmonic_schedule independently for each section. "
        "mode=explicit has no cycle fields: harmonic_events is the COMPLETE section-local "
        "timeline, including all intended returns, or a deliberate held chord. "
        "mode=repeat requires positive cycle_bars and repeat_count. repeat_count counts "
        "TOTAL performances of the cycle including its first appearance, not additional "
        "repeats and never global bar numbers. Their product must fit this section. "
        "For a 16-bar section, a 4-bar cycle performed four times uses repeat_count=4. "
        "Write first-cycle events; any events after cycle_bars*repeat_count are an "
        "explicit transition/coda. Choose explicit if later passes change the progression. "
        "Both modes start their harmony at local bar 0. These are chord-map instructions, "
        "not MIDI loops: every voice's notes remain independently composed by you. "
        "The layer-development horizon is not the harmonic-cycle duration.\n";
}
// Compile an AI-authored harmonic repeat sign into a time map, never MIDI notes.
// Idempotent: checkpoints may already contain the fully expanded map.
inline bool resolve(juce::var& root, int beatsPerBar, juce::String& error) {
    auto* sections = root.getProperty("sections", {}).getArray();
    if (!sections) { error = "Harmonic contract needs sections"; return false; }
    for (auto& value : *sections) {
        auto* s = value.getDynamicObject();
        if (!s) continue;
        const bool tagged=s->hasProperty("harmonic_schedule");
        if (tagged) {
            const auto schedule=s->getProperty("harmonic_schedule");
            const auto mode=schedule.getProperty("mode", {}).toString();
            if (mode=="explicit") {
                if (schedule.hasProperty("cycle_bars") || schedule.hasProperty("repeat_count")) {
                    error="Explicit harmonic schedule must not declare repeat fields"; return false;
                }
                s->setProperty("harmonic_cycle_bars",0);
                s->setProperty("harmonic_cycle_repetitions",0);
                s->setProperty("harmonic_cycle_until_bar",0);
            } else if (mode=="repeat") {
                const auto length=schedule.getProperty("cycle_bars", {});
                const auto count=schedule.getProperty("repeat_count", {});
                if (!length.isInt() || !count.isInt() || static_cast<int>(length)<1 || static_cast<int>(count)<1) {
                    error="Repeated harmonic schedule needs positive integer cycle_bars and repeat_count"; return false;
                }
                s->setProperty("harmonic_cycle_bars",length);
                s->setProperty("harmonic_cycle_repetitions",count);
            } else { error="Unknown harmonic schedule mode"; return false; }
        }
        if (!s->hasProperty("harmonic_cycle_bars")) continue; // legacy plans unchanged
        const int bars = s->getProperty("bars"), cycle = s->getProperty("harmonic_cycle_bars");
        const int repetitions = s->getProperty("harmonic_cycle_repetitions");
        const bool counted = s->hasProperty("harmonic_cycle_repetitions");
        if (counted && (repetitions < 0 || repetitions > 512 || cycle < 0 || cycle > 512)) {
            error = "Harmonic cycle length/count outside supported section range"; return false;
        }
        const int until = counted ? cycle * repetitions : static_cast<int>(s->getProperty("harmonic_cycle_until_bar"));
        auto fail = [&](const juce::String& why) { error = "Harmonic cycle in " + s->getProperty("name").toString() + ": " + why; return false; };
        const bool explicitSchedule=cycle == 0 && until == 0 && (!counted || repetitions == 0);
        if (explicitSchedule && !tagged) continue; // legacy through-composed checkpoint
        if (bars < 1 || bars > 512 || (!explicitSchedule && (cycle < 1 || cycle > bars || until < cycle || until > bars || until % cycle != 0)))
            return fail("cycle length times repetition count must fit this section (bars=" + juce::String(bars) + ", cycle=" + juce::String(cycle) + ", count=" + juce::String(repetitions) + "). Counts are NOT global bar numbers.");
        const auto* events = s->getProperty("harmonic_events").getArray();
        if (!events || events->isEmpty() || events->size() > 32768) return fail("missing or oversized cycle harmony");
        std::map<double, juce::var> supplied, expanded;
        for (const auto& e : *events) {
            const auto* o = e.getDynamicObject();
            if (!o) return fail("invalid harmonic event");
            const int bar = o->getProperty("bar_offset");
            const double beat = o->getProperty("beat_offset");
            if (bar < 0 || bar >= bars || !std::isfinite(beat) || beat < 0 || beat >= beatsPerBar || o->getProperty("chord_id").toString().isEmpty())
                return fail("event outside its section or beat range");
            if (!supplied.emplace(bar * beatsPerBar + beat, e).second) return fail("duplicate harmonic arrival");
        }
        if (!supplied.count(0)) return fail("first cycle must start at bar 0 beat 0");
        if (explicitSchedule) continue; // validate positions, preserve every authored event
        for (const auto& [at, e] : supplied) {
            if (at >= cycle * beatsPerBar) continue;
            for (int start = 0; start < until; start += cycle) {
                if (expanded.size()>=32768) return fail("expanded harmonic map exceeds supported event capacity");
                auto copy = e.clone();
                copy.getDynamicObject()->setProperty("bar_offset", static_cast<int>(e.getProperty("bar_offset", 0)) + start);
                expanded.emplace(at + start * beatsPerBar, copy);
            }
        }
        for (const auto& [at,e] : supplied) {
            if (at >= until * beatsPerBar) expanded.emplace(at,e); // explicit ending/transition, never repeated
            else if (!expanded.count(at) || expanded.at(at).getProperty("chord_id", {}) != e.getProperty("chord_id", {}))
                return fail("explicit event contradicts the declared repeat; use cycle 0 for a varied progression");
        }
        juce::Array<juce::var> timeline;
        for (const auto& [at,e] : expanded) timeline.add(e);
        s->setProperty("harmonic_events", timeline);
        s->setProperty("harmonic_cycle_until_bar", until); // derived section-local boundary for existing consumers
    }
    return true;
}
}
