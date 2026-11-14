#pragma once
#include <juce_core/juce_core.h>
#include <set>
#include "AiFoundation.h"
#include "AiChromaticPolicy.h"
#include <algorithm>

namespace pulso::plugin::allocation {
// AI interpretation of explicit functional requests. The host checks references
// and counts; it does not infer musical roles from keywords or compose material.
inline juce::var schema() {
    return juce::JSON::parse(R"({"type":"array","maxItems":64,"items":{"type":"object","additionalProperties":false,"required":["source_quote","function","exact_count","owner_ids"],"properties":{"source_quote":{"type":"string"},"function":{"type":"string","enum":["static_tonal_rhythm","rhythmic_chords","sustained_harmony","bass","foreground","other"]},"exact_count":{"type":"integer","minimum":0,"maximum":64,"description":"Explicit requested number for this function; 0 when no number was requested. Never invent a quota."},"owner_ids":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}}}}})");
}
inline bool explicitFunctions(const juce::var& root) {
    const auto* a=root.getProperty("requested_functions",{}).getArray();return a&&!a->isEmpty();
}
inline bool validate(const juce::String& json,const juce::String& direction,juce::String& error) {
    const auto root=juce::JSON::parse(json);
    if(!root.hasProperty("requested_functions")) return true; // Historical plans.
    const auto* groups=root.getProperty("requested_functions",{}).getArray();
    const auto* cast=root.getProperty("instruments",{}).getArray();
    if(!groups||!cast) {error="Requested function allocation is malformed";return false;}
    std::set<juce::String> ids,assigned;
    for(const auto& owner:*cast) ids.insert(owner.getProperty("id",{}).toString());
    for(const auto& group:*groups) {
        const auto quote=group.getProperty("source_quote",{}).toString().trim();
        const auto count=group.getProperty("exact_count",{});
        const auto* owners=group.getProperty("owner_ids",{}).getArray();
        if(quote.isEmpty()||!direction.containsIgnoreCase(quote)||!count.isInt()||int(count)<0||int(count)>64||
           !owners||owners->isEmpty()||(int(count)>0&&owners->size()!=int(count))) {
            error="Requested function allocation does not match its quoted count: "+quote;return false;
        }
        for(const auto& owner:*owners) if(!ids.contains(owner.toString())||!assigned.insert(owner.toString()).second) {
            error="Requested independent functions need distinct existing owners: "+owner.toString();return false;
        }
    }
    return true;
}
inline juce::String behavior(const juce::String& function) {
    if(function=="static_tonal_rhythm"||function=="rhythmic_chords") return "static_tonal_rhythm";
    if(function=="sustained_harmony") return "stable_foundation";
    if(function=="foreground") return "developing_voice";
    return {};
}
inline std::set<juce::String> staticOwners(const juce::var& blueprint) {
    std::set<juce::String> result;
    if(const auto* groups=blueprint.getProperty("requested_functions",{}).getArray())
        for(const auto& group:*groups) if(behavior(group.getProperty("function",{}).toString())=="static_tonal_rhythm")
            if(const auto* owners=group.getProperty("owner_ids",{}).getArray())
                for(const auto& owner:*owners) result.insert(owner.toString());
    return result;
}
inline juce::String context(const juce::var& blueprint) {
    if(!explicitFunctions(blueprint)) return {};
    return "\nAUTHORITATIVE USER FUNCTION ALLOCATION: "+
        juce::JSON::toString(blueprint.getProperty("requested_functions",{}),true)+
        "\nCompose within these identities, not against them. Static tonal rhythm owners "
        "sustain their recurring rhythmic thoughts, not melodic answering speeches. They may "
        "contribute exact patterns to ensemble_excerpt and carry retained_pulse, but do not "
        "assign them a handoff that replaces their requested identity with a developed tune. "
        "Use a compatible speaking owner for a melodic answer, or null when the phrase needs "
        "no answer. Do not invent another owner or require constant soloing. Evaluate the "
        "shared groove without the protagonist: recognizable complementary attack/rest/accent "
        "patterns should create a body, not isolated chord markers. Select their actual density "
        "and patterns yourself. The foreground's development and resolution happen over this "
        "stable body, not by forcing every layer to change its argument.\n";
}
// Prevent a planning stage from promising melodic speech by an explicitly
// static owner, then forcing the later note writer to violate that function.
inline juce::String constrainShortScoreSchema(const juce::String& schemaJson,const juce::var& blueprint) {
    const auto fixed=staticOwners(blueprint);
    if(fixed.empty()) return schemaJson;
    const auto* cast=blueprint.getProperty("instruments",{}).getArray();
    if(!cast) return schemaJson;
    juce::Array<juce::var> receivers;
    for(const auto& owner:*cast) {
        const auto id=owner.getProperty("id",{}).toString();
        if(!id.isEmpty()&&!fixed.contains(id)) receivers.add(id);
    }
    auto root=juce::JSON::parse(schemaJson);
    auto handoff=root.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{})
        .getProperty("properties",{}).getProperty("handoff",{});
    if(auto* choices=handoff.getProperty("anyOf",{}).getArray()) {
        if(receivers.isEmpty()) {
            choices->clear();choices->add(juce::JSON::parse(R"({"type":"null"})"));
        } else for(auto& choice:*choices)
            if(auto* field=choice.getProperty("properties",{}).getProperty("to_owner",{}).getDynamicObject())
                field->setProperty("enum",receivers);
    }
    return juce::JSON::toString(root);
}
inline bool validateShortScore(const juce::var& score,const juce::var& blueprint,juce::String& error) {
    const auto fixed=staticOwners(blueprint);
    if(const auto* journey=score.getProperty("journey",{}).getArray())
        for(const auto& passage:*journey) {
            const auto recipient=passage.getProperty("handoff",{}).getProperty("to_owner",{}).toString();
            if(fixed.contains(recipient)) {
                error="AI short score assigns melodic speech to a requested static rhythm owner: "+recipient;return false;
            }
        }
    return true;
}
inline juce::String constrainSchema(const juce::String& schemaJson,const juce::var& blueprint,const SongPlan* plan=nullptr) {
    auto root=juce::JSON::parse(schemaJson);
    auto items=root.getProperty("properties",{}).getProperty("layers",{}).getProperty("items",{});
    auto* choices=items.getProperty("anyOf",{}).getArray();
    const auto* groups=blueprint.getProperty("requested_functions",{}).getArray();
    if(!choices||!groups) return schemaJson;
    juce::Array<juce::var> rhythmicCarriers;
    for(auto& choice:*choices) {
        auto props=choice.getProperty("properties",{});
        const auto* ids=props.getProperty("instrument_id",{}).getProperty("enum",{}).getArray();
        if(!ids||ids->size()!=1) continue;
        for(const auto& group:*groups) if(const auto* owners=group.getProperty("owner_ids",{}).getArray())
            if(owners->contains((*ids)[0])) {
                const auto expected=behavior(group.getProperty("function",{}).toString());
                if(expected.isNotEmpty()) if(auto* field=props.getProperty("material_behavior",{}).getDynamicObject())
                    field->setProperty("enum",juce::Array<juce::var>{expected});
                if(expected=="static_tonal_rhythm") {
                    props.getDynamicObject()->setProperty("static_identity",foundation::identitySchema());
                    choice.getProperty("required",{}).getArray()->add("static_identity");
                    auto identity=props.getProperty("static_identity",{});
                    identity.getProperty("required",{}).getArray()->add("attack_structure");
                    identity.getProperty("properties",{}).getDynamicObject()->setProperty("attack_structure",
                        juce::JSON::parse(R"({"type":"string","enum":["single_tone","chord"],"description":"A repeated pitched pulse or simultaneous chord attacks. Chord stabs are polyphonic rhythmic chords, not single-note markers."})"));
                    if(group.getProperty("function",{}).toString()=="rhythmic_chords") {
                        identity.getProperty("properties",{}).getProperty("attack_structure",{}).getDynamicObject()
                            ->setProperty("enum",juce::Array<juce::var>{"chord"});
                        identity.getProperty("properties",{}).getProperty("anchor_pitches",{}).getDynamicObject()->setProperty("minItems",2);
                        props.getProperty("polyphonic",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{true});
                    }
                    if(plan) {
                        const auto owner=std::find_if(plan->instruments.begin(),plan->instruments.end(),
                            [&](const auto& item){return juce::String(item.id)==(*ids)[0].toString();});
                        if(owner!=plan->instruments.end()) {
                            auto pitch=props.getProperty("static_identity",{}).getProperty("properties",{})
                                .getProperty("anchor_pitches",{}).getProperty("items",{});
                            pitch.getDynamicObject()->setProperty("minimum",std::max(0,owner->minimumPitch));
                            pitch.getDynamicObject()->setProperty("maximum",std::min(127,owner->maximumPitch));
                            AiChromaticPolicy::constrain(*pitch.getDynamicObject(),*plan,owner->sourceVoice,
                                owner->minimumPitch,owner->maximumPitch);
                        }
                    }
                    // These requested owners commit to recurring attack identities.
                    // The AI still chooses every offset, gate, rest and variant.
                    if(auto* mode=props.getProperty("timing_mode",{}).getDynamicObject())
                        mode->setProperty("enum",juce::Array<juce::var>{"cycle_bound"});
                    rhythmicCarriers.add((*ids)[0]);
                }
            }
    }
    // Never ask a phrase-flexible sustained bed to be the regular rhythmic
    // carrier when explicit repeating tonal rhythm owners have been allocated.
    if(!rhythmicCarriers.isEmpty())
        if(auto* carrier=root.getProperty("properties",{}).getProperty("tonal_nucleus",{})
                .getProperty("properties",{}).getProperty("meter_carrier_id",{}).getDynamicObject())
            carrier->setProperty("enum",rhythmicCarriers);
    return juce::JSON::toString(root);
}
inline bool validateArchitecture(const juce::String& json,const juce::var& blueprint,juce::String& error) {
    const auto* groups=blueprint.getProperty("requested_functions",{}).getArray();
    if(!groups) return true;
    const auto root=juce::JSON::parse(json);
    const auto* layers=root.getProperty("layers",{}).getArray();
    if(!layers) {error="Requested function allocation lost its layers";return false;}
    for(const auto& group:*groups) if(const auto* owners=group.getProperty("owner_ids",{}).getArray())
        for(const auto& owner:*owners) {
            int found=0;const auto expected=behavior(group.getProperty("function",{}).toString());
            for(const auto& layer:*layers) if(layer.getProperty("instrument_id",{})==owner) {
                ++found;
                if(expected.isNotEmpty()&&layer.getProperty("material_behavior",{}).toString()!=expected) {
                    error="Requested function changed before MIDI writing: "+owner.toString()+" must remain "+expected;return false;
                }
                if(expected=="static_tonal_rhythm"&&layer.getProperty("timing_mode",{}).toString()!="cycle_bound") {
                    error="Requested repeating rhythm lacks a cyclic timing commitment: "+owner.toString();return false;
                }
                if(group.getProperty("function",{}).toString()=="rhythmic_chords"&&
                   (!foundation::chordal(root,owner.toString())||!bool(layer.getProperty("polyphonic",false)))) {
                    error="Requested rhythmic chords became a non-chordal owner: "+owner.toString();return false;
                }
            }
            if(found!=1) {error="Requested function lost its unique owner: "+owner.toString();return false;}
        }
    return true;
}
inline juce::String brief() {
    return "\nREQUESTED FUNCTION ALLOCATION: Before selecting sounds, quote each explicitly requested "
        "independent musical function in requested_functions and bind its exact requested count to distinct "
        "owner_ids. Copy source_quote verbatim from USER; exact_count=0 if no number was given. "
        "Do not include the overall track count as a functional group. Use [] when no independent "
        "functions were requested. Do not split one owner's combined jobs into independent groups. "
        "Use rhythmic_chords for requested rhythmic chord stabs: actual repeated simultaneous chord "
        "attacks, not a one-note pulse or held pad. Other pitched rhythmic layers are static_tonal_rhythm "
        "owners, not substitutes for the requested chord stabs, bass or sustained pad. "
        "This example is semantic guidance, not a mandatory track count. Requested allocation takes "
        "precedence over default ensemble suggestions: do not spend its slots on extra harmonic bodies.\n";
}
}
