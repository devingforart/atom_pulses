#pragma once
#include "AiClubProductionPlan.h"

namespace pulso::plugin::groove_repair {
inline bool handles(const juce::String& error) {
    return error.startsWith("Club groove hierarchy: declared main accent")||
        error.startsWith("Club groove hierarchy: reference attacks inside")||
        error.startsWith("Club groove hierarchy: invalid or duplicate accent")||
        error.startsWith("Club groove hierarchy: invalid shared attack-space")||
        error.startsWith("Club groove hierarchy: shared attack-space names");
}
inline std::set<juce::String> scope(const juce::var& root) {
    std::set<juce::String> requested,result;
    const auto hierarchy=root.getProperty("groove_hierarchy",{});
    if(const auto* ids=hierarchy.getProperty("identity_owners",{}).getArray())
        for(const auto& id:*ids) requested.insert(id.toString());
    if(const auto* spaces=hierarchy.getProperty("shared_spaces",{}).getArray())
        for(const auto& space:*spaces) if(const auto* ids=space.getProperty("owner_ids",{}).getArray())
            for(const auto& id:*ids) requested.insert(id.toString());
    if(const auto* lanes=root.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray())
        for(const auto& lane:*lanes) {
            const auto id=lane.getProperty("instrument_id",{}).toString();
            if(requested.contains(id)) result.insert(id);
        }
    return result;
}
inline juce::String schema(const SongPlan& plan,const juce::String& original) {
    auto source=juce::JSON::parse(original);const auto allowedOwners=scope(source);
    const double cycle=source.getProperty("tonal_nucleus",{}).getProperty("cycle_beats",0);
    if(allowedOwners.empty()||(cycle!=4&&cycle!=8&&cycle!=16)) return {};
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["lane_updates","accent_offsets","shared_spaces","reason"],"properties":{"lane_updates":{"type":"array","items":{}},"accent_offsets":{},"shared_spaces":{},"reason":{"type":"string"}}})");
    auto container=juce::JSON::parse(R"({"properties":{},"required":[]})");
    groove_hierarchy::extend(container,&plan);
    auto fields=container.getProperty("properties",{}).getProperty("groove_hierarchy",{}).getProperty("properties",{});
    auto properties=root.getProperty("properties",{});
    properties.getDynamicObject()->setProperty("accent_offsets",fields.getProperty("accent_offsets",{}).clone());
    properties.getProperty("accent_offsets",{}).getProperty("items",{}).getDynamicObject()->setProperty("maximum",cycle-.25);
    properties.getDynamicObject()->setProperty("shared_spaces",fields.getProperty("shared_spaces",{}).clone());
    auto space=properties.getProperty("shared_spaces",{}).getProperty("items",{}).getProperty("properties",{});
    space.getProperty("from_beat",{}).getDynamicObject()->setProperty("maximum",cycle-.25);
    space.getProperty("duration_beats",{}).getDynamicObject()->setProperty("maximum",cycle);
    juce::Array<juce::var> ids,choices;
    for(const auto& id:allowedOwners) ids.add(id);
    space.getProperty("owner_ids",{}).getProperty("items",{}).getDynamicObject()->setProperty("enum",ids);
    // Existing attack grammar stays binding. Spaces may be reconciled by AI,
    // so do not bake the currently contradictory spaces into these choices.
    source.getDynamicObject()->removeProperty("groove_hierarchy");
    const auto timing=juce::JSON::toString(source);
    for(const auto& id:allowedOwners) {
        auto lane=juce::JSON::parse(club::nucleus::laneNotesRepairSchema(plan,id,cycle));
        if(!lane.isObject()) return {};
        auto pitch=lane.getProperty("properties",{}).getProperty("notes",{}).getProperty("items",{}).getProperty("properties",{}).getProperty("pitch",{}).clone();
        const auto anchors=foundation::retainedPitches(source,id);
        if(anchors.isArray()&&!anchors.getArray()->isEmpty()) pitch.getDynamicObject()->setProperty("enum",anchors);
        auto note=club::production::boundedNoteSchema(cycle);
        auto* onsets=note.getProperty("anyOf",{}).getArray();
        for(int i=onsets->size();--i>=0;) {
            auto props=(*onsets)[i].getProperty("properties",{});
            const double beat=props.getProperty("beat",{}).getProperty("enum",{})[0];
            if(!club::production::coreAttackAllowed(timing,id.toStdString(),beat)) onsets->remove(i);
            else props.getDynamicObject()->setProperty("pitch",pitch.clone());
        }
        if(onsets->isEmpty()) continue;
        lane.getProperty("properties",{}).getProperty("notes",{}).getDynamicObject()->setProperty("items",note);
        lane.getProperty("required",{}).getArray()->add("instrument_id");
        lane.getProperty("properties",{}).getDynamicObject()->setProperty("instrument_id",
            juce::JSON::parse("{\"type\":\"string\",\"enum\":["+juce::JSON::toString(juce::var(id))+"]}"));
        choices.add(lane);
    }
    if(choices.isEmpty()) return {};
    auto updates=properties.getProperty("lane_updates",{});
    updates.getDynamicObject()->setProperty("maxItems",choices.size());
    updates.getProperty("items",{}).getDynamicObject()->setProperty("anyOf",choices);
    return juce::JSON::toString(root);
}
inline bool apply(const juce::String& original,const juce::String& response,juce::String& merged) {
    const auto repair=juce::JSON::parse(response);const auto* object=repair.getDynamicObject();
    const auto* updates=repair.getProperty("lane_updates",{}).getArray();
    const auto* accents=repair.getProperty("accent_offsets",{}).getArray();
    const auto* spaces=repair.getProperty("shared_spaces",{}).getArray();
    if(!object||!updates||!accents||accents->isEmpty()||!spaces||repair.getProperty("reason",{}).toString().trim().isEmpty()) return false;
    for(const auto& property:object->getProperties()) {
        const auto key=property.name.toString();
        if(key!="lane_updates"&&key!="accent_offsets"&&key!="shared_spaces"&&key!="reason") return false;
    }
    auto candidate=original;const auto allowed=scope(juce::JSON::parse(original));std::set<juce::String> seen;
    for(const auto& update:*updates) {
        const auto id=update.getProperty("instrument_id",{}).toString();juce::String next;
        if(!allowed.contains(id)||!seen.insert(id).second||
           !club::nucleus::replaceLaneNotes(candidate,id,juce::JSON::toString(update),next)) return false;
        candidate=next;
    }
    auto root=juce::JSON::parse(candidate);auto hierarchy=root.getProperty("groove_hierarchy",{});
    if(!hierarchy.isObject()) return false;
    hierarchy.getDynamicObject()->setProperty("accent_offsets",repair.getProperty("accent_offsets",{}));
    hierarchy.getDynamicObject()->setProperty("shared_spaces",repair.getProperty("shared_spaces",{}));
    merged=juce::JSON::toString(root);return true; // Full architecture validation follows.
}
inline juce::String prompt(const juce::String& original,const juce::String& error) {
    return "Reconcile ONLY the joint groove reference and its accent/space declarations. Preserve the "
        "musical signature, identity owners, subordinate roles, cast, harmony, cycles, timing grammars, "
        "layer states, melodic references and all unrelated lanes. If the signature promises a complete "
        "repeating gesture but actual notes omit its attacks, author the missing coherent reference "
        "material in the eligible lane_updates; do not merely remove accents to hide an underwritten "
        "groove. If an accent/space declaration itself is mistaken, choose the musically correct "
        "declaration. Reconcile shared breathing with the actual reference without flattening the "
        "whole groove. You choose all actual notes, rests and declarations. No code inserts, deletes, "
        "retimes or quantizes reference notes. Empty lane_updates preserves every source note. "
        "Provide the complete accent_offsets/shared_spaces and an explanation of the musical choice. "
        "Every lane's notes must fit the nucleus cycle and its actual register/scale/attack grammar.\nERROR: "+
        error+"\nPLAN: "+original;
}
}
