#pragma once
#include "AiFoundation.h"
#include <cmath>
#include <cstdlib>

namespace pulso::plugin::mono_events {
inline bool eligible(const juce::var& root,const juce::String& id) {
    if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
        if(layer.getProperty("instrument_id",{}).toString()==id)
            return layer.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm"&&
                layer.getProperty("timing_mode",{}).toString()=="cycle_bound"&&
                layer.hasProperty("polyphonic")&&!bool(layer.getProperty("polyphonic",true));
    return false;
}
// One value per AI-declared permitted onset. A null value is an authored rest.
// This is serialization of choices, not a host-authored onset or pattern.
inline juce::var schema(const juce::var& array) {
    auto events=juce::JSON::parse(R"({"type":"object","properties":{},"required":[],"additionalProperties":false})");
    const auto* choices=array.getProperty("items",{}).getProperty("anyOf",{}).getArray();
    const bool silent=array.hasProperty("maxItems")&&int(array.getProperty("maxItems",0))==0;
    if(!silent&&!choices) return {}; // Keep the legacy codec if no bounded clock exists.
    if(!silent) for(const auto& choice:*choices) {
        const auto beat=choice.getProperty("properties",{}).getProperty("beat",{}).getProperty("enum",{})[0];
        if(!(beat.isInt()||beat.isInt64()||beat.isDouble())) return {};
        const auto key=juce::String(double(beat),2);
        auto note=choice.clone();
        note.getProperty("properties",{}).getDynamicObject()->removeProperty("beat");
        note.getProperty("required",{}).getArray()->removeAllInstancesOf(juce::var("beat"));
        auto nullable=new juce::DynamicObject();
        nullable->setProperty("anyOf",juce::Array<juce::var>{note,juce::JSON::parse(R"({"type":"null"})")});
        events.getProperty("properties",{}).getDynamicObject()->setProperty(juce::Identifier(key),juce::var(nullable));
        events.getProperty("required",{}).getArray()->add(key);
    }
    auto root=juce::JSON::parse(R"({"type":"object","required":["events"],"properties":{},"additionalProperties":false})");
    root.getProperty("properties",{}).getDynamicObject()->setProperty("events",events);
    root.getDynamicObject()->setProperty("description","One note or null rest per permitted LOCAL beat key. Never return two notes at one key. Keys are timing, not note indices. Do not repeat the pattern beyond this supplied window.");
    return root;
}
inline bool decode(const juce::var& supplied,juce::Array<juce::var>& notes,juce::String& error) {
    notes.clear();
    const auto fail=[&](){error="Invalid monophonic events: use canonical local beat keys and one duration/pitch/velocity object or null per key";return false;};
    const auto* root=supplied.getDynamicObject();
    const auto events=supplied.getProperty("events",{});
    const auto* object=events.getDynamicObject();
    if(!root||supplied.isArray()||root->getProperties().size()!=1||!object||events.isArray()) return fail();
    for(const auto& entry:object->getProperties()) {
        const auto key=entry.name.toString();const auto bytes=key.toRawUTF8();char* end=nullptr;
        const double beat=std::strtod(bytes,&end);
        if(end==bytes||*end!='\0'||!std::isfinite(beat)||beat<0||juce::String(beat,2)!=key) return fail();
        if(entry.value.isVoid()) continue;
        const auto* value=entry.value.getDynamicObject();
        if(!value||entry.value.isArray()||value->getProperties().size()!=3||
           !entry.value.hasProperty("duration")||!entry.value.hasProperty("pitch")||!entry.value.hasProperty("velocity")) return fail();
        auto note=entry.value.clone();note.getDynamicObject()->setProperty("beat",beat);notes.add(note);
    }
    return true;
}
}
