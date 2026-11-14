#pragma once
#include <juce_core/juce_core.h>
#include <set>
#include <map>

namespace pulso::plugin::foundation {
inline juce::var identitySchema() {
    return juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["pitch_policy","anchor_pitches","harmonic_fit","rhythmic_sentence","variation_intent"],"properties":{"pitch_policy":{"type":"string","enum":["retain_anchor","harmonic_variant"],"description":"Retain the chosen pitch/interval over harmonic changes when musically viable. Choose harmonic_variant only when deliberate pitch adaptation is needed; do not automatically transpose with every chord."},"anchor_pitches":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"integer","minimum":0,"maximum":127}},"harmonic_fit":{"type":"string","description":"Explain how the chosen anchor sounds against the work's actual changing harmonies, not only its key. Describe intentional tensions or why a harmonic variant is needed."},"rhythmic_sentence":{"type":"string","description":"Describe the complete recurring attack/rest/accent sentence. For body layers consider articulated subdivisions or short attack groups separated by recognizable gaps; sparse counterparts remain valid. No fixed density quota or supplied pattern."},"variation_intent":{"type":"string","description":"What remains invariant and where a planned withdrawal or authored variant belongs. Do not turn every chord change into a new cell."}}})");
}
inline juce::var retainedPitches(const juce::var& root,const juce::String& id) {
    if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
        if(layer.getProperty("instrument_id",{}).toString()==id&&
           layer.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm") {
            const auto identity=layer.getProperty("static_identity",{});
            if(identity.getProperty("pitch_policy",{}).toString()=="retain_anchor")
                return identity.getProperty("anchor_pitches",{});
        }
    return {};
}
inline bool validIdentity(const juce::var& layer,int minimumPitch,int maximumPitch) {
    if(!layer.hasProperty("static_identity")) return true; // Historical plans.
    const auto identity=layer.getProperty("static_identity",{});
    const auto policy=identity.getProperty("pitch_policy",{}).toString();
    const auto* pitches=identity.getProperty("anchor_pitches",{}).getArray();
    if(layer.getProperty("material_behavior",{}).toString()!="static_tonal_rhythm"||
       (policy!="retain_anchor"&&policy!="harmonic_variant")||!pitches||pitches->isEmpty()||pitches->size()>4) return false;
    if(identity.hasProperty("attack_structure")) {
        const auto structure=identity.getProperty("attack_structure",{}).toString();
        if(structure!="single_tone"&&structure!="chord") return false;
        if(structure=="chord"&&(pitches->size()<2||!bool(layer.getProperty("polyphonic",false)))) return false;
    }
    std::set<int> seen;
    for(const auto& value:*pitches)
        if(!value.isInt()||int(value)<minimumPitch||int(value)>maximumPitch||!seen.insert(int(value)).second) return false;
    for(const auto* field:{"harmonic_fit","rhythmic_sentence","variation_intent"})
        if(identity.getProperty(field,{}).toString().trim().isEmpty()) return false;
    return true;
}
inline bool allowsPitch(const juce::var& root,const juce::String& id,int pitch) {
    const auto values=retainedPitches(root,id);
    const auto* pitches=values.getArray();
    return !pitches||pitches->contains(juce::var(pitch));
}
inline bool chordal(const juce::var& root,const juce::String& id) {
    if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
        if(layer.getProperty("instrument_id",{}).toString()==id)
            return layer.getProperty("static_identity",{}).getProperty("attack_structure",{}).toString()=="chord";
    return false;
}
inline bool chordAttacks(const juce::Array<juce::var>& notes) {
    std::map<double,std::set<int>> attacks;
    for(const auto& note:notes) attacks[double(note.getProperty("beat",0))].insert(int(note.getProperty("pitch",0)));
    for(const auto& [beat,pitches]:attacks) if(pitches.size()<2) return false;
    return true; // Empty is a withdrawal, never a request for generated filler.
}
// Encode the AI's own declared pitch set, never choose or retune a note.
inline juce::var constrainNotePitch(juce::var note,const juce::var& pitches,const juce::var& definitions) {
    note=note.clone();
    const auto ref=note.getProperty("$ref",{}).toString();
    if(ref.startsWith("#/$defs/")) {
        const auto target=definitions.getProperty(juce::Identifier(ref.substring(8)),{});
        if(target.isObject()) note=target.clone();
    }
    if(auto* choices=note.getProperty("anyOf",{}).getArray())
        for(auto& choice:*choices) choice=constrainNotePitch(choice,pitches,definitions);
    else if(auto* properties=note.getProperty("properties",{}).getDynamicObject()) {
        auto pitch=juce::JSON::parse(R"({"type":"integer"})");
        pitch.getDynamicObject()->setProperty("enum",pitches.clone());
        properties->setProperty("pitch",pitch);
    }
    return note;
}
// Read-only projection of authored rhythmic identities, not a pattern generator.
inline juce::String writerBrief(const juce::var& root) {
    juce::Array<juce::var> identities;
    const auto* layers=root.getProperty("layers",{}).getArray();
    if(!layers) return {};
    for(const auto& layer:*layers) {
        if(layer.getProperty("material_behavior",{}).toString()!="static_tonal_rhythm") continue;
        auto entry=new juce::DynamicObject();
        for(const auto* field:{"instrument_id","cycle_beats","cycle_origin_beat","rhythm_cell","timing_mode","articulation","interaction","states","static_identity"})
            entry->setProperty(field,layer.getProperty(field,{}).clone());
        if(const auto* lanes=root.getProperty("tonal_nucleus",{}).getProperty("lanes",{}).getArray())
            for(const auto& lane:*lanes)
                if(lane.getProperty("instrument_id",{})==layer.getProperty("instrument_id",{}))
                    entry->setProperty("authored_reference",lane.clone());
        identities.add(juce::var(entry));
    }
    if(identities.isEmpty()) return {};
    return "\nSTATIC TONAL RHYTHM IDENTITIES (read-only AI plan): "+juce::JSON::toString(identities,true)+
        "\nRealize these as simultaneous complementary pitched rhythmic layers in their planned body states. "
        "Their composition is the recognizable attack/rest/accent pattern on a nearly fixed pitch or interval, "
        "not a melody waiting to be developed. For attack_structure=chord, each rhythmic attack "
        "is an actual simultaneous voiced chord, not one note or an arpeggiated stand-in. "
        "Single-tone layers remain independent functions. A syncopated cell may alternate a sparse and denser bar; "
        "the exact pattern, cycle length, notes and gates are yours, not a supplied preset. "
        "Keep the reference identity over complete cycles; do not turn every return into new speech or "
        "thin it into occasional cues. Preserve planned withdrawals and variants. Distinguish these owners "
        "from bass, held harmony and foreground: one of those does not replace several rhythmic identities. "
        "The static_identity pitch_policy is an authored decision: retain_anchor keeps its chosen pitches "
        "even as the bass/pad harmony changes; harmonic_variant permits the explained adaptation. Do not "
        "silently turn every harmonic arrival into a transposition of all rhythmic layers. Articulate the "
        "complete rhythmic_sentence, including its attack groups and gaps, rather than reducing it to "
        "occasional chord markers. For unchanged complete repetitions you may author the explicit repeat "
        "object; otherwise write all events.\n";
}
inline bool stable(const juce::String& json,const juce::String& id) {
    const auto root=juce::JSON::parse(json);
    if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& l:*layers)
        if(l.getProperty("instrument_id",{}).toString()==id)
            return l.getProperty("material_behavior",{}).toString()=="stable_foundation" ||
                l.getProperty("material_behavior",{}).toString()=="static_tonal_rhythm";
    return false;
}
inline juce::var repeatSchema(const juce::var& notes) {
    auto object=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["cycle_beats","repeat_count","notes"],"properties":{"cycle_beats":{"type":"number","minimum":0.25,"multipleOf":0.25},"repeat_count":{"type":"integer","minimum":1,"maximum":64},"notes":{}}})");
    object.getProperty("properties",{}).getDynamicObject()->setProperty("notes",notes.clone());
    auto result=new juce::DynamicObject(); result->setProperty("anyOf",juce::Array<juce::var>{notes.clone(),object});
    return juce::var(result);
}
}
