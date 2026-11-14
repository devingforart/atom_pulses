#pragma once
#include "core/SongComposer.h"
#include <juce_core/juce_core.h>
#include <algorithm>

namespace pulso::plugin::execution {
inline juce::String attachPhraseLibrary(const juce::String& json) {
    auto root=juce::JSON::parse(json);
    juce::Array<juce::var> phrases;
    if(auto* arcs=root.getProperty("musical_arcs",{}).getArray()) for(int i=0;i<arcs->size();++i) {
        auto arc=arcs->getReference(i);
        for(const auto* key:{"phrase_reference","response_reference"}) {
            const auto reference=arc.getProperty(key,{});
            if(!reference.isObject()) continue;
            auto phrase=new juce::DynamicObject();
            const auto id="arc-"+juce::String(i)+":"+key;
            phrase->setProperty("id",id);phrase->setProperty("owner_id",arc.getProperty("foreground_id",{}));
            phrase->setProperty("reference",reference.clone());
            phrase->setProperty("answers",juce::String(key)=="response_reference"?juce::var("arc-"+juce::String(i)+":phrase_reference"):juce::var());
            phrases.add(juce::var(phrase));
            arc.getDynamicObject()->setProperty(juce::String(key)+"_id",id);
        }
    }
    root.getDynamicObject()->setProperty("phrase_library",phrases);
    root.getDynamicObject()->setProperty("continuous_phrase_execution",true);
    return juce::JSON::toString(root);
}
inline bool continuousPhrases(const SongPlan& plan) {
    return static_cast<bool>(juce::JSON::parse(juce::String(plan.phraseArchitectureJson)).getProperty("continuous_phrase_execution",false));
}
inline bool reviewUnifiedVoicings(const SongPlan& plan) {
    return plan.compositionBehavior==CompositionBehavior::Club &&
        static_cast<bool>(juce::JSON::parse(juce::String(plan.phraseArchitectureJson)).getProperty("unified_execution",false));
}
// Writer-only projection of the reconciled plan. Archive the planning sources,
// but do not ask the writer to arbitrate competing drafts a second time.
inline SongPlan writerView(const SongPlan& source) {
    auto result=source;
    auto root=juce::JSON::parse(juce::String(source.phraseArchitectureJson));
    if(!root.getProperty("unified_execution",false)) return result;
    for(const auto* field:{"compositional_short_score","ensemble_arguments","grounded_story","act_links"})
        root.getDynamicObject()->removeProperty(field);
    if(const auto* scenes=root.getProperty("ensemble_scenes",{}).getArray()) {
        juce::Array<juce::var> identities;
        for(const auto& scene:*scenes) {
            auto item=new juce::DynamicObject();
            item->setProperty("section_index",scene.getProperty("section_index",{}));
            item->setProperty("foreground_id",scene.getProperty("foreground_id",{}));
            identities.add(juce::var(item));
        }
        root.getDynamicObject()->setProperty("ensemble_scenes",identities);
    }
    root.getDynamicObject()->setProperty("execution_writer_view",true);
    result.phraseArchitectureJson=juce::JSON::toString(root).toStdString();
    return result;
}
// Request sizing only: never changes the AI's sections, notes or phrase lengths.
inline int writingWindowBars(const SongPlan& plan,std::size_t owners) {
    const int base=plan.compositionBehavior==CompositionBehavior::Club?8:16;
    if(plan.compositionBehavior==CompositionBehavior::Club && plan.totalBars>0 &&
       plan.totalBars<=40 && owners>0 && owners<=3) {
        int largest=1;
        for(const auto& section:plan.sections) largest=std::max(largest,section.bars);
        return std::min(plan.totalBars,largest);
    }
    return owners>12?base/2:base;
}
inline juce::String sustainedContext(const Pattern& accepted,double begin,double end) {
    juce::Array<juce::var> held;
    for(const auto& n:accepted.notes) {
        if(n.startBeat>=begin || n.endBeat()<=begin || n.durationBeats<=0) continue;
        auto item=new juce::DynamicObject();
        item->setProperty("part_id",static_cast<int>(n.partId));
        item->setProperty("pitch",n.pitch);
        item->setProperty("window_local_release",std::min(end,n.endBeat())-begin);
        held.add(juce::var(item));
    }
    return "\nIMMUTABLE HELD VOICES AT ENTRY: "+juce::JSON::toString(held,true)+
        "\nCompose this window as a jointly heard phrase. First decide the foreground's "
        "connected line and structural held arrivals; then choose the concurrent inner "
        "voices and extensions against those ACTUAL pitches and durations. Do not emit "
        "the same full chord stack at every attack beneath a changing melody merely "
        "because all notes are in scale. A voicing may retain common tones, move an inner "
        "voice, omit an extension or create a prepared suspension; choose musically. "
        "Before returning, inspect sustained overlaps within your NEW notes as well "
        "as these immutable holds: if a semitone or minor ninth is intentional, make "
        "its preparation, continuation and release audible rather than just naming it. "
        "Do not remove every tension, move accepted notes, silence the foundation or "
        "shorten expressive melody notes merely to avoid choosing a support voicing. "
        "Keep the earned closing arrival; a technical boundary is not a fresh cadence.\n";
}
// One clock for independently authored plans. Read-only: no event scheduling,
// note generation, note filtering, musical rejection or inferred resolution.
inline juce::var harmony(const SongPlan& plan, double begin, double end) {
    struct Event { double beat; std::string chord; };
    std::vector<Event> events;
    for(const auto& section:plan.sections)
        for(const auto& event:section.harmonicEvents)
            events.push_back({(section.startBar+event.barOffset)*plan.beatsPerBar+event.beatOffset,event.chordId});
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.beat<b.beat;});
    juce::Array<juce::var> result;
    for(std::size_t i=0;i<events.size();++i) {
        const auto stop=i+1<events.size()?events[i+1].beat:plan.totalBars*plan.beatsPerBar;
        if(events[i].beat>=end || stop<=begin || stop<=events[i].beat) continue;
        auto item=new juce::DynamicObject();
        item->setProperty("absolute_start",std::max(begin,events[i].beat));
        item->setProperty("absolute_end_exclusive",std::min(end,stop));
        item->setProperty("chord_id",juce::String(events[i].chord));
        result.add(juce::var(item));
    }
    return result;
}
inline juce::String planningBrief(const SongPlan& plan) {
    return "\nAUTHORITATIVE HARMONIC CLOCK (absolute beats, exclusive ends): "+
        juce::JSON::toString(harmony(plan,0,plan.totalBars*plan.beatsPerBar),true)+
        "\nPlace phrase_route arrivals against this actual schedule BEFORE assigning layer "
        "releases. Arc start_bar is song-local; route from_bar is arc-local; layer "
        "entry/release is section-local. A tonic described in prose is not present until "
        "its mapped interval. Reconcile the route, scene, support and speaker release "
        "in one chronology. A breath means an intentional musical rest, not necessarily "
        "the entire remaining section. If a final sustain is intended, reserve its real "
        "time in the route instead of marking that same time as absence. No required "
        "terminal duration or compulsory activity; choose the phrase yourself.\n";
}
inline juce::String brief(const SongPlan& plan,const Pattern& accepted,
                          std::size_t sectionIndex,int firstBar,int bars) {
    if(sectionIndex>=plan.sections.size() || bars<=0) return {};
    const auto root=juce::JSON::parse(juce::String(plan.phraseArchitectureJson));
    const auto* arcs=root.getProperty("musical_arcs",{}).getArray();
    if(!arcs) return {}; // Legacy and other architecture formats retain their path.
    const auto& section=plan.sections[sectionIndex];
    const double origin=section.startBar*plan.beatsPerBar;
    const double begin=origin+firstBar*plan.beatsPerBar, end=begin+bars*plan.beatsPerBar;
    juce::Array<juce::var> routes;
    for(const auto& arc:*arcs) {
        const double arcStart=static_cast<double>(arc.getProperty("start_bar",0))*plan.beatsPerBar;
        const auto owner=arc.getProperty("foreground_id",{}).toString();
        if(const auto* spans=arc.getProperty("phrase_route",{}).getArray()) for(const auto& span:*spans) {
            const double a=arcStart+static_cast<double>(span.getProperty("from_bar",0))*plan.beatsPerBar;
            const double b=a+static_cast<double>(span.getProperty("bars",0))*plan.beatsPerBar;
            if(a>=origin+section.bars*plan.beatsPerBar || b<=origin || b<=a) continue;
            auto entry=new juce::DynamicObject();
            entry->setProperty("owner_id",owner);
            entry->setProperty("phase",b<=begin?"past":a>=end?"future":"current");
            entry->setProperty("action",span.getProperty("action",{}));
            entry->setProperty("material",span.getProperty("material",{}));
            entry->setProperty("consequence",span.getProperty("consequence",{}));
            entry->setProperty("absolute_start",a);
            entry->setProperty("absolute_end_exclusive",b);
            entry->setProperty("window_local_start",a-begin);
            entry->setProperty("window_local_end_exclusive",b-begin);
            entry->setProperty("actual_harmony",harmony(plan,a,b));
            routes.add(juce::var(entry));
        }
    }
    juce::Array<juce::var> evidence;
    for(std::size_t i=0;i<plan.instruments.size();++i) {
        auto entry=new juce::DynamicObject();
        entry->setProperty("owner_id",juce::String(plan.instruments[i].id));
        int attacks=0; double lastRelease=-1;
        const NoteEvent* latest=nullptr;
        for(const auto& n:accepted.notes) if(n.partId==i+1 && n.startBeat<begin && n.durationBeats>0) {
            if(n.startBeat>=origin) ++attacks;
            if(n.endBeat()>lastRelease) lastRelease=n.endBeat();
            if(!latest || n.startBeat>latest->startBeat) latest=&n;
        }
        entry->setProperty("accepted_attacks_this_section",attacks);
        entry->setProperty("last_accepted_release_absolute",lastRelease<0?juce::var():juce::var(lastRelease));
        if(latest) {
            entry->setProperty("last_attack_absolute",latest->startBeat);
            entry->setProperty("last_pitch",latest->pitch);
        }
        if(const auto* layers=root.getProperty("layers",{}).getArray()) for(const auto& layer:*layers)
            if(layer.getProperty("instrument_id",{}).toString()==juce::String(plan.instruments[i].id))
                if(const auto* states=layer.getProperty("states",{}).getArray()) for(const auto& state:*states)
                    if(static_cast<int>(state.getProperty("section_index",-1))==static_cast<int>(sectionIndex)) {
                        entry->setProperty("state",state.getProperty("mode",{}));
                        entry->setProperty("activity_start_absolute",origin+static_cast<double>(state.getProperty("entry_beat",0)));
                        entry->setProperty("activity_end_absolute",origin+static_cast<double>(state.getProperty("release_beat",section.bars*plan.beatsPerBar)));
                    }
        evidence.add(juce::var(entry));
    }
    return "\nPLAN-TO-SCORE EXECUTION LEDGER: window absolute ["+juce::String(begin)+","+juce::String(end)+")\n"+
        "PHRASE LIBRARY (identified source notes, not automatic loops): "+juce::JSON::toString(root.getProperty("phrase_library",{}),true)+
        "\nPROJECTED ROUTES: "+juce::JSON::toString(routes,true)+"\nACCEPTED EXECUTION: "+juce::JSON::toString(evidence,true)+
        "\nUse this shared clock to reconcile prose, route and sounding harmony. Past "
        "routes are evidence, not permission to replay them; future routes are preparation, "
        "not arrivals to perform now. Current route duration is time for a musical thought, "
        "not a command to play its short reference once and disappear. Accepted attack "
        "counts and releases are observations, never density targets. Do not infer silence "
        "from a missing reference. If prose names a chord absent from the mapped span, "
        "compose the current thought against the real chord and prepare the later arrival "
        "rather than claiming a resolution that has not occurred. Respect explicit absence "
        "and immutable accepted notes. A completed ending needs no refill; an unresolved "
        "thought needs an AI-composed consequence, not an automatic held note.\n";
}
}
