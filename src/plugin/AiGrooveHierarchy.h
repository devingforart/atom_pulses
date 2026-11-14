#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <set>

namespace pulso::plugin::groove_hierarchy {
inline void extend(juce::var& root,const SongPlan* plan) {
    auto hierarchy=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["identity_owners","signature","accent_offsets","support_roles","shared_spaces"],"properties":{"identity_owners":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}},"signature":{"type":"string","description":"The one recognizable combined rhythmic thought, not independent solos. Describe its accented groups and shared breathing in the nucleus cycle."},"accent_offsets":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"number","minimum":0,"multipleOf":0.25}},"support_roles":{"type":"array","maxItems":64,"items":{"type":"object","additionalProperties":false,"required":["owner_id","role","dynamic_intent"],"properties":{"owner_id":{"type":"string"},"role":{"type":"string","enum":["clock","reinforcement","counterweight","ornament"]},"dynamic_intent":{"type":"string"}}}},"shared_spaces":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,"required":["from_beat","duration_beats","owner_ids","meaning"],"properties":{"from_beat":{"type":"number","minimum":0,"multipleOf":0.25},"duration_beats":{"type":"number","minimum":0.25,"multipleOf":0.25},"owner_ids":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}},"meaning":{"type":"string","description":"Why these owners leave this common span without new attacks. Existing held notes may ring; this is not a gate truncation."}}}}}})");
    if(plan&&!plan->instruments.empty()) {
        juce::Array<juce::var> ids;
        for(const auto& owner:plan->instruments) ids.add(juce::String(owner.id));
        auto props=hierarchy.getProperty("properties",{});
        props.getProperty("identity_owners",{}).getProperty("items",{}).getDynamicObject()->setProperty("enum",ids);
        props.getProperty("support_roles",{}).getProperty("items",{}).getProperty("properties",{})
            .getProperty("owner_id",{}).getDynamicObject()->setProperty("enum",ids);
        props.getProperty("shared_spaces",{}).getProperty("items",{}).getProperty("properties",{})
            .getProperty("owner_ids",{}).getProperty("items",{}).getDynamicObject()->setProperty("enum",ids);
    }
    root.getProperty("required",{}).getArray()->add("groove_hierarchy");
    root.getProperty("properties",{}).getDynamicObject()->setProperty("groove_hierarchy",hierarchy);
}
inline bool attackAllowed(const juce::var& root,const juce::String& owner,double absoluteBeat) {
    if(!root.hasProperty("groove_hierarchy")) return true;
    const double cycle=root.getProperty("tonal_nucleus",{}).getProperty("cycle_beats",0);
    if(cycle<=0||!std::isfinite(cycle)) return false;
    auto phase=std::fmod(absoluteBeat,cycle);if(phase<0) phase+=cycle;
    if(const auto* spaces=root.getProperty("groove_hierarchy",{}).getProperty("shared_spaces",{}).getArray())
        for(const auto& space:*spaces) if(const auto* owners=space.getProperty("owner_ids",{}).getArray())
            if(owners->contains(juce::var(owner))) {
                const double begin=space.getProperty("from_beat",0),length=space.getProperty("duration_beats",0);
                if(phase>=begin-.0001&&phase<begin+length-.0001) return false;
            }
    return true;
}
inline bool validate(const juce::var& root,const SongPlan& plan,juce::String& error) {
    if(!root.hasProperty("groove_hierarchy")) return true;
    const auto h=root.getProperty("groove_hierarchy",{});
    const auto* identity=h.getProperty("identity_owners",{}).getArray();
    const auto* supports=h.getProperty("support_roles",{}).getArray();
    const auto* accents=h.getProperty("accent_offsets",{}).getArray();
    const auto* spaces=h.getProperty("shared_spaces",{}).getArray();
    const double cycle=root.getProperty("tonal_nucleus",{}).getProperty("cycle_beats",0);
    const auto grid=[](const juce::var& value) {
        if(!value.isInt()&&!value.isInt64()&&!value.isDouble()) return false;
        const double n=value;return std::isfinite(n)&&std::abs(n*4-std::round(n*4))<.0001;
    };
    const auto fail=[&](const juce::String& why){error="Club groove hierarchy: "+why;return false;};
    if(!identity||identity->isEmpty()||!supports||!accents||accents->isEmpty()||!spaces||cycle<=0||
       h.getProperty("signature",{}).toString().trim().isEmpty()) return fail("missing joint rhythmic intention");
    std::set<juce::String> owners,used,primary;
    for(const auto& owner:plan.instruments) owners.insert(juce::String(owner.id));
    for(const auto& id:*identity) {
        if(!owners.contains(id.toString())||!used.insert(id.toString()).second) return fail("invalid or duplicate identity owner");
        primary.insert(id.toString());
    }
    for(const auto& role:*supports) {
        const auto id=role.getProperty("owner_id",{}).toString(),kind=role.getProperty("role",{}).toString();
        if(!owners.contains(id)||!used.insert(id).second||
           (kind!="clock"&&kind!="reinforcement"&&kind!="counterweight"&&kind!="ornament")||
           role.getProperty("dynamic_intent",{}).toString().trim().isEmpty()) return fail("support role contradicts identity ownership");
    }
    std::set<double> seen;
    for(const auto& accent:*accents) {
        const double beat=accent;
        if(!grid(accent)||beat<0||beat>=cycle||!seen.insert(beat).second) return fail("invalid or duplicate accent offset");
        bool actual=false;
        if(const auto* lanes=root.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray())
            for(const auto& lane:*lanes) if(primary.contains(lane.getProperty("instrument_id",{}).toString()))
                if(const auto* notes=lane.getProperty("notes",{}).getArray()) for(const auto& note:*notes)
                    if(std::abs(double(note.getProperty("beat",-1))-beat)<.0001) actual=true;
        if(!actual) return fail("declared main accent has no actual identity attack at beat "+juce::String(beat));
    }
    for(const auto& space:*spaces) {
        const auto begin=space.getProperty("from_beat",{}),length=space.getProperty("duration_beats",{});
        const auto* ids=space.getProperty("owner_ids",{}).getArray();
        if(!grid(begin)||!grid(length)||double(begin)<0||double(length)<=0||double(begin)+double(length)>cycle+.0001||
           !ids||ids->isEmpty()||space.getProperty("meaning",{}).toString().trim().isEmpty()) return fail("invalid shared attack-space");
        std::set<juce::String> unique;
        for(const auto& id:*ids) if(!owners.contains(id.toString())||!unique.insert(id.toString()).second)
            return fail("shared attack-space names invalid owners");
    }
    if(const auto* lanes=root.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray())
        for(const auto& lane:*lanes) if(const auto* notes=lane.getProperty("notes",{}).getArray())
            for(const auto& note:*notes) if(!attackAllowed(root,lane.getProperty("instrument_id",{}).toString(),double(note.getProperty("beat",0))))
                return fail("reference attacks inside its own shared space: "+lane.getProperty("instrument_id",{}).toString()+
                    " beat="+note.getProperty("beat",{}).toString()+". Reconcile the AI reference and space; no note was removed.");
    return true;
}
inline juce::String planningBrief() {
    return "\nJOINT RHYTHMIC HIERARCHY: Compose groove_hierarchy before independent lane rhythms. "
        "Choose the owners carrying ONE recognizable main rhythmic gesture (often bass and chord stabs), "
        "its actual accent_offsets, and the remaining layers' subordinate clock/reinforcement/counterweight/ornament "
        "roles. Complementarity is NOT filling every vacant sixteenth with another equally prominent gesture. "
        "Keep intensity while preserving an audible grouping and shared negative space. A quiet clock may "
        "continue underneath; do not give it a competing foreground accent pattern. shared_spaces are YOUR "
        "chosen nucleus-cycle-local spans without NEW attacks by the named owners; ringing notes may continue. "
        "Use them where the combined phrase needs a breath, not an obligatory pause every bar. [] remains "
        "valid for deliberately continuous textures. Their periods inherit tonal_nucleus.cycle_beats, "
        "with song phase zero. Make the actual nucleus notes, lane cells, accents and spaces agree. "
        "Choose every beat, note, velocity, gate and withdrawal; no density ceiling or preset groove is supplied.\n";
}
inline juce::String context(const juce::var& root) {
    if(!root.hasProperty("groove_hierarchy")) return {};
    return "\nAUTHORITATIVE JOINT RHYTHMIC HIERARCHY: "+juce::JSON::toString(root.getProperty("groove_hierarchy",{}),true)+
        "\nRealize one audible main gesture and the declared subordinate roles. Do not make every layer "
        "an equally accented syncopated solo. Preserve the declared shared no-new-attack spaces on the "
        "nucleus's absolute cycle phase; do not fill them with another owner's cue. Held notes need "
        "not be clipped. Intensity comes from a convincing repeating group, not maximum grid occupancy.\n";
}
}
