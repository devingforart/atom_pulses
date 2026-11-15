#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <set>

namespace pulso::plugin::groove_hierarchy {
inline void extend(juce::var& root,const SongPlan* plan) {
    auto hierarchy=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["identity_owners","signature","accent_offsets","support_roles","shared_spaces"],"properties":{"identity_owners":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}},"signature":{"type":"string","description":"The one recognizable combined rhythmic thought, not independent solos. Describe its accented groups and shared breathing in the nucleus cycle."},"accent_offsets":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"number","minimum":0,"multipleOf":0.25}},"support_roles":{"type":"array","maxItems":64,"items":{"type":"object","additionalProperties":false,"required":["owner_id","role","dynamic_intent"],"properties":{"owner_id":{"type":"string"},"role":{"type":"string","enum":["clock","reinforcement","counterweight","ornament"]},"dynamic_intent":{"type":"string"}}}},"shared_spaces":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,"required":["from_beat","duration_beats","owner_ids","meaning"],"properties":{"from_beat":{"type":"number","minimum":0,"multipleOf":0.25},"duration_beats":{"type":"number","minimum":0.25,"multipleOf":0.25},"owner_ids":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}},"meaning":{"type":"string","description":"Why these owners leave this common span without new attacks. Existing held notes may ring; this is not a gate truncation."}}}}}})");
    auto coordination=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["leader_id","partners"],"properties":{"leader_id":{"type":"string","description":"Choose the existing identity owner whose rhythmic sentence leads the combined gesture. Not necessarily the bass."},"partners":{"type":"array","maxItems":64,"items":{"type":"object","additionalProperties":false,"required":["owner_id","relationship","musical_intention"],"properties":{"owner_id":{"type":"string"},"relationship":{"type":"string","enum":["reinforce","sustain","answer"]},"musical_intention":{"type":"string","description":"Explain how this identity partner supports the leader's actual reference attacks, gates and accents without becoming a second simultaneous solo. Keep propulsion and intensity; choose the notes jointly."}}}}}})");
    hierarchy.getProperty("required",{}).getArray()->add("gesture_coordination");
    hierarchy.getProperty("properties",{}).getDynamicObject()->setProperty("gesture_coordination",coordination);
    if(plan&&!plan->instruments.empty()) {
        juce::Array<juce::var> ids;
        for(const auto& owner:plan->instruments) ids.add(juce::String(owner.id));
        auto props=hierarchy.getProperty("properties",{});
        coordination.getProperty("properties",{}).getProperty("leader_id",{}).getDynamicObject()->setProperty("enum",ids);
        coordination.getProperty("properties",{}).getProperty("partners",{}).getProperty("items",{}).getProperty("properties",{})
            .getProperty("owner_id",{}).getDynamicObject()->setProperty("enum",ids);
        props.getProperty("identity_owners",{}).getProperty("items",{}).getDynamicObject()->setProperty("enum",ids);
        props.getProperty("support_roles",{}).getProperty("items",{}).getProperty("properties",{})
            .getProperty("owner_id",{}).getDynamicObject()->setProperty("enum",ids);
        props.getProperty("shared_spaces",{}).getProperty("items",{}).getProperty("properties",{})
            .getProperty("owner_ids",{}).getProperty("items",{}).getDynamicObject()->setProperty("enum",ids);
        // One AI decision per owner; do not ask it to repeat the same membership
        // across identity, support and coordination lists that can disagree.
        auto roles=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{},"required":[]})");
        const auto row=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["role","musical_intention"],"properties":{"role":{"type":"string","enum":["lead","reinforce","sustain","answer","clock","counterweight","ornament"]},"musical_intention":{"type":"string","description":"This owner's contribution to ONE combined rhythmic sentence. Exactly one owner leads; reinforce/sustain/answer serve that leader; clock/counterweight/ornament stay subordinate. Explain actual grouping, articulation and accents without inventing a second foreground solo."}}})");
        for(const auto& id:ids) {
            roles.getProperty("properties",{}).getDynamicObject()->setProperty(juce::Identifier(id.toString()),row.clone());
            roles.getProperty("required",{}).getArray()->add(id);
        }
        for(const auto* key:{"identity_owners","support_roles","gesture_coordination"}) {
            props.getDynamicObject()->removeProperty(key);
            hierarchy.getProperty("required",{}).getArray()->removeAllInstancesOf(juce::var(key));
        }
        props.getDynamicObject()->setProperty("owner_roles",roles);
        hierarchy.getProperty("required",{}).getArray()->add("owner_roles");
    }
    root.getProperty("required",{}).getArray()->add("groove_hierarchy");
    root.getProperty("properties",{}).getDynamicObject()->setProperty("groove_hierarchy",hierarchy);
}
inline bool decode(const juce::String& original,const SongPlan& plan,juce::String& decoded,juce::String& error) {
    error.clear();
    auto root=juce::JSON::parse(original);
    auto hierarchy=root.getProperty("groove_hierarchy",{});
    if(!hierarchy.hasProperty("owner_roles")) { decoded=original;return true; }
    const auto roles=hierarchy.getProperty("owner_roles",{});
    const auto* object=roles.getDynamicObject();
    const auto fail=[&](const juce::String& why){error="Club groove role encoding: "+why;return false;};
    if(!object||object->getProperties().size()!=static_cast<int>(plan.instruments.size()))
        return fail("declare one role per existing owner");
    if(hierarchy.hasProperty("identity_owners")||hierarchy.hasProperty("support_roles")||hierarchy.hasProperty("gesture_coordination"))
        return fail("mixed keyed and legacy role declarations");
    juce::Array<juce::var> identity,supports,partners;juce::String leader,leaderIntention;
    for(const auto& owner:plan.instruments) {
        const auto id=juce::String(owner.id);
        const auto row=roles.getProperty(juce::Identifier(id),{});
        const auto role=row.getProperty("role",{}).toString();
        const auto intention=row.getProperty("musical_intention",{}).toString();
        if(!row.isObject()||intention.trim().isEmpty()) return fail("missing intention for "+id);
        if(role=="lead") {
            if(leader.isNotEmpty()) return fail("choose exactly one gesture leader");
            leader=id;leaderIntention=intention;identity.add(id);
        } else if(role=="reinforce"||role=="sustain"||role=="answer") {
            identity.add(id);auto* partner=new juce::DynamicObject();
            partner->setProperty("owner_id",id);partner->setProperty("relationship",role);
            partner->setProperty("musical_intention",intention);partners.add(juce::var(partner));
        } else if(role=="clock"||role=="counterweight"||role=="ornament") {
            auto* support=new juce::DynamicObject();support->setProperty("owner_id",id);
            support->setProperty("role",role);support->setProperty("dynamic_intent",intention);
            supports.add(juce::var(support));
        } else return fail("unknown role for "+id);
    }
    if(leader.isEmpty()) return fail("choose exactly one gesture leader");
    auto* coordination=new juce::DynamicObject();coordination->setProperty("leader_id",leader);
    coordination->setProperty("leader_intention",leaderIntention);
    coordination->setProperty("partners",partners);
    hierarchy.getDynamicObject()->setProperty("identity_owners",identity);
    hierarchy.getDynamicObject()->setProperty("support_roles",supports);
    hierarchy.getDynamicObject()->setProperty("gesture_coordination",juce::var(coordination));
    hierarchy.getDynamicObject()->removeProperty("owner_roles");
    decoded=juce::JSON::toString(root);return true;
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
    // Legacy accepted plans remain valid. New plans make primary-owner dependency explicit.
    if(h.hasProperty("gesture_coordination")) {
        const auto coordination=h.getProperty("gesture_coordination",{});
        const auto leader=coordination.getProperty("leader_id",{}).toString();
        const auto* partners=coordination.getProperty("partners",{}).getArray();
        if(!primary.contains(leader)||!partners) return fail("invalid gesture coordination leader");
        std::set<juce::String> coordinated{leader};
        for(const auto& partner:*partners) {
            const auto id=partner.getProperty("owner_id",{}).toString();
            const auto relationship=partner.getProperty("relationship",{}).toString();
            if(!primary.contains(id)||!coordinated.insert(id).second||
               (relationship!="reinforce"&&relationship!="sustain"&&relationship!="answer")||
               partner.getProperty("musical_intention",{}).toString().trim().isEmpty())
                return fail("invalid gesture coordination partner");
        }
        if(coordinated!=primary) return fail("gesture coordination omitted an identity partner");
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
        "Declare owner_roles once per existing owner: exactly one lead; reinforce/sustain/answer are "
        "identity partners, while clock/counterweight/ornament are subordinate layers. Do not also emit "
        "identity_owners, support_roles or gesture_coordination: the host derives these internal lists "
        "losslessly from your one role per owner. This gives one identity leader, with identity partners supporting "
        "that sentence by reinforcement, sustain or an answering gesture. Choose leadership by musical "
        "function, never instrument name. Design their actual nucleus notes TOGETHER: when stabs lead, "
        "the bass grounds or propels their accented groups instead of adding a second busy syncopated "
        "sentence; when bass leads, chords give it harmonic weight. Answers must belong to the same "
        "recognizable sentence, not fill every gap. Partners may stay active and intense; this is not "
        "a note-count reduction, unison requirement or ban on syncopation. Explain the dependency in "
        "musical_intention using the actual reference grouping, articulation and accents. Choose "
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
        "not be clipped. Realize gesture_coordination in the actual notes: preserve the leader's "
        "recognizable sentence and make its identity partners reinforce, sustain or answer it as declared. "
        "Do not expand an accompanying bass or chord figure into a competing solo when writing later "
        "windows. Keep intensity through conviction, articulation and layered support, not an imposed "
        "density reduction. Intensity comes from a convincing repeating group, not maximum grid occupancy.\n";
}
}
