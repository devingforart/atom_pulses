#pragma once
#include "MidiExporter.h"
#include <set>
#include <tuple>
#include <cmath>
#include <map>
#include <deque>

namespace pulso::plugin {
// Exact on/off event multiset, tolerant only of integer tick encoding.
// Overlapping equal-pitch notes have no note IDs in SMF: FIFO pairing cannot
// recover which release belongs to which attack. Compare the actual events,
// rather than manufacturing a duration/velocity association from that ambiguity.
inline bool verifyMidiRoundTrip(const juce::File& file,const Pattern& expected,juce::String& error) {
    auto stream=file.createInputStream();juce::MidiFile midi;
    if(!stream||!midi.readFrom(*stream,false)||midi.getTimeFormat()<=0) {error="Cannot decode exported MIDI";return false;}
    const auto ppq=midi.getTimeFormat();
    using Key=std::tuple<long long,int,int,int>;
    std::multiset<Key> source,actual;
    for(const auto& n:expected.notes) {
        source.emplace(std::llround(n.startBeat*ppq),1,n.pitch,n.velocity);
        source.emplace(std::llround((n.startBeat+n.durationBeats)*ppq),0,n.pitch,0);
    }
    for(int t=0;t<midi.getNumTracks();++t) {
        const auto& track=*midi.getTrack(t);
        std::map<std::pair<int,int>,int> pending;
        for(int i=0;i<track.getNumEvents();++i) {
            const auto& message=track.getEventPointer(i)->message;
            if(!message.isNoteOn()&&!message.isNoteOff()) continue;
            auto& count=pending[{message.getChannel(),message.getNoteNumber()}];
            if(message.isNoteOn()) {
                ++count;
                actual.emplace(std::llround(message.getTimeStamp()),1,message.getNoteNumber(),static_cast<int>(message.getVelocity()));
            }
            else {
                if(count==0) {error="Exported MIDI has an orphan note-off";return false;}
                --count;
                actual.emplace(std::llround(message.getTimeStamp()),0,message.getNoteNumber(),0);
            }
        }
        for(const auto& [key,count]:pending) if(count!=0) {error="Exported MIDI has an orphan note-on";return false;}
    }
    if(source!=actual) {error="Exported MIDI differs from rendered note events (expected="+juce::String(static_cast<int>(source.size()))+", actual="+juce::String(static_cast<int>(actual.size()))+")";
        auto a=source.begin(),b=actual.begin();while(a!=source.end()&&b!=actual.end()&&*a==*b){++a;++b;}
        if(a!=source.end()) error+=" expected first difference pitch="+juce::String(std::get<2>(*a))+" tick="+juce::String(std::get<0>(*a))+" on="+juce::String(std::get<1>(*a));
        if(b!=actual.end()) error+=" actual pitch="+juce::String(std::get<2>(*b))+" tick="+juce::String(std::get<0>(*b))+" on="+juce::String(std::get<1>(*b));
        return false;}
    return true;
}
}
