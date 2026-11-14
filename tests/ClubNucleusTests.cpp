#include "plugin/AiClubProductionPlan.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
void setting(const char* key, const juce::String& value) {
#ifdef _WIN32
    _putenv_s(key, value.toRawUTF8());
#else
    if (value.isEmpty()) unsetenv(key); else setenv(key, value.toRawUTF8(), 1);
#endif
}
}

#include "plugin/AiHarmonicCycleContract.h"
#include "plugin/AiEnsembleCohesion.h"
#include "plugin/AiPhraseArchitecture.h"
#include "plugin/AiComposer.h"
#include "plugin/AiPlanExecution.h"
#include "plugin/AiPlanReconciliation.h"
#include "plugin/AiStructuredResponse.h"
#include "plugin/MidiIntegrity.h"
#include "plugin/AiFoundation.h"
#include "plugin/AiRequestedFunctions.h"
#include "plugin/AiMonophonicEvents.h"
#include "plugin/AiGrooveHierarchy.h"
#include "plugin/AiGrooveRepair.h"
#include "plugin/AiSectionTrajectory.h"
int main(int argc,char** argv) {
    using namespace pulso;
    using namespace pulso::plugin;
    try {
      {
        const auto templateRoot=juce::JSON::parse(club::production::schema());
        const auto row=templateRoot.getProperty("properties",{}).getProperty("energy_trajectory",{}).getProperty("items",{});
        const auto one=section_trajectory::schema(row,1),two=section_trajectory::schema(row,2);
        check(one.getProperty("required",{}).size()==1&&one.getProperty("properties",{}).hasProperty("section_0")&&
              !one.getProperty("properties",{}).hasProperty("section_1"),"single-section trajectory permits invented summaries");
        check(two.getProperty("required",{}).size()==2&&!two.getProperty("properties",{}).getProperty("section_1",{}).getProperty("properties",{}).hasProperty("section_index"),"trajectory key and payload can disagree about section identity");
        const juce::String input=R"({"energy_trajectory":{"section_0":{"energy_intent":0.64,"density_intent":0.62,"layer_change":"reveal, retain then withdraw within the same section"}},"tonal_nucleus":{"lanes":[{"instrument_id":"bass","notes":[{"beat":0,"pitch":38,"duration":1}]}]}})";
        juce::String decoded,error;
        check(section_trajectory::decode(input,1,decoded,error),"keyed trajectory rejected");
        const auto normalized=juce::JSON::parse(decoded),raw=juce::JSON::parse(input);
        const auto summaries=normalized.getProperty("energy_trajectory",{});
        check(summaries.isArray()&&summaries.size()==1&&int(summaries[0].getProperty("section_index",-1))==0&&
              double(summaries[0].getProperty("energy_intent",0))==.64,"decoder invented, averaged or reordered section energy");
        check(summaries[0].getProperty("layer_change",{})==raw.getProperty("energy_trajectory",{}).getProperty("section_0",{}).getProperty("layer_change",{}),"decoder flattened the AI's internal musical phases");
        check(juce::JSON::toString(normalized.getProperty("tonal_nucleus",{}))==juce::JSON::toString(raw.getProperty("tonal_nucleus",{})),"trajectory codec changed musical source notes");
        const auto before=decoded;
        check(section_trajectory::decode(before,1,decoded,error)&&decoded==before,"trajectory decoding is not idempotent");
        decoded="unchanged";
        check(!section_trajectory::decode(input.replace("section_0","section_1"),1,decoded,error)&&decoded=="unchanged","unknown section was silently renumbered");
        check(!section_trajectory::decode(input,2,decoded,error),"missing section filled automatically");
        auto conflict=raw.clone();conflict.getProperty("energy_trajectory",{}).getProperty("section_0",{}).getDynamicObject()->setProperty("section_index",1);
        check(!section_trajectory::decode(juce::JSON::toString(conflict),1,decoded,error),"conflicting section identities accepted");
        const juce::String legacy=R"({"energy_trajectory":[{"section_index":0,"energy_intent":0.7}]})";
        check(section_trajectory::decode(legacy,1,decoded,error)&&decoded==legacy,"historical trajectory rewritten");
        const juce::String duplicates=R"({"energy_trajectory":[{"section_index":0},{"section_index":0},{"section_index":0}]})";
        check(section_trajectory::decode(duplicates,1,decoded,error)&&decoded==duplicates,"codec silently merged invalid historical summaries");
      }
      {
        const auto schema=juce::JSON::parse(cohesion::shortScoreSchema());
        const auto note=schema.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{})
            .getProperty("properties",{}).getProperty("notes",{}).getProperty("items",{});
        check(!note.getProperty("properties",{}).hasProperty("duration")&&
              int(note.getProperty("properties",{}).getProperty("end_beat",{}).getProperty("maximum",0))==128,
              "reference schema still permits independent duration to overflow the reference clock");
        check(note.getProperty("required",{}).getArray()->contains("end_beat"),"explicit AI release is not required");
        auto reference=juce::JSON::parse(R"({"reference_timing":"excerpt_endpoints_v1","journey":[{"notes":[{"beat":0,"end_beat":2,"pitch":60}],"continuation_notes":[{"beat":0.5,"end_beat":1.25,"pitch":62}],"ensemble_excerpt":[{"beat":113,"end_beat":128,"owner_id":"pad_foundation","pitch":50}],"handoff":{"notes":[{"beat":1,"end_beat":2.5,"pitch":64}]}}]})");
        juce::String error;const auto raw=juce::JSON::toString(reference);
        check(cohesion::decodeReferenceTiming(reference,error),"valid explicit endpoint references rejected");
        const auto passage=reference.getProperty("journey",{})[0];
        const auto sustained=passage.getProperty("ensemble_excerpt",{})[0];
        check(double(sustained.getProperty("beat",0))==113&&double(sustained.getProperty("end_beat",0))==128&&
              double(sustained.getProperty("duration",0))==15&&int(sustained.getProperty("pitch",0))==50,
              "endpoint decoder chose or changed an onset/release/pitch");
        check(double(passage.getProperty("continuation_notes",{})[0].getProperty("duration",0))==.75&&
              double(passage.getProperty("handoff",{}).getProperty("notes",{})[0].getProperty("duration",0))==1.5,
              "continuation/handoff clocks decoded incorrectly");
        const auto decoded=juce::JSON::toString(reference);
        check(cohesion::decodeReferenceTiming(reference,error)&&decoded==juce::JSON::toString(reference),"endpoint decoding is not idempotent");
        auto bad=juce::JSON::parse(raw);bad.getProperty("journey",{})[0].getProperty("ensemble_excerpt",{})[0].getDynamicObject()->setProperty("end_beat",129);
        const auto before=juce::JSON::toString(bad);
        check(!cohesion::decodeReferenceTiming(bad,error)&&error.contains("ensemble_excerpt[0]"),"overflowing AI release accepted without precise diagnostic");
        check(before==juce::JSON::toString(bad),"overflowing reference was clipped or partially rewritten");
        bad=juce::JSON::parse(raw);bad.getProperty("journey",{})[0].getProperty("notes",{})[0].getDynamicObject()->setProperty("end_beat",0);
        check(!cohesion::decodeReferenceTiming(bad,error),"zero-length endpoint interval accepted");
        bad=juce::JSON::parse(decoded);bad.getProperty("journey",{})[0].getProperty("notes",{})[0].getDynamicObject()->setProperty("duration",3);
        check(!cohesion::decodeReferenceTiming(bad,error),"conflicting endpoint and duration silently reconciled");
        auto legacy=juce::JSON::parse(R"({"journey":[{"notes":[{"beat":0,"duration":1,"pitch":60}]}]})");
        const auto old=juce::JSON::toString(legacy);
        check(cohesion::decodeReferenceTiming(legacy,error)&&old==juce::JSON::toString(legacy),"historical duration reference changed");
      }
      {
        SongPlan plan;plan.compositionBehavior=CompositionBehavior::Club;plan.totalBars=4;plan.sections.resize(1);plan.sections[0].bars=4;
        for(const auto* id:{"bass","stab","pulse","clock"}){InstrumentAssignment owner;owner.id=id;owner.sourceVoice=VoiceId::Lead;owner.minimumPitch=48;owner.maximumPitch=84;plan.instruments.push_back(owner);}
        auto root=juce::JSON::parse(R"({"groove_hierarchy":{"identity_owners":["bass","stab"],"signature":"paired accents followed by a common breath","accent_offsets":[0,2],"support_roles":[{"owner_id":"pulse","role":"counterweight","dynamic_intent":"quiet groups subordinate to the main accents"},{"owner_id":"clock","role":"clock","dynamic_intent":"soft regular reference"}],"shared_spaces":[{"from_beat":1,"duration_beats":0.5,"owner_ids":["bass","stab","pulse"],"meaning":"the main rhythm breathes while the quiet clock stays"}]},"tonal_nucleus":{"cycle_beats":4,"lanes":[{"instrument_id":"bass","notes":[{"beat":0},{"beat":2}]},{"instrument_id":"stab","notes":[{"beat":0},{"beat":2}]},{"instrument_id":"pulse","notes":[{"beat":0.5},{"beat":2.5}]},{"instrument_id":"clock","notes":[{"beat":0},{"beat":1},{"beat":2},{"beat":3}]}]},"layers":[{"instrument_id":"bass","timing_mode":"cycle_bound","cycle_beats":4,"cycle_origin_beat":0,"rhythm_cell":[0,1,2]}]})");
        root.getDynamicObject()->setProperty("kind","club_production_v1");
        juce::String error;const auto original=juce::JSON::toString(root);
        check(groove_hierarchy::validate(root,plan,error),"valid joint rhythmic hierarchy rejected");
        check(original==juce::JSON::toString(root),"hierarchy validation edited source rhythm");
        check(!groove_hierarchy::attackAllowed(root,"bass",1)&&!groove_hierarchy::attackAllowed(root,"pulse",5.25),"AI-declared shared space lost cycle phase");
        check(groove_hierarchy::attackAllowed(root,"clock",1)&&groove_hierarchy::attackAllowed(root,"bass",1.5),"shared space imposed blanket silence or wrong end boundary");
        check(!club::production::coreAttackAllowed(original,"bass",1)&&club::production::coreAttackAllowed(original,"bass",2),"existing onset validator ignored joint phrase");
        plan.phraseArchitectureJson=original.toStdString();PerformanceScore score;
        check(AiComposer::parseDirectWindowJson(R"({"pulse":[{"beat":0.5,"duration":2,"pitch":62,"velocity":80}]})",plan,{2},0,0,4,score,error),"shared attack-space incorrectly clipped a ringing note");
        check(score.cells[0].notes[0].durationBeats==2,"joint groove altered a held gate");
        check(!AiComposer::parseDirectWindowJson(R"({"pulse":[{"beat":1.25,"duration":0.25,"pitch":62,"velocity":80}]})",plan,{2},0,0,4,score,error)&&error.contains("shared space"),"MIDI parser ignored AI-authored joint phrase space");
        auto invalid=root.clone();invalid.getProperty("tonal_nucleus",{}).getProperty("lanes",{})[0].getProperty("notes",{}).getArray()->add(juce::JSON::parse(R"({"beat":1})"));
        const auto invalidBefore=juce::JSON::toString(invalid);
        check(!groove_hierarchy::validate(invalid,plan,error)&&error.contains("reference attacks inside"),"plan promised space but reference filled it");
        check(invalidBefore==juce::JSON::toString(invalid),"invalid reference note deleted automatically");
        invalid=root.clone();invalid.getProperty("groove_hierarchy",{}).getProperty("accent_offsets",{}).getArray()->set(0,1.75);
        check(!groove_hierarchy::validate(invalid,plan,error),"fictional main accent accepted without source attack");
        invalid=root.clone();invalid.getProperty("groove_hierarchy",{}).getProperty("support_roles",{})[0].getDynamicObject()->setProperty("owner_id","bass");
        check(!groove_hierarchy::validate(invalid,plan,error),"same owner simultaneously primary and subordinate");
        check(groove_hierarchy::attackAllowed(juce::JSON::parse("{}"),"bass",1)&&groove_hierarchy::validate(juce::JSON::parse("{}"),plan,error),"legacy groove changed without an authored hierarchy");
        auto schema=juce::JSON::parse(R"({"properties":{},"required":[]})");groove_hierarchy::extend(schema,&plan);
        const auto spaces=schema.getProperty("properties",{}).getProperty("groove_hierarchy",{}).getProperty("properties",{}).getProperty("shared_spaces",{});
        check(int(spaces.getProperty("minItems",0))==0,"rhythmic hierarchy imposes a compulsory rest quota");
        check(groove_repair::handles("Club groove hierarchy: declared main accent has no actual identity attack at beat 3.5")&&
              !groove_repair::handles("Club groove hierarchy: missing joint rhythmic intention"),"hierarchy repair dispatched unrelated failures");
        const auto repairSchema=juce::JSON::parse(groove_repair::schema(plan,original));
        check(repairSchema.getProperty("properties",{}).getProperty("lane_updates",{}).getProperty("items",{}).getProperty("anyOf",{}).size()==3,
              "hierarchy repair scope includes unrelated clock or loses primary/space owners");
        const juce::String repair=R"({"lane_updates":[{"instrument_id":"bass","notes":[{"beat":0,"duration":0.5,"pitch":60,"velocity":90},{"beat":1,"duration":0.25,"pitch":60,"velocity":75},{"beat":2,"duration":0.5,"pitch":60,"velocity":85}],"repeat_intent":"retain the shared rhythmic identity"}],"accent_offsets":[0,1,2],"shared_spaces":[{"from_beat":1.25,"duration_beats":0.25,"owner_ids":["bass","stab","pulse"],"meaning":"shared breath after the authored answer"}],"reason":"Complete the intended bass answer and align its breathing declaration"})";
        juce::String merged;
        check(groove_repair::apply(original,repair,merged)&&groove_hierarchy::validate(juce::JSON::parse(merged),plan,error),"AI-authored hierarchy/reference reconciliation rejected");
        const auto after=juce::JSON::parse(merged);
        check(juce::JSON::toString(after.getProperty("layers",{}))==juce::JSON::toString(root.getProperty("layers",{})),"hierarchy repair changed rhythmic grammar");
        check(after.getProperty("groove_hierarchy",{}).getProperty("signature",{})==root.getProperty("groove_hierarchy",{}).getProperty("signature",{}),"hierarchy repair changed musical signature");
        for(int index=1;index<4;++index)
            check(juce::JSON::toString(after.getProperty("tonal_nucleus",{}).getProperty("lanes",{})[index])==
                  juce::JSON::toString(root.getProperty("tonal_nucleus",{}).getProperty("lanes",{})[index]),"hierarchy repair changed unrelated source notes");
        check(after.getProperty("tonal_nucleus",{}).getProperty("lanes",{})[0].getProperty("notes",{}).size()==3,"host altered the AI's replacement note list");
        merged="unchanged";
        check(!groove_repair::apply(original,repair.replace("\"instrument_id\":\"bass\"","\"instrument_id\":\"clock\""),merged)&&merged=="unchanged", "repair escaped its declared owner scope");
        auto forbidden=juce::JSON::parse(repair);forbidden.getDynamicObject()->setProperty("identity_owners",juce::Array<juce::var>{"clock"});
        check(!groove_repair::apply(original,juce::JSON::toString(forbidden),merged),"repair rewrote immutable owner hierarchy");
        auto declarationsOnly=juce::JSON::parse(repair);declarationsOnly.getDynamicObject()->setProperty("lane_updates",juce::Array<juce::var>{});
        check(groove_repair::apply(original,juce::JSON::toString(declarationsOnly),merged)&&
              juce::JSON::toString(juce::JSON::parse(merged).getProperty("tonal_nucleus",{}))==juce::JSON::toString(root.getProperty("tonal_nucleus",{})),
              "declaration-only AI correction changed musical notes");
      }
      {
        SongPlan plan;plan.compositionBehavior=CompositionBehavior::Club;plan.totalBars=4;plan.beatsPerBar=4;
        plan.sections.resize(1);plan.sections[0].bars=4;plan.instruments.resize(1);
        plan.instruments[0].id="pulse";plan.instruments[0].sourceVoice=VoiceId::Lead;
        plan.instruments[0].minimumPitch=48;plan.instruments[0].maximumPitch=84;
        plan.phraseArchitectureJson=R"({"kind":"club_production_v1","layers":[{"instrument_id":"pulse","material_behavior":"static_tonal_rhythm","polyphonic":false,"timing_mode":"cycle_bound","cycle_beats":4,"cycle_origin_beat":0,"rhythm_cell":[0.5],"static_identity":{"pitch_policy":"retain_anchor","anchor_pitches":[62]}}]})";
        const auto schema=juce::JSON::parse(AiComposer::directWindowSchemaFor(plan,{0},0,4,0));
        const auto events=schema.getProperty("properties",{}).getProperty("pulse",{}).getProperty("properties",{}).getProperty("events",{});
        check(events.getProperty("required",{}).size()==4&&events.getProperty("properties",{}).hasProperty("0.50"),"monophonic schema lost permitted cyclic onsets");
        const auto valueSchema=events.getProperty("properties",{}).getProperty("0.50",{}).getProperty("anyOf",{})[0];
        check(!valueSchema.getProperty("properties",{}).hasProperty("beat"),"slot schema still permits competing explicit attack positions");
        const juce::String input=R"({"pulse":{"events":{"0.50":{"duration":0.25,"pitch":62,"velocity":90},"4.50":{"duration":0.5,"pitch":62,"velocity":80},"8.50":null,"12.50":{"duration":1,"pitch":62,"velocity":70}}}})";
        PerformanceScore score;juce::String error;
        check(AiComposer::parseDirectWindowJson(input,plan,{0},0,0,4,score,error),"onset-keyed monophonic performance rejected");
        check(score.cells.size()==1&&score.cells[0].notes.size()==3,"decoder invented a note for null rest");
        const auto& notes=score.cells[0].notes;
        check(notes[0].beat==.5&&notes[1].beat==4.5&&notes[2].beat==12.5&&notes[1].durationBeats==.5&&notes[2].velocity==70,"decoder changed timing/gate/velocity");
        Pattern rendered;PerformanceScoreEngine::replaceChunk(rendered,score,0,0,16,plan.instruments,true,16);rendered.lengthBeats=16;
        InstrumentPart part;part.id=1;part.name="pulse";part.sourceVoice=VoiceId::Lead;rendered.parts.push_back(part);
        const auto midi=juce::File::createTempFile(".mid");
        check(writePatternToMidiFile(rendered,midi,{})&&verifyMidiRoundTrip(midi,rendered,error),"onset-keyed authored events changed during MIDI export");midi.deleteFile();
        check(!AiComposer::parseDirectWindowJson(input.replace("0.50","0.75"),plan,{0},0,0,4,score,error),"slot codec allowed a rhythm displacement");
        check(!AiComposer::parseDirectWindowJson(input.replace("0.50","0.5"),plan,{0},0,0,4,score,error),"alternate beat spelling allows duplicate logical onset");
        juce::Array<juce::var> decoded;
        check(mono_events::decode(juce::JSON::parse(R"({"events":{"0.50":null}})"),decoded,error)&&decoded.isEmpty(),"slot rest filled automatically");
        check(!mono_events::decode(juce::JSON::parse(R"({"events":{"0.50":[{"pitch":62},{"pitch":62}]}})"),decoded,error),"two simultaneous events accepted in a monophonic slot");
        check(!mono_events::decode(juce::JSON::parse(R"({"events":{"0.50":{"beat":0.5,"duration":1,"pitch":62,"velocity":80}}})"),decoded,error),"conflicting beat field accepted in onset-keyed note");
        check(!AiComposer::parseDirectWindowJson(R"({"pulse":[{"beat":0.5,"duration":0.5,"pitch":62,"velocity":90},{"beat":0.5,"duration":0.5,"pitch":62,"velocity":80}]})",plan,{0},0,0,4,score,error),"legacy duplicate attack was silently deleted");
        auto chordPlan=juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
        chordPlan.getProperty("layers",{})[0].getDynamicObject()->setProperty("polyphonic",true);
        check(!mono_events::eligible(chordPlan,"pulse"),"onset-keyed monophonic codec substituted for chord voicing");
      }
      {
        const juce::String base=R"({"properties":{"sections":{"minItems":3,"maxItems":24,"items":{"properties":{"bars":{"minimum":8},"harmonic_events":{"minItems":2}}}},"chord_palette":{"minItems":4},"narrative_spine":{"properties":{"acts":{"minItems":3,"maxItems":24}}}}})";
        const auto clubProperties=juce::JSON::parse(club::production::macroSchema(base,CompositionBehavior::Club,"club",32)).getProperty("properties",{});
        check(int(clubProperties.getProperty("sections",{}).getProperty("minItems",0))==1&&
              int(clubProperties.getProperty("chord_palette",{}).getProperty("minItems",0))==1&&
              int(clubProperties.getProperty("sections",{}).getProperty("items",{}).getProperty("properties",{}).getProperty("harmonic_events",{}).getProperty("minItems",0))==1,
              "Club macro still requires extra form/harmonic events");
        check(club::production::macroSchema(base,CompositionBehavior::Adaptive,"",32)==base,"Club simplification changed non-Club architecture");
        SongPlan plan;plan.compositionBehavior=CompositionBehavior::Club;plan.preserveAuthoredCast=true;
        plan.totalBars=32;plan.targetSeconds=64;plan.rootPitchClass=0;plan.scale=ScaleKind::Major;
        plan.sections.resize(1);plan.sections[0].bars=32;plan.sections[0].harmonicEvents={{0,0,"pedal",.8,"held groove harmony"}};
        HarmonicChord chord;chord.id="pedal";chord.label="Authored pedal";chord.rootPitchClass=0;chord.bassPitchClass=0;chord.pitchClasses={0,4,7};
        plan.chordPalette={chord};
        InstrumentAssignment owner;owner.id="stab";owner.sourceVoice=VoiceId::HarmonicFoundation;plan.instruments={owner};
        SongComposer::normalizePlan(plan);
        check(plan.chordPalette.size()==1&&plan.chordPalette[0].id=="pedal"&&plan.chordPalette[0].pitchClasses==std::vector<int>({0,4,7}),
              "normalization replaced one authored Club harmony with fallback progression");
        check(plan.sections.size()==1&&plan.sections[0].harmonicEvents.size()==1&&plan.sections[0].harmonicEvents[0].chordId=="pedal",
              "normalization inserted harmonic arrivals into sustained Club state");
        check(cohesion::shortScorePrompt(plan,"club").contains("CLUB GROOVE SHORT SCORE"),"Club still uses mandatory melodic short-score brief");
        plan.compositionBehavior=CompositionBehavior::Adaptive;
        check(!cohesion::shortScorePrompt(plan,"chamber").contains("CLUB GROOVE SHORT SCORE"),"groove-first brief leaked into other modes");
      }
      {
        SongPlan plan;plan.compositionBehavior=CompositionBehavior::Club;plan.totalBars=4;plan.beatsPerBar=4;
        plan.sections.resize(1);plan.sections[0].bars=4;plan.instruments.resize(1);
        plan.instruments[0].id="stab";plan.instruments[0].sourceVoice=VoiceId::HarmonicPulse;
        plan.instruments[0].minimumPitch=48;plan.instruments[0].maximumPitch=84;
        plan.phraseArchitectureJson=R"({"kind":"club_production_v1","layers":[{"instrument_id":"stab","polyphonic":true,"timing_mode":"cycle_bound","cycle_beats":4,"cycle_origin_beat":0,"rhythm_cell":[0.5],"material_behavior":"static_tonal_rhythm","static_identity":{"attack_structure":"chord","pitch_policy":"retain_anchor","anchor_pitches":[60,64,67]}}]})";
        PerformanceScore score;juce::String error;
        const auto contract=juce::JSON::parse(R"({"instruments":[{"id":"stab"}],"requested_functions":[{"source_quote":"chord stabs","function":"rhythmic_chords","exact_count":1,"owner_ids":["stab"]}]})");
        check(allocation::validate(juce::JSON::toString(contract),"chord stabs",error)&&allocation::staticOwners(contract).contains("stab"),"rhythmic chord function lost allocation or static identity");
        const auto schema=juce::JSON::parse(allocation::constrainSchema(club::production::schema({},&plan),contract,&plan));
        const auto layerProperties=schema.getProperty("properties",{}).getProperty("layers",{}).getProperty("items",{}).getProperty("anyOf",{})[0].getProperty("properties",{});
        check(bool(layerProperties.getProperty("polyphonic",{}).getProperty("enum",{})[0])&&
              layerProperties.getProperty("static_identity",{}).getProperty("properties",{}).getProperty("attack_structure",{}).getProperty("enum",{})[0].toString()=="chord",
              "chord-stab request permits monophonic planning");
        check(allocation::validateArchitecture(juce::String(plan.phraseArchitectureJson),contract,error),"declared chord allocation rejected");
        const auto nonChord=juce::String(plan.phraseArchitectureJson).replace("\"attack_structure\":\"chord\"","\"attack_structure\":\"single_tone\"");
        check(!allocation::validateArchitecture(nonChord,contract,error),"requested rhythmic chords silently reclassified as one-note pulses");
        check(!AiComposer::parseDirectWindowJson(R"({"stab":[{"beat":0.5,"duration":0.25,"pitch":60,"velocity":90}]})",plan,{0},0,0,4,score,error),"declared chord stab accepted as single-note marker");
        check(error.contains("chord stabs"),"chord structure failure lacks focused repair feedback");
        const juce::String notes=R"({"stab":{"cycle_beats":4,"repeat_count":4,"notes":[{"beat":0.5,"duration":0.25,"pitch":60,"velocity":90},{"beat":0.5,"duration":0.25,"pitch":64,"velocity":84},{"beat":0.5,"duration":0.25,"pitch":67,"velocity":80}]}})";
        check(AiComposer::parseDirectWindowJson(notes,plan,{0},0,0,4,score,error),"authored repetitive chord stab rejected");
        check(score.cells.size()==1&&score.cells[0].notes.size()==3&&score.placements[0].repeats==4,"chord validation composed or removed notes");
        const auto mono=juce::JSON::parse(R"([{"beat":0,"pitch":60},{"beat":1,"pitch":60}])");
        check(!foundation::chordAttacks(*mono.getArray()),"monophonic rhythm mistaken for chord attacks");
        check(foundation::chordAttacks(juce::Array<juce::var>{}),"withdrawn chord layer filled automatically");
      }
      {
        auto root=juce::JSON::parse(R"({"layers":[{"instrument_id":"pulse","material_behavior":"static_tonal_rhythm","static_identity":{"pitch_policy":"retain_anchor","anchor_pitches":[62],"harmonic_fit":"D remains a deliberate pedal against the changing harmony","rhythmic_sentence":"short attack groups and recognizable rests","variation_intent":"retain the sentence until the planned withdrawal"}}]})");
        const auto original=juce::JSON::toString(root);
        auto layer=root.getProperty("layers",{})[0];
        check(foundation::validIdentity(layer,48,72),"valid authored static identity rejected");
        check(foundation::allowsPitch(root,"pulse",62)&&!foundation::allowsPitch(root,"pulse",64),"authored retained anchor silently transposed");
        const auto base=juce::JSON::parse(R"({"$ref":"#/$defs/windowNote"})");
        const auto defs=juce::JSON::parse(R"({"windowNote":{"anyOf":[{"type":"object","properties":{"beat":{"type":"number","enum":[0.5]},"duration":{"type":"number","maximum":3.5},"pitch":{"type":"integer","minimum":0,"maximum":127}}}]}})");
        const auto constrained=foundation::constrainNotePitch(base,foundation::retainedPitches(root,"pulse"),defs);
        const auto props=constrained.getProperty("anyOf",{})[0].getProperty("properties",{});
        check(props.getProperty("pitch",{}).getProperty("enum",{})[0]==juce::var(62),"note schema lost AI-selected retained anchor");
        check(double(props.getProperty("duration",{}).getProperty("maximum",0))==3.5&&
              double(props.getProperty("beat",{}).getProperty("enum",{})[0])==0.5,"pitch contract retimed authored rhythm");
        check(defs.getProperty("windowNote",{}).getProperty("anyOf",{})[0].getProperty("properties",{}).getProperty("pitch",{}).hasProperty("minimum"),"pitch restriction mutated shared note schema");
        check(original==juce::JSON::toString(root),"pitch contract changed authored identity");
        layer.getProperty("static_identity",{}).getDynamicObject()->setProperty("pitch_policy","harmonic_variant");
        check(foundation::allowsPitch(root,"pulse",64),"AI-declared harmonic variant was forbidden");
        layer.getProperty("static_identity",{}).getProperty("anchor_pitches",{}).getArray()->add(62);
        check(!foundation::validIdentity(layer,48,72),"duplicate anchors accepted");
        layer.getProperty("static_identity",{}).getProperty("anchor_pitches",{}).getArray()->set(1,90);
        check(!foundation::validIdentity(layer,48,72),"out-of-register anchor accepted");
        check(foundation::allowsPitch(juce::JSON::parse("{}"),"pulse",64),"historical pitch freedom changed");
      }
      if(argc==3&&juce::String(argv[1])=="--validate-architecture") {
        const auto blueprint=juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString();
        int bars=0;const auto macro=juce::JSON::parse(blueprint);
        if(const auto* sections=macro.getProperty("sections",{}).getArray())
            for(const auto& section:*sections) bars+=int(section.getProperty("bars",0));
        SongPlan plan;plan.preserveAuthoredCast=true;juce::String error;
        check(AiComposer::parseSongPlanJson(blueprint,bars*2,bars,120,4,1,plan,error),"cannot read architecture audit blueprint");
        const std::string input{std::istreambuf_iterator<char>(std::cin),std::istreambuf_iterator<char>()};
        const auto draft=juce::String::fromUTF8(input.c_str());
        if(!AiPhraseArchitecture::validate(draft,plan,error)||!allocation::validateArchitecture(draft,macro,error))
            throw std::runtime_error(error.toStdString());
        std::cout<<"Architecture validated without MIDI changes\n";return 0;
      }
      {
        const juce::String request="Three static layers and one sustained bed";
        const auto source=juce::JSON::parse(R"({"instruments":[{"id":"a"},{"id":"b"},{"id":"c"},{"id":"pad"}],"requested_functions":[{"source_quote":"Three static layers","function":"static_tonal_rhythm","exact_count":3,"owner_ids":["a","b","c"]},{"source_quote":"one sustained bed","function":"sustained_harmony","exact_count":1,"owner_ids":["pad"]}]})");
        const auto original=juce::JSON::toString(source);juce::String error;
        check(allocation::validate(original,request,error),"valid user functional allocation rejected");
        SongPlan cast;for(const auto* id:{"a","b","c","pad"}){InstrumentAssignment owner;owner.id=id;cast.instruments.push_back(owner);}
        const auto shortSchema=cohesion::shortScoreSchema(&cast);
        const auto constrainedShort=juce::JSON::parse(allocation::constrainShortScoreSchema(shortSchema,source));
        const auto shortChoices=constrainedShort.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{})
            .getProperty("properties",{}).getProperty("handoff",{}).getProperty("anyOf",{});
        const auto receivers=shortChoices[0].getProperty("properties",{}).getProperty("to_owner",{}).getProperty("enum",{});
        check(receivers.size()==1&&receivers[0].toString()=="pad","short score still assigns melodic answer to static rhythm owner");
        auto staticOnly=source.clone();staticOnly.getProperty("instruments",{}).getArray()->remove(3);
        const auto noSpeech=juce::JSON::parse(allocation::constrainShortScoreSchema(shortSchema,staticOnly))
            .getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{})
            .getProperty("properties",{}).getProperty("handoff",{}).getProperty("anyOf",{});
        check(noSpeech.size()==1&&noSpeech[0].getProperty("type",{}).toString()=="null","static-only ensemble forced to invent a speaking owner");
        check(allocation::constrainShortScoreSchema(shortSchema,juce::JSON::parse("{}"))==shortSchema,"legacy short-score choices changed");
        auto shortScore=juce::JSON::parse(R"({"journey":[{"handoff":{"to_owner":"a","notes":[{"beat":0,"duration":1,"pitch":60}]} }]})");
        const auto scoreBefore=juce::JSON::toString(shortScore);
        check(!allocation::validateShortScore(shortScore,source,error),"contradictory static-owner melodic handoff accepted");
        check(scoreBefore==juce::JSON::toString(shortScore),"role check changed musical source notes");
        shortScore.getProperty("journey",{})[0].getDynamicObject()->setProperty("handoff",juce::var());
        check(allocation::validateShortScore(shortScore,source,error),"intentional no-handoff plateau rejected");
        check(allocation::context(source).contains("Three static layers"),"authoritative allocation does not reach early musical planning");
        auto bad=source.clone();bad.getProperty("requested_functions",{})[0].getDynamicObject()->setProperty("exact_count",4);
        check(!allocation::validate(juce::JSON::toString(bad),request,error),"underallocated requested function accepted");
        bad=source.clone();bad.getProperty("requested_functions",{})[0].getProperty("owner_ids",{}).getArray()->set(2,"a");
        check(!allocation::validate(juce::JSON::toString(bad),request,error),"one owner counted twice toward requested layering");
        bad=source.clone();bad.getProperty("requested_functions",{})[0].getDynamicObject()->setProperty("source_quote","Four layers");
        check(!allocation::validate(juce::JSON::toString(bad),request,error),"invented user request accepted");
        check(allocation::validate(R"({"instruments":[]})",request,error),"historical cast compatibility lost");
        auto architecture=juce::JSON::parse(R"({"layers":[{"instrument_id":"a","material_behavior":"static_tonal_rhythm","timing_mode":"cycle_bound"},{"instrument_id":"b","material_behavior":"static_tonal_rhythm","timing_mode":"cycle_bound"},{"instrument_id":"c","material_behavior":"static_tonal_rhythm","timing_mode":"cycle_bound"},{"instrument_id":"pad","material_behavior":"stable_foundation","timing_mode":"phrase_flexible"}]})");
        check(allocation::validateArchitecture(juce::JSON::toString(architecture),source,error),"consistent function mapping rejected");
        architecture.getProperty("layers",{})[0].getDynamicObject()->setProperty("timing_mode","phrase_flexible");
        check(!allocation::validateArchitecture(juce::JSON::toString(architecture),source,error),"explicit cyclic identity silently made flexible");
        architecture.getProperty("layers",{})[0].getDynamicObject()->setProperty("timing_mode","cycle_bound");
        architecture.getProperty("layers",{})[2].getDynamicObject()->setProperty("material_behavior","stable_foundation");
        check(!allocation::validateArchitecture(juce::JSON::toString(architecture),source,error),"requested rhythm silently replaced by pad");
        SongPlan plan;plan.totalBars=8;plan.sections.resize(1);plan.sections[0].bars=8;
        for(const auto* id:{"a","b","c","pad"}){InstrumentAssignment owner;owner.id=id;owner.sourceVoice=VoiceId::Lead;owner.minimumPitch=60;owner.maximumPitch=72;plan.instruments.push_back(owner);}
        plan.rootPitchClass=0;plan.scale=ScaleKind::Major;
        const auto savedNucleus=juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_NUCLEUS_V2",{});
        const auto savedChromatic=juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC",{});
        setting("PULSO_AI_CLUB_NUCLEUS_V2","1");
        setting("PULSO_AI_ALLOW_CHROMATIC","0");
        const auto constrained=juce::JSON::parse(allocation::constrainSchema(club::production::schema({},&plan),source,&plan));
        setting("PULSO_AI_CLUB_NUCLEUS_V2",savedNucleus);
        setting("PULSO_AI_ALLOW_CHROMATIC",savedChromatic);
        const auto carriers=constrained.getProperty("properties",{}).getProperty("tonal_nucleus",{}).getProperty("properties",{}).getProperty("meter_carrier_id",{}).getProperty("enum",{});
        check(carriers.size()==3&&!carriers.getArray()->contains("pad"),"sustained flexible bed allowed as mandatory cyclic carrier");
        const auto choices=constrained.getProperty("properties",{}).getProperty("layers",{}).getProperty("items",{}).getProperty("anyOf",{});
        check(choices.size()==4,"allocation constraint lost actual cast choices");
        for(int i=0;i<4;++i){const auto values=choices[i].getProperty("properties",{}).getProperty("material_behavior",{}).getProperty("enum",{});
            check(values.size()==1&&values[0].toString()==(i<3?"static_tonal_rhythm":"stable_foundation"),"writer schema permits requested role substitution");
            const auto timing=choices[i].getProperty("properties",{}).getProperty("timing_mode",{}).getProperty("enum",{});
            check(i<3?(timing.size()==1&&timing[0].toString()=="cycle_bound"):timing.getArray()->contains("phrase_flexible"),"requested rhythm timing or harmonic freedom lost");}
        const auto identity=choices[0].getProperty("properties",{}).getProperty("static_identity",{});
        const auto anchorPitch=identity.getProperty("properties",{}).getProperty("anchor_pitches",{}).getProperty("items",{});
        check(int(anchorPitch.getProperty("minimum",0))==60&&int(anchorPitch.getProperty("maximum",0))==72,
            "new anchor declaration ignored owner register");
        const auto eligible=anchorPitch.getProperty("enum",{});
        check(eligible.isArray()&&eligible.getArray()->contains(62)&&!eligible.getArray()->contains(61),
            "new anchor schema permits out-of-scale declared pitches");
        check(original==juce::JSON::toString(source),"role constraint mutated source request allocation");
      }
      {
        const auto root=juce::JSON::parse(R"({"layers":[{"instrument_id":"pulse_a","material_behavior":"static_tonal_rhythm","cycle_beats":8,"rhythm_cell":[0.5,0.75,2.75]},{"instrument_id":"pulse_b","material_behavior":"static_tonal_rhythm","cycle_beats":4,"rhythm_cell":[1,3]},{"instrument_id":"lead","material_behavior":"developing_voice"}],"tonal_nucleus":{"lanes":[{"instrument_id":"pulse_a","notes":[{"beat":0.5,"duration":0.25,"pitch":60,"velocity":99}]}]}})");
        const auto original=juce::JSON::toString(root);
        const auto brief=foundation::writerBrief(root);
        check(brief.contains("pulse_a")&&brief.contains("pulse_b")&&brief.contains("authored_reference"),"writer lost multiple static rhythm identities or exact reference");
        check(!brief.contains("\"lead\""),"melodic voice incorrectly frozen as static rhythm");
        check(original==juce::JSON::toString(root),"identity projection changed authored plan");
        check(foundation::writerBrief(juce::JSON::parse("{}")).isEmpty(),"legacy plan invented static owners");
      }
      {
        Pattern p;p.lengthBeats=8;
        NoteEvent n;n.startBeat=0;n.durationBeats=4;n.pitch=60;n.velocity=90;p.notes.push_back(n);
        n.startBeat=1;n.durationBeats=1;n.velocity=70;p.notes.push_back(n);
        const auto file=juce::File::createTempFile(".mid");juce::String error;
        check(writePatternToMidiFile(p,file,{})&&verifyMidiRoundTrip(file,p,error),"nested equal-pitch events falsely paired FIFO");
        p.notes[1].velocity=71;
        check(!verifyMidiRoundTrip(file,p,error),"altered attack velocity passed raw event audit");
        p.notes[1].velocity=70;p.notes[1].durationBeats=0.5;
        check(!verifyMidiRoundTrip(file,p,error),"altered release passed raw event audit");
        file.deleteFile();
      }
      {
        auto root=juce::JSON::parse(R"({"focus_owner_id":"lead","groove_core_ids":["bass","pulse"],"groove_interlock":{"bass_owner_id":"bass","chord_owner_id":"bed","foreground_owner_id":"lead"}})");
        const std::set<std::string> owners{"bass","bed","lead","pulse"};
        check(club::production::interlockOwnersValid(root,owners),"non-core harmonic bed rejected as interlock owner");
        auto link=root.getProperty("groove_interlock",{});
        link.getDynamicObject()->setProperty("chord_owner_id","bass");
        check(!club::production::interlockOwnersValid(root,owners),"duplicate interlock owners accepted");
        link.getDynamicObject()->setProperty("chord_owner_id","ghost");
        check(!club::production::interlockOwnersValid(root,owners),"unknown interlock owner accepted");
        link.getDynamicObject()->setProperty("chord_owner_id","bed");
        link.getDynamicObject()->setProperty("foreground_owner_id","pulse");
        check(!club::production::interlockOwnersValid(root,owners),"wrong foreground focus accepted");
        link.getDynamicObject()->setProperty("foreground_owner_id","bed");
        root.getDynamicObject()->setProperty("focus_owner_id","bed");
        check(!club::production::interlockOwnersValid(root,owners),"foreground shared with chord owner accepted");
      }
      {
        SongPlan p;p.compositionBehavior=CompositionBehavior::Club;p.totalBars=4;p.beatsPerBar=4;p.sections.resize(1);p.sections[0].bars=4;
        p.instruments.resize(1);p.instruments[0].id="pulse";p.instruments[0].sourceVoice=VoiceId::Lead;p.instruments[0].minimumPitch=0;p.instruments[0].maximumPitch=127;
        p.phraseArchitectureJson=R"({"kind":"club_production_v1","layers":[{"instrument_id":"pulse","material_behavior":"stable_foundation","polyphonic":false}]})";
        const juce::String notes=R"({"pulse":{"cycle_beats":4,"repeat_count":4,"notes":[{"beat":0,"duration":0.5,"pitch":62,"velocity":80},{"beat":2,"duration":0.5,"pitch":62,"velocity":70}]}})";
        PerformanceScore score;juce::String error;
        check(AiComposer::parseDirectWindowJson(notes,p,{0},0,0,4,score,error),"explicit AI foundation repetition rejected");
        check(score.cells.size()==1&&score.cells[0].notes.size()==2&&score.placements[0].repeats==4,"repeat sign expanded into invented cells");
        Pattern rendered;PerformanceScoreEngine::replaceChunk(rendered,score,0,0,16,p.instruments,true,16);
        check(rendered.notes.size()==8,"explicit repeat count not realized exactly");
        for(const auto& n:rendered.notes)check(n.pitch==62&&n.durationBeats==0.5&&n.partId==1,"foundation playback altered authored note");
        rendered.lengthBeats=16;InstrumentPart part;part.id=1;part.name="pulse";part.sourceVoice=VoiceId::Lead;rendered.parts.push_back(part);
        const auto file=juce::File::createTempFile(".mid");
        check(writePatternToMidiFile(rendered,file,{})&&verifyMidiRoundTrip(file,rendered,error),"repeated AI foundation changed on MIDI export");file.deleteFile();
        check(!AiComposer::parseDirectWindowJson(notes.replace("\"repeat_count\":4","\"repeat_count\":5"),p,{0},0,0,4,score,error),"foundation exceeded assigned window");
        p.phraseArchitectureJson=juce::String(p.phraseArchitectureJson).replace("stable_foundation","static_tonal_rhythm").toStdString();
        check(AiComposer::parseDirectWindowJson(notes,p,{0},0,0,4,score,error),"static tonal rhythm cannot use explicit repeat signs");
        auto declared=juce::JSON::parse(juce::String(p.phraseArchitectureJson));
        declared.getProperty("layers",{})[0].getDynamicObject()->setProperty("static_identity",
            juce::JSON::parse(R"({"pitch_policy":"retain_anchor","anchor_pitches":[62]})"));
        p.phraseArchitectureJson=juce::JSON::toString(declared).toStdString();
        check(AiComposer::parseDirectWindowJson(notes,p,{0},0,0,4,score,error),"retained anchor rejected exact authored repeat");
        check(!AiComposer::parseDirectWindowJson(notes.replace("\"pitch\":62","\"pitch\":64"),p,{0},0,0,4,score,error),"writer changed retained anchor without rejection");
        check(error.contains("retained pitch set"),"anchor violation lost focused recovery feedback");
        p.phraseArchitectureJson="{}";
        check(!AiComposer::parseDirectWindowJson(notes,p,{0},0,0,4,score,error),"repetition applied without AI foundation declaration");
      }
      if(argc==3) {
        const auto root=juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[1])).loadFileAsString());
        const auto* notes=root.getProperty("notes",{}).getArray();check(notes!=nullptr,"audit checkpoint notes missing");
        Pattern expected;
        for(const auto& value:*notes){NoteEvent n;n.startBeat=value.getProperty("start_beat",0);n.durationBeats=value.getProperty("duration_beats",0);n.pitch=value.getProperty("pitch",0);n.velocity=value.getProperty("velocity",0);expected.notes.push_back(n);}
        juce::String error;
        if(!verifyMidiRoundTrip(juce::File(juce::String::fromUTF8(argv[2])),expected,error)) throw std::runtime_error(error.toStdString());
        std::cout<<"Checkpoint-to-MIDI verified: "<<expected.notes.size()<<" events\n";
      }
      {
        Pattern p;p.lengthBeats=324;
        NoteEvent a;a.startBeat=312.5;a.durationBeats=4;a.pitch=55;a.velocity=87;p.notes.push_back(a);
        a.startBeat=316.25;a.durationBeats=4;a.velocity=82;p.notes.push_back(a);
        const auto file=juce::File::createTempFile(".mid");juce::String error;
        check(writePatternToMidiFile(p,file,{})&&verifyMidiRoundTrip(file,p,error),"raw FIFO MIDI check clipped an overlapping retrigger");
        p.notes[0].durationBeats=3.75;
        check(!verifyMidiRoundTrip(file,p,error),"changed overlap duration passed integrity check");
        file.deleteFile();
      }
      {
        const juce::String plan=R"({"groove_core_ids":["bass","chord"],"tonal_nucleus":{},"layers":[{"instrument_id":"bass","timing_mode":"cycle_bound","cycle_beats":4,"cycle_origin_beat":0,"rhythm_cell":[0,1,2,3]},{"instrument_id":"chord","timing_mode":"cycle_bound","cycle_beats":4,"cycle_origin_beat":0,"rhythm_cell":[0.5,2]}],"groove_interlock":{"bass_owner_id":"bass","chord_owner_id":"chord","shared_landings":[0]}})";
        juce::String merged="unchanged";
        check(!club::production::replaceInterlockLandings(plan,R"({"shared_landings":[0]})",merged)&&merged=="unchanged","false joint landing accepted");
        check(club::production::replaceInterlockLandings(plan,R"({"shared_landings":[2]})",merged),"actual common landing rejected");
        const auto a=juce::JSON::parse(plan),b=juce::JSON::parse(merged);
        check(juce::JSON::toString(a.getProperty("layers",{}))==juce::JSON::toString(b.getProperty("layers",{})),"landing correction changed rhythm cells");
        check(juce::JSON::parse(club::production::interlockLandingSchema(plan)).isObject(),"invalid landing repair schema");
      }
      {
        SongPlan p;p.compositionBehavior=CompositionBehavior::Club;p.totalBars=4;p.beatsPerBar=4;
        p.sections.resize(1);p.sections[0].bars=4;p.instruments.resize(3);
        const std::array<const char*,3> ids={"bass","chord","lead"};
        const std::array<VoiceId,3> voices={VoiceId::MovementBass,VoiceId::HarmonicFoundation,VoiceId::Lead};
        for(int i=0;i<3;++i){p.instruments[i].id=ids[i];p.instruments[i].sourceVoice=voices[i];p.instruments[i].minimumPitch=0;p.instruments[i].maximumPitch=127;}
        p.phraseArchitectureJson=R"({"kind":"club_production_v1","layers":[{"instrument_id":"bass","polyphonic":false},{"instrument_id":"chord","polyphonic":true},{"instrument_id":"lead","polyphonic":false}]})";
        PerformanceScore score;juce::String error;
        if(!AiComposer::parseDirectWindowJson(R"({"bass":[{"beat":0,"duration":2,"pitch":38,"velocity":90}],"chord":[{"beat":0,"duration":2,"pitch":62,"velocity":70},{"beat":0,"duration":2,"pitch":65,"velocity":70},{"beat":0,"duration":2,"pitch":69,"velocity":70}],"lead":[{"beat":1,"duration":3,"pitch":74,"velocity":100}]})",p,{0,1,2},0,0,4,score,error)) throw std::runtime_error(error.toStdString());
        Pattern rendered;PerformanceScoreEngine::replaceChunk(rendered,score,0,0,16,p.instruments,true,16);
        rendered.lengthBeats=16;
        for(int i=0;i<3;++i){InstrumentPart part;part.id=static_cast<std::uint16_t>(i+1);part.name=ids[i];part.sourceVoice=voices[i];rendered.parts.push_back(part);}
        check(rendered.notes.size()==5,"parser-to-score render lost or duplicated events");
        for(const auto& n:rendered.notes){check(n.partId>=1&&n.partId<=3,"render lost owner");if(n.partId==1)check(n.pitch==38,"harmony moved into bass");if(n.partId==2)check(n.pitch==62||n.pitch==65||n.pitch==69,"lead moved into harmony");if(n.partId==3)check(n.pitch==74&&n.startBeat==1&&n.durationBeats==3,"lead owner/timing changed");}
        const auto file=juce::File::createTempFile(".mid");
        check(writePatternToMidiFile(rendered,file,{})&&verifyMidiRoundTrip(file,rendered,error),"AI-parser-score-MIDI round trip failed");file.deleteFile();
      }
      {
        Pattern p;NoteEvent n;n.pitch=62;n.startBeat=0.5;n.durationBeats=2;n.velocity=87;p.notes.push_back(n);
        const auto file=juce::File::createTempFile(".mid");juce::String error;
        check(writePatternToMidiFile(p,file,{}),"integrity fixture export failed");
        check(verifyMidiRoundTrip(file,p,error),"exact MIDI round-trip failed");
        p.notes[0].pitch=65;
        check(!verifyMidiRoundTrip(file,p,error),"MIDI pitch corruption not detected");
        p.notes[0].pitch=62;p.notes[0].durationBeats=1;
        check(!verifyMidiRoundTrip(file,p,error),"MIDI release corruption not detected");
        file.deleteFile();
      }
      {
        auto schema=juce::JSON::parse(R"({"minItems":3,"maxItems":24,"items":{"properties":{"name":{"type":"string"},"harmonic_events":{"minItems":2}}}})");
        harmonic_cycle::shortPassageSchema(*schema.getDynamicObject(),16,true);
        check(static_cast<int>(schema.getProperty("minItems",0))==1&&static_cast<int>(schema.getProperty("maxItems",0))==2,"short Club passage forced into three sections");
        check(static_cast<int>(schema.getProperty("items",{}).getProperty("properties",{}).getProperty("name",{}).getProperty("minLength",0))==1,"blank padding section allowed");
        auto unchanged=juce::JSON::parse(R"({"minItems":3,"maxItems":24})");
        harmonic_cycle::shortPassageSchema(*unchanged.getDynamicObject(),96,true);
        check(static_cast<int>(unchanged.getProperty("minItems",0))==3,"long-form schema changed");
      }
      {
        const auto old=juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC",{});
        setting("PULSO_AI_ALLOW_CHROMATIC","0");
        SongPlan plan;plan.rootPitchClass=2;plan.scale=ScaleKind::Dorian;plan.sections.resize(1);
        plan.instruments.resize(1);plan.instruments[0].id="lead";plan.instruments[0].minimumPitch=60;plan.instruments[0].maximumPitch=84;
        const juce::String score=R"({"identity":"D dorian","journey":[{"section_index":0,"function":"statement","connection":"opening","destination":"D","support_response":"D bass","notes":[{"beat":0,"duration":1,"pitch":66}]}]})";
        check(!cohesion::validShortScore(juce::JSON::parse(score),plan),"F sharp accepted in strict D dorian reference");
        check(cohesion::validShortScore(juce::JSON::parse(score.replace("66","65")),plan),"F natural rejected in D dorian");
        const juce::String draft=R"({"musical_arcs":[{"foreground_id":"lead","phrase_reference":{"beats":4}}]})";
        auto schema=juce::JSON::parse(club::production::referenceNotesRepairSchema(draft,plan,0,"phrase_reference"));
        auto pitches=schema.getProperty("properties",{}).getProperty("notes",{}).getProperty("items",{}).getProperty("anyOf",{})[0].getProperty("properties",{}).getProperty("pitch",{}).getProperty("enum",{});
        check(pitches.isArray()&&pitches.getArray()->contains(juce::var(65))&&!pitches.getArray()->contains(juce::var(66)),"repair schema disagrees with MIDI tonal policy");
        setting("PULSO_AI_ALLOW_CHROMATIC","1");
        check(cohesion::validShortScore(juce::JSON::parse(score),plan),"chromatic permission lost");
        setting("PULSO_AI_ALLOW_CHROMATIC",old);
      }
      {
        SongPlan plan;plan.instruments.resize(1);plan.instruments[0].id="lead";
        plan.instruments[0].role="UNIQUE_INITIAL_STATEMENT_AND_RETURN";
        plan.instruments[0].activeSections={"plateau","release"};
        check(reconciliation::prompt(plan).contains("UNIQUE_INITIAL_STATEMENT_AND_RETURN")&&reconciliation::prompt(plan).contains("plateau"),"review omitted original voice responsibilities");
        const juce::String original=R"({"layers":[{"instrument_id":"lead","states":[{"mode":"absent","entry_beat":0,"release_beat":0}]}]})";
        const juce::String review=R"({"assessment":"restore originally promised statement","edits":[{"scope":"layer_state","index":0,"child_index":0,"field":"mode","number":null,"text":"active"},{"scope":"layer_state","index":0,"child_index":0,"field":"release_beat","number":16,"text":null}]})";
        juce::String candidate,error;
        check(reconciliation::apply(original,review,candidate,error),"review cannot restore declared speaker activity");
        check(juce::JSON::parse(candidate).getProperty("layers",{})[0].getProperty("states",{})[0].getProperty("mode",{}).toString()=="active","speaker activity correction lost");
      }
      {
        const juce::String source=R"({"musical_arcs":[{"foreground_id":"lead","phrase_reference":{"beats":4,"notes":[{"beat":0,"duration":4,"pitch":62}]},"response_reference":{"beats":4,"notes":[{"beat":0,"duration":4,"pitch":65}]}}]})";
        const auto compiled=juce::JSON::parse(execution::attachPhraseLibrary(source));
        const auto phrases=compiled.getProperty("phrase_library",{});
        check(phrases.size()==2&&phrases[1].getProperty("answers",{}).toString()=="arc-0:phrase_reference","phrase identity/response link lost");
        check(phrases[0].getProperty("reference",{}).getProperty("notes",{})[0].getProperty("pitch",0)==juce::var(62),"phrase compilation rewrote source notes");
        const auto schema=club::production::boundedNoteSchema(8,16);
        check(schema.getProperty("anyOf",{}).size()==32,"sustain support widened attack window");
        check(static_cast<double>(schema.getProperty("anyOf",{})[0].getProperty("properties",{}).getProperty("duration",{}).getProperty("maximum",0))==16,"cross-window sustain not supported");
      }
      {
        const juce::String original=R"({"musical_arcs":[{"bars":8,"phrase_reference":{"notes":[{"pitch":62}]},"phrase_route":[{"from_bar":0,"bars":2},{"from_bar":2,"bars":3},{"from_bar":5,"bars":1},{"from_bar":6,"bars":2}]}]})";
        juce::String candidate="unchanged",error;
        const juce::String repair=R"({"assessment":"reserve arrival one bar later","edits":[{"scope":"route","index":0,"child_index":1,"field":"bars","number":4,"text":null},{"scope":"route","index":0,"child_index":3,"field":"bars","number":1,"text":null}]})";
        check(reconciliation::apply(original,repair,candidate,error),"duration-only route reconciliation failed");
        const auto arc=juce::JSON::parse(candidate).getProperty("musical_arcs",{})[0];
        check(static_cast<int>(arc.getProperty("phrase_route",{})[2].getProperty("from_bar",0))==6&&static_cast<int>(arc.getProperty("phrase_route",{})[3].getProperty("from_bar",0))==7,"cumulative route timing incorrect");
        const auto accepted=candidate;
        auto bad=juce::JSON::parse(repair);bad.getProperty("edits",{}).getArray()->removeLast();
        check(!reconciliation::apply(original,juce::JSON::toString(bad),candidate,error)&&candidate==accepted,"invalid duration sum partially applied");
        check(error.contains("durations sum"),"duration mismatch lacks actionable feedback");
      }
      {
        SongPlan original;
        original.phraseArchitectureJson=R"({"unified_execution":true,"ensemble_scenes":[{"section_index":0,"foreground_id":"lead","transition":"OLD_CONTRADICTORY_TEXT"}],"compositional_short_score":{"identity":"OLD_DRAFT"},"ensemble_arguments":[],"musical_arcs":[{"phrase_reference":{"notes":[{"beat":0,"duration":4,"pitch":62}]}}],"layers":[{"instrument_id":"lead"}]})";
        const auto saved=original.phraseArchitectureJson;
        original.compositionBehavior=CompositionBehavior::Club;
        check(execution::reviewUnifiedVoicings(original),"unified Club skips measured voicing review");
        original.compositionBehavior=CompositionBehavior::Adaptive;
        check(!execution::reviewUnifiedVoicings(original),"voicing experiment leaked into Adaptive");
        original.compositionBehavior=CompositionBehavior::Club;
        const auto view=execution::writerView(original);
        check(original.phraseArchitectureJson==saved,"writer projection mutated archived source");
        check(view.phraseArchitectureJson.find("OLD_DRAFT")==std::string::npos&&view.phraseArchitectureJson.find("OLD_CONTRADICTORY_TEXT")==std::string::npos,"competing drafts remain in writer view");
        check(juce::JSON::toString(juce::JSON::parse(juce::String(saved)).getProperty("musical_arcs",{}))==juce::JSON::toString(juce::JSON::parse(juce::String(view.phraseArchitectureJson)).getProperty("musical_arcs",{})),"projection changed authored phrase references");
        check(execution::writerView(view).phraseArchitectureJson==view.phraseArchitectureJson,"writer projection not idempotent");
      }
      {
        reconciliation::FragmentRepairBudget budget;
        check(budget.take("reference:0:phrase")&&budget.take("reference:1:phrase"),"independent references blocked");
        check(budget.take("nucleus:primary_chord_bed"),"two previous corrections starve chord repair");
        check(budget.take("nucleus:primary_chord_bed")&&!budget.take("nucleus:primary_chord_bed"),"per-fragment repair cap ignored");
        for(int i=0;i<4;++i) check(budget.take("other:"+juce::String(i)),"global repair budget exhausted early");
        check(!budget.take("ninth"),"global repair cap ignored");
      }
      {
        SongPlan shortPlan; shortPlan.compositionBehavior=CompositionBehavior::Club;
        shortPlan.totalBars=32; shortPlan.sections.resize(1);shortPlan.sections[0].bars=32;
        check(execution::writingWindowBars(shortPlan,3)==32,"small short work split at arbitrary eight-bar boundaries");
        check(execution::writingWindowBars(shortPlan,16)==4,"large-cast request budget changed");
        shortPlan.totalBars=96;
        check(execution::writingWindowBars(shortPlan,3)==8,"long-form request capacity changed");
        shortPlan.compositionBehavior=CompositionBehavior::Adaptive;
        check(execution::writingWindowBars(shortPlan,3)==16,"non-Club request capacity changed");
      }
      {
        const juce::String flexible=R"({"groove_core_ids":["bass","chord"],"tonal_nucleus":{"meter_carrier_id":"bass"},"layers":[{"instrument_id":"bass","timing_mode":"cycle_bound"},{"instrument_id":"chord","timing_mode":"phrase_flexible"}]})";
        check(club::production::cycleBoundLayer(flexible,"bass"),"meter carrier lost stable timing");
        check(!club::production::cycleBoundLayer(flexible,"chord"),"core membership overrides explicit flexible timing");
        check(club::production::cycleBoundLayer(R"({"groove_core_ids":["bass"]})","bass"),"legacy core timing changed");
        check(!club::production::cycleBoundLayer(R"({"layers":[{"instrument_id":"lead","timing_mode":"phrase_flexible"}]})","lead"),"flexible lead constrained");
      }
      {
        const auto completed=juce::JSON::parse(R"({"status":"completed"})");
        check(structured::ready(completed,true,200,false,false),"completed response unavailable");
        for(const auto* state:{"in_progress","queued","incomplete","cancelled","failed"}) {
            auto envelope=new juce::DynamicObject(); envelope->setProperty("status",state);
            check(!structured::ready(juce::var(envelope),true,200,false,false),"unfinished response reached musical validator");
        }
        check(!structured::ready(completed,true,200,true,false),"timeout ignored");
        check(!structured::ready(completed,true,200,false,true),"cancellation ignored");
        check(!structured::ready(completed,false,200,false,false),"transport failure ignored");
        check(!structured::ready(completed,true,500,false,false),"HTTP failure ignored");
        check(!structured::ready(juce::var(),true,200,false,false),"missing response treated as complete");
      }
      {
        const juce::String original=R"({"musical_arcs":[{"start_bar":0,"bars":16,"arrival":"early","phrase_reference":{"notes":[{"pitch":62,"beat":0,"duration":1}]},"phrase_route":[{"from_bar":0,"bars":8,"action":"arrival","material":"tonic","consequence":"close"}]}],"layers":[{"instrument_id":"lead","states":[{"entry_beat":0,"release_beat":32}]}]})";
        juce::String candidate="untouched",error;
        const juce::String review=R"({"assessment":"move the promised arrival to its actual chord","edits":[{"scope":"route","index":0,"child_index":0,"field":"from_bar","number":8,"text":null},{"scope":"layer_state","index":0,"child_index":0,"field":"release_beat","number":64,"text":null},{"scope":"arc","index":0,"child_index":0,"field":"arrival","number":null,"text":"tonic after the question"}]})";
        check(reconciliation::apply(original,review,candidate,error),"valid AI declaration edits rejected");
        const auto before=juce::JSON::parse(original),after=juce::JSON::parse(candidate);
        check(juce::JSON::toString(before.getProperty("musical_arcs",{})[0].getProperty("phrase_reference",{}))==juce::JSON::toString(after.getProperty("musical_arcs",{})[0].getProperty("phrase_reference",{})),"reconciliation rewrote musical notes");
        check(static_cast<int>(after.getProperty("musical_arcs",{})[0].getProperty("phrase_route",{})[0].getProperty("from_bar",0))==8,"route edit not applied");
        auto bad=juce::JSON::parse(review);
        bad.getProperty("edits",{})[1].getDynamicObject()->setProperty("field","pitch");
        const auto acceptedCandidate=candidate;
        check(!reconciliation::apply(original,juce::JSON::toString(bad),candidate,error)&&candidate==acceptedCandidate,"unauthorized edits not atomic");
        bad=juce::JSON::parse(review); bad.getProperty("edits",{})[0].getDynamicObject()->setProperty("index",99);
        check(!reconciliation::apply(original,juce::JSON::toString(bad),candidate,error),"invalid index accepted");
        bad=juce::JSON::parse(review); bad.getProperty("edits",{})[0].getDynamicObject()->setProperty("number",1.5);
        check(!reconciliation::apply(original,juce::JSON::toString(bad),candidate,error),"fractional route bar accepted");
        check(reconciliation::apply(original,R"({"assessment":"consistent","edits":[]})",candidate,error),"no-op reconciliation rejected");
        check(juce::JSON::parse(reconciliation::schema()).isObject(),"invalid reconciliation schema");
        const juce::String scene=R"({"ensemble_scenes":[{"voices":[{"owner_id":"harmony","material":"G6_9","speaking_span":"56-64","arrival":"held A"}]}]})";
        check(reconciliation::apply(scene,R"({"assessment":"actual chords are Cmaj7 and Em7","edits":[{"scope":"scene_voice","index":0,"child_index":0,"field":"material","number":null,"text":"Voz alta B71-A69-G67-A69 sobre Cmaj7 y Em7."}]})",candidate,error),"real scene-material reconciliation regression");
        check(juce::JSON::parse(candidate).getProperty("ensemble_scenes",{})[0].getProperty("voices",{})[0].getProperty("owner_id",{}).toString()=="harmony","scene material edit changed owner");
        const auto choices=juce::JSON::parse(reconciliation::schema()).getProperty("properties",{}).getProperty("edits",{}).getProperty("items",{}).getProperty("anyOf",{});
        check(choices.size()==9,"missing typed edit scopes");
        for(const auto* invalidAction:{"response","preparation"}) {
            auto actionReview=juce::JSON::parse(review);
            auto action=actionReview.getProperty("edits",{})[0];
            action.getDynamicObject()->setProperty("field","action");
            action.getDynamicObject()->setProperty("number",juce::var());
            action.getDynamicObject()->setProperty("text",invalidAction);
            const auto unchanged=candidate;
            check(!reconciliation::apply(original,juce::JSON::toString(actionReview),candidate,error)&&candidate==unchanged,"invalid route action not rejected atomically");
            check(error.contains("action must be"),"route action failure lacks precise feedback");
            action.getDynamicObject()->setProperty("text","continuation");
            check(reconciliation::apply(original,juce::JSON::toString(actionReview),candidate,error),"valid continuation correction rejected");
        }
        for(const auto& choice:*choices.getArray()) {
            const auto props=choice.getProperty("properties",{});
            const auto scope=props.getProperty("scope",{}).getProperty("enum",{})[0].toString();
            const auto fields=props.getProperty("field",{}).getProperty("enum",{});
            if(scope=="route" && fields.size()==1 && fields[0].toString()=="action") {
                const auto actions=props.getProperty("text",{}).getProperty("enum",{});
                check(actions.size()==4&&!actions.getArray()->contains(juce::var("response"))&&!actions.getArray()->contains(juce::var("preparation")),"schema permits unsupported route actions");
            }
            if(scope=="scene_voice") {
                check(fields.getArray()->contains(juce::var("material")),"schema omitted supported scene material");
                check(!fields.getArray()->contains(juce::var("release_beat")),"schema allows numeric field in scene voice");
                check(props.getProperty("number",{}).getProperty("type",{}).toString()=="null","scene edit has ambiguous value type");
            }
            if(scope=="layer_state") check(!fields.getArray()->contains(juce::var("material")),"schema allows material edit in activity state");
        }
      }
      {
        SongPlan p;
        p.beatsPerBar=3; p.totalBars=20;
        p.sections.resize(2); p.sections[0].bars=8;
        p.sections[1].startBar=8; p.sections[1].bars=12;
        p.instruments.resize(1); p.instruments[0].id="lead";
        HarmonicEvent first; first.barOffset=0; first.chordId="QUESTION";
        HarmonicEvent final; final.barOffset=8; final.chordId="DESTINATION";
        p.sections[1].harmonicEvents={first,final};
        p.phraseArchitectureJson=R"({"kind":"club_production_v1","musical_arcs":[{"foreground_id":"lead","start_bar":8,"bars":12,"phrase_route":[{"from_bar":0,"bars":4,"action":"statement","material":"opening","consequence":"question"},{"from_bar":4,"bars":4,"action":"continuation","material":"reply","consequence":"toward destination"},{"from_bar":8,"bars":4,"action":"arrival","material":"held destination","consequence":"close"}]}],"layers":[{"instrument_id":"lead","states":[{"section_index":1,"mode":"active","entry_beat":0,"release_beat":35}]}]})";
        const auto h=execution::harmony(p,36,48);
        check(h.size()==1 && h[0].getProperty("chord_id",{}).toString()=="QUESTION","future chord leaked into earlier arrival span");
        check(static_cast<double>(h[0].getProperty("absolute_end_exclusive",0))==48,"harmonic boundary calculation wrong");
        check(execution::harmony(p,48,60)[0].getProperty("chord_id",{}).toString()=="DESTINATION","chord boundary not exclusive");
        Pattern accepted; NoteEvent n; n.partId=1; n.startBeat=33; n.durationBeats=4; n.pitch=62; accepted.notes.push_back(n);
        const auto text=execution::brief(p,accepted,1,4,4);
        const auto held=execution::sustainedContext(accepted,36,48).removeCharacters(" \r\n\t");
        check(held.contains("\"pitch\":62")&&held.contains("\"window_local_release\":1"),"sustain context lost actual release");
        check(!execution::sustainedContext(accepted,37,48).contains("\"pitch\""),"released voice reported as held");
        const auto flat=text.removeCharacters(" \r\n\t");
        check(text.contains("window absolute ["+juce::String(36.0)+","+juce::String(48.0)+")") && flat.contains("\"activity_end_absolute\":59"),"section/route/activity clocks disagree");
        check(flat.contains("\"last_accepted_release_absolute\":37"),"cross-window sustain lost from execution evidence");
        check(flat.contains("\"phase\":\"past\"") && flat.contains("\"phase\":\"current\"") && flat.contains("\"phase\":\"future\""),"route phases not distinguished");
        check(accepted.notes.size()==1 && accepted.notes[0]==n,"execution ledger changed accepted MIDI");
        p.compositionBehavior=CompositionBehavior::Club;
        auto mono=p;
        auto monoRoot=juce::JSON::parse(juce::String(mono.phraseArchitectureJson));
        monoRoot.getProperty("layers",{})[0].getDynamicObject()->setProperty("polyphonic",false);
        mono.phraseArchitectureJson=juce::JSON::toString(monoRoot).toStdString();
        PerformanceScore monoScore;juce::String monoError;
        check(!AiComposer::parseDirectWindowJson(R"({"lead":[{"beat":0,"duration":1,"pitch":62,"velocity":80},{"beat":0,"duration":1,"pitch":65,"velocity":80}]})",mono,{0},1,4,4,monoScore,monoError),"monophonic owner accepted chord attacks");
        check(monoError.contains("Monophonic owner lead"),"monophonic defect lacks owner diagnostic");
        auto tiedPlan=p;
        auto tiedRoot=juce::JSON::parse(juce::String(tiedPlan.phraseArchitectureJson));
        tiedRoot.getDynamicObject()->setProperty("continuous_phrase_execution",true);
        tiedPlan.phraseArchitectureJson=juce::JSON::toString(tiedRoot).toStdString();
        PerformanceScore tiedScore;juce::String tiedError;
        check(AiComposer::parseDirectWindowJson(R"({"lead":[{"beat":11,"duration":4,"pitch":62,"velocity":80}]})",tiedPlan,{0},1,4,4,tiedScore,tiedError),"parser rejects authored cross-window sustain");
        check(!AiComposer::parseDirectWindowJson(R"({"lead":[{"beat":12,"duration":4,"pitch":62,"velocity":80}]})",tiedPlan,{0},1,4,4,tiedScore,tiedError),"sustain support widened onset window");
        check(AiComposer::directWindowPromptFor("instrumental",p,accepted,{0},1,4,4).contains("PLAN-TO-SCORE EXECUTION LEDGER"),"real writer bypasses execution ledger");
        check(AiComposer::directWindowPromptFor("instrumental",p,accepted,{0},1,4,4).contains("IMMUTABLE HELD VOICES AT ENTRY"),"writer lacks held-voice realization context");
        check(AiComposer::directWindowPromptFor("instrumental",p,accepted,{0},1,4,4,"retry",true).contains("PLAN-TO-SCORE EXECUTION LEDGER"),"retry bypasses execution ledger");
        p.phraseArchitectureJson="{}";
        check(execution::brief(p,accepted,1,4,4).isEmpty(),"legacy plan acquires invented routes");
      }
      {
        SongPlan ensemble;
        ensemble.sections.resize(2);
        ensemble.sections[0].bars=8;
        ensemble.sections[1].bars=16;
        ensemble.instruments.resize(1);
        ensemble.instruments[0].id="solo";
        auto shortScore=juce::JSON::parse(R"({"identity":"rise and answer","journey":[{"section_index":0,"function":"statement","connection":"opening","destination":"held fifth","support_response":"bass supports arrival","notes":[{"beat":0,"duration":1,"pitch":64},{"beat":1,"duration":3,"pitch":67}]}]})");
        const auto shortBefore=juce::JSON::toString(shortScore);
        check(cohesion::validShortScore(shortScore,ensemble),"valid AI short score rejected");
        check(shortBefore==juce::JSON::toString(shortScore),"short score validation rewrote notes");
        auto oneSection=ensemble;oneSection.sections.resize(1);oneSection.sections[0].bars=32;
        const auto oneSchema=juce::JSON::parse(cohesion::shortScoreSchema(&oneSection));
        const auto sectionIds=oneSchema.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{})
            .getProperty("properties",{}).getProperty("section_index",{}).getProperty("enum",{});
        check(sectionIds.size()==1&&int(sectionIds[0])==0,"short-score schema permits nonexistent sections in one-section Club body");
        auto mistakenMoments=juce::JSON::parse(shortBefore);
        auto* moments=mistakenMoments.getProperty("journey",{}).getArray();
        for(int index=1;index<4;++index){auto moment=(*moments)[0].clone();moment.getDynamicObject()->setProperty("section_index",index);moments->add(moment);}
        const auto mistakenBefore=juce::JSON::toString(mistakenMoments);juce::String structureError;
        check(!cohesion::validShortScore(mistakenMoments,oneSection,&structureError)&&
              structureError.contains("journey[1].section_index=1"),"invalid short-score section reference lacks exact diagnosis");
        check(mistakenBefore==juce::JSON::toString(mistakenMoments),"validator renumbered musical moments procedurally");
        auto sharedSection=mistakenMoments.clone();
        for(auto& moment:*sharedSection.getProperty("journey",{}).getArray())moment.getDynamicObject()->setProperty("section_index",0);
        check(cohesion::validShortScore(sharedSection,oneSection,&structureError)&&structureError.isEmpty(),"multiple AI musical moments cannot share an existing section");
        const auto twoSchema=juce::JSON::parse(cohesion::shortScoreSchema(&ensemble)).getProperty("properties",{}).getProperty("journey",{})
            .getProperty("items",{}).getProperty("properties",{}).getProperty("section_index",{}).getProperty("enum",{});
        check(twoSchema.size()==2&&int(twoSchema[1])==1,"short-score schema lost existing multi-section references");
        auto handoffPlan=ensemble;
        handoffPlan.instruments.resize(2);
        handoffPlan.instruments[1].id="harmony";
        auto handed=juce::JSON::parse(shortBefore);
        auto handoff=juce::JSON::parse(R"({"from_owner":"solo","to_owner":"harmony","inherited_gesture":"rising third","answer_and_return":"lower answer prepares return","notes":[{"beat":0,"duration":2,"pitch":60},{"beat":2,"duration":2,"pitch":62}]})");
        handed.getProperty("journey",{})[0].getDynamicObject()->setProperty("handoff",handoff);
        check(cohesion::validShortScore(handed,handoffPlan),"valid secondary sentence rejected");
        auto afterAnswer=juce::JSON::parse(R"({"owner_id":"harmony","consequence":"UNIQUE_HELD_EXPECTATION","next_entry":"lead answers the held third","retained_pulse":"bass keeps its original pulse"})");
        handoff.getDynamicObject()->setProperty("after_answer",afterAnswer);
        const auto afterBefore=juce::JSON::toString(handed);
        check(cohesion::validShortScore(handed,handoffPlan),"valid consequence rejected");
        check(afterBefore==juce::JSON::toString(handed),"consequence validation changed music");
        afterAnswer.getDynamicObject()->setProperty("owner_id","ghost");
        check(!cohesion::validShortScore(handed,handoffPlan),"unknown consequence carrier accepted");
        afterAnswer.getDynamicObject()->setProperty("owner_id","harmony");
        afterAnswer.getDynamicObject()->setProperty("next_entry","");
        check(!cohesion::validShortScore(handed,handoffPlan),"missing consequence destination accepted");
        afterAnswer.getDynamicObject()->setProperty("next_entry","lead answers the held third");
        handoff.getDynamicObject()->setProperty("to_owner","ghost");
        check(!cohesion::validShortScore(handed,handoffPlan),"unknown handoff owner accepted");
        handoff.getDynamicObject()->setProperty("to_owner","solo");
        check(!cohesion::validShortScore(handed,handoffPlan),"self handoff accepted");
        auto ambiguous=handed.clone();
        const auto originalAmbiguous=juce::JSON::toString(ambiguous);
        check(cohesion::ambiguousHandoffSources(ambiguous).size()==1,"ambiguous source not detected");
        const auto sourceSchema=juce::JSON::parse(cohesion::handoffSourceSchema(ambiguous,handoffPlan));
        const auto sourceChoices=sourceSchema.getProperty("properties",{}).getProperty("corrections",{}).getProperty("items",{}).getProperty("anyOf",{});
        check(sourceChoices[0].getProperty("properties",{}).getProperty("from_owner",{}).getProperty("enum",{}).size()==1,"repair schema includes receiver as source");
        check(!cohesion::repairHandoffSources(ambiguous,juce::JSON::parse(R"({"corrections":[{"journey_index":0,"from_owner":"solo","reason":"same"}]})"),handoffPlan),"repair permits same source and receiver");
        check(originalAmbiguous==juce::JSON::toString(ambiguous),"failed repair mutated score");
        check(cohesion::repairHandoffSources(ambiguous,juce::JSON::parse(R"({"corrections":[{"journey_index":0,"from_owner":"harmony","reason":"preceding inner voice supplies the descending third"}]})"),handoffPlan),"bounded source correction failed");
        auto expected=juce::JSON::parse(originalAmbiguous);
        expected.getProperty("journey",{})[0].getProperty("handoff",{}).getDynamicObject()->setProperty("from_owner","harmony");
        check(juce::JSON::toString(ambiguous)==juce::JSON::toString(expected),"source repair changed music or other fields");
        check(cohesion::handoffSourceSchema(juce::JSON::parse(originalAmbiguous),ensemble).isEmpty(),"solo repair invents another voice");
        handoff.getDynamicObject()->setProperty("to_owner","harmony");
        handoff.getProperty("notes",{})[0].getDynamicObject()->setProperty("duration",-1);
        check(!cohesion::validShortScore(handed,handoffPlan),"invalid handoff notes accepted");
        handed.getProperty("journey",{})[0].getDynamicObject()->setProperty("handoff",juce::var());
        check(cohesion::validShortScore(handed,ensemble),"intentional solo/rest rejected");
        const auto handoffSchema=juce::JSON::parse(cohesion::shortScoreSchema(&handoffPlan));
        check(handoffSchema.getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{}).getProperty("properties",{}).getProperty("handoff",{}).getProperty("anyOf",{})[0].getProperty("properties",{}).getProperty("to_owner",{}).getProperty("enum",{}).size()==2,"handoff schema lacks actual cast");
        check(juce::JSON::parse(cohesion::shortScoreSchema()).isObject(),"invalid short score schema");
        auto endingScore=juce::JSON::parse(shortBefore);
        auto ending=juce::JSON::parse(R"({"prepared_phrase":"recall then answer","shared_arrival":"UNIQUE_SHARED_ARRIVAL","speaker_release":"overlap support then release","after_arrival":"sustained floor"})");
        endingScore.getDynamicObject()->setProperty("ensemble_ending",ending);
        check(cohesion::validShortScore(endingScore,ensemble),"coordinated ending rejected");
        ending.getDynamicObject()->setProperty("speaker_release","");
        check(!cohesion::validShortScore(endingScore,ensemble),"incomplete ending accepted");
        ending.getDynamicObject()->setProperty("speaker_release","overlap support then release");
        auto shortProperties=juce::JSON::parse(cohesion::shortScoreSchema()).getProperty("properties",{}).getProperty("journey",{}).getProperty("items",{}).getProperty("properties",{});
        check(shortProperties.hasProperty("continuation_notes")&&shortProperties.hasProperty("development_route"),"short score lacks concrete middle development");
        check(shortProperties.hasProperty("vertical_plan"),"short score lacks simultaneous voice planning");
        check(shortProperties.hasProperty("ensemble_excerpt"),"short score lacks joint note evidence");
        check(shortProperties.hasProperty("register_route"),"short score lacks expressive register choice");
        auto registerScore=juce::JSON::parse(shortBefore);
        registerScore.getProperty("journey",{})[0].getDynamicObject()->setProperty("register_route","Retain the narrow tessitura for a hypnotic plateau.");
        const auto registerBefore=juce::JSON::toString(registerScore);
        check(cohesion::validShortScore(registerScore,ensemble),"narrow expressive register rejected");
        check(registerBefore==juce::JSON::toString(registerScore),"register validation changed notes");
        registerScore.getProperty("journey",{})[0].getDynamicObject()->setProperty("register_route","");
        check(!cohesion::validShortScore(registerScore,ensemble),"empty register intent accepted");
        auto excerptScore=juce::JSON::parse(shortBefore);
        auto excerptPassage=excerptScore.getProperty("journey",{})[0];
        const auto excerpt=juce::JSON::parse(R"([{"owner_id":"solo","pitch":64,"beat":0,"duration":4},{"owner_id":"harmony","pitch":77,"beat":1,"duration":2},{"owner_id":"solo","pitch":64,"beat":2,"duration":1}])");
        excerptPassage.getDynamicObject()->setProperty("ensemble_excerpt",excerpt);
        const auto excerptBefore=juce::JSON::toString(excerptScore);
        check(cohesion::validShortScore(excerptScore,handoffPlan),"joint reference rejected for musical tension");
        auto largerExcerpt=excerptScore.clone();juce::Array<juce::var> body;
        for(int i=0;i<128;++i){auto n=excerpt[0].clone();n.getDynamicObject()->setProperty("beat",i*.25);n.getDynamicObject()->setProperty("duration",.25);body.add(n);}
        largerExcerpt.getProperty("journey",{})[0].getDynamicObject()->setProperty("ensemble_excerpt",body);
        auto clubPlan=handoffPlan;clubPlan.compositionBehavior=CompositionBehavior::Club;
        check(cohesion::validShortScore(largerExcerpt,clubPlan),"Club joint sketch cannot represent complete multi-layer body");
        check(!cohesion::validShortScore(largerExcerpt,handoffPlan),"Club excerpt expansion changed other modes");
        body.add(body[0].clone());largerExcerpt.getProperty("journey",{})[0].getDynamicObject()->setProperty("ensemble_excerpt",body);
        check(!cohesion::validShortScore(largerExcerpt,clubPlan),"Club excerpt bound lost");
        const auto evidence=cohesion::excerptEvidence(excerptPassage);
        check(evidence.contains("interval_semitones=13 overlap_beats=2"),"compound interval or overlap measured incorrectly");
        check(evidence.contains("interval_semitones=0 overlap_beats=1"),"same-owner overlapping retrigger not reported");
        check(excerptBefore==juce::JSON::toString(excerptScore),"evidence analysis changed reference notes");
        excerpt[1].getDynamicObject()->setProperty("beat",4);
        check(!cohesion::excerptEvidence(excerptPassage).contains("interval_semitones=13"),"touching releases mistaken for overlap");
        excerpt[1].getDynamicObject()->setProperty("owner_id","unknown");
        check(!cohesion::validShortScore(excerptScore,handoffPlan),"unknown joint-score owner accepted");
        excerpt[1].getDynamicObject()->setProperty("owner_id","harmony");
        excerpt[1].getDynamicObject()->setProperty("beat",1);
        auto verticalScore=juce::JSON::parse(shortBefore);
        auto vertical=juce::JSON::parse(R"({"foreground_landings":"F4","support_voicing":"D3 A3 C4","tension_resolution":"E approaches F before the held arrival"})");
        verticalScore.getProperty("journey",{})[0].getDynamicObject()->setProperty("vertical_plan",vertical);
        const auto verticalBefore=juce::JSON::toString(verticalScore);
        check(cohesion::validShortScore(verticalScore,ensemble),"valid vertical plan rejected");
        check(verticalBefore==juce::JSON::toString(verticalScore),"vertical validation mutated notes");
        vertical.getDynamicObject()->setProperty("tension_resolution","");
        check(!cohesion::validShortScore(verticalScore,ensemble),"incomplete vertical plan accepted");
        auto developed=juce::JSON::parse(shortBefore);
        auto passage=developed.getProperty("journey",{})[0];
        passage.getDynamicObject()->setProperty("development_route","Return the contour, then answer while bass approaches the arrival.");
        passage.getDynamicObject()->setProperty("continuation_notes",juce::JSON::parse(R"([{"beat":0,"duration":2,"pitch":65},{"beat":2,"duration":2,"pitch":64}])"));
        check(cohesion::validShortScore(developed,ensemble),"concrete continuation rejected");
        passage.getProperty("continuation_notes",{})[0].getDynamicObject()->setProperty("duration",-1);
        check(!cohesion::validShortScore(developed,ensemble),"invalid continuation accepted");
        shortScore.getProperty("journey",{})[0].getProperty("notes",{})[0].getDynamicObject()->setProperty("duration",-1);
        check(!cohesion::validShortScore(shortScore,ensemble),"invalid reference duration accepted");
        shortScore=juce::JSON::parse(shortBefore);
        shortScore.getProperty("journey",{})[0].getDynamicObject()->setProperty("section_index",8);
        check(!cohesion::validShortScore(shortScore,ensemble),"unknown short-score section accepted");
        const auto json=juce::String(R"({"ensemble_arguments":[{"section_index":0,"foreground_owner":"solo","shared_thought":"question","inherited_question":"opening","shared_arrival":"held fifth","contributions":[{"owner_id":"solo","relationship":"self answer","phrase_path":"statement then continuation"}]},{"section_index":1,"foreground_owner":"solo","shared_thought":"answer","inherited_question":"held fifth","shared_arrival":"tonic after reply","contributions":[{"owner_id":"solo","relationship":"recall","phrase_path":"transform then resolve"}]}]})");
        const auto root=juce::JSON::parse(json);
        const auto before=juce::JSON::toString(root);
        juce::String error;
        check(cohesion::validate(root,ensemble,error),"solo ensemble argument rejected");
        check(before==juce::JSON::toString(root),"cohesion validation mutated source");
        check(cohesion::validate(juce::JSON::parse("{}"),ensemble,error),"legacy cohesion compatibility lost");
        check(cohesion::context(json,1).contains("INHERITED:") && cohesion::context(json,1).contains("CURRENT:"),"writer lost connected argument");
        check(cohesion::context(json,1,4,4,16).contains("an interior span"),"writer lacks actual interior section position");
        check(cohesion::context(json,1,12,4,16).contains("the closing span"),"writer lacks closing section position");
        auto withScore=juce::JSON::parse(json);
        withScore.getDynamicObject()->setProperty("compositional_short_score",juce::JSON::parse(shortBefore));
        check(cohesion::context(juce::JSON::toString(withScore),1).contains("rise and answer"),"writer lost the actual short score");
        withScore.getDynamicObject()->setProperty("kind","club_production_v1");
        ensemble.compositionBehavior=CompositionBehavior::Club;
        ensemble.totalBars=24;
        ensemble.sections[1].startBar=8;
        ensemble.phraseArchitectureJson=juce::JSON::toString(withScore).toStdString();
        const auto realClubPrompt=AiComposer::directWindowPromptFor("instrumental",ensemble,{}, {0},1,4,4);
        check(realClubPrompt.contains("rise and answer") && realClubPrompt.contains("an interior span"),
            "actual Club MIDI writer bypasses short score and section position");
        const auto savedArchitecture=ensemble.phraseArchitectureJson;
        auto enriched=juce::JSON::parse(ensemble.phraseArchitectureJson);
        enriched.getProperty("compositional_short_score",{}).getDynamicObject()->setProperty("ensemble_ending",ending);
        auto shortPassage=enriched.getProperty("compositional_short_score",{}).getProperty("journey",{})[0];
        shortPassage.getDynamicObject()->setProperty("development_route","UNIQUE_CONTINUATION_ROUTE");
        shortPassage.getDynamicObject()->setProperty("handoff",juce::JSON::parse(R"({"answer_and_return":"UNIQUE_RECEIVER_SENTENCE"})"));
        shortPassage.getDynamicObject()->setProperty("continuation_notes",juce::JSON::parse(R"([{"beat":0,"duration":2,"pitch":65}])"));
        ensemble.phraseArchitectureJson=juce::JSON::toString(enriched).toStdString();
        const auto repairPrompt=AiComposer::directWindowPromptFor("instrumental",ensemble,{}, {0},1,4,4,"retry",true);
        check(repairPrompt.contains("UNIQUE_CONTINUATION_ROUTE") && repairPrompt.contains("continuation_notes"),
            "Club retry loses concrete continuation material");
        check(repairPrompt.contains("UNIQUE_RECEIVER_SENTENCE"),"actual Club writer loses handoff evidence");
        shortPassage.getDynamicObject()->setProperty("section_index",1);
        shortPassage.getDynamicObject()->setProperty("register_route","UNIQUE_REGISTER_JOURNEY");
        shortPassage.getDynamicObject()->setProperty("ensemble_excerpt",excerpt.clone());
        shortPassage.getDynamicObject()->setProperty("vertical_plan",juce::JSON::parse(R"({"foreground_landings":"F4","support_voicing":"UNIQUE_SUPPORT_VOICING","tension_resolution":"prepared suspension"})"));
        shortPassage.getProperty("handoff",{}).getDynamicObject()->setProperty("to_owner","solo");
        shortPassage.getProperty("handoff",{}).getDynamicObject()->setProperty("after_answer",afterAnswer.clone());
        enriched.getDynamicObject()->setProperty("layers",juce::JSON::parse(R"([{"instrument_id":"solo","timing_mode":"cycle_bound","cycle_beats":4,"rhythm_cell":[0,1,2,3],"polyphonic":false,"states":[{"section_index":1,"mode":"active","entry_beat":0,"release_beat":64}]}])"));
        ensemble.phraseArchitectureJson=juce::JSON::toString(enriched).toStdString();
        const auto boundPrompt=AiComposer::directWindowPromptFor("instrumental",ensemble,{}, {0},1,4,4);
        check(boundPrompt.contains("CURRENT HANDOFF RECEIVER: solo")&&boundPrompt.contains("RECEIVER TIMING: mode=cycle_bound")&&boundPrompt.contains("RECEIVER ACTIVITY:"),"handoff missing its actual rhythmic/activity contract");
        check(boundPrompt.contains("CURRENT VERTICAL PLAN:")&&boundPrompt.contains("UNIQUE_SUPPORT_VOICING"),"actual MIDI writer lacks vertical plan");
        check(boundPrompt.contains("CURRENT EXPRESSIVE REGISTER ROUTE:")&&boundPrompt.contains("UNIQUE_REGISTER_JOURNEY"),"actual writer lacks current register route");
        check(!cohesion::context(ensemble.phraseArchitectureJson,0,0,4,8).contains("CURRENT EXPRESSIVE REGISTER ROUTE:"),"future register route activated early");
        check(boundPrompt.contains("SHARED-CLOCK ENSEMBLE EXCERPT:")&&boundPrompt.contains("interval_semitones=13"),"actual Club writer lacks measured simultaneous reference");
        check(!cohesion::context(ensemble.phraseArchitectureJson,0,0,4,8).contains("OVERLAP EVIDENCE:"),"future reference treated as active interval evidence");
        check(boundPrompt.contains("FINAL ENSEMBLE HANDOFF:")&&boundPrompt.contains("UNIQUE_SHARED_ARRIVAL"),"actual closing writer lacks coordinated ending");
        check(boundPrompt.contains("AFTER THE ANSWER, BEFORE THE NEXT ENTRY:")&&boundPrompt.contains("UNIQUE_HELD_EXPECTATION"),"actual writer loses post-answer continuity");
        check(!cohesion::context(ensemble.phraseArchitectureJson,0,0,4,8).contains("AFTER THE ANSWER, BEFORE THE NEXT ENTRY:"),"future consequence activated in wrong section");
        check(!cohesion::context(ensemble.phraseArchitectureJson,0,0,4,8).contains("FINAL ENSEMBLE HANDOFF:"),"ending directive activated in opening");
        check(!cohesion::context(ensemble.phraseArchitectureJson,0,0,4,8).contains("CURRENT HANDOFF RECEIVER:"),"future handoff treated as currently active");
        check(ensemble.phraseArchitectureJson==juce::JSON::toString(enriched).toStdString(),"writer mutated accepted architecture");
        ensemble.phraseArchitectureJson=savedArchitecture;
        check(cohesion::context("{}",0).isEmpty(),"legacy context invented musical intent");
        for (const auto behavior:{CompositionBehavior::Club,CompositionBehavior::Adaptive}) {
            ensemble.compositionBehavior=behavior;
            auto schema=juce::JSON::parse(AiPhraseArchitecture::schema(ensemble,{}));
            check(schema.getProperty("properties",{}).hasProperty("ensemble_arguments"),"cohesion missing from general architecture schema");
            check(AiPhraseArchitecture::prompt(ensemble,{},false).contains("ENSEMBLE COHESION"),"planner missing shared brief");
        }
        auto invalid=juce::JSON::parse(json);
        invalid.getProperty("ensemble_arguments",{})[1].getDynamicObject()->setProperty("section_index",0);
        check(!cohesion::validate(invalid,ensemble,error),"duplicate section accepted");
        invalid=juce::JSON::parse(json);
        invalid.getProperty("ensemble_arguments",{})[1].getProperty("contributions",{})[0].getDynamicObject()->setProperty("owner_id","ghost");
        check(!cohesion::validate(invalid,ensemble,error),"unknown contribution accepted");
      }
      {
        auto cyclic = juce::JSON::parse(R"({"sections":[{"name":"repeat","bars":24,"harmonic_cycle_bars":4,"harmonic_cycle_until_bar":24,"harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"Dm"},{"bar_offset":1,"beat_offset":0,"chord_id":"G"},{"bar_offset":2,"beat_offset":0,"chord_id":"C"},{"bar_offset":3,"beat_offset":0,"chord_id":"Am"}]}]})");
        juce::String cycleError;
        auto explicitPlan=juce::JSON::parse(R"({"sections":[{"name":"coda","bars":16,"harmonic_schedule":{"mode":"explicit"},"harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"Dm"},{"bar_offset":14,"beat_offset":0,"chord_id":"Dm9"}]}]})");
        const auto originalEvents=juce::JSON::toString(explicitPlan.getProperty("sections",{})[0].getProperty("harmonic_events",{}));
        check(harmonic_cycle::resolve(explicitPlan,4,cycleError),"explicit coda rejected");
        check(originalEvents==juce::JSON::toString(explicitPlan.getProperty("sections",{})[0].getProperty("harmonic_events",{})),"explicit harmony rewritten");
        const auto explicitOnce=juce::JSON::toString(explicitPlan);
        check(harmonic_cycle::resolve(explicitPlan,4,cycleError)&&explicitOnce==juce::JSON::toString(explicitPlan),"explicit checkpoint not idempotent");
        auto tagged=juce::JSON::parse(R"({"sections":[{"name":"repeat then coda","start_bar":64,"bars":16,"harmonic_schedule":{"mode":"repeat","cycle_bars":4,"repeat_count":3},"harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"Dm"},{"bar_offset":2,"beat_offset":0,"chord_id":"G"},{"bar_offset":14,"beat_offset":0,"chord_id":"Dm9"}]}]})");
        check(harmonic_cycle::resolve(tagged,4,cycleError),"tagged repeat with coda rejected");
        check(tagged.getProperty("sections",{})[0].getProperty("harmonic_events",{}).size()==7,"tagged repeat did not preserve coda");
        const auto taggedOnce=juce::JSON::toString(tagged);
        check(harmonic_cycle::resolve(tagged,4,cycleError)&&taggedOnce==juce::JSON::toString(tagged),"tagged repeat not idempotent");
        tagged.getProperty("sections",{})[0].getProperty("harmonic_schedule",{}).getDynamicObject()->setProperty("repeat_count",0);
        check(!harmonic_cycle::resolve(tagged,4,cycleError),"zero repeat count silently reinterpreted");
        tagged.getProperty("sections",{})[0].getProperty("harmonic_schedule",{}).getDynamicObject()->setProperty("repeat_count",5);
        check(!harmonic_cycle::resolve(tagged,4,cycleError),"overflowing repetition accepted");
        explicitPlan.getProperty("sections",{})[0].getProperty("harmonic_events",{})[0].getDynamicObject()->setProperty("bar_offset",16);
        check(!harmonic_cycle::resolve(explicitPlan,4,cycleError),"explicit event outside section accepted");
        check(harmonic_cycle::planningBrief().contains("TOTAL performances"),"repeat semantics absent from active planning brief");
        auto counted = juce::JSON::parse(R"({"sections":[{"name":"later section","start_bar":64,"bars":32,"harmonic_cycle_bars":4,"harmonic_cycle_repetitions":8,"harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"Dm"},{"bar_offset":2,"beat_offset":0,"chord_id":"Am"}]}]})");
        check(harmonic_cycle::resolve(counted,4,cycleError),"repetition count failed in later section");
        auto countSection=counted.getProperty("sections",{}).getArray()->getReference(0);
        check(static_cast<int>(countSection.getProperty("harmonic_cycle_until_bar",0))==32&&countSection.getProperty("harmonic_events",{}).getArray()->size()==16,"global section position leaked into count");
        const auto countOnce=juce::JSON::toString(counted);
        check(harmonic_cycle::resolve(counted,4,cycleError)&&countOnce==juce::JSON::toString(counted),"counted checkpoint not idempotent");
        countSection.getDynamicObject()->setProperty("harmonic_cycle_repetitions",64);
        check(!harmonic_cycle::resolve(counted,4,cycleError),"global bar number accepted as count");
        check(harmonic_cycle::resolve(cyclic,4,cycleError), "explicit harmonic repeat failed");
        auto& first = cyclic.getDynamicObject()->getProperty("sections").getArray()->getReference(0);
        check(first.getProperty("harmonic_events",{}).getArray()->size()==24,"four-bar repeat left a last-chord plateau");
        const auto once=juce::JSON::toString(cyclic);
        check(harmonic_cycle::resolve(cyclic,4,cycleError)&&juce::JSON::toString(cyclic)==once,"checkpoint expansion was not idempotent");
        first.getDynamicObject()->setProperty("harmonic_cycle_until_bar",23);
        check(!harmonic_cycle::resolve(cyclic,4,cycleError),"partial-cycle boundary accepted silently");
        first.getDynamicObject()->setProperty("harmonic_cycle_until_bar",24);
        first.getProperty("harmonic_events",{}).getArray()->getReference(4).getDynamicObject()->setProperty("chord_id","wrong");
        check(!harmonic_cycle::resolve(cyclic,4,cycleError),"contradictory cycle event accepted");
        auto legacy=juce::JSON::parse(R"({"sections":[{"bars":24,"harmonic_events":[{"bar_offset":0,"chord_id":"held"}]}]})");
        auto legacyText=juce::JSON::toString(legacy);
        check(harmonic_cycle::resolve(legacy,4,cycleError)&&legacyText==juce::JSON::toString(legacy),"legacy harmony changed");
        auto ending=juce::JSON::parse(R"({"performance_score":{"notes":[60,64]},"sections":[{"name":"tail","bars":12,"harmonic_cycle_bars":4,"harmonic_cycle_until_bar":8,"harmonic_events":[{"bar_offset":0,"beat_offset":0,"chord_id":"Dm"},{"bar_offset":2,"beat_offset":0,"chord_id":"Am"},{"bar_offset":8,"beat_offset":0,"chord_id":"closure"}]}]})");
        const auto originalNotes=juce::JSON::toString(ending.getProperty("performance_score",{}),true);
        check(harmonic_cycle::resolve(ending,4,cycleError),"cycle followed by explicit coda failed");
        auto tail=ending.getProperty("sections",{}).getArray()->getReference(0).getProperty("harmonic_events",{});
        check(tail.getArray()->size()==5&&tail.getArray()->getLast().getProperty("chord_id",{}).toString()=="closure","cycle overwrote explicit ending");
        check(juce::JSON::toString(ending.getProperty("performance_score",{}),true)==originalNotes,"harmonic contract changed note material");
      }
        const auto savedPace = juce::SystemStats::getEnvironmentVariable("PULSO_LOCAL_CLUB_PACE", {});
        const auto savedVariety = juce::SystemStats::getEnvironmentVariable("PULSO_LOCAL_CLUB_VARIETY", {});
        setting("PULSO_LOCAL_CLUB_PACE", "auto");
        setting("PULSO_LOCAL_CLUB_VARIETY", "0");
        check(club::nucleus::localClubPreferences().isEmpty(), "default controls changed historical brief");
        for (const auto* pace : {"8-16", "16-32", "32-64"}) {
            setting("PULSO_LOCAL_CLUB_PACE", pace);
            check(club::nucleus::editorial().contains(juce::String(pace) + " BARS"), "editorial omitted saved pace");
            check(club::nucleus::planningBrief().contains(juce::String(pace) + " BARS"), "planning omitted saved pace");
        }
        setting("PULSO_LOCAL_CLUB_PACE", "invalid");
        check(club::nucleus::localClubPreferences().isEmpty(), "invalid pace altered brief");
        setting("PULSO_LOCAL_CLUB_VARIETY", "1");
        check(club::nucleus::planningBrief().contains("Omit the arp entirely"), "free resource omitted from plan");
        auto writerRoot = juce::JSON::parse(R"({"tonal_nucleus":{"lanes":[]}})");
        check(club::nucleus::writerBrief(writerRoot).contains("Omit the arp entirely"), "free resource omitted from writer");
        setting("PULSO_LOCAL_CLUB_PACE", savedPace);
        setting("PULSO_LOCAL_CLUB_VARIETY", savedVariety);
        SongPlan plan;
        plan.rootPitchClass = 0;
        plan.scale = ScaleKind::Major;
        plan.chordPalette.emplace_back();
        plan.chordPalette.back().id = "tonic";
        plan.chordPalette.back().pitchClasses = {0, 4, 7};
        InstrumentAssignment part;
        part.id = "bass";
        part.minimumPitch = 36;
        part.maximumPitch = 72;
        plan.instruments.push_back(part);
        auto root = juce::JSON::parse(R"json({"groove_core_ids":["bass"],"tonal_nucleus":{
          "reference_chord_id":"tonic","cycle_beats":4,"meter_carrier_id":"bass",
          "audible_relationship":"bass articulates the shared pulse","lanes":[{
            "instrument_id":"bass","repeat_intent":"retain contour",
            "notes":[{"beat":0,"duration":1,"pitch":48,"velocity":90},
                     {"beat":2,"duration":1.5,"pitch":55,"velocity":75}]}]}})json");
        const auto original = juce::JSON::toString(root);
        const auto previousStrict = juce::SystemStats::getEnvironmentVariable("PULSO_AI_ALLOW_CHROMATIC", {});
        const auto previousFlag = juce::SystemStats::getEnvironmentVariable("PULSO_AI_CLUB_NUCLEUS_V2", {});
        setting("PULSO_AI_ALLOW_CHROMATIC", "0");
        juce::String error;
        check(club::nucleus::validate(root, plan, error), "valid explicit AI reference rejected");
        check(juce::JSON::toString(root) == original, "validation mutated the AI notes");
        auto pulse = juce::JSON::parse(original);
        auto sketch = pulse.getProperty("tonal_nucleus", {});
        sketch.getDynamicObject()->setProperty("carrier_subdivision_beats", .5);
        check(!club::nucleus::validate(pulse, plan, error), "a false eighth-note declaration was accepted");
        sketch.getDynamicObject()->setProperty("carrier_subdivision_beats", 2);
        check(club::nucleus::validate(pulse, plan, error), "a genuinely declared two-beat pulse was rejected");
        check(club::nucleus::writerBrief(root).contains("retain contour"), "writer lost the actual nucleus");
        auto invalid = juce::JSON::parse(original);
        auto first = invalid.getProperty("tonal_nucleus", {}).getProperty("lanes", {})[0]
            .getProperty("notes", {})[0];
        first.getDynamicObject()->setProperty("duration", 5);
        check(!club::nucleus::validate(invalid, plan, error), "out-of-span reference was silently clipped");
        first.getDynamicObject()->setProperty("duration", 1);
        first.getDynamicObject()->setProperty("pitch", 49);
        check(!club::nucleus::validate(invalid, plan, error), "chromatic reference ignored strict tonal policy");
        first.getDynamicObject()->setProperty("pitch", 84);
        check(!club::nucleus::validate(invalid, plan, error), "out-of-register reference was accepted");
        first.getDynamicObject()->setProperty("pitch", 48);
        invalid.getProperty("tonal_nucleus", {}).getDynamicObject()->setProperty("meter_carrier_id", "ghost");
        check(!club::nucleus::validate(invalid, plan, error), "unknown meter carrier was accepted");
        check(club::nucleus::validate(juce::JSON::parse("{}"), plan, error), "legacy plans lost compatibility");
        setting("PULSO_AI_CLUB_NUCLEUS_V2", "");
        check(!juce::JSON::parse(club::production::schema()).getProperty("properties", {}).hasProperty("tonal_nucleus"),
            "experiment leaked into production schema");
        setting("PULSO_AI_CLUB_NUCLEUS_V2", "1");
        check(juce::JSON::parse(club::production::schema({}, &plan)).getProperty("properties", {}).hasProperty("tonal_nucleus"),
            "experimental schema omitted concrete reference");
        check(!club::production::prompt(plan, "test").contains("No full note arrays yet"),
            "new plan retained a contradictory ban on note sketches");
        check(club::production::windowBrief(juce::JSON::toString(root), 0).contains("CONCRETE TONAL NUCLEUS"),
            "actual reference does not reach writing windows");
        check(club::nucleus::writerBrief(root).contains("0.25-beat collective delay") &&
              club::nucleus::writerBrief(root).contains("offbeat quarters"),
            "continuation instructions lost attack-phase and subdivision distinction");
        SongSection firstSection;
        firstSection.startBar = 0;
        firstSection.bars = 8;
        firstSection.harmonicEvents.emplace_back();
        firstSection.harmonicEvents.back().chordId = "tonic";
        SongSection nextSection = firstSection;
        nextSection.startBar = 8;
        plan.sections = {firstSection, nextSection};
        const auto harmony = club::nucleus::harmonyWindow(plan, 28, 36);
        check(harmony.contains("local [0.00,4.00) absolute [28.00,32.00)") &&
              harmony.contains("local [4.00,8.00) absolute [32.00,36.00)"),
            "window-local harmony lost the preceding held chord or a section boundary");
        setting("PULSO_AI_ALLOW_CHROMATIC", previousStrict);
        setting("PULSO_AI_CLUB_NUCLEUS_V2", previousFlag);
        std::cout << "Club nucleus tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
