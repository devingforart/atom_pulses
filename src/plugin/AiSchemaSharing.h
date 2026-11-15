#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace pulso::plugin::schema_sharing {
// Lossless structural factoring. No schema alternatives, notes, owners or
// parameter bounds are removed; repeated object schemas become local $refs.
inline void children(juce::var& schema,const std::function<void(juce::var&)>& visit) {
    if(!schema.isObject()) return;
    for(const auto* key:{"properties","$defs","patternProperties"})
        if(auto* object=schema.getProperty(key,{}).getDynamicObject()) {
            std::vector<juce::Identifier> names;for(const auto& entry:object->getProperties()) names.push_back(entry.name);
            for(const auto& name:names) {auto child=object->getProperty(name);visit(child);object->setProperty(name,child);}
        }
    for(const auto* key:{"items","additionalProperties","not"}) {
        auto item=schema.getProperty(key,{});
        if(item.isObject()) {visit(item);schema.getDynamicObject()->setProperty(key,item);}
    }
    for(const auto* key:{"anyOf","oneOf","allOf","prefixItems"})
        if(auto* array=schema.getProperty(key,{}).getArray()) for(auto& item:*array) visit(item);
}
inline int propertyCount(const juce::var& schema) {
    auto copy=schema.clone();int count=0;
    std::function<void(juce::var&)> walk=[&](juce::var& node) {
        if(const auto* props=node.getProperty("properties",{}).getDynamicObject()) count+=props->getProperties().size();
        children(node,walk);
    };walk(copy);return count;
}
inline juce::var compact(const juce::var& input) {
    auto result=input.clone();if(!result.isObject()) return result;
    std::map<juce::String,int> counts;
    const auto shareable=[](const juce::var& node) {
        const auto* properties=node.getProperty("properties",{}).getDynamicObject();
        return node.getProperty("type",{}).toString()=="object"&&properties&&properties->getProperties().size()>0;
    };
    std::function<void(juce::var&)> count=[&](juce::var& node) {
        if(shareable(node)) ++counts[juce::JSON::toString(node,true)];children(node,count);
    };count(result);
    auto definitions=result.getProperty("$defs",{});
    if(!definitions.isObject()) definitions=juce::var(new juce::DynamicObject());
    std::map<juce::String,juce::String> names;std::map<juce::String,juce::var> added;
    int serial=0;
    std::function<void(juce::var&)> rewrite=[&](juce::var& node) {
        if(shareable(node)) {
            const auto signature=juce::JSON::toString(node,true);
            if(counts[signature]>1) {
                juce::String name;
                if(const auto existing=names.find(signature);existing!=names.end()) name=existing->second;
                else {
                    do {name="pulso_shared_"+juce::String(++serial);} while(definitions.hasProperty(juce::Identifier(name)));
                    names[signature]=name;auto body=node.clone();children(body,rewrite);added[name]=body;
                }
                auto* ref=new juce::DynamicObject();ref->setProperty("$ref","#/$defs/"+name);node=juce::var(ref);return;
            }
        }
        children(node,rewrite);
    };
    children(result,rewrite); // Never replace the schema's root with a reference.
    definitions=result.getProperty("$defs",definitions);
    for(const auto& [name,body]:added) definitions.getDynamicObject()->setProperty(juce::Identifier(name),body);
    // Remove only definitions unreachable from the effective root. Unused
    // generic window schemas otherwise still consume the provider's budget.
    std::set<juce::String> reachable;
    std::function<void(juce::var&)> refs=[&](juce::var& node) {
        const auto ref=node.getProperty("$ref",{}).toString();
        if(ref.startsWith("#/$defs/")) {
            const auto key=ref.substring(8).upToFirstOccurrenceOf("/",false,false).replace("~1","/").replace("~0","~");
            if(reachable.insert(key).second) {
                auto target=definitions.getProperty(juce::Identifier(key),{});if(target.isObject()) refs(target);
            }
        }
        children(node,refs);
    };
    auto effective=result.clone();effective.getDynamicObject()->removeProperty("$defs");refs(effective);
    auto kept=juce::var(new juce::DynamicObject());
    for(const auto& key:reachable) if(definitions.hasProperty(juce::Identifier(key)))
        kept.getDynamicObject()->setProperty(juce::Identifier(key),definitions.getProperty(juce::Identifier(key),{}));
    if(reachable.empty()) result.getDynamicObject()->removeProperty("$defs");
    else result.getDynamicObject()->setProperty("$defs",kept);
    return result;
}
}
