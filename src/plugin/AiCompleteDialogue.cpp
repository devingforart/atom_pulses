#include "AiCompleteDialogue.h"
#include "AiComposer.h"
#include "AiThematicSubject.h"
#include "AiEnsembleDiscourse.h"
#include "AiPhraseDevelopment.h"
#include "AiMaterialDialogue.h"
#include "core/Orchestration.h"
#include <algorithm>
#include <set>

namespace pulso::plugin {
bool AiCompleteDialogue::enabled() {
    return juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT", {}) == "1";
}
bool AiCompleteDialogue::pitchedEnsembleEnabled() {
    return !wholeArgumentEnabled() && AiEnsembleDiscourse::prosodyEnabled() &&
        juce::SystemStats::getEnvironmentVariable("PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT", {}) == "1";
}
bool AiCompleteDialogue::wholeArgumentEnabled() {
    return enabled() && juce::SystemStats::getEnvironmentVariable(
        "PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT", {}) == "1";
}
juce::String AiCompleteDialogue::argumentPlanningSchema(const juce::String& original) {
    if (!wholeArgumentEnabled()) return original;
    auto root = juce::JSON::parse(original);
    auto* object = root.getDynamicObject();
    object->getProperty("properties").getDynamicObject()->setProperty("argument_owner_ids",
        juce::JSON::parse(R"json({"type":"array","maxItems":6,"items":{"type":"string"},
          "description":"Exact cast IDs that together carry the entire musical argument, including its structural bass. Other cast members are orchestrated afterwards. Not a quota for every voice to play constantly."})json"));
    object->getProperty("required").getArray()->add("argument_owner_ids");
    return juce::JSON::toString(root, true);
}
juce::String AiCompleteDialogue::argumentPlanningBrief() {
    if (!wholeArgumentEnabled()) return {};
    return "\nWHOLE ARGUMENT EXPERIMENT: In argument_owner_ids choose the real cast voices that must "
        "jointly speak the WHOLE musical argument before orchestration: the main speaker, substantive "
        "answering/contrapuntal speakers where needed and the actual structural bass. Include the declared "
        "protagonist. Choose a small intelligible partnership, not every orchestral color. Six is a payload "
        "ceiling, not a target or a limit on the eventual arrangement. Plan speaker/response phrases using "
        "these selected owners; the remaining owners will support their completed actual MIDI later. "
        "The selected voices will be composed in ONE response from beginning through ending, with every "
        "section present as coordinates, NOT successive composition requests. Give the whole line a "
        "specific opening claim, an implication that the next phrase really takes forward, a meaningful "
        "development and an ending that concludes that same thought. These are musical responsibilities, "
        "not a compulsory form or a new motive per section. A sustained or repeated idea can remain "
        "central, but a sequence of compatible figures is not a connected thought merely because the "
        "section descriptions say it is. Hear the selected voices with one neutral piano.\n";
}
bool AiCompleteDialogue::validateArgumentOwners(const juce::var& root, const SongPlan& plan, juce::String& error) {
    if (!wholeArgumentEnabled()) return true;
    const auto* owners = root.getProperty("argument_owner_ids", {}).getArray();
    if (!owners || owners->isEmpty() || owners->size() > 6) {
        error = "Whole argument needs a bounded, explicit cast selection"; return false;
    }
    std::set<std::string> ids;
    for (const auto& owner : *owners) {
        const auto id = owner.toString().toStdString();
        const auto found = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& part) { return part.id == id; });
        if (!owner.isString() || !ids.insert(id).second || found == plan.instruments.end() ||
            voiceDefinition(found->sourceVoice).family == VoiceFamily::Rhythm) {
            error = "Whole argument names an unknown, duplicate or unpitched owner"; return false;
        }
    }
    if (!plan.narrativeSpine.protagonistInstrumentId.empty() &&
        !ids.contains(plan.narrativeSpine.protagonistInstrumentId)) {
        error = "Whole argument omitted its declared principal speaker"; return false;
    }
    if (const auto* phrases = root.getProperty("phrases", {}).getArray())
        for (const auto& phrase : *phrases)
            for (const auto* field : {"speaker_id", "response_id"}) {
                const auto id = phrase.getProperty(field, {}).toString().toStdString();
                if (!id.empty() && !ids.contains(id)) {
                    error = "A planned melodic speaker is outside argument_owner_ids"; return false;
                }
            }
    return true;
}
juce::String AiCompleteDialogue::accompanimentBrief() {
    if (!enabled()) return {};
    return "\nCOMPLETE-DIALOGUE ACCOMPANIMENT: The accepted foreground now "
        "contains its actual future as well as its past. Read its attacks, "
        "held tones, breaths and arrivals IN THIS WINDOW before composing "
        "the ensemble around it. Preserve a rich harmonic body and independent "
        "counterpoint where called for; do not solve clarity by removing the "
        "orchestration. Decide which accompaniment motion advances that actual "
        "sentence and which would merely compete with it. Shared pulse and "
        "meaningful common arrivals are compatible with different inner rhythms. "
        "A rapid foreground gesture does not require every supporting part "
        "to insert an unrelated passing figure. When doubling a held foreground "
        "pitch, author an intelligible shared attack or deliberate echo, not "
        "an accidental near reattack in the same register. The combined MIDI "
        "must remain comprehensible through one piano, not only through "
        "separate instrumental timbres. You choose every note; none of this "
        "will be applied as an automatic thinning or alignment process.\n" + AiMaterialDialogue::accompanimentBrief();
}
std::vector<std::size_t> AiCompleteDialogue::owners(const SongPlan& plan,
    const std::vector<std::size_t>& available) {
    std::set<std::string> ids{plan.narrativeSpine.protagonistInstrumentId};
    const auto jointIds = AiEnsembleDiscourse::jointVoiceIds(plan);
    ids.insert(jointIds.begin(), jointIds.end());
    const auto parsed = juce::JSON::parse(
        juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (wholeArgumentEnabled()) {
        ids.clear();
        if (const auto* declared = parsed.getProperty("argument_owner_ids", {}).getArray())
            for (const auto& owner : *declared) ids.insert(owner.toString().toStdString());
    }
    if (!wholeArgumentEnabled() && AiThematicSubject::enabled())
        ids.insert(parsed.getProperty("musical_subject", {}).getProperty(
            "instrument_id", {}).toString().toStdString());
    if (const auto* phrases = !wholeArgumentEnabled() ? parsed.getProperty("phrases", {}).getArray() : nullptr)
        for (const auto& phrase : *phrases)
            for (const auto* field : {"speaker_id", "response_id"})
                ids.insert(phrase.getProperty(field, {}).toString().toStdString());
    std::vector<std::size_t> result;
    for (const auto owner : available)
        if (owner < plan.instruments.size() &&
            (ids.contains(plan.instruments[owner].id) || pitchedEnsembleEnabled()) &&
            (voiceDefinition(plan.instruments[owner].sourceVoice).family != VoiceFamily::Rhythm ||
             plan.instruments[owner].id == plan.narrativeSpine.protagonistInstrumentId))
            result.push_back(owner);
    return result;
}
std::vector<std::vector<DialogueWindow>> AiCompleteDialogue::blocks(const SongPlan& plan) {
    if (wholeArgumentEnabled()) {
        std::vector<DialogueWindow> all;
        for (std::size_t section = 0; section < plan.sections.size(); ++section)
            all.push_back({section, 0, plan.sections[section].bars});
        return all.empty() ? std::vector<std::vector<DialogueWindow>>{} :
            std::vector<std::vector<DialogueWindow>>{std::move(all)};
    }
    // A larger joint ensemble needs smaller payloads, NOT more musical cadences.
    const int maximumRequestBars = AiEnsembleDiscourse::prosodyEnabled() ? 24 : 48;
    std::vector<std::vector<DialogueWindow>> result;
    int used = 0;
    for (std::size_t section = 0; section < plan.sections.size(); ++section)
        for (int first = 0; first < plan.sections[section].bars;) {
            const auto bars = std::min(maximumRequestBars, plan.sections[section].bars - first);
            if (result.empty() || used + bars > maximumRequestBars) {
                result.emplace_back(); used = 0;
            }
            result.back().push_back({section, first, bars});
            used += bars;
            first += bars;
        }
    return result;
}
juce::String AiCompleteDialogue::schema(const SongPlan& plan,
    const std::vector<std::size_t>& owners) {
    auto parts = juce::JSON::parse(AiComposer::directWindowSchemaFor(plan, owners));
    for (const auto owner : owners) {
        auto* part = parts.getDynamicObject()->getProperty("properties").getDynamicObject()->getProperty(
            juce::Identifier(juce::String::fromUTF8(plan.instruments[owner].id.c_str()))).getDynamicObject();
        part->setProperty("maxItems", 768);
        auto* note = part->getProperty("items").getDynamicObject()->getProperty("properties").getDynamicObject();
        auto* pitch = note->getProperty("pitch").getDynamicObject();
        pitch->setProperty("minimum", std::max(0, plan.instruments[owner].minimumPitch - 12));
        pitch->setProperty("maximum", std::min(127, plan.instruments[owner].maximumPitch + 12));
        auto* velocity = note->getProperty("velocity").getDynamicObject();
        velocity->setProperty("minimum", 1); velocity->setProperty("maximum", 127);
        note->getProperty("beat").getDynamicObject()->setProperty("minimum", 0);
        note->getProperty("beat").getDynamicObject()->setProperty("multipleOf", .25);
        note->getProperty("duration").getDynamicObject()->setProperty("minimum", .25);
        note->getProperty("duration").getDynamicObject()->setProperty("multipleOf", .25);
    }
    auto result = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["windows"],"properties":{"windows":{"type":"array","maxItems":64,
      "items":{"type":"object","additionalProperties":false,
      "required":["section_index","first_bar","parts"],"properties":{
      "section_index":{"type":"integer"},"first_bar":{"type":"integer"},
      "parts":{"type":"object"}}}}}})json");
    result.getDynamicObject()->getProperty("properties").getDynamicObject()
        ->getProperty("windows").getDynamicObject()->getProperty("items").getDynamicObject()
        ->getProperty("properties").getDynamicObject()->setProperty("parts",
            parts);
    return juce::JSON::toString(result, true);
}
juce::String AiCompleteDialogue::prompt(const juce::String& direction,
    const SongPlan& plan,
    const std::vector<std::size_t>& selected, const std::vector<DialogueWindow>& windows,
    const Pattern& accepted, bool includePlanningIntentions, bool gestureCoordinates) {
    juce::String out;
    const bool expectationLed = AiMaterialDialogue::expectationLedEnabled();
    if (gestureCoordinates) {
        out << "\nEXACT SCORE CONTEXT FOR THE GESTURE FORMAT:\n";
    } else if (expectationLed && !includePlanningIntentions) {
        out << "\nEXACT SCORE CONTEXT FOR REVIEW: the review contract below determines whether "
            "to keep or revise the actual score; this is not a new composition request.\n";
    } else if (expectationLed) {
        out << AiMaterialDialogue::realizationBrief();
    } else {
    out << AiThematicSubject::realizationBrief();
    if (includePlanningIntentions) out << AiMaterialDialogue::realizationBrief();
    if (wholeArgumentEnabled())
        out << "\nONE COMPLETE MUSICAL ARGUMENT, BEFORE ORCHESTRATION: You have the entire work in "
            "this single request. Compose all selected voices as a connected, consequential musical "
            "discourse from the first actual utterance through the final cadence. Think of their full "
            "interaction before emitting any window. A phrase must create an expectation that the next "
            "actually receives, develops, contradicts or fulfils; compatible pitches or a recurring "
            "rhythm alone do not create that relationship. Let the continuation follow the specific "
            "previous thought, not restart from the motif clue at each section. Repetition and silence "
            "remain valid when their context carries the implication. The bass is a musical partner, "
            "not scheduled root accompaniment. Sing or hear these few voices together on one piano: "
            "the listener should recognize what is being concluded at the end without reading any "
            "section labels. This is a complete reduced score, not a collection of thematic hints. "
            "The remaining cast will be composed around THESE exact notes afterwards. Do not make "
            "the reduction empty in expectation of that later orchestration. Do not add filler "
            "merely to avoid an intentional rest. Window objects are local-coordinate containers only; "
            "they do not represent separate composing tasks or compulsory phrase endings.\n";
    if (pitchedEnsembleEnabled())
        out << "\nCOMPLETE PITCHED ENSEMBLE AUTHORSHIP: This request contains ALL "
            "pitched cast members, including quiet inner lines, chordal bodies "
            "and bass support. No additional pitched shadow will be written "
            "later to fill their gaps. The AI-planned foreground and joint "
            "relationships define what the listener follows, not a requirement "
            "that every listed part be melodic or active. Write a full harmonic "
            "body, bass and real counterpoint as needed while letting a clear "
            "sentence speak. Owners may sustain or rest; all owners appearing "
            "in one writing request does NOT mean they must sound together. "
            "Hear the combined onsets and releases, including an apparently "
            "secondary part. Coherent ordinary subdivisions can be deeply "
            "expressive; arbitrary tiny displacements or gaps are not human "
            "phrasing. The only later additions may be unpitched percussion.\n";
    if (AiEnsembleDiscourse::prosodyEnabled())
        out << "\nJOINT PHRASE REALIZATION: The owners below include the AI-selected "
            "rhythmic partners, not only the lead and its answering melody. Write "
            "their ACTUAL entries, support, motion and release together. Maintain "
            "the rhythmic identity and articulation of the thought as it develops. "
            "An available sixteenth-grid coordinate is not a reason to shift an "
            "eighth-based phrase by one sixteenth. Make a displacement deliberate "
            "and intelligible against the shared supports; do not add arbitrary "
            "fractional gaps to simulate independence or human expression. "
            "A moving bass can have a complete contribution without changing "
            "under every small foreground event. A partner must not merely "
            "repeat the same pitch stream with a tiny delay. Keep intentional "
            "repetition and counterpoint; this is not a thinning or homorhythm "
            "rule. Metric supports guide the phrase and are NOT extra scheduled "
            "notes. No code will align attacks, join notes, or humanize the result. "
            "Carry the thought across technical windows without restarting it.\n";
    out << (pitchedEnsembleEnabled() ?
        "Compose the ACTUAL COMPLETE PITCHED ENSEMBLE for the requested span. " :
        "Compose the ACTUAL COMPLETE MELODIC DIALOGUE for the requested span before any accompaniment is written. ")
        << "Every onset, pitch, sounding "
        "duration, rest and accent is your musical decision. Work across ALL "
        "requested windows as one consequential argument, hearing how its "
        "opening becomes its continuation and ending. Do not write one small "
        "motif per box and connect the boxes afterwards. The requested section "
        "coordinates encode the existing harmony; they do not demand a new "
        "rhythm, a rest or a cadence at every boundary.\n"
        "A statement must actually continue beyond its identifying clue. "
        "Choose complete, singable or instrumentally meaningful sentences "
        "for this style, including prepared virtuosity where earned. Give "
        "passing notes a destination and repeated rhythmic gestures a reason. "
        "Do not substitute prolonged absence for a missing continuation. "
        "Dialogue can overlap in genuine counterpoint: let the partner inherit "
        "a specific interval, rhythm or unresolved destination, not just wait "
        "for its solo. Keep the common pulse intelligible through independent "
        "rhythm. Preserve recognizable melodic identity without looping the "
        "same little cell. Hear the whole melodic score with one piano.\n"
        "Any remaining unrequested accompaniment will follow THESE accepted "
        "notes later; it cannot supply missing melody or change this score. Resolve "
        "or deliberately defer harmonic tensions according to the musical "
        "context. When the requested span reaches the end of the work, write "
        "the actual earned melodic conclusion and release, not a closing tag "
        "after an unfinished stream. Intermediate payload boundaries are not "
        "musical endings. No algorithm will add, delete, stretch or retune notes. "
        "A sketch's bearing tones are revisable intentions, not extra scheduled "
        "MIDI attacks to append after writing the real line. If your actual "
        "sentence reaches its goal differently, author one convincing arrival "
        "rather than stuttering the same pitch just to hit a sketch timestamp. "
        "For repeated notes of a single melodic voice, compose the preceding "
        "release before its next same-pitch attack; a held note with another "
        "overlapping attack is not a tie. Keep meaningful rhythmic repetition "
        "and varied articulation, choosing durations yourself.\n";
    }
    out << "USER: " << direction << "\nOWNERS (exact IDs, preferred MIDI ranges "
        "with one octave expressive latitude):\n";
    for (const auto index : selected) {
        const auto& part = plan.instruments[index];
        out << juce::String::fromUTF8(part.id.c_str()) << " " << part.minimumPitch
            << ".." << part.maximumPitch << " | " << juce::String::fromUTF8(part.role.c_str())
            << " | active sections=";
        if (part.activeSections.empty()) out << "all";
        else for (const auto& section : part.activeSections)
            out << "[" << juce::String::fromUTF8(section.c_str()) << "]";
        out << "\n";
    }
    if (!gestureCoordinates) {
    out << (expectationLed && !includePlanningIntentions ?
        "REVIEW WINDOWS: If decision=keep, return windows=[] and revised_owner_ids=[]. "
        "Only for decision=revise return each listed window exactly once. Inside parts, beat is " :
        "OUTPUT WINDOWS: Return each exactly once. Inside parts, beat is ")
        << "WINDOW-LOCAL, duration is in beats. Use the quarter-beat grid, MIDI "
        "velocity 1..127 and positive duration. Attacks belong inside their "
        "listed window; a sounding duration MAY extend into later windows "
        "or sections, but never beyond the whole work's end. Such a sustain "
        "is one note: do not duplicate its attack in the following window. "
        "Do not clip a musical idea at a section boundary. "
        "Every requested owner gets an array; intentional rest is [].\n";
    for (const auto& window : windows)
        out << "section_index=" << static_cast<int>(window.section)
            << " first_bar=" << window.firstBar << " bars=" << window.bars
            << " length_beats=" << juce::String(window.bars * plan.beatsPerBar, 2)
            << " absolute_start=" << juce::String((plan.sections[window.section].startBar +
                window.firstBar) * plan.beatsPerBar, 2) << "\n";
    }
    out << "WHOLE WORK: " << plan.targetSeconds << " seconds, " << plan.totalBars
        << " bars, " << juce::String(plan.bpm, 2) << " BPM, beats_per_bar="
        << juce::String(plan.beatsPerBar, 2) << ", key="
        << juce::String::fromUTF8(plan.key.c_str()) << ", tonal_policy="
        << juce::String(tonalPolicyKey(plan.harmonicLanguage.tonalPolicy).data());
    if (includePlanningIntentions && !expectationLed)
        out << "\nMOTIF: " << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
        << "\nHARMONIC DEBT: " << juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str())
        << "\nRESOLUTION: " << juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str());
    if (!gestureCoordinates) {
    out << "\nNORMALIZED FORM:\n";
    for (std::size_t i = 0; i < plan.sections.size(); ++i) {
        const auto& section = plan.sections[i];
        out << static_cast<int>(i) << " absolute_bar=" << section.startBar << " bars=" << section.bars;
        if (includePlanningIntentions && !expectationLed)
            out << " | " << juce::String::fromUTF8(section.name.c_str())
            << " function=" << juce::String::fromUTF8(section.function.c_str())
            << " treatment=" << juce::String::fromUTF8(section.motifTreatment.c_str())
            << " harmony=" << juce::String::fromUTF8(section.harmonicDirection.c_str());
        out << "\n";
    }
    }
    out << "CHORD PALETTE (pitch classes, C=0):\n";
    out << "Pitch-class names: 0=C, 1=C#, 2=D, 3=Eb, 4=E, 5=F, "
        "6=F#, 7=G, 8=Ab, 9=A, 10=Bb, 11=B. C4=60. "
        "A chord's actual pitch-class list is authoritative, not a poetic "
        "label that could be read as a different mode.\n";
    for (const auto& chord : plan.chordPalette) {
        out << juce::String::fromUTF8(chord.id.c_str()) << " "
            << juce::String::fromUTF8(chord.label.c_str()) << " pcs=";
        for (const auto pc : chord.pitchClasses) out << pc << ",";
        out << " bass=" << chord.bassPitchClass << "\n";
    }
    struct Event { double beat{}; const HarmonicChord* chord{}; };
    std::vector<Event> timeline;
    for (const auto& section : plan.sections)
        for (const auto& event : section.harmonicEvents) {
            const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
                [&](const auto& item) { return item.id == event.chordId; });
            if (chord != plan.chordPalette.end())
                timeline.push_back({(section.startBar + event.barOffset) * plan.beatsPerBar +
                    event.beatOffset, &*chord});
        }
    std::stable_sort(timeline.begin(), timeline.end(), [](const auto& a, const auto& b) {
        return a.beat < b.beat;
    });
    out << (gestureCoordinates ? "EXACT NORMALIZED HARMONY (ABSOLUTE start..end beats). " :
        "EXACT NORMALIZED HARMONY FOR EACH OUTPUT WINDOW (WINDOW-LOCAL start..end beats). ")
        << "This is harmonic context, not an instruction to "
        "turn the melody into chord tones only.\n";
    if (plan.harmonicLanguage.tonalPolicy == TonalPolicy::Consolidated)
        out << "Under the existing consolidated policy, borrowed colour belongs "
            "to its declared interval, not every later phrase. In-key passing "
            "and neighbour tones are available; an undeclared chromatic tone "
            "needs a real short approach/resolution, not an unexplained held "
            "foreign pitch. Check the actual pitch class at its actual time "
            "before committing the final JSON.\n";
    else
        out << "The expanded/free policy permits the wider language requested "
            "by the user. Author coherent preparation and consequence for "
            "tension without pretending every note must belong to a chord.\n";
    if (gestureCoordinates)
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            out << "  " << juce::String(timeline[i].beat, 2) << ".."
                << juce::String(i + 1 < timeline.size() ? timeline[i + 1].beat :
                    plan.totalBars * plan.beatsPerBar, 2) << " "
                << juce::String::fromUTF8(timeline[i].chord->id.c_str()) << " pcs=";
            for (const auto pc : timeline[i].chord->pitchClasses) out << pc << ",";
            out << "\n";
        }
    if (!gestureCoordinates) for (const auto& window : windows) {
        const auto start = (plan.sections[window.section].startBar + window.firstBar) * plan.beatsPerBar;
        const auto end = start + window.bars * plan.beatsPerBar;
        out << "section=" << static_cast<int>(window.section) << " first_bar=" << window.firstBar << "\n";
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            const auto finish = i + 1 < timeline.size() ? timeline[i + 1].beat :
                plan.totalBars * plan.beatsPerBar;
            if (finish <= start || timeline[i].beat >= end) continue;
            out << "  " << juce::String(std::max(start, timeline[i].beat) - (gestureCoordinates ? 0 : start), 2)
                << ".." << juce::String(std::min(end, finish) - (gestureCoordinates ? 0 : start), 2) << " "
                << juce::String::fromUTF8(timeline[i].chord->id.c_str()) << " pcs=";
            for (const auto pc : timeline[i].chord->pitchClasses) out << pc << ",";
            out << "\n";
        }
    }
    auto intentions = juce::JSON::parse(
        juce::String::fromUTF8(plan.phraseArchitectureJson.c_str()));
    if (auto* phrases = intentions.getDynamicObject() == nullptr ? nullptr :
            intentions.getDynamicObject()->getProperty("phrases").getArray())
        for (auto& phrase : *phrases)
            if (auto* delivery = phrase.getProperty("delivery", {}).getDynamicObject()) {
                juce::String purposes;
                if (const auto* tones = delivery->getProperty("structural_tones").getArray())
                    for (const auto& tone : *tones)
                        purposes << tone.getProperty("purpose", {}).toString() << "; ";
                delivery->removeProperty("structural_tones");
                if (purposes.isNotEmpty()) delivery->setProperty("bearing_intentions", purposes);
            }
    if (includePlanningIntentions && !expectationLed)
        out << "EXISTING AI PHRASE INTENTIONS (semantic guidance; the complete "
            "dialogue you author is the single numerical authority for the melodic "
            "MIDI, not a second layer on top of fixed sketch-note timestamps):\n"
            << juce::JSON::toString(intentions, true);
    else if (const auto* subject = intentions.getProperty("musical_subject", {}).getDynamicObject()) {
        out << "\nREFERENCE SUBJECT: instrument=" << subject->getProperty("instrument_id").toString()
            << " length_beats=" << subject->getProperty("length_beats").toString()
            << " actual local notes=" << juce::JSON::toString(subject->getProperty("notes"), true)
            << "\nThe planning narrative has intentionally been withheld. "
            "Judge the realized notes rather than confirming claims about their meaning.\n";
        if (AiPhraseDevelopment::enabled())
            out << "ADDITIONAL REFERENCE MATERIAL (intentions, not proof of success): "
                << juce::JSON::toString(intentions.getProperty("thematic_materials", {}), true) << "\n";
    }
    if (!accepted.notes.empty()) {
        out << "\nACTUAL EARLIER MELODIC MIDI (absolute beat/pitch/duration/velocity):\n";
        for (const auto index : selected) {
            out << juce::String::fromUTF8(plan.instruments[index].id.c_str()) << ": ";
            std::vector<const NoteEvent*> notes;
            for (const auto& note : accepted.notes)
                if (note.partId == index + 1) notes.push_back(&note);
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (i < 48 || i + 64 >= notes.size()) {
                    const auto& note = *notes[i];
                    out << juce::String(note.startBeat, 2) << "/" << note.pitch << "/"
                        << juce::String(note.durationBeats, 2) << "/" << note.velocity << "; ";
                }
            out << "\n";
        }
    }
    return out;
}
bool AiCompleteDialogue::parse(const juce::String& json, const SongPlan& plan,
    const std::vector<std::size_t>& selected, const std::vector<DialogueWindow>& windows,
    PerformanceScore& score, juce::String& error) {
    score = {};
    const auto parsed = juce::JSON::parse(json);
    const auto* rows = parsed.getProperty("windows", {}).getArray();
    if (rows == nullptr || static_cast<std::size_t>(rows->size()) != windows.size()) {
        error = "Complete dialogue omitted a requested window"; return false;
    }
    std::set<std::pair<int, int>> seen;
    for (const auto& row : *rows) {
        const auto section = static_cast<int>(row.getProperty("section_index", -1));
        const auto first = static_cast<int>(row.getProperty("first_bar", -1));
        const auto found = std::find_if(windows.begin(), windows.end(), [&](const auto& item) {
            return static_cast<int>(item.section) == section && item.firstBar == first;
        });
        if (found == windows.end() || !seen.emplace(section, first).second) {
            error = "Complete dialogue has an unknown or repeated window"; return false;
        }
        PerformanceScore passage;
        if (!AiComposer::parseDirectWindowJson(juce::JSON::toString(row.getProperty("parts", {})),
            plan, selected, found->section, found->firstBar, found->bars,
            passage, error, false, true, true)) return false;
        for (auto& cell : passage.cells) score.cells.push_back(std::move(cell));
        for (auto& placement : passage.placements) score.placements.push_back(std::move(placement));
    }
    return true;
}
}
