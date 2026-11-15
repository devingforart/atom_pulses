#pragma once
#include <juce_core/juce_core.h>
#include <set>

namespace pulso::plugin::club_support {
// Cast availability is not a command to sustain two pads over a fixed share
// of the song. Musical entries, withdrawals and notes remain AI decisions.
inline bool validate(const juce::var& macro,const juce::var& manifest,juce::String& error) {
    std::set<juce::String> sections;
    if(const auto* rows=macro.getProperty("sections",{}).getArray())
        for(const auto& row:*rows) sections.insert(row.getProperty("name",{}).toString());
    if(const auto* owners=manifest.getProperty("instruments",{}).getArray())
        for(const auto& owner:*owners) {
            if(owner.getProperty("source_voice",{}).toString()!="harmonic_foundation" ||
               owner.getProperty("orchestral_function",{}).toString()!="foundation" ||
               !owner.getProperty("role",{}).toString().containsIgnoreCase("primary_chord_bed")) continue;
            if(const auto* active=owner.getProperty("active_sections",{}).getArray())
                for(const auto& name:*active) if(sections.contains(name.toString())) return true;
        }
    error="Club cast has no primary harmonic foundation available in a declared section. "
        "Keep one real primary_chord_bed (harmonic_foundation, foundation) with valid active_sections. "
        "A second pad and fixed-duration harmonic coverage are not required; no notes were added.";
    return false;
}
}
