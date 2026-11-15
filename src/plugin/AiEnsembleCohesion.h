#pragma once
#include "core/SongComposer.h"
#include "AiChromaticPolicy.h"
#include <juce_core/juce_core.h>
#include <set>
#include <cmath>
#include <algorithm>

namespace pulso::plugin::cohesion {
// A ceiling, not a target note count: longer ensembles need space for the same
// reference fields that already exist in the schema. Small anchors keep 8k.
inline int shortScoreOutputBudget(const SongPlan& plan,bool recovery=false) {
    const auto scenes=std::clamp(static_cast<int>(plan.sections.size()),1,8);
    const auto owners=std::clamp(static_cast<int>(plan.instruments.size()),1,64);
    const auto base=std::clamp(4000+scenes*2200+owners*100,8000,28000);
    return recovery ? std::min(40000,base+std::max(4000,base/2)) : base;
}
// AI short score precedes layer planning; never rendered or tiled by host code.
inline juce::String shortScoreSchema(const SongPlan* plan=nullptr) {
    auto root=juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["identity","journey"],"properties":{"identity":{"type":"string"},
      "journey":{"type":"array","minItems":1,"maxItems":8,"items":{
        "type":"object","additionalProperties":false,
        "required":["section_index","function","connection","destination","support_response","notes"],
        "properties":{"section_index":{"type":"integer","minimum":0},
          "function":{"type":"string"},"connection":{"type":"string"},
          "destination":{"type":"string"},"support_response":{"type":"string"},
          "notes":{"type":"array","minItems":1,"maxItems":32,"items":{
            "type":"object","additionalProperties":false,
            "required":["beat","duration","pitch"],"properties":{
              "beat":{"type":"number","minimum":0,"maximum":127},
              "duration":{"type":"number","minimum":0.0625,"maximum":128},
              "pitch":{"type":"integer","minimum":0,"maximum":127}}}}}}}}})json");
    auto item=root.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{});
    auto props=item.getProperty("properties",{});
    // Use native MIDI onset + positive gate. Asking for absolute releases made
    // the model repeatedly put short gate values before their own onset.
    auto noteSchema=props.getProperty("notes",{}).getProperty("items",{});
    noteSchema.getProperty("properties",{}).getProperty("duration",{}).getDynamicObject()->setProperty("description",
        "AI-chosen positive held length in quarter-note beats, NOT an absolute release. beat + duration must be <= 128 in this local reference.");
    root.getProperty("required",{}).getArray()->add("reference_timing");
    root.getProperty("properties",{}).getDynamicObject()->setProperty("reference_timing",
        juce::JSON::parse(R"({"type":"string","enum":["excerpt_durations_v2"],"description":"beat is onset and duration is a positive held length, in a local reference excerpt, NOT absolute song time. Each notes/continuation/handoff reference has its own zero; ensemble_excerpt shares one zero across its owners."})"));
    if(plan&&!plan->sections.empty()) {
        juce::Array<juce::var> sectionIds;
        for(std::size_t index=0;index<plan->sections.size();++index) sectionIds.add(static_cast<int>(index));
        props.getProperty("section_index",{}).getDynamicObject()->setProperty("enum",sectionIds);
        props.getProperty("section_index",{}).getDynamicObject()->setProperty("description",
            "An existing macro section index, NOT the journey entry number. Several musical moments may share the same section index; never invent a section for each moment.");
    }
    if(plan) AiChromaticPolicy::constrain(*props.getProperty("notes",{}).getProperty("items",{}).getProperty("properties",{}).getProperty("pitch",{}).getDynamicObject(),*plan,VoiceId::Lead,0,127);
    // An actual second sentence, not another adjective describing development.
    props.getDynamicObject()->setProperty("continuation_notes",props.getProperty("notes",{}).clone());
    props.getDynamicObject()->setProperty("development_route",juce::JSON::parse(R"({"type":"string","description":"How the initial sentence leads to the continuation across this section: recognisable returns, evolving middle, any purposeful plateau or handoff, and preparation of the next section. Not a fixed repeat count."})"));
    item.getProperty("required",{}).getArray()->add("continuation_notes");
    item.getProperty("required",{}).getArray()->add("development_route");
    props.getDynamicObject()->setProperty("register_route",juce::JSON::parse(R"({"type":"string","description":"AI-chosen expressive register journey: initial tessitura, any motivated expansion or contraction, relation to earlier material and return. Explicitly retain a narrow tessitura when it serves the work. Stay within existing owner ranges."})"));
    item.getProperty("required",{}).getArray()->add("register_route");
    props.getDynamicObject()->setProperty("vertical_plan",juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["foreground_landings","support_voicing","tension_resolution"],"properties":{"foreground_landings":{"type":"string"},"support_voicing":{"type":"string"},"tension_resolution":{"type":"string"}}})"));
    item.getProperty("required",{}).getArray()->add("vertical_plan");
    auto excerpt=props.getProperty("notes",{}).clone();
    if(plan&&plan->compositionBehavior==CompositionBehavior::Club)
        excerpt.getDynamicObject()->setProperty("maxItems",128); // Shared chord tones count separately.
    auto excerptItem=excerpt.getProperty("items",{});
    auto ownerSchema=juce::JSON::parse(R"({"type":"string"})");
    if(plan && !plan->instruments.empty()) {
        juce::Array<juce::var> owners;
        for(const auto& owner:plan->instruments) owners.add(juce::String(owner.id));
        ownerSchema.getDynamicObject()->setProperty("enum",owners);
    }
    excerptItem.getProperty("properties",{}).getDynamicObject()->setProperty("owner_id",ownerSchema);
    excerptItem.getProperty("required",{}).getArray()->add("owner_id");
    props.getDynamicObject()->setProperty("ensemble_excerpt",excerpt);
    item.getProperty("required",{}).getArray()->add("ensemble_excerpt");
    auto handoff=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["from_owner","to_owner","inherited_gesture","answer_and_return","notes"],"properties":{"from_owner":{"type":"string"},"to_owner":{"type":"string"},"inherited_gesture":{"type":"string"},"answer_and_return":{"type":"string"},"notes":{"type":"array"}}})");
    auto hp=handoff.getProperty("properties",{});
    hp.getDynamicObject()->setProperty("notes",props.getProperty("notes",{}).clone());
    auto aftermath=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["owner_id","consequence","next_entry","retained_pulse"],"properties":{"owner_id":{"type":"string"},"consequence":{"type":"string"},"next_entry":{"type":"string"},"retained_pulse":{"type":"string"}}})");
    hp.getDynamicObject()->setProperty("after_answer",aftermath);
    handoff.getProperty("required",{}).getArray()->add("after_answer");
    if (plan && !plan->instruments.empty()) {
        juce::Array<juce::var> ids;
        for(const auto& owner:plan->instruments) ids.add(juce::String(owner.id));
        for(const auto* key:{"from_owner","to_owner"}) hp.getProperty(key,{}).getDynamicObject()->setProperty("enum",ids);
        aftermath.getProperty("properties",{}).getProperty("owner_id",{}).getDynamicObject()->setProperty("enum",ids);
    }
    juce::Array<juce::var> alternatives;
    alternatives.add(handoff);
    alternatives.add(juce::JSON::parse(R"({"type":"null"})"));
    auto choice=new juce::DynamicObject();
    choice->setProperty("anyOf",alternatives);
    props.getDynamicObject()->setProperty("handoff",juce::var(choice));
    item.getProperty("required",{}).getArray()->add("handoff");
    root.getProperty("properties",{}).getDynamicObject()->setProperty("ensemble_ending",juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["prepared_phrase","shared_arrival","speaker_release","after_arrival"],"properties":{"prepared_phrase":{"type":"string"},"shared_arrival":{"type":"string"},"speaker_release":{"type":"string"},"after_arrival":{"type":"string"}}})"));
    root.getProperty("required",{}).getArray()->add("ensemble_ending");
    return juce::JSON::toString(root);
}
// Lossless time-interval decoding only. Never select a release, clamp a gate,
// shift an onset, infer a missing note or change the caller on invalid input.
inline bool decodeReferenceTiming(juce::var& source,juce::String& error) {
    error.clear();
    if(!source.hasProperty("reference_timing")) return true; // Historical duration references.
    const auto encoding=source.getProperty("reference_timing",{}).toString();
    const bool durations=encoding=="excerpt_durations_v2";
    if(!durations&&encoding!="excerpt_endpoints_v1") {
        error="Unknown short-score reference timing encoding";return false;
    }
    auto candidate=source.clone();
    auto* journey=candidate.getProperty("journey",{}).getArray();
    if(!journey){error="Endpoint short score has no journey array";return false;}
    for(int i=0;i<journey->size();++i) {
        auto passage=(*journey)[i];
        for(const auto* field:{"notes","continuation_notes","ensemble_excerpt","handoff"}) {
            const auto holder=juce::String(field)=="handoff"?passage.getProperty("handoff",{}):passage;
            if(juce::String(field)=="handoff"&&holder.isVoid()) continue;
            const auto values=holder.getProperty(juce::String(field)=="handoff"?"notes":field,{});
            auto* notes=values.getArray();
            if(!notes) continue; // Structural validator diagnoses missing fields.
            for(int n=0;n<notes->size();++n) {
                auto note=(*notes)[n];const auto start=note.getProperty("beat",{}),end=note.getProperty(durations?"duration":"end_beat",{});
                const auto numeric=[](const juce::var& value){return value.isInt()||value.isInt64()||value.isDouble();};
                const double onset=start,release=durations?double(start)+double(end):double(end);
                if(!numeric(start)||!numeric(end)||!std::isfinite(onset)||!std::isfinite(release)||
                   onset<0||onset>127||release<=onset||release>128||
                   (durations&&double(end)<0.0625)||
                   (!durations&&note.hasProperty("duration")&&std::abs(double(note.getProperty("duration",0))-(release-onset))>.0001)||
                   (durations&&note.hasProperty("end_beat"))) {
                    error="short_score.journey["+juce::String(i)+"]."+field+"["+juce::String(n)+
                        (durations?"] has invalid excerpt onset/duration: ":"] has invalid/conflicting excerpt endpoints: ")+juce::JSON::toString(note,true)+
                        ". Choose a positive held length within the local reference; no note was clipped.";
                    return false;
                }
                if(!durations) note.getDynamicObject()->setProperty("duration",release-onset);
            }
        }
    }
    source=candidate;return true;
}
struct ReferenceTimingTarget { juce::String key; juce::var note; };
inline std::vector<ReferenceTimingTarget> invalidReferenceTimings(const juce::var& source) {
    std::vector<ReferenceTimingTarget> targets;
    const auto encoding=source.getProperty("reference_timing",{}).toString();
    if(encoding!="excerpt_endpoints_v1"&&encoding!="excerpt_durations_v2") return targets;
    if(const auto* journey=source.getProperty("journey",{}).getArray())
        for(int i=0;i<journey->size();++i) for(const auto* field:{"notes","continuation_notes","ensemble_excerpt","handoff"}) {
            const auto holder=juce::String(field)=="handoff"?(*journey)[i].getProperty("handoff",{}):(*journey)[i];
            if(const auto* notes=holder.getProperty(juce::String(field)=="handoff"?"notes":field,{}).getArray())
                for(int n=0;n<notes->size();++n) {
                    auto probe=juce::JSON::parse(R"({"reference_timing":"excerpt_endpoints_v1","journey":[{"notes":[]}]})");
                    probe.getDynamicObject()->setProperty("reference_timing",encoding);
                    probe.getProperty("journey",{}).getArray()->getReference(0).getProperty("notes",{}).getArray()->add((*notes)[n].clone());
                    juce::String error;
                    if(!decodeReferenceTiming(probe,error))
                        targets.push_back({"j"+juce::String(i)+"_"+field+"_"+juce::String(n),(*notes)[n]});
                }
        }
    return targets;
}
inline juce::String referenceTimingRepairSchema(const juce::var& source) {
    const auto targets=invalidReferenceTimings(source);
    if(targets.empty()) return {};
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":[],"properties":{}})");
    for(const auto& target:targets) {
        auto field=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["beat","duration_beats"],"properties":{"beat":{"type":"number","minimum":0,"maximum":127},"duration_beats":{"type":"number","minimum":0.0625,"maximum":128}}})");
        const auto start=target.note.getProperty("beat",{});
        if((start.isInt()||start.isInt64()||start.isDouble())&&std::isfinite(double(start))&&double(start)>=0&&double(start)<=127) {
            field.getProperty("properties",{}).getProperty("beat",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{start});
            field.getProperty("properties",{}).getProperty("duration_beats",{}).getDynamicObject()->setProperty("maximum",128-double(start));
        }
        root.getProperty("properties",{}).getDynamicObject()->setProperty(target.key,field);
        root.getProperty("required",{}).getArray()->add(target.key);
    }
    return juce::JSON::toString(root);
}
// Only the AI replaces invalid endpoints. No host-selected gate, onset, pitch,
// added note, or mutation of already valid references; commit atomically.
inline bool repairReferenceTiming(juce::var& source,const juce::var& correction,juce::String& error) {
    const auto targets=invalidReferenceTimings(source);
    if(targets.empty()||!correction.isObject()||correction.getDynamicObject()->getProperties().size()!=int(targets.size())) return false;
    auto candidate=source.clone();
    for(const auto& target:targets) {
        const auto patch=correction.getProperty(target.key,{});
        if(!patch.isObject()||patch.getDynamicObject()->getProperties().size()!=2||!patch.hasProperty("beat")||!patch.hasProperty("duration_beats")) return false;
        const auto start=patch.getProperty("beat",{}),gate=patch.getProperty("duration_beats",{});
        const auto numeric=[](const juce::var& v){return v.isInt()||v.isInt64()||v.isDouble();};
        if(!numeric(start)||!numeric(gate)||!std::isfinite(double(start))||!std::isfinite(double(gate))||double(gate)<.0625) return false;
        const auto oldStart=target.note.getProperty("beat",{});
        if(numeric(oldStart)&&std::isfinite(double(oldStart))&&double(oldStart)>=0&&double(oldStart)<=127&&double(oldStart)!=double(start)) return false;
        const auto parts=juce::StringArray::fromTokens(target.key,"_","");
        const int index=parts[0].substring(1).getIntValue(),noteIndex=parts[parts.size()-1].getIntValue();
        const auto field=target.key.substring(target.key.indexOfChar('_')+1,target.key.lastIndexOfChar('_'));
        auto passage=candidate.getProperty("journey",{}).getArray()->getReference(index);
        auto holder=field=="handoff"?passage.getProperty("handoff",{}):passage;
        auto note=holder.getProperty(field=="handoff"?"notes":field,{}).getArray()->getReference(noteIndex);
        note.getDynamicObject()->setProperty("beat",start);
        if(source.getProperty("reference_timing",{}).toString()=="excerpt_endpoints_v1") {
            note.getDynamicObject()->setProperty("end_beat",double(start)+double(gate));
            if(note.hasProperty("duration")) note.getDynamicObject()->setProperty("duration",gate);
        } else note.getDynamicObject()->setProperty("duration",gate);
    }
    if(!decodeReferenceTiming(candidate,error)) return false;
    source=candidate;return true;
}
inline juce::String shortScorePrompt(const SongPlan& plan, const juce::String& direction) {
    juce::String out="Compose a playable SHORT SCORE before planning layers. Find the "
        "instrumental work's musical argument, not backing with scattered cues. Compose "
        "connected pitch/rhythm sentences for its identity, substantive continuation, "
        "meaningful contrast and earned return, as appropriate to the request. These are "
        "functions, not four mandatory sections. Each journey entry is a complete local "
        "phrase excerpt with beat starting at zero, not markers stretched over a section. "
        "Compose continuation_notes as a SECOND connected sentence with its own local beat zero: "
        "it must develop the first sentence's actual material, not reduce it to two target notes. "
        "development_route explains the musical path THROUGH the section: where memory returns, "
        "how the middle proceeds, who carries it during rests, and what prepares the next stage. "
        "These two sentences are source material for a continuous argument, not the only allowed "
        "activity. In register_route decide whether height contributes to this passage's "
        "meaning: an initially contained tessitura may open through a developed contour, "
        "receive an answer in another part of the permitted range, or return lower with "
        "changed weight. Compose that trajectory in the reference notes when chosen, "
        "not only in adjectives. Preserve the motif's rhythmic identity and prepare any "
        "leap through phrasing and harmonic support. A narrow hypnotic line is equally "
        "valid; do not require a two-octave span, an upward climax, a fixed register change "
        "per section, or automatic octave copies. Existing owner ranges and explicit "
        "user register requests remain binding. Expansion must develop the thought, "
        "not replace it with an isolated high spike. These sentences also support "
        "activity at the beginning and end. A deliberately static passage is allowed: describe "
        "its function instead of inventing arbitrary ornament. Do not require one new idea per "
        "technical window, a constant solo, or maximum density. Repetition can change meaning "
        "through the accompaniment's voice-leading or a delayed phrase consequence. "
        "Use handoff when a focal withdrawal should develop the shared argument: name "
        "the source and receiving existing owners, the actual inherited interval/rhythm, "
        "and compose a connected answering sentence in notes at the receiver's register. "
        "answer_and_return explains how that answer develops and prepares the next focal entry. "
        "after_answer identifies who carries the consequence AFTER the answering sentence "
        "finishes, until the next focal entry. consequence names the specific inherited "
        "gesture, harmonic suspense or deliberately settled plateau; next_entry explains "
        "what the returning voice will answer and how its arrival becomes audible. "
        "retained_pulse states what stays stable underneath. A completed bass reply is not "
        "permission to reset the remaining scene to anonymous backing. Nor does it require "
        "a constant solo: a held expectation, changed inner voice or intentional repose "
        "may carry the consequence. Describe its musical purpose and place in the section, "
        "not a compulsory event every few bars. Never invent an absent fourth voice. "
        "The answer is an independent related thought, not a transposed copy or one target note. "
        "A harmony owner can carry it as an audible inner/top voice while retaining its chord "
        "floor; a bass can respond through a shaped tonal phrase while preserving meter. "
        "Do not turn every support into a solo or require a handoff for every rest. Set handoff "
        "to null for intentional unadorned plateaus, single-voice music or passages that "
        "continue with the same speaker. Never create extra tracks. "
        "In vertical_plan, identify the real foreground landing pitches and the concurrent "
        "bass/chord tones. support_voicing specifies feasible registers and retained, omitted "
        "or moving chord extensions around those landings without replacing the harmony. "
        "tension_resolution names deliberate simultaneous friction and its preparation/release, "
        "or states that no such friction is intended. Do not assume all scale tones sound "
        "settled together: a sustained semitone or compound minor ninth may need a chosen "
        "voice-leading response. Preserve purposeful suspensions and passing tones; this is "
        "not a ban on dissonance, extensions or chromaticism allowed by the request. "
        "Do not change the foreground sentence merely to fit a generic stacked chord. "
        "Compose ensemble_excerpt as one short playable reduction of the section's most "
        "important overlapping phrase/arrival: foreground AND its sounding support notes "
        "on ONE shared local beat-zero clock, with existing owner_id on every note. "
        "Use at most 32 notes, selecting the relevant moment rather than compressing the "
        "whole section. Include the preparation and release of any tension you describe. "
        "This is concrete counterpoint, not a list of independent arpeggios or compulsory "
        "simultaneous attacks. Retain intentional rests. Do not sustain every chord extension "
        "through the melody simply because all pitches belong to the scale. A promised "
        "moving inner voice must actually move in these notes; calling an unchanged cluster "
        "a suspension does not supply its resolution. Use exact MIDI pitches and onset/release times, "
        "not mistaken interval names. Preserve harmonic color and the foreground identity. "
        "In ensemble_ending, coordinate the final phrase with the actual bass/chord arrival "
        "and the speaker's release. Describe their temporal relation in the final scene, "
        "not merely three independent tonic notes. A speaker may overlap the arrival or "
        "hand it to another voice intentionally; do not accidentally release the destination "
        "just as its supporting final chord starts. Prepare any intervening silence with "
        "a concrete voice carrying expectation, rather than appending a disconnected "
        "terminal cue. after_arrival says what sustains and what releases without reopening "
        "the question. An explicit loop/excerpt may have a deliberate open ending instead. "
        "No compulsory last-bar attack, fixed sustain length or automatic prolongation. "
        "Choose natural phrase lengths. Later passages answer actual earlier notes, not "
        "just the same ascent with another last note. Give the middle its own developed "
        "material. Preserve Club's physical pulse and memory when requested, without "
        "reducing the argument to repeated backing. Respect explicit drone/accompaniment "
        "requests instead of imposing solos. support_response describes how simultaneous "
        "bass/harmony participate in the sentence's tension and arrival. These are YOUR "
        "reference notes, never commands for automatic looping. The full writer develops "
        "these sentences over the form. Respect the tonal policy, palette and voice ranges.\n";
    if(plan.compositionBehavior==CompositionBehavior::Club)
        out="Compose a playable CLUB GROOVE SHORT SCORE, not a chamber dialogue. The musical "
            "argument may be the interaction of repeated bass, chord stabs and tonal rhythmic "
            "layers. Begin with their complete playable body in ensemble_excerpt on one shared "
            "clock: include the repeated attacks, chord voicings, short gates, accents and "
            "intentional gaps that make it convincing without a lead. A chord stab means "
            "simultaneous pitched chord tones with rhythmic articulation, not a single-note "
            "cue or a sustained pad. Single-tone pulses remain separate valid functions. "
            "Choose density and patterns yourself; no mandatory attack count or supplied groove. "
            "journey notes and continuation_notes describe an identifiable focus gesture and "
            "its meaningful continuation or recurring variant, not two compulsory melodic "
            "speeches. development_route describes how the groove is established, retained, "
            "revealed or withdrawn; register_route need not expand a solo. handoff=null is "
            "valid when the groove continues. A hook is welcome when requested but is not a "
            "license to turn every layer into counterpoint. Respect the actual harmonic map: "
            "a held chord may support intense rhythmic motion; never invent harmonic changes "
            "to demonstrate development. vertical_plan describes actual simultaneous voicings "
            "and intended tensions. For a full song, earn larger contrast and a final release; "
            "for a production section or loop, ensemble_ending describes its intentional wrap, "
            "not a compulsory tonic cadence. Do not clip phrases or leave accidental gaps. "
            "Reference notes, voicings, repeats and every later performance are authored by AI.\n";
    out << "\nREFERENCE CLOCK: Write beat and duration, never end_beat. Duration is the positive held "
        "length, not an absolute release position. Each notes, continuation_notes "
        "and handoff.notes phrase uses its own excerpt-local zero. ensemble_excerpt uses ONE local "
        "zero shared by all its owners. These are short playable references, not absolute positions "
        "in the complete song or a full-section transcription. Describe their later placement in "
        "development_route; do not offset reference notes to minute/section/song positions. "
        "Choose duration so beat + duration <= 128 within this reference "
        "clock. The host preserves your onset and held length; it never clips a release. This "
        "reference bound does not limit the duration of the composed song.\n";
    out << "REQUEST: " << direction << "\nKEY: " << juce::String(plan.key)
        << "\nDURATION_BARS: " << plan.totalBars << "\nBEATS_PER_BAR: " << plan.beatsPerBar;
    for (std::size_t i=0;i<plan.sections.size();++i) {
        const auto& s=plan.sections[i];
        out << "\nSECTION " << static_cast<int>(i) << ": " << juce::String(s.name) << " bars=" << s.bars;
        for (const auto& e:s.harmonicEvents) out << " " << e.barOffset << ":" << juce::String(e.chordId);
    }
    for (const auto& c:plan.chordPalette) {
        out << "\nCHORD " << juce::String(c.id) << ":";
        for (auto pc:c.pitchClasses) out << pc << ",";
    }
    for (const auto& owner:plan.instruments)
        out << "\nVOICE " << juce::String(owner.id) << " range=" << owner.minimumPitch << ".." << owner.maximumPitch;
    return out;
}
inline bool validShortScore(const juce::var& root, const SongPlan& plan,juce::String* diagnostic=nullptr) {
    if(diagnostic) diagnostic->clear();
    const auto fail=[&](const juce::String& why){if(diagnostic)*diagnostic=why;return false;};
    if(root.hasProperty("ensemble_ending"))
        for(const auto* key:{"prepared_phrase","shared_arrival","speaker_release","after_arrival"}) {
            const auto value=root.getProperty("ensemble_ending",{}).getProperty(key,{});
            if(!value.isString() || value.toString().trim().isEmpty()) return fail("short_score.ensemble_ending."+juce::String(key)+" is empty or invalid");
        }
    const auto* journey=root.getProperty("journey",{}).getArray();
    if (!journey || journey->isEmpty() || journey->size()>8 || root.getProperty("identity",{}).toString().trim().isEmpty()) return fail("short_score needs identity and 1..8 journey entries");
    int journeyIndex=0;
    for (const auto& passage:*journey) {
        const auto location="short_score.journey["+juce::String(journeyIndex++)+"]";
        const auto index=passage.getProperty("section_index",{});
        if (!index.isInt() || static_cast<int>(index)<0 || static_cast<int>(index)>=static_cast<int>(plan.sections.size()))
            return fail(location+".section_index="+index.toString()+" does not name an existing macro section (count="+juce::String(static_cast<int>(plan.sections.size()))+"). Musical moments are not additional sections.");
        for (const auto* key:{"function","connection","destination","support_response"})
            if (!passage.getProperty(key,{}).isString() || passage.getProperty(key,{}).toString().trim().isEmpty()) return fail(location+"."+key+" is empty or invalid");
        if (passage.hasProperty("continuation_notes") && passage.getProperty("development_route",{}).toString().trim().isEmpty()) return fail(location+".development_route is empty");
        if(passage.hasProperty("register_route") && (!passage.getProperty("register_route",{}).isString() || passage.getProperty("register_route",{}).toString().trim().isEmpty())) return fail(location+".register_route is empty or invalid");
        if (passage.hasProperty("vertical_plan")) {
            const auto vertical=passage.getProperty("vertical_plan",{});
            for(const auto* key:{"foreground_landings","support_voicing","tension_resolution"})
                if(!vertical.getProperty(key,{}).isString() || vertical.getProperty(key,{}).toString().trim().isEmpty()) return fail(location+".vertical_plan."+key+" is empty or invalid");
        }
        const auto handoff=passage.getProperty("handoff",{});
        if (!handoff.isVoid()) {
            if (!handoff.isObject()) return fail(location+".handoff must be an object or null");
            for(const auto* field:{"from_owner","to_owner"}) {
                bool found=false;
                for(const auto& owner:plan.instruments)
                    if (handoff.getProperty(field,{}).toString()==juce::String(owner.id)) found=true;
                if (!found) return fail(location+".handoff."+field+" is not an existing owner");
            }
            if(handoff.getProperty("from_owner",{})==handoff.getProperty("to_owner",{})) return fail(location+".handoff has the same source and receiver");
            for(const auto* field:{"inherited_gesture","answer_and_return"})
                if(handoff.getProperty(field,{}).toString().trim().isEmpty()) return fail(location+".handoff."+field+" is empty");
            if(handoff.hasProperty("after_answer")) {
                const auto after=handoff.getProperty("after_answer",{});
                bool found=false;
                for(const auto& owner:plan.instruments)
                    if(after.getProperty("owner_id",{}).toString()==juce::String(owner.id)) found=true;
                if(!found) return fail(location+".handoff.after_answer.owner_id is not an existing owner");
                for(const auto* key:{"consequence","next_entry","retained_pulse"})
                    if(!after.getProperty(key,{}).isString() || after.getProperty(key,{}).toString().trim().isEmpty()) return fail(location+".handoff.after_answer."+key+" is empty or invalid");
            }
        }
        for (const auto* field:{"notes","continuation_notes","handoff","ensemble_excerpt"}) {
        if (juce::String(field)=="ensemble_excerpt" && !passage.hasProperty(field)) continue;
        if (juce::String(field)=="continuation_notes" && !passage.hasProperty(field)) continue; // old checkpoints
        if (juce::String(field)=="handoff" && handoff.isVoid()) continue;
        const auto* notes=(juce::String(field)=="handoff"?handoff.getProperty("notes",{}):passage.getProperty(field,{})).getArray();
        const auto noteLimit=juce::String(field)=="ensemble_excerpt"&&plan.compositionBehavior==CompositionBehavior::Club?128:32;
        if (!notes || notes->isEmpty() || notes->size()>noteLimit) return fail(location+"."+field+" needs 1.."+juce::String(noteLimit)+" reference notes");
        int noteIndex=0;
        for (const auto& note:*notes) {
            const auto noteLocation=location+"."+field+"["+juce::String(noteIndex++)+"]";
            if(juce::String(field)=="ensemble_excerpt") {
                bool found=false;
                for(const auto& owner:plan.instruments)
                    if(note.getProperty("owner_id",{}).toString()==juce::String(owner.id)) found=true;
                if(!found) return fail(noteLocation+".owner_id is not an existing owner");
            }
            const double beat=note.getProperty("beat",-1), duration=note.getProperty("duration",-1);
            const auto pitch=note.getProperty("pitch",{});
            if (!std::isfinite(beat)||!std::isfinite(duration)||beat<0||duration<=0||beat+duration>128||
                !pitch.isInt()||static_cast<int>(pitch)<0||static_cast<int>(pitch)>127||!AiChromaticPolicy::allows(plan,static_cast<int>(pitch)))
                return fail(noteLocation+" has invalid timing or tonal pitch: "+juce::JSON::toString(note,true));
        }
        }
    }
    return true;
}
inline juce::Array<juce::var> ambiguousHandoffSources(const juce::var& score) {
    juce::Array<juce::var> indices;
    if(const auto* journey=score.getProperty("journey",{}).getArray())
        for(int i=0;i<journey->size();++i) {
            const auto h=(*journey)[i].getProperty("handoff",{});
            if(h.isObject() && h.getProperty("from_owner",{}).toString().isNotEmpty() &&
                h.getProperty("from_owner",{})==h.getProperty("to_owner",{})) indices.add(i);
        }
    return indices;
}
inline juce::String handoffSourceSchema(const juce::var& score,const SongPlan& plan) {
    const auto indices=ambiguousHandoffSources(score);
    auto root=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["corrections"],"properties":{"corrections":{"type":"array","items":{}}}})");
    juce::Array<juce::var> choices;
    for(const auto& index:indices) {
        auto item=juce::JSON::parse(R"({"type":"object","additionalProperties":false,"required":["journey_index","from_owner","reason"],"properties":{"journey_index":{"type":"integer"},"from_owner":{"type":"string"},"reason":{"type":"string"}}})");
        const auto receiver=score.getProperty("journey",{})[static_cast<int>(index)].getProperty("handoff",{}).getProperty("to_owner",{}).toString();
        juce::Array<juce::var> owners;
        for(const auto& owner:plan.instruments) if(juce::String(owner.id)!=receiver) owners.add(juce::String(owner.id));
        if(owners.isEmpty()) return {}; // No guessed source for a solo composition.
        auto props=item.getProperty("properties",{});
        props.getProperty("journey_index",{}).getDynamicObject()->setProperty("enum",juce::Array<juce::var>{index});
        props.getProperty("from_owner",{}).getDynamicObject()->setProperty("enum",owners);
        choices.add(item);
    }
    if(choices.isEmpty()) return {};
    auto list=root.getProperty("properties",{}).getProperty("corrections",{});
    list.getDynamicObject()->setProperty("minItems",indices.size());
    list.getDynamicObject()->setProperty("maxItems",indices.size());
    list.getProperty("items",{}).getDynamicObject()->setProperty("anyOf",choices);
    return juce::JSON::toString(root);
}
inline bool repairHandoffSources(juce::var& score,const juce::var& repair,const SongPlan& plan) {
    const auto indices=ambiguousHandoffSources(score);
    const auto* edits=repair.getProperty("corrections",{}).getArray();
    if(!edits || edits->size()!=indices.size()) return false;
    auto candidate=score.clone();
    std::set<int> seen;
    for(const auto& edit:*edits) {
        const auto index=edit.getProperty("journey_index",{});
        if(!index.isInt()||!indices.contains(index)||!seen.insert(static_cast<int>(index)).second) return false;
        auto handoff=candidate.getProperty("journey",{})[static_cast<int>(index)].getProperty("handoff",{});
        const auto owner=edit.getProperty("from_owner",{}).toString();
        bool known=false;
        for(const auto& voice:plan.instruments) if(juce::String(voice.id)==owner) known=true;
        if(!known||owner==handoff.getProperty("to_owner",{}).toString()||edit.getProperty("reason",{}).toString().trim().isEmpty()) return false;
        handoff.getDynamicObject()->setProperty("from_owner",owner);
    }
    if(!validShortScore(candidate,plan)) return false;
    score=candidate; return true;
}
// Read-only evidence: never edits, rejects or automatically resolves a dissonance.
inline juce::String excerptEvidence(const juce::var& passage) {
    const auto* notes=passage.getProperty("ensemble_excerpt",{}).getArray();
    if(!notes) return {};
    juce::String out="\nSHARED-CLOCK ENSEMBLE EXCERPT: ";
    out << juce::JSON::toString(passage.getProperty("ensemble_excerpt",{}),true);
    int reported=0;
    for(int i=0;i<notes->size() && reported<12;++i)
        for(int j=i+1;j<notes->size() && reported<12;++j) {
            const auto& a=notes->getReference(i); const auto& b=notes->getReference(j);
            const double as=a.getProperty("beat",0), bs=b.getProperty("beat",0);
            const double ae=as+static_cast<double>(a.getProperty("duration",0));
            const double be=bs+static_cast<double>(b.getProperty("duration",0));
            const double overlap=std::min(ae,be)-std::max(as,bs);
            const int ap=a.getProperty("pitch",0), bp=b.getProperty("pitch",0);
            const int distance=std::abs(ap-bp);
            const bool retrigger=ap==bp && as!=bs && a.getProperty("owner_id",{})==b.getProperty("owner_id",{});
            if(overlap<=0 || (distance%12!=1 && !retrigger)) continue;
            out << "\nOVERLAP EVIDENCE: " << a.getProperty("owner_id",{}).toString() << ":" << ap
                << " with " << b.getProperty("owner_id",{}).toString() << ":" << bp
                << " interval_semitones=" << distance << " overlap_beats=" << overlap;
            ++reported;
        }
    return out+"\nThese are reference intervals, not automatic errors or a template to loop. "
        "Realize the chosen voice-leading and articulation in the full score. If the notes "
        "contradict a claimed resolution, compose that resolution or an intentional stable "
        "sonority yourself while keeping the phrase, harmonic function and pulse. Verify "
        "the same relationships against actual accepted notes, not just this excerpt.\n";
}
// Shared composition intent, not a MIDI generator or a density gate.
inline juce::String brief() {
    return "\nENSEMBLE COHESION: Plan ensemble_arguments before assigning independent layer states. "
        "For each section choose one audible shared thought and its destination. Decide what "
        "the listener follows, how that thought continues from the previous section, and how "
        "each participating owner changes or supports its meaning. A stable pulse may support "
        "a complete developing sentence; shared scale membership alone is not a relationship. "
        "Write each contribution against the SAME phrase destination and harmonic timeline. "
        "Specify concrete contour, accent grouping, voice-leading or held tension, not adjectives. "
        "A return recalls a recognizable gesture and answers something left pending. "
        "When a focal voice rests, identify what carries the attention or explicitly choose "
        "a resting plateau; do not accidentally reserve all substantive speech for the edges. "
        "A requested song is complete instrumental music, not backing awaiting an absent singer. "
        "An explicitly requested accompaniment, drone or loop remains that instead. "
        "No extra tracks, density quota, constant solo or obligatory harmonic change. "
        "Reconcile these arguments with phrases, references, scenes and layer states BEFORE "
        "writing notes: an absent layer cannot deliver the planned answer. Sustained harmony "
        "and stabs on the same owner must have intentional articulation, not unintended "
        "overlapping retriggers of the same pitch. All onsets, durations and pitches are yours.\n";
}

inline juce::String extend(const juce::String& schema, const SongPlan& plan) {
    if (plan.sections.empty() || plan.instruments.empty()) return schema;
    auto root = juce::JSON::parse(schema);
    auto item = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["section_index","foreground_owner","shared_thought","inherited_question","shared_arrival","contributions"],
      "properties":{"section_index":{"type":"integer"},"foreground_owner":{"type":"string"},
        "shared_thought":{"type":"string"},"inherited_question":{"type":"string"},
        "shared_arrival":{"type":"string"},"contributions":{"type":"array","minItems":1,
          "items":{"type":"object","additionalProperties":false,
            "required":["owner_id","relationship","phrase_path"],"properties":{
              "owner_id":{"type":"string"},"relationship":{"type":"string"},
              "phrase_path":{"type":"string"}}}}}})json");
    juce::Array<juce::var> sections, owners;
    for (int s=0; s<static_cast<int>(plan.sections.size()); ++s) sections.add(s);
    for (const auto& owner:plan.instruments) owners.add(juce::String(owner.id));
    auto props=item.getProperty("properties", {});
    props.getProperty("section_index", {}).getDynamicObject()->setProperty("enum", sections);
    props.getProperty("foreground_owner", {}).getDynamicObject()->setProperty("enum", owners);
    auto contributions=props.getProperty("contributions", {});
    contributions.getDynamicObject()->setProperty("maxItems", owners.size());
    contributions.getProperty("items", {}).getProperty("properties", {}).getProperty("owner_id", {})
        .getDynamicObject()->setProperty("enum", owners);
    auto list=juce::JSON::parse(R"({"type":"array"})");
    list.getDynamicObject()->setProperty("minItems", sections.size());
    list.getDynamicObject()->setProperty("maxItems", sections.size());
    list.getDynamicObject()->setProperty("items", item);
    root.getProperty("required", {}).getArray()->add("ensemble_arguments");
    root.getProperty("properties", {}).getDynamicObject()->setProperty("ensemble_arguments", list);
    return juce::JSON::toString(root);
}

inline bool validate(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!root.hasProperty("ensemble_arguments")) return true; // Existing checkpoints.
    const auto* items=root.getProperty("ensemble_arguments", {}).getArray();
    std::set<int> sections;
    std::set<juce::String> owners;
    for (const auto& owner:plan.instruments) owners.insert(juce::String(owner.id));
    const auto fail=[&](const juce::String& detail) { error="Ensemble arguments: "+detail; return false; };
    if (!items || items->size()!=static_cast<int>(plan.sections.size())) return fail("expected exactly "+juce::String(int(plan.sections.size()))+" section arguments");
    int argumentIndex=-1;
    for (const auto& item:*items) {
        ++argumentIndex;
        const auto location="entry "+juce::String(argumentIndex)+": ";
        const auto index=item.getProperty("section_index", {});
        const int s=static_cast<int>(index);
        if (!index.isInt() || s<0 || s>=items->size()) return fail(location+"invalid section_index "+index.toString());
        if (!sections.insert(s).second) return fail(location+"duplicate section_index "+index.toString());
        const auto foreground=item.getProperty("foreground_owner", {}).toString();
        if(!owners.contains(foreground)) return fail(location+"unknown foreground_owner "+foreground);
        for (const auto* key:{"shared_thought","inherited_question","shared_arrival"})
            if (!item.getProperty(key, {}).isString() || item.getProperty(key, {}).toString().trim().isEmpty()) return fail(location+"missing musical intent in "+key);
        const auto* parts=item.getProperty("contributions", {}).getArray();
        std::set<juce::String> used;
        if (!parts || parts->isEmpty() || parts->size()>static_cast<int>(owners.size())) return fail(location+"invalid contributions count");
        for (const auto& part:*parts) {
            auto id=part.getProperty("owner_id", {}).toString();
            if (!owners.contains(id)) return fail(location+"unknown contribution owner "+id);
            if (!used.insert(id).second) return fail(location+"duplicate contribution owner "+id);
            for (const auto* key:{"relationship","phrase_path"})
                if (!part.getProperty(key, {}).isString() || part.getProperty(key, {}).toString().trim().isEmpty()) return fail(location+"missing "+key+" for "+id);
        }
        if (!used.contains(foreground)) return fail(location+"foreground owner has no contribution: "+foreground);
    }
    return true;
}

inline juce::String argumentRepairSchema(const SongPlan& plan) {
    if(plan.sections.empty()||plan.instruments.empty()) return {};
    return extend(R"({"type":"object","additionalProperties":false,"required":[],"properties":{}})",plan);
}
inline bool replaceArguments(const juce::String& original,const juce::String& patch,
                            const SongPlan& plan,juce::String& merged,juce::String& error) {
    auto candidate=juce::JSON::parse(original);
    const auto correction=juce::JSON::parse(patch);
    if(!candidate.isObject()||!correction.isObject()||
       correction.getDynamicObject()->getProperties().size()!=1||!correction.hasProperty("ensemble_arguments")) return false;
    if(!validate(correction,plan,error)) return false;
    auto accepted=correction.clone();
    // Preserve each individually valid, uniquely identified original argument.
    // Only inconsistent/missing entries need AI-authored replacement.
    if(const auto* originals=candidate.getProperty("ensemble_arguments",{}).getArray())
        for(const auto& originalItem:*originals) {
            const auto section=originalItem.getProperty("section_index",{});
            int occurrences=0;
            for(const auto& item:*originals) if(item.getProperty("section_index",{})==section) ++occurrences;
            if(occurrences!=1) continue;
            auto probe=accepted.clone();
            auto* entries=probe.getProperty("ensemble_arguments",{}).getArray();
            for(auto& entry:*entries) if(entry.getProperty("section_index",{})==section) {entry=originalItem.clone();break;}
            juce::String ignored;
            if(validate(probe,plan,ignored)) accepted=probe;
        }
    candidate.getDynamicObject()->setProperty("ensemble_arguments",accepted.getProperty("ensemble_arguments",{}).clone());
    merged=juce::JSON::toString(candidate);return true;
}

inline juce::String context(const juce::String& json, std::size_t sectionIndex,
                           int firstBar=-1, int bars=0, int sectionBars=0) {
    const auto root=juce::JSON::parse(json);
    const auto* items=root.getProperty("ensemble_arguments", {}).getArray();
    if (!items) return {};
    juce::String out="\nSHARED ENSEMBLE ARGUMENT (the writing window is not a new phrase):\n";
    const auto shortScore=root.getProperty("compositional_short_score",{});
    if(shortScore.hasProperty("ensemble_ending") && sectionIndex+1==static_cast<std::size_t>(items->size()))
        out << "\nFINAL ENSEMBLE HANDOFF: " << juce::JSON::toString(shortScore.getProperty("ensemble_ending",{}),true)
            << "\nRealize the temporal relationship of speaker, bass and chord arrival in "
               "actual notes, not just common tonic pitch classes. Check release times as "
               "well as attacks: preserve a connected sentence or explicit intentional "
               "handoff across the arrival. Do not append an unrelated final cue after "
               "the phrase has already ended, or prolong a note merely to reach the boundary. "
               "Respect existing accepted music and write only this window.\n";
    if (firstBar>=0 && sectionBars>0)
        out << "CURRENT SECTION POSITION: zero-based bars " << firstBar << ".." << firstBar+bars
            << " (exclusive) of " << sectionBars << ". This is "
            << (firstBar==0?"the opening":firstBar+bars>=sectionBars?"the closing span":"an interior span")
            << ", not a new exposition. Follow the development_route from the actual accepted notes. "
               "In an interior span, realize the connected continuation or intentional plateau; "
               "do not collapse the argument into target-note reminders over anonymous backing. "
               "An accepted arrival may rest; a planned unfinished sentence still needs its answer.\n";
    if (root.hasProperty("compositional_short_score"))
        out << "AI COMPOSED MUSICAL ARGUMENT, reference only, never tile automatically: "
            << juce::JSON::toString(root.getProperty("compositional_short_score",{}),true)
            << "\nDevelop the current stage's complete sentence with the ensemble; future stages "
               "are context, not material to expose prematurely. Preserve the rhythmic/interval "
               "identity, continuation and arrival rather than just the first notes. "
               "Use continuation_notes and development_route to compose what happens BETWEEN "
               "the opening excerpt and the next scene. Do not mechanically paste either excerpt. "
               "Let harmony and bass respond to the actual phrase direction; their stable pulse "
               "does not require static voicing or an unrelated melodic destination. "
               "Where a handoff is planned, the named receiving owner's actual notes must "
               "carry its connected answering sentence at the receiver's register. When that "
               "owner is harmony, distinguish the speaking inner/top voice from the chord pulse; "
               "do not hide the answer in unchanged stabs or double a held pitch with retriggers. "
               "Respect its planned speaking time and return, not one fill per writing window. "
               "A null handoff permits genuine rest or a plateau; never auto-fill it.\n";
    if (const auto* journey=root.getProperty("compositional_short_score",{}).getProperty("journey",{}).getArray())
        for(const auto& passage:*journey)
            if(static_cast<int>(passage.getProperty("section_index",-1))==static_cast<int>(sectionIndex) && passage.hasProperty("vertical_plan"))
                out << "\nCURRENT VERTICAL PLAN: " << juce::JSON::toString(passage.getProperty("vertical_plan",{}),true)
                    << "\nCheck the actual overlapping durations of ALL voices before returning this window. "
                       "Voice the chord floor around the foreground arrivals and bass; check its internal "
                       "extensions too. A change of octave alone may retain a harsh compound minor ninth. "
                       "For unintended sustained friction, compose a compatible support voicing or "
                       "connected resolution while preserving the phrase, groove and harmonic function. "
                       "Do not solve it by silencing the floor, stripping all color, moving accepted "
                       "notes or mechanically snapping pitches. Keep intentional prepared tensions.\n";
    // Bind the authored handoff to its real receiver contract, instead of leaving
    // the writer to reconcile two distant and potentially conflicting contexts.
    if (const auto* journey=root.getProperty("compositional_short_score",{}).getProperty("journey",{}).getArray())
        for (const auto& passage:*journey) {
            if (static_cast<int>(passage.getProperty("section_index",-1))!=static_cast<int>(sectionIndex)) continue;
            if(passage.hasProperty("register_route"))
                out << "\nCURRENT EXPRESSIVE REGISTER ROUTE: " << passage.getProperty("register_route",{}).toString()
                    << "\nRealize the chosen contour in the actual line within its owner range. "
                       "Preserve accepted notes, rhythm and connected voice-leading. Height is "
                       "an expressive decision, never an octave-span quota or a copied transposition.\n";
            out << excerptEvidence(passage);
            const auto handoff=passage.getProperty("handoff",{});
            if (!handoff.isObject()) continue;
            const auto receiver=handoff.getProperty("to_owner",{}).toString();
            if(handoff.hasProperty("after_answer"))
                out << "\nAFTER THE ANSWER, BEFORE THE NEXT ENTRY: "
                    << juce::JSON::toString(handoff.getProperty("after_answer",{}),true)
                    << "\nRead the accepted notes to determine whether the reply is still unfolding "
                       "or has already arrived. Continue it if unfinished; otherwise compose its "
                       "planned consequence toward the next entry, not a restart of the same reply "
                       "at each technical window. The named carrier can retain a purposeful sustain "
                       "or repose. Keep the groove stable while the shared argument changes meaning. "
                       "Do not fill silence by quota, add tracks or reopen a completed ending.\n";
            out << "\nCURRENT HANDOFF RECEIVER: " << receiver << "\nCONNECTED SENTENCE: "
                << juce::JSON::toString(handoff,true);
            if (const auto* layers=root.getProperty("layers",{}).getArray()) for (const auto& layer:*layers)
                if (layer.getProperty("instrument_id",{}).toString()==receiver) {
                    out << "\nRECEIVER TIMING: mode=" << layer.getProperty("timing_mode",{}).toString()
                        << " cycle_beats=" << layer.getProperty("cycle_beats",{}).toString()
                        << " rhythm_cell=" << juce::JSON::toString(layer.getProperty("rhythm_cell",{}),true)
                        << " polyphonic=" << layer.getProperty("polyphonic",{}).toString();
                    if (const auto* states=layer.getProperty("states",{}).getArray()) for(const auto& state:*states)
                        if(static_cast<int>(state.getProperty("section_index",-1))==static_cast<int>(sectionIndex))
                            out << "\nRECEIVER ACTIVITY: " << juce::JSON::toString(state,true);
                }
            out << "\nREALIZE THE ENTIRE ANSWERING GESTURE: references are musical source, not "
                "fixed timestamps. If their subdivision differs from this receiver's legal "
                "attacks, recompose the connected sentence yourself in that grammar, retaining "
                "its ordered contour, response and destination at a natural phrase tempo. "
                "Do not drop incompatible notes, collapse sequential pitches into a chord, "
                "or replace the reply with the old backing pattern. Preserve declared rests "
                "and accepted notes; continue any unfinished reply across technical windows. "
                "The planned phrase_route determines when the reply speaks, not every window.\n";
        }
    for (int offset=-1;offset<=1;++offset) for (const auto& item:*items)
        if (static_cast<int>(item.getProperty("section_index", -2))==static_cast<int>(sectionIndex)+offset)
            out << (offset<0?"INHERITED: ":offset>0?"DESTINATION, not to play prematurely: ":"CURRENT: ")
                << juce::JSON::toString(item,true)<<"\n";
    return out + "Use accepted notes as the actual unfinished thought, not merely a template. "
        "Continue the speaker's phrase and coordinate the other owners with its arrival. "
        "Do not replace a declared developing sentence with isolated cues over unchanged backing. "
        "A sustain or rest is welcome when it carries that thought. Preserve accepted MIDI; "
        "author the remaining notes together. No automatic filling or duplicated melodic tracks.\n";
}
}
