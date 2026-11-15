#pragma once
#include "AiFoundation.h"
#include <juce_core/juce_core.h>
#include <set>

namespace pulso::plugin::tonal_pulses {
inline juce::var kindSchema() {
    return juce::JSON::parse(R"({"type":"string","enum":["none","single_tone_pulse","chord_stabs"],"description":"Authoritative attack identity. single_tone_pulse is a monophonic one-pitch rhythmic layer; chord_stabs is repeated simultaneous chords and may follow harmony; none is another musical function. A repeating chord is NOT a single-tone pulse. This field outranks descriptive role wording."})");
}
inline juce::String kind(const juce::var& owner) {
    if(owner.hasProperty("tonal_rhythm_kind")) return owner.getProperty("tonal_rhythm_kind",{}).toString();
    return owner.getProperty("role",{}).toString().contains("retained_tonal_pulse")?"single_tone_pulse":"none";
}
// An explicit AI-authored functional role, not classification by a track name,
// instrument preset, or words inferred from the user's creative prompt.
inline std::set<juce::String> owners(const juce::var& blueprint) {
    std::set<juce::String> result;
    if(const auto* cast=blueprint.getProperty("instruments",{}).getArray())
        for(const auto& owner:*cast) if(kind(owner)=="single_tone_pulse")
            result.insert(owner.getProperty("id",{}).toString());
    return result;
}
inline std::set<juce::String> chordOwners(const juce::var& blueprint) {
    std::set<juce::String> result;
    if(const auto* cast=blueprint.getProperty("instruments",{}).getArray())
        for(const auto& owner:*cast) if(kind(owner)=="chord_stabs") result.insert(owner.getProperty("id",{}).toString());
    return result;
}
// Old descriptive tags did not distinguish a chord rhythm from a one-tone
// rhythm. Resolve only those absent typed decisions; never infer from names.
inline juce::var legacyKindSchema(const juce::var& blueprint,const juce::var& acceptedArchitecture={}) {
    auto schema=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{},"required":[]})");
    if(const auto* cast=blueprint.getProperty("instruments",{}).getArray())
        for(const auto& owner:*cast) if(!owner.hasProperty("tonal_rhythm_kind")&&owner.getProperty("role",{}).toString().contains("retained_tonal_pulse")) {
            const auto id=owner.getProperty("id",{}).toString();
            auto field=kindSchema();
            if(const auto* layers=acceptedArchitecture.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
                if(layer.getProperty("instrument_id",{}).toString()==id&&layer.getProperty("polyphonic",{}).isBool()) {
                    const auto attack=layer.getProperty("static_identity",{}).getProperty("attack_structure",{}).toString();
                    if(attack=="single_tone"&&!bool(layer.getProperty("polyphonic",true))) {
                        const auto identity=layer.getProperty("static_identity",{});
                        const auto fixed=identity.getProperty("pitch_policy",{}).toString()=="retain_anchor"&&identity.getProperty("anchor_pitches",{}).size()==1;
                        field.getDynamicObject()->setProperty("enum",juce::Array<juce::var>{fixed?"single_tone_pulse":"none"});
                    }
                    if(attack=="chord"&&bool(layer.getProperty("polyphonic",false)))
                        field.getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"chord_stabs"});
                }
            schema.getProperty("properties",{}).getDynamicObject()->setProperty(juce::Identifier(id),field);
            schema.getProperty("required",{}).getArray()->add(id);
        }
    return schema;
}
inline bool applyKinds(const juce::var& blueprint,const juce::var& choices,juce::var& revised,const juce::var& acceptedArchitecture={}) {
    const auto contract=legacyKindSchema(blueprint,acceptedArchitecture);const auto required=contract.getProperty("required",{});
    if(!choices.isObject()||choices.getDynamicObject()->getProperties().size()!=required.size()) return false;
    auto candidate=blueprint.clone();auto* cast=candidate.getProperty("instruments",{}).getArray();if(!cast) return false;
    for(const auto& id:*required.getArray()) {
        const auto choice=choices.getProperty(juce::Identifier(id.toString()),{}).toString();
        const auto allowed=contract.getProperty("properties",{}).getProperty(juce::Identifier(id.toString()),{}).getProperty("enum",{});
        if(!allowed.isArray()||!allowed.getArray()->contains(juce::var(choice))) return false;
        for(auto& owner:*cast) if(owner.getProperty("id",{})==id) owner.getDynamicObject()->setProperty("tonal_rhythm_kind",choice);
    }
    revised=candidate;return true;
}
inline void inheritKind(const juce::var& manifest,juce::var& detail) {
    if(const auto* cast=manifest.getProperty("instruments",{}).getArray()) for(const auto& owner:*cast)
        if(owner.getProperty("id",{})==detail.getProperty("id",{})&&owner.hasProperty("tonal_rhythm_kind"))
            detail.getDynamicObject()->setProperty("tonal_rhythm_kind",owner.getProperty("tonal_rhythm_kind",{}));
}
inline bool validateManifest(const juce::String& json,juce::String& error) {
    const auto root=juce::JSON::parse(json);const auto ids=owners(root);
    if(const auto* cast=root.getProperty("instruments",{}).getArray()) for(const auto& owner:*cast) {
        const auto attackKind=kind(owner);
        if(attackKind!="none"&&attackKind!="single_tone_pulse"&&attackKind!="chord_stabs") {error="Invalid typed tonal rhythm identity";return false;}
        const auto id=owner.getProperty("id",{}).toString();if(!ids.contains(id)) continue;
        const auto voice=owner.getProperty("source_voice",{}).toString();
        if(id==root.getProperty("protagonist_instrument_id",{}).toString()||voice=="movement_bass"||voice=="sub_bass"||
           voice=="harmonic_foundation"||owner.getProperty("role",{}).toString().contains("primary_chord_bed")) {
            error="Retained tonal pulse must have its own pitched rhythmic owner, not replace bass, chord bed or melodic protagonist: "+id;return false;
        }
        if(const auto* groups=root.getProperty("requested_functions",{}).getArray()) for(const auto& group:*groups)
            if(const auto* groupIds=group.getProperty("owner_ids",{}).getArray()) if(groupIds->contains(juce::var(id))) {
                const auto function=group.getProperty("function",{}).toString();
                if(function!="static_tonal_rhythm"&&function!="other") {
                    error="Retained tonal pulse conflicts with an explicit independent user function: "+id;return false;
                }
            }
    }
    return true;
}
inline void constrainIdentity(juce::var& props) {
    props.getProperty("polyphonic",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{false});
    auto identity=props.getProperty("static_identity",{}).getProperty("properties",{});
    identity.getProperty("attack_structure",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"single_tone"});
    identity.getProperty("pitch_policy",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{"retain_anchor"});
    identity.getProperty("anchor_pitches",{}).getDynamicObject()->setProperty("minItems",1);
    identity.getProperty("anchor_pitches",{}).getDynamicObject()->setProperty("maxItems",1);
}
// Required owner-keyed fields make reference presence representable in the
// schema, instead of hoping an unconstrained lane array remembers every pulse.
inline void constrainReferences(juce::var& root,const juce::var& blueprint) {
    const auto ids=owners(blueprint);if(ids.empty()) return;
    auto nucleus=root.getProperty("properties",{}).getProperty("tonal_nucleus",{}).getProperty("properties",{});
    auto lanes=nucleus.getProperty("lanes",{});
    auto lane=lanes.getProperty("items",{});
    if(!lane.isObject()) return;
    auto references=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{},"required":[]})");
    for(const auto& id:ids) {
        auto entry=lane.clone();
        entry.getProperty("properties",{}).getDynamicObject()->removeProperty("instrument_id");
        entry.getProperty("required",{}).getArray()->removeAllInstancesOf(juce::var("instrument_id"));
        references.getProperty("properties",{}).getDynamicObject()->setProperty(id,entry);
        references.getProperty("required",{}).getArray()->add(id);
    }
    auto owner=lane.getProperty("properties",{}).getProperty("instrument_id",{});
    auto exclusiveChoices=owner.getProperty("enum",{}).clone();
    if(auto* choices=exclusiveChoices.getArray()) {
        for(const auto& id:ids) choices->removeAllInstancesOf(juce::var(id));
        lanes.getDynamicObject()->setProperty("maxItems",choices->size());
        if(choices->isEmpty()) {
            owner.getDynamicObject()->removeProperty("enum");
            lanes.getDynamicObject()->setProperty("maxItems",0);
        } else owner.getDynamicObject()->setProperty("enum",exclusiveChoices);
    }
    lanes.getDynamicObject()->setProperty("minItems",0);
    root.getProperty("properties",{}).getDynamicObject()->setProperty("retained_pulse_references",references);
    root.getProperty("required",{}).getArray()->add("retained_pulse_references");
}
inline bool decodeReferences(const juce::String& source,const juce::var& blueprint,
                             juce::String& decoded,juce::String& error) {
    auto candidate=juce::JSON::parse(source);
    if(!candidate.hasProperty("retained_pulse_references")) {decoded=source;return true;} // Historical plans.
    const auto ids=owners(blueprint);
    const auto references=candidate.getProperty("retained_pulse_references",{});
    auto* lanes=candidate.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray();
    if(!lanes||!references.isObject()||references.getDynamicObject()->getProperties().size()!=int(ids.size())) {
        error="Invalid typed retained-pulse reference map";return false;
    }
    for(const auto& id:ids) {
        auto entry=references.getProperty(id,{}).clone();
        if(!entry.isObject()||entry.getDynamicObject()->getProperties().size()!=2||
           !entry.hasProperty("notes")||!entry.hasProperty("repeat_intent")||!entry.getProperty("notes",{}).isArray()||
           entry.getProperty("notes",{}).size()==0||entry.getProperty("repeat_intent",{}).toString().trim().isEmpty()) {
            error="Invalid retained-pulse reference for "+id;return false;
        }
        for(const auto& existing:*lanes) if(existing.getProperty("instrument_id",{}).toString()==id) {
            error="Duplicate typed retained-pulse reference for "+id;return false;
        }
        entry.getDynamicObject()->setProperty("instrument_id",id);
        lanes->add(entry); // Lossless owner-key decoding, never new musical events.
    }
    candidate.getDynamicObject()->removeProperty("retained_pulse_references");
    decoded=juce::JSON::toString(candidate);return true;
}
inline bool validateArchitecture(const juce::String& json,const juce::var& blueprint,juce::String& error) {
    const auto root=juce::JSON::parse(json);
    for(const auto& id:chordOwners(blueprint)) {
        bool found=false;
        if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
            if(layer.getProperty("instrument_id",{}).toString()==id)
                found=bool(layer.getProperty("polyphonic",false))&&foundation::chordal(root,id)&&
                    layer.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm";
        if(!found) {error="Typed chord stabs lost their polyphonic chord identity: "+id;return false;}
    }
    const auto ids=owners(blueprint);if(ids.empty()) return true;
    for(const auto& id:ids) {
        bool committed=false,reference=false;int anchor=-1;
        if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
            if(layer.getProperty("instrument_id",{}).toString()==id) {
                const auto identity=layer.getProperty("static_identity",{});
                committed=layer.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm"&&
                    layer.getProperty("timing_mode",{}).toString()=="cycle_bound"&&!bool(layer.getProperty("polyphonic",true))&&
                    identity.getProperty("attack_structure",{}).toString()=="single_tone"&&
                    identity.getProperty("pitch_policy",{}).toString()=="retain_anchor"&&
                    identity.getProperty("anchor_pitches",{}).isArray()&&identity.getProperty("anchor_pitches",{}).size()==1;
                if(committed) anchor=int(identity.getProperty("anchor_pitches",{})[0]);
            }
        if(const auto* lanes=root.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray()) for(const auto& lane:*lanes)
            if(lane.getProperty("instrument_id",{}).toString()==id&&lane.getProperty("notes",{}).isArray()&&lane.getProperty("notes",{}).size()>0) {
                reference=true;
                if(committed) for(const auto& note:*lane.getProperty("notes",{}).getArray()) if(int(note.getProperty("pitch",-1))!=anchor) {
                    error="Retained tonal pulse reference disagrees with its AI-chosen anchor: "+id;return false;
                }
            }
        if(!committed) {error="Retained tonal pulse has invalid recurring identity: "+id;return false;}
        if(!reference) {error="Tonal nucleus omitted a declared retained pulse owner: "+id;return false;}
    }
    return true;
}
inline juce::String brief() {
    return "\nRETAINED TONAL RHYTHM FABRIC: A complete tonal Club arrangement needs an energetic repeating body, "
        "not only bass, two chord stabs and speaking melodies. Normally cast several independent complementary "
        "pitched pulse layers: plan at least four dedicated owners, normally four or five, IN ADDITION to "
        "bass, chord stabs and sustained harmony when the requested cast allows it. Spread them across "
        "complementary registers and functions rather than duplicate one pattern in several tracks. "
        "Declare tonal_rhythm_kind=single_tone_pulse for those dedicated owners, chord_stabs for polyphonic "
        "chord rhythms, and none for other roles. This typed choice is authoritative; never classify a chord "
        "rhythm as single_tone_pulse just because its timing repeats. These are short synth-like tonal "
        "attacks with one AI-chosen retained pitch: percussion-like rhythmic functions, NOT drums, arpeggios "
        "or rapidly wandering tunes. Their grouped subdivisions and deliberate rests should form a stable "
        "interlocking body for extended passages, with small accents/gate variations rather than a new motif "
        "every bar. Develop a lively retained fabric: some owners carry grouped rapid short attacks, others "
        "answer their gaps or reinforce selected accents. Do not reduce every owner to two isolated ticks "
        "per bar, and do not make all owners equally busy. Keep recognizable rhythmic identities while "
        "using restrained velocity, articulation and occasional phrase-end variations for movement. "
        "Choose different complementary rhythms and suitable registers yourself; do not copy a "
        "reference MIDI, standard hat pattern or supplied onset template. Keep them subordinate to one shared "
        "gesture, not competing solos or constant maximum-grid filling. Enter/withdraw layers across the "
        "macro form, including real intro, breakdown and outro space; do not make every layer permanent tutti. "
        "The role commits to mono single-tone cycle-bound identity later. Respect explicit small track counts "
        "and user exclusions: do not secretly add tracks to a requested solo or three-part score. If genuinely "
        "incompatible with that request, omit the role rather than replace its requested owners. This is a "
        "production choice, NOT a quotation or count to invent in requested_functions.\n";
}
inline juce::String context(const juce::var& blueprint) {
    const auto ids=owners(blueprint);const auto chords=chordOwners(blueprint);juce::String out;
    if(!chords.empty()) {
        juce::Array<juce::var> list;for(const auto& id:chords) list.add(id);
        out<<"\nAI-DECLARED CHORD STAB OWNERS: "<<juce::JSON::toString(list,true)<<
            ". These are simultaneous polyphonic chords, NOT monophonic single-tone pulses. Keep their repeating "
            "rhythmic identity while choosing voiced chord tones against actual harmony; harmonic_variant permits "
            "intentional chord changes. A repeated rhythm does not require every note to equal one anchor.\n";
    }
    if(ids.empty()) return out;
    juce::Array<juce::var> list;for(const auto& id:ids) list.add(id);
    return out+"\nAI-DECLARED RETAINED TONAL PULSE OWNERS: "+juce::JSON::toString(list,true)+
        ". Give EACH its own actually notated nucleus rhythm and one in-key anchor pitch in its register. "
        "When the schema supplies retained_pulse_references, write each dedicated pulse there under its "
        "owner key, with notes and repeat_intent; do not also duplicate it in tonal_nucleus.lanes. "
        "These layers supply persistent grouped short attacks and hypnotic body, not sparse FX cues or melodic "
        "dialogue. Coordinate the complete fabric: include grouped rapid attacks as well as breathing "
        "supporting patterns, with differentiated accents and registers. Do not give every owner the same "
        "sparse tick or simultaneous busy subdivision. Establish complementary repeating identities that remain recognizable across many cycles; "
        "small velocity/gate inflections and purposeful entrances/withdrawals provide development. "
        "Grouped pulse layers may have intentional gaps: they need not be the perfectly regular meter "
        "carrier. Choose a separate existing owner for that reference when appropriate. Keep their "
        "stable patterns through writing windows and harmonic scenes. Do not put every pulse into every gap "
        "or make their accents compete with the main bass/stab grouping. Do not use them as melodic handoff "
        "recipients. You choose all beats, rests, repetitions, dynamics, gates and section activity.\n";
}
}
