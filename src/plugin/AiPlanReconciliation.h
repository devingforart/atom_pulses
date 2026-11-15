#pragma once
#include "AiPlanExecution.h"
#include <set>
#include <map>

namespace pulso::plugin::reconciliation {
class FragmentRepairBudget {
public:
    bool take(const juce::String& target) {
        if(target.isEmpty() || total>=8 || attempts[target]>=2) return false;
        ++attempts[target]; ++total; return true;
    }
private:
    std::map<juce::String,int> attempts;
    int total{};
};
inline juce::String schema() {
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["assessment","edits"],"properties":{"assessment":{"type":"string"},"edits":{"type":"array","maxItems":96,"items":{}}}})");
    juce::Array<juce::var> choices;
    const auto add=[&](const char* scope,const char* fields,bool numeric) {
        auto item=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["scope","index","child_index","field","number","text"],"properties":{"scope":{"type":"string"},"index":{"type":"integer","minimum":0},"child_index":{"type":"integer","minimum":0},"field":{"type":"string"},"number":{},"text":{}}})");
        auto p=item.getProperty("properties",{});
        p.getProperty("scope",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{juce::String(scope)});
        juce::Array<juce::var> names;
        for(const auto& name:juce::StringArray::fromTokens(fields,"|","")) names.add(name);
        p.getProperty("field",{}).getDynamicObject()->setProperty("enum",names);
        p.getDynamicObject()->setProperty("number",juce::JSON::parse(numeric?R"({"type":"number","minimum":0})":R"({"type":"null"})"));
        if(numeric && juce::String(scope)!="layer_state")
            p.getDynamicObject()->setProperty("number",juce::JSON::parse(R"({"type":"integer","minimum":0})"));
        p.getDynamicObject()->setProperty("text",juce::JSON::parse(numeric?R"({"type":"null"})":R"({"type":"string"})"));
        if(juce::String(scope)=="route" && juce::String(fields)=="action")
            p.getProperty("text",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"statement","continuation","arrival","breath"});
        if(juce::String(scope)=="layer_state" && juce::String(fields)=="mode")
            p.getProperty("text",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"active","reduced","absent"});
        if(juce::String(scope)=="arc"||juce::String(scope)=="ending")
            p.getProperty("child_index",{}).getDynamicObject()->setProperty("maximum",0);
        if(juce::String(scope)=="ending") p.getProperty("index",{}).getDynamicObject()->setProperty("maximum",0);
        choices.add(item);
    };
    add("arc","start_bar|bars",true); add("arc","arrival",false);
    add("route","bars",true); add("route","material|consequence",false);
    add("route","action",false);
    add("layer_state","entry_beat|release_beat",true);
    add("layer_state","mode",false);
    add("scene_voice","speaking_span|arrival|material",false);
    add("ending","prepared_phrase|shared_arrival|speaker_release|after_arrival",false);
    root.getProperty("properties",{}).getProperty("edits",{}).getProperty("items",{}).getDynamicObject()->setProperty("anyOf",choices);
    return juce::JSON::toString(root);
}
struct EditTarget {
    juce::String key,scope,field;
    int index{},child{};
    bool numeric{};
};
inline std::vector<EditTarget> targets(const juce::var& plan) {
    std::vector<EditTarget> out;
    const auto add=[&](const juce::var& object,const juce::String& scope,int i,int j,const char* fields) {
        if(!object.isObject()) return;
        for(const auto& field:juce::StringArray::fromTokens(fields,"|","")) if(object.hasProperty(field)) {
            const bool numeric=field=="start_bar"||field=="bars"||field=="entry_beat"||field=="release_beat";
            out.push_back({scope+"_"+juce::String(i)+"_"+juce::String(j)+"_"+field,scope,field,i,j,numeric});
        }
    };
    if(const auto* arcs=plan.getProperty("musical_arcs",{}).getArray()) for(int i=0;i<arcs->size();++i) {
        add((*arcs)[i],"arc",i,0,"start_bar|bars|arrival");
        if(const auto* route=(*arcs)[i].getProperty("phrase_route",{}).getArray()) for(int j=0;j<route->size();++j)
            add((*route)[j],"route",i,j,"bars|action|material|consequence");
    }
    if(const auto* layers=plan.getProperty("layers",{}).getArray()) for(int i=0;i<layers->size();++i)
        if(const auto* states=(*layers)[i].getProperty("states",{}).getArray()) for(int j=0;j<states->size();++j)
            add((*states)[j],"layer_state",i,j,"entry_beat|release_beat|mode");
    if(const auto* scenes=plan.getProperty("ensemble_scenes",{}).getArray()) for(int i=0;i<scenes->size();++i)
        if(const auto* voices=(*scenes)[i].getProperty("voices",{}).getArray()) for(int j=0;j<voices->size();++j)
            add((*voices)[j],"scene_voice",i,j,"speaking_span|arrival|material");
    add(plan.getProperty("compositional_short_score",{}).getProperty("ensemble_ending",{}),"ending",0,0,
        "prepared_phrase|shared_arrival|speaker_release|after_arrival");
    return out;
}
// One schema key per real, whitelisted declaration. null means unchanged;
// no sequential patches, invented indices or repeated field decisions.
inline juce::String schema(const SongPlan& plan) {
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["assessment","edits_by_target"],"properties":{"assessment":{"type":"string"},"edits_by_target":{"type":"object","additionalProperties":false,"required":[],"properties":{}}}})");
    auto edits=root.getProperty("properties",{}).getProperty("edits_by_target",{});
    for(const auto& target:targets(juce::JSON::parse(juce::String(plan.phraseArchitectureJson)))) {
        auto value=juce::JSON::parse(target.numeric?
            (target.scope=="layer_state"?R"({"type":"number","minimum":0})":R"({"type":"integer","minimum":0})"):
            R"({"type":"string","minLength":1})");
        if(target.field=="bars") value.getDynamicObject()->setProperty("minimum",1);
        if(target.scope=="route"&&target.field=="action") value.getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"statement","continuation","arrival","breath"});
        if(target.scope=="layer_state"&&target.field=="mode") value.getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"active","reduced","absent"});
        auto choice=juce::JSON::parse(R"({"anyOf":[]})");
        choice.getProperty("anyOf",{}).getArray()->add(value);
        choice.getProperty("anyOf",{}).getArray()->add(juce::JSON::parse(R"({"type":"null"})"));
        edits.getProperty("properties",{}).getDynamicObject()->setProperty(target.key,choice);
        edits.getProperty("required",{}).getArray()->add(target.key);
    }
    return juce::JSON::toString(root);
}
inline int outputBudget(const SongPlan& plan) {
    return std::clamp(6000+int(targets(juce::JSON::parse(juce::String(plan.phraseArchitectureJson))).size())*16,8000,32000);
}
inline bool decodeEdits(const juce::var& plan,const juce::var& source,juce::var& decoded,juce::String& error) {
    if(!source.hasProperty("edits_by_target")) {decoded=source;return true;} // Historical edit lists.
    const auto available=targets(plan);
    const auto edits=source.getProperty("edits_by_target",{});
    if(!source.isObject()||source.getDynamicObject()->getProperties().size()!=2||source.hasProperty("edits")||!edits.isObject()||edits.getDynamicObject()->getProperties().size()!=int(available.size())) {
        error="AI plan reconciliation returned an incomplete or mixed target map";return false;
    }
    juce::Array<juce::var> list;
    for(const auto& target:available) {
        if(!edits.hasProperty(target.key)) {error="AI plan reconciliation omitted target "+target.key;return false;}
        const auto value=edits.getProperty(target.key,{});
        if(value.isVoid()) continue;
        auto entry=juce::var(new juce::DynamicObject());
        entry.getDynamicObject()->setProperty("scope",target.scope);
        entry.getDynamicObject()->setProperty("index",target.index);
        entry.getDynamicObject()->setProperty("child_index",target.child);
        entry.getDynamicObject()->setProperty("field",target.field);
        entry.getDynamicObject()->setProperty("number",target.numeric?value:juce::var());
        entry.getDynamicObject()->setProperty("text",target.numeric?juce::var():value);
        list.add(entry);
    }
    auto result=juce::var(new juce::DynamicObject());
    result.getDynamicObject()->setProperty("assessment",source.getProperty("assessment",{}));
    result.getDynamicObject()->setProperty("edits",list);
    decoded=result;return true;
}
inline juce::String prompt(const SongPlan& plan) {
    juce::String cast="\nORIGINAL CAST RESPONSIBILITIES (existing voices, not requests to add notes):\n";
    for(const auto& owner:plan.instruments) {
        cast << juce::String(owner.id) << " | " << juce::String(owner.role) << " | allowed sections:";
        for(const auto& section:owner.activeSections) cast << " " << juce::String(section);
        cast << "\n";
    }
    return "Reconcile this AI musical plan BEFORE any MIDI is written. Find contradictions "
        "between numeric routes, the actual harmonic map, scene speaking spans, layer activity "
        "and the ensemble ending. This is not a new composition or an aesthetic rewrite. "
        "Preserve motifs, reference notes, cast, groove, tonal policy, sections and harmony. "
        "Return only the necessary whitelisted field edits, or no edits if already consistent. "
        "An arrival described over a chord must occur while that chord really exists, or be "
        "explicitly prepared as a suspension with its later consequence. Coordinate the "
        "speaker's final arrival, supporting voices and their release order. Never describe "
        "a voice as last to release while assigning it an earlier deadline. Make the actual "
        "route reserve the intended phrase and sustain; do not call that span breath while "
        "another declaration promises it will speak. Keep intentional rests and plateaus; "
        "do not maximize coverage, invent fills or extend all releases to the file boundary. "
        "A layer release is an upper bound, not a requirement to sound continuously. "
        "Every changed plan remains playable within its own section/arc. Retain route "
        "Compare layer states with ORIGINAL CAST RESPONSIBILITIES: do not discard a promised "
        "initial statement and transformed return merely by making that owner absent until "
        "the final cue. Preserve intentional withheld entrances when that is the original "
        "responsibility. Activity is not a note quota. If changing absent to active/reduced, "
        "supply compatible entry/release bounds and coordinate its existing scene/route. "
        "coverage and order. Do not shrink the work just to hide an unresolved contradiction. "
        "For route timing edit only bars (durations): the host serializes from_bar as the "
        "cumulative sum of those AI-chosen durations, preserving order. Edit neighboring "
        "durations together so the sum still equals the arc length. Never request from_bar "
        "edits. To delay an arrival, allocate the required time to the preceding span "
        "and take it from the following span, preserving positive lengths and the musical intent. "
        "Route action must be exactly statement, continuation, arrival or breath. An answering "
        "or preparatory gesture is continuation, not a new action named response or preparation. "
        "Use zero-based ARRAY indices, not section_index values: arc=index in musical_arcs; "
        "route=index in musical_arcs and child_index in phrase_route; layer_state=index in "
        "layers and child_index in states; scene_voice=index in ensemble_scenes and child_index "
        "in voices; ending=index=0,child_index=0 in compositional_short_score.ensemble_ending. "
        "For arc the child index is always 0. The response schema uses edits_by_target: "
        "each key already identifies one existing field; "
        "return null to preserve it or exactly one final numeric/text value to change it. "
        "Do not return a sequence of alternative values or intermediate editing steps. "
        "Choose a coherent final set before responding. Explain specific reconciliations in assessment.\n"+
        execution::planningBrief(plan)+cast+"\nPLAN TO RECONCILE: "+juce::String(plan.phraseArchitectureJson);
}
// Apply AI-authored declaration changes only, atomically. Never touch note arrays.
inline bool apply(const juce::String& original,const juce::String& response,
                  juce::String& candidate,juce::String& error) {
    auto root=juce::JSON::parse(original);
    juce::var review;
    if(!decodeEdits(root,juce::JSON::parse(response),review,error)) return false;
    const auto* edits=review.getProperty("edits",{}).getArray();
    const auto fail=[&](){error="AI plan reconciliation returned an invalid or unauthorized declaration edit";return false;};
    if(!root.isObject() || !edits || edits->size()>96 || review.getProperty("assessment",{}).toString().trim().isEmpty()) return fail();
    std::set<juce::String> used;
    std::set<int> reflowArcs;
    juce::String location;
    const auto reject=[&](const juce::String& reason) {
        error="AI plan reconciliation edit "+location+": "+reason;return false;
    };
    for(const auto& edit:*edits) {
        const auto scope=edit.getProperty("scope",{}).toString(),field=edit.getProperty("field",{}).toString();
        const auto index=edit.getProperty("index",{}),child=edit.getProperty("child_index",{});
        if(!index.isInt() || !child.isInt() || static_cast<int>(index)<0 || static_cast<int>(child)<0) return fail();
        const int i=index,j=child;
        location=scope+"["+juce::String(i)+"]["+juce::String(j)+"]."+field;
        juce::var destination;
        juce::String allowed;
        auto at=[](const juce::var& array,int n)->juce::var {
            const auto* a=array.getArray(); return a && n>=0 && n<a->size()?(*a)[n]:juce::var();
        };
        if(scope=="arc" && j==0) {
            destination=at(root.getProperty("musical_arcs",{}),i);allowed="|start_bar|bars|arrival|";
        } else if(scope=="route") {
            destination=at(at(root.getProperty("musical_arcs",{}),i).getProperty("phrase_route",{}),j);
            allowed="|from_bar|bars|action|material|consequence|";
        } else if(scope=="layer_state") {
            destination=at(at(root.getProperty("layers",{}),i).getProperty("states",{}),j);
            allowed="|entry_beat|release_beat|mode|";
        } else if(scope=="scene_voice") {
            destination=at(at(root.getProperty("ensemble_scenes",{}),i).getProperty("voices",{}),j);
            allowed="|speaking_span|arrival|material|";
        } else if(scope=="ending" && i==0 && j==0) {
            destination=root.getProperty("compositional_short_score",{}).getProperty("ensemble_ending",{});
            allowed="|prepared_phrase|shared_arrival|speaker_release|after_arrival|";
        }
        if(field.isEmpty() || field.containsChar('|') || !destination.isObject() ||
            !destination.hasProperty(field) || !allowed.contains("|"+field+"|")) return reject("target is missing or field is not permitted for this scope");
        const auto key=scope+":"+juce::String(i)+":"+juce::String(j)+":"+field;
        if(!used.insert(key).second) return reject("duplicate target field");
        const bool numeric=field=="start_bar"||field=="bars"||field=="from_bar"||field=="entry_beat"||field=="release_beat";
        const auto number=edit.getProperty("number",{}),text=edit.getProperty("text",{});
        if(numeric) {
            if(!(number.isInt()||number.isDouble()||number.isInt64()) || !text.isVoid()) return reject("numeric field requires number and null text");
            const double value=number;
            if(!std::isfinite(value)||value<0 || ((field=="start_bar"||field=="bars"||field=="from_bar") && std::floor(value)!=value)) return reject("time must be nonnegative, with whole bars for bar fields");
            destination.getDynamicObject()->setProperty(field,number);
            if((scope=="route" && field=="bars")||(scope=="arc" && field=="bars")) reflowArcs.insert(i);
        } else {
            if(!number.isVoid() || !text.isString() || text.toString().trim().isEmpty()) return reject("text field requires nonempty text and null number");
            if(scope=="route" && field=="action" && text.toString()!="statement" && text.toString()!="continuation" && text.toString()!="arrival" && text.toString()!="breath")
                return reject("action must be statement, continuation, arrival or breath");
            if(scope=="layer_state" && field=="mode" && text.toString()!="active" && text.toString()!="reduced" && text.toString()!="absent") return reject("invalid activity mode");
            destination.getDynamicObject()->setProperty(field,text);
        }
    }
    for(const auto i:reflowArcs) {
        auto arc=root.getProperty("musical_arcs",{})[i];
        auto* route=arc.getProperty("phrase_route",{}).getArray();
        if(!route) return reject("edited arc has no route");
        double sum=0;
        for(const auto& span:*route) {
            const double bars=span.getProperty("bars",0);
            if(bars<=0 || std::floor(bars)!=bars) return reject("route durations must be positive whole bars");
            sum+=bars;
        }
        if(sum!=static_cast<double>(arc.getProperty("bars",0))) {
            error="Reconciliation arc "+juce::String(i)+" durations sum to "+juce::String(sum)+
                " but arc length is "+arc.getProperty("bars",{}).toString()+"; edit neighboring durations together";
            return false;
        }
        int cursor=0;
        for(auto& span:*route) {
            span.getDynamicObject()->setProperty("from_bar",cursor);
            cursor+=static_cast<int>(span.getProperty("bars",0));
        }
    }
    root.getDynamicObject()->setProperty("plan_reconciliation",review);
    candidate=juce::JSON::toString(root);
    return true;
}
}
