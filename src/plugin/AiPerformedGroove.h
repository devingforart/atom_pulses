#pragma once
#include "core/SongComposer.h"
#include "AiFoundation.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace pulso::plugin::performed_groove {
struct Attack {
    std::string id;
    std::size_t cell{},owner{},section{};
    std::vector<std::size_t> notes;
    double origin{},beat{},length{};
};
inline std::set<std::string> owners(const SongPlan& plan) {
    const auto root=juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto coordination=root.getProperty("groove_hierarchy",{}).getProperty("gesture_coordination",{});
    std::set<std::string> eligible,result;
    eligible.insert(coordination.getProperty("leader_id",{}).toString().toStdString());
    if(const auto* partners=coordination.getProperty("partners",{}).getArray())
        for(const auto& p:*partners) eligible.insert(p.getProperty("owner_id",{}).toString().toStdString());
    bool bass=false,chord=false;
    for(const auto& owner:plan.instruments) if(eligible.contains(owner.id)) {
        const bool b=owner.sourceVoice==VoiceId::MovementBass;
        bool c=foundation::chordal(root,juce::String(owner.id));
        // Optional static_identity is absent in valid generic-prompt plans.
        // Their explicit functional voice + polyphonic rhythmic commitment is
        // also authoritative; never infer a role from a track's display name.
        if(!c&&owner.sourceVoice==VoiceId::HarmonicPulse)
            if(const auto* layers=root.getProperty("layers",{}).getArray())
                for(const auto& layer:*layers) if(layer.getProperty("instrument_id",{}).toString()==juce::String(owner.id))
                    c=bool(layer.getProperty("polyphonic",false))&&layer.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm";
        if(b||c) { result.insert(owner.id);bass|=b;chord|=c; }
    }
    if(!bass||!chord||!result.contains(coordination.getProperty("leader_id",{}).toString().toStdString())) return {};
    return result;
}
inline std::vector<Attack> targets(const PerformanceScore& score,const SongPlan& plan) {
    const auto scope=owners(plan);std::vector<Attack> result;
    for(std::size_t ci=0;ci<score.cells.size();++ci) {
        const auto& cell=score.cells[ci];const PerformancePlacement* placement=nullptr;
        for(const auto& p:score.placements) if(p.cellId==cell.id) {
            if(placement) { placement=nullptr;break; } placement=&p;
        }
        if(!placement||placement->sectionIndex<0||placement->sectionIndex>=static_cast<int>(plan.sections.size())||
           placement->repeats!=1||placement->transpose!=0||placement->retrograde||placement->invertContour||
           std::abs(placement->timeScale-1)>.000001||std::abs(placement->velocityScale-1)>.000001||
           !placement->voiceMap.empty()||std::abs(placement->fragmentStart)>.000001||
           (placement->fragmentEnd>=0&&std::abs(placement->fragmentEnd-cell.lengthBeats)>.000001)) continue;
        std::map<std::pair<std::string,double>,std::vector<std::size_t>> groups;
        for(std::size_t ni=0;ni<cell.notes.size();++ni)
            if(scope.contains(cell.notes[ni].instrumentId)) groups[{cell.notes[ni].instrumentId,cell.notes[ni].beat}].push_back(ni);
        for(const auto& [key,indices]:groups) {
            const auto owner=std::find_if(plan.instruments.begin(),plan.instruments.end(),[&](const auto& o){return o.id==key.first;});
            const auto section=static_cast<std::size_t>(placement->sectionIndex);
            const auto origin=plan.sections[section].startBar*plan.beatsPerBar+placement->startBeat;
            result.push_back({"attack_"+std::to_string(result.size()),ci,static_cast<std::size_t>(owner-plan.instruments.begin()),
                section,indices,origin,key.second,cell.lengthBeats});
        }
    }
    return result;
}
inline juce::String schema(const std::vector<Attack>& attacks,double end) {
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["decision","reason","intensity_plan","edits"],"properties":{"decision":{"type":"string","enum":["keep","revise"]},"reason":{"type":"string"},"intensity_plan":{"type":"string"},"edits":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["attack_id","beat","gate_delta","velocity_delta"],"properties":{"attack_id":{"type":"string"},"beat":{"type":"number","minimum":0,"multipleOf":0.25},"gate_delta":{"type":"number"},"velocity_delta":{"type":"integer","minimum":-126,"maximum":126}}}}}})");
    auto edits=root.getProperty("properties",{}).getProperty("edits",{});
    edits.getDynamicObject()->setProperty("maxItems",static_cast<int>(attacks.size()));
    auto props=edits.getProperty("items",{}).getProperty("properties",{});
    // Long songs can exceed the service's aggregate enum limits. Membership
    // remains exact at apply(), without constraining musical track length.
    props.getProperty("attack_id",{}).getDynamicObject()->setProperty("description","Exact attack_id from the supplied address table. Unknown or duplicate IDs invalidate the whole proposal.");
    props.getProperty("beat",{}).getDynamicObject()->setProperty("maximum",end-.25);
    return juce::JSON::toString(root);
}
// Exact AI-specified edits, atomically applied. Never infer offsets, snap a
// note, delete an attack, change pitches or partially accept a malformed answer.
inline bool apply(const PerformanceScore& original,const SongPlan& plan,const std::vector<Attack>& attacks,
                  const juce::String& json,PerformanceScore& revised,juce::String& error) {
    const auto root=juce::JSON::parse(json);const auto* edits=root.getProperty("edits",{}).getArray();
    const auto decision=root.getProperty("decision",{}).toString();
    const auto fail=[&](const juce::String& why){error="Performed groove proposal: "+why;return false;};
    if(!edits||(decision!="keep"&&decision!="revise")||root.getProperty("reason",{}).toString().trim().isEmpty()||
       root.getProperty("intensity_plan",{}).toString().trim().isEmpty()||
       (decision=="keep"&&!edits->isEmpty())||(decision=="revise"&&edits->isEmpty())) return fail("inconsistent decision");
    for(const auto& field:root.getDynamicObject()->getProperties()) {
        const auto key=field.name.toString();if(key!="decision"&&key!="reason"&&key!="intensity_plan"&&key!="edits") return fail("unexpected proposal field");
    }
    auto candidate=original;std::set<std::string> seen;bool changed=false;
    for(const auto& edit:*edits) {
        if(!edit.isObject()) return fail("invalid edit object");
        for(const auto& field:edit.getDynamicObject()->getProperties()) {
            const auto key=field.name.toString();if(key!="attack_id"&&key!="beat"&&key!="gate_delta"&&key!="velocity_delta") return fail("unexpected edit field");
        }
        const auto id=edit.getProperty("attack_id",{}).toString().toStdString();
        const auto found=std::find_if(attacks.begin(),attacks.end(),[&](const auto& a){return a.id==id;});
        if(found==attacks.end()||!seen.insert(id).second) return fail("unknown or duplicate attack ID");
        const auto numeric=[](const juce::var& v){return v.isInt()||v.isInt64()||v.isDouble();};
        const auto beatValue=edit.getProperty("beat",{}),gateValue=edit.getProperty("gate_delta",{}),velocityValue=edit.getProperty("velocity_delta",{});
        if(!numeric(beatValue)||!numeric(gateValue)||!velocityValue.isInt()) return fail("invalid numeric edit");
        const double beat=beatValue,gate=gateValue;const int velocity=velocityValue;
        if(!std::isfinite(beat)||!std::isfinite(gate)||std::abs(beat*4-std::round(beat*4))>.000001||velocity< -126||velocity>126)
            return fail("invalid attack grid or articulation");
        const auto local=beat-found->origin;
        const auto sectionStart=plan.sections[found->section].startBar*plan.beatsPerBar;
        const auto sectionEnd=sectionStart+plan.sections[found->section].bars*plan.beatsPerBar;
        for(const auto ni:found->notes) {
            auto& note=candidate.cells[found->cell].notes[ni];
            const auto duration=note.durationBeats+gate;const int newVelocity=note.velocity+velocity;
            if(local<0||local>=found->length||duration<.0625||local+duration>found->length+.000001||
               beat<sectionStart||beat+duration>sectionEnd+.000001||newVelocity<1||newVelocity>127)
                return fail("edit exceeds its authored cell/section or MIDI limits");
            changed|=std::abs(note.beat-local)>.000001||std::abs(gate)>.000001||velocity!=0;
            note.beat=local;note.durationBeats=duration;note.velocity=newVelocity;
        }
    }
    if(decision=="revise"&&!changed) return fail("revision contains no actual change");
    revised=std::move(candidate);error.clear();return true;
}
inline juce::String evidence(const PerformanceScore& score,const SongPlan& plan,const std::vector<Attack>& attacks) {
    juce::String out;
    for(const auto& a:attacks) {
        out<<"\n"<<juce::String(a.id)<<" owner="<<juce::String(plan.instruments[a.owner].id)<<" section="<<static_cast<int>(a.section)
           <<" absolute_beat="<<a.origin+a.beat<<" editable_cell=["<<a.origin<<","<<a.origin+a.length<<"] notes=";
        for(const auto ni:a.notes) {const auto& n=score.cells[a.cell].notes[ni];out<<n.pitch<<"/"<<n.durationBeats<<"/"<<n.velocity<<" ";}
    }
    return out;
}
inline bool preserves(const Pattern& before,const Pattern& after,const SongPlan& plan) {
    const auto ids=owners(plan);std::set<std::uint16_t> parts;
    for(std::size_t i=0;i<plan.instruments.size();++i) if(ids.contains(plan.instruments[i].id)) parts.insert(static_cast<std::uint16_t>(i+1));
    const auto pitches=[](const Pattern& p) {
        std::multiset<std::pair<std::uint16_t,int>> keys;for(const auto& n:p.notes) keys.emplace(n.partId,n.pitch);return keys;
    };
    const auto outside=[&](const Pattern& p) {
        std::vector<std::tuple<std::uint16_t,double,double,int,int,int>> keys;
        for(const auto& n:p.notes) if(!parts.contains(n.partId)) keys.emplace_back(n.partId,n.startBeat,n.durationBeats,n.pitch,n.velocity,n.channel);
        std::sort(keys.begin(),keys.end());return keys;
    };
    const auto overlaps=[&](const Pattern& p) {
        auto notes=p.notes;std::sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return a.startBeat<b.startBeat;});
        std::map<std::pair<std::uint16_t,int>,double> ends;int count=0;
        for(const auto& n:notes) if(parts.contains(n.partId)) {
            auto& end=ends[{n.partId,n.pitch}];if(end>n.startBeat+.000001) ++count;
            end=std::max(end,n.endBeat());
        }
        return count;
    };
    return before.lengthBeats==after.lengthBeats && pitches(before)==pitches(after) && outside(before)==outside(after) &&
        before.controls==after.controls && before.expressions==after.expressions && before.markers==after.markers && overlaps(after)<=overlaps(before);
}
}
