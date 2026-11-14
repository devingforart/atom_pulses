#pragma once
#include <juce_core/juce_core.h>

namespace pulso::plugin::section_trajectory {
inline juce::var schema(const juce::var& entry,int sections) {
    auto result=juce::JSON::parse(R"({"type":"object","properties":{},"required":[],"additionalProperties":false})");
    result.getDynamicObject()->setProperty("description","Exactly one energy/density summary per existing macro section. Musical phases inside one section belong in its layer_change/transition text, not duplicate entries or invented sections.");
    for(int i=0;i<sections;++i) {
        auto row=entry.clone();
        row.getProperty("properties",{}).getDynamicObject()->removeProperty("section_index");
        row.getProperty("required",{}).getArray()->removeAllInstancesOf(juce::var("section_index"));
        const auto key="section_"+juce::String(i);
        result.getProperty("properties",{}).getDynamicObject()->setProperty(juce::Identifier(key),row);
        result.getProperty("required",{}).getArray()->add(key);
    }
    return result;
}
// Decode field identity, never merge phases, average energy or invent a row.
// The canonical persisted representation remains the historical indexed array.
inline bool decode(const juce::String& original,int sections,juce::String& decoded,juce::String& error) {
    error.clear();auto root=juce::JSON::parse(original);
    if(!root.hasProperty("energy_trajectory")||root.getProperty("energy_trajectory",{}).isArray()) {
        decoded=original;return true;
    }
    const auto trajectory=root.getProperty("energy_trajectory",{});
    const auto* object=trajectory.getDynamicObject();
    if(!object||sections<1||object->getProperties().size()!=sections) {
        error="Club energy trajectory encoding must provide one keyed summary per actual section";return false;
    }
    juce::Array<juce::var> rows;
    for(int i=0;i<sections;++i) {
        const auto key="section_"+juce::String(i);
        auto row=trajectory.getProperty(juce::Identifier(key),{}).clone();
        if(!row.isObject()||row.isArray()||row.hasProperty("section_index")) {
            error="Club energy trajectory encoding has missing or conflicting "+key;return false;
        }
        row.getDynamicObject()->setProperty("section_index",i);rows.add(row);
    }
    root.getDynamicObject()->setProperty("energy_trajectory",rows);
    decoded=juce::JSON::toString(root);return true;
}
}
