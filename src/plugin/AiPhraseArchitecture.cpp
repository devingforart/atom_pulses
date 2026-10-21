#include "AiPhraseArchitecture.h"
#include "AiMelodicDiscourse.h"
#include "AiEnsembleDiscourse.h"
#include "AiThematicSubject.h"
#include "AiPhraseDevelopment.h"
#include "AiCompleteDialogue.h"
#include "AiMaterialDialogue.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pulso::plugin {
namespace {

bool integer(const juce::var& value, int& result) {
    if (!(value.isInt() || value.isInt64() || value.isDouble())) return false;
    const auto number = static_cast<double>(value);
    if (!std::isfinite(number) || number != std::floor(number) ||
        number < -100000 || number > 100000) return false;
    result = static_cast<int>(number);
    return true;
}

bool knownOwner(const SongPlan& plan, const juce::String& id) {
    return std::any_of(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& part) { return id == juce::String::fromUTF8(part.id.c_str()); });
}

bool shortText(const juce::var& value, int maximum = 2000) {
    return value.isString() && value.toString().trim().isNotEmpty() &&
        value.toString().length() <= maximum;
}

} // namespace

juce::String AiPhraseArchitecture::schema() {
    return AiThematicSubject::extendSchema(AiEnsembleDiscourse::extendSchema(AiMelodicDiscourse::extendSchema(R"json({"type":"object","additionalProperties":false,
      "required":["motif_clue","dramatic_question","answer_principle","phrases"],
      "properties":{
        "motif_clue":{"type":"string"},
        "dramatic_question":{"type":"string"},
        "answer_principle":{"type":"string"},
        "phrases":{"type":"array","maxItems":64,"items":{
          "type":"object","additionalProperties":false,
          "required":["section_index","local_start_bar","bars","speaker_id",
            "response_id","gesture","rhythm_shape","contour","register_low",
            "register_high","silence_strategy","relationship","consequence"],
          "properties":{
            "section_index":{"type":"integer"},
            "local_start_bar":{"type":"integer"},
            "bars":{"type":"integer"},
            "speaker_id":{"type":"string"},
            "response_id":{"type":"string","description":"Exact instrument id from INSTRUMENTS, or empty string. Never a phrase id."},
            "gesture":{"type":"string"},
            "rhythm_shape":{"type":"string"},
            "contour":{"type":"string"},
            "register_low":{"type":"integer"},
            "register_high":{"type":"integer"},
            "silence_strategy":{"type":"string"},
            "relationship":{"type":"string"},
            "consequence":{"type":"string"}
          }}}
      }})json")));
}

juce::String AiPhraseArchitecture::schema(const SongPlan& plan) {
    return AiMaterialDialogue::planningSchema(
        AiPhraseDevelopment::bindCastSchema(AiCompleteDialogue::argumentPlanningSchema(schema()), plan), plan);
}

juce::String AiPhraseArchitecture::prompt(const SongPlan& plan,
                                          const juce::String& direction,
                                          bool longFormArc) {
    juce::String out;
    out << "You are the phrase architect for a complete symbolic MIDI work. "
        "The form, harmony and instrument cast below are fixed. Plan audible phrases "
        "and relationships BEFORE any notes are written. This is a creative score "
        "intention, not a quota, algorithm or instruction to fill every bar. "
        "Return ";
    if (AiPhraseDevelopment::enabled())
        out << "the meaningful phrase intentions that realize each section's internal "
            "argument, within the bounded schema. A section is not itself a phrase. They may ";
    else
        out << "one or two significant phrase intentions per section. For "
            "long works with more than twelve sections, favor one intention per "
            "section and add a second only for essential dialogue. They may ";
    out <<
        "overlap or leave intentional silence. Choose their lengths and placements "
        "for this work; never restart the song at a technical writing boundary. "
        "Name a speaker and, where musically useful, a distinct answering voice. "
        "speaker_id and response_id MUST be exact instrument IDs from INSTRUMENTS; "
        "response_id is NEVER a phrase name/id. Use an empty string when no "
        "answering instrument is needed. Give the "
        "motif a memorable pitch-and-rhythm clue, a dramatic question, and a "
        "different final consequence. The clue is a seed, NOT the whole melody: "
        "describe how a recognizable thought becomes a complete sentence, is "
        "interrupted or answered, then changes meaning later. A few evenly "
        "spaced cue notes in each scene do not by themselves constitute melodic "
        "development. Plan a traceable foreground line across the work: identify "
        "which exact earlier interval-and-rhythm gesture each later statement "
        "inherits, what changes in its continuation, and which voice actually "
        "utters the consequence. A chord pad, bass root or arpeggio can support "
        "the argument but must not silently replace an intended melodic speaker. "
        "If the main voice rests for a scene, identify which other "
        "voice carries its question in audible notes and what it transforms; "
        "otherwise let the main voice continue its own argument. Silence is "
        "welcome only when its musical consequence is heard elsewhere. "
        "Specify rhythm, contour, register trajectory, "
        "rests and the causal relationship to the previous phrase. Name a "
        "response_id only when that part will actually answer within the phrase "
        "span; an intended silence without an answer uses an empty response_id. "
        "If you promise an answer, describe its relation to the preceding "
        "statement and leave audible space for it; do not label another instrument "
        "as response while assigning it no musical action. "
        "Register bounds "
        "are expressive INTENT for the speaker, not a command to fill every pitch. "
        "An octave leap, a two-octave arpeggio, a narrow repeated figure, a solo, "
        "a long held note or silence are equally valid if the harmony and story "
        "earn them. Do not default to one of these gestures. Do not output MIDI "
        "notes or invent instruments. The note writer remains the sole AI author.\n"
        "USER DIRECTION: " << direction << "\n"
        "WHOLE WORK: " << plan.targetSeconds << " seconds, " << plan.totalBars
        << " bars, " << juce::String::fromUTF8(plan.key.c_str()) << ", "
        << juce::String(plan.bpm, 2) << " BPM. Existing motif identity: "
        << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
        << ". Harmonic debt: "
        << juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str())
        << ". Intended resolution: "
        << juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str()) << "\n"
        "INSTRUMENTS (use exact ids):\n";
    for (const auto& part : plan.instruments)
        out << juce::String::fromUTF8(part.id.c_str()) << " | "
            << juce::String::fromUTF8(part.role.c_str()) << " | preferred MIDI "
            << part.minimumPitch << ".." << part.maximumPitch << "\n";
    out << "ACTIVE SECTION CONTRACT (do not assign a speaker or response "
        "outside its active sections):\n";
    for (const auto& part : plan.instruments) {
        out << juce::String::fromUTF8(part.id.c_str()) << ": ";
        if (part.activeSections.empty()) out << "all";
        else for (const auto& section : part.activeSections)
            out << "[" << juce::String::fromUTF8(section.c_str()) << "] ";
        out << "\n";
    }
    out << "SECTIONS (local_start_bar is relative to its section):\n";
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        out << static_cast<int>(index) << " | "
            << juce::String::fromUTF8(section.name.c_str())
            << " | absolute bars " << section.startBar << ".."
            << section.startBar + section.bars << " | "
            << juce::String::fromUTF8(section.function.c_str()) << " | "
            << juce::String::fromUTF8(section.motifTreatment.c_str()) << " | "
            << juce::String::fromUTF8(section.harmonicDirection.c_str()) << "\n";
        for (const auto& event : section.harmonicEvents)
            out << "  chord local_bar=" << event.barOffset << " id="
                << juce::String::fromUTF8(event.chordId.c_str()) << "\n";
    }
    if (longFormArc)
        out << "LONG-WORK CONTINUITY: An ensemble can answer the main voice, but "
            "do not mistake a relay through a new instrument every section for "
            "development. Let a recognizable speaker or small partnership carry "
            "a musical thought across adjacent scenes, including the middle of "
            "the work. The protagonist may breathe and disappear briefly, yet it "
            "must have a substantive transformed melodic statement during the "
            "central developmental span, before the climax. A hint in an arpeggio, "
            "pad or bass alone does not count as that speaker developing its idea. "
            "Its question should be heard evolving before its late return, not "
            "only at the opening and coda. Use the existing voices for meaningful dialogue, "
            "not as substitutes for a missing central argument. This is an "
            "expressive instruction, not a note count or periodic cadence rule.\n";
    if (juce::SystemStats::getEnvironmentVariable(
            "PULSO_AI_EXPRESSIVE_ENSEMBLE_EXPERIMENT", {}) == "1")
        out << "EXPRESSIVE ENSEMBLE STUDY: Plan the actual development of the "
            "musical thought, not only its identifying cell. In gesture and contour, "
            "describe how the sentence continues beyond its opening figure, which "
            "register it visits and why its arrival follows from that journey. "
            "Where the idea earns a wider span, choose a prepared register crossing, "
            "an extended arpeggiated line or an expressive leap with a connected "
            "consequence; an instrument's available range is not an obligation to "
            "use it all. Avoid keeping every consequential phrase inside the same "
            "small register merely because the motif began there. In relationship, "
            "name what the answering or inner voice develops while the speaker "
            "continues: contrary motion, an overlapping imitation, a suspension "
            "with its destination, or another style-appropriate relationship. "
            "Choose among those possibilities rather than requiring all of them. "
            "An independent bass or inner part can speak instead of always holding "
            "roots beneath a solo. Let its rhythm and direction be its own, while "
            "the voices agree at meaningful arrivals. Preserve a memorable clue "
            "when transforming the continuation; do not lock every later sentence "
            "to the opening's complete rhythm. Distinguish the motif's contour "
            "from the direction of a harmonic resolution; interval and direction "
            "words must agree with the intended musical relationship. "
            "State the audible consequence in "
            "consequence so the note writer can enact it. Reserve the clearest "
            "answer and a deliberate release for the ending appropriate to this "
            "work's tonal or modal language. No MIDI is requested here.\n";
    out << AiMelodicDiscourse::planningBrief(plan.bpm);
    out << AiEnsembleDiscourse::planningBrief();
    out << AiThematicSubject::planningBrief(plan);
    out << AiCompleteDialogue::argumentPlanningBrief();
    out << AiMaterialDialogue::planningBrief();
    out << "Use ZERO-BASED section_index exactly as listed and ZERO-BASED "
        "local_start_bar: the first bar of each section is 0. bars must be at "
        "least 1. A phrase MAY CROSS a section boundary when its continuation "
        "needs to carry into the next scene, but must end before the whole song ends. "
        "Every section must be covered by a phrase intention, including a "
        "phrase continuing from the preceding section. "
        "speaker_id and response_id must be exact instrument IDs, never phrase IDs. "
        "register_low/high must be feasible for its "
        "speaker within one octave of its preferred MIDI range. Keep fields "
        "concrete enough that a later listener can compare the intended "
        "gesture with actual MIDI, but do not prescribe note-by-note results.";
    return out;
}

bool AiPhraseArchitecture::validate(const juce::String& json,
                                    const SongPlan& plan, juce::String& error,
                                    bool enforceActiveSections) {
    const auto value = juce::JSON::parse(json);
    const auto* root = value.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr :
        root->getProperty("phrases").getArray();
    if (root == nullptr) {
        error = "AI phrase architecture response is not a JSON object";
        return false;
    }
    if (phrases == nullptr || phrases->isEmpty() || phrases->size() > 64 ||
        plan.sections.empty()) {
        error = "AI phrase architecture has no valid phrase list";
        return false;
    }
    if (!shortText(root->getProperty("motif_clue")) ||
        !shortText(root->getProperty("dramatic_question")) ||
        !shortText(root->getProperty("answer_principle"))) {
        error = "AI phrase architecture omits its global musical question or answer";
        return false;
    }
    std::set<int> covered;
    for (int phraseNumber = 0; phraseNumber < phrases->size(); ++phraseNumber) {
        const auto& value = (*phrases)[phraseNumber];
        const auto* phrase = value.getDynamicObject();
        int sectionIndex{}, start{}, bars{}, low{}, high{};
        if (phrase == nullptr ||
            !integer(phrase->getProperty("section_index"), sectionIndex) ||
            !integer(phrase->getProperty("local_start_bar"), start) ||
            !integer(phrase->getProperty("bars"), bars) ||
            !integer(phrase->getProperty("register_low"), low) ||
            !integer(phrase->getProperty("register_high"), high) ||
            sectionIndex < 0 || sectionIndex >= static_cast<int>(plan.sections.size()) ||
            start < 0 || bars < 1 ||
            start >= plan.sections[static_cast<std::size_t>(sectionIndex)].bars ||
            plan.sections[static_cast<std::size_t>(sectionIndex)].startBar +
                start + bars > plan.totalBars ||
            low < 0 || high > 127 || low > high) {
            error = "AI phrase " + juce::String(phraseNumber + 1) +
                " has invalid section/span/register: section=" +
                juce::String(sectionIndex) + " local_start_bar=" +
                juce::String(start) + " bars=" + juce::String(bars) +
                " register=" + juce::String(low) + ".." + juce::String(high) +
                " section_count=" + juce::String(static_cast<int>(plan.sections.size())) +
                (sectionIndex >= 0 && sectionIndex < static_cast<int>(plan.sections.size()) ?
                    " section_bars=" + juce::String(
                        plan.sections[static_cast<std::size_t>(sectionIndex)].bars) : "") +
                " total_bars=" + juce::String(plan.totalBars);
            return false;
        }
        const auto speaker = phrase->getProperty("speaker_id").toString();
        const auto response = phrase->getProperty("response_id").toString();
        if (!knownOwner(plan, speaker) ||
            (response.isNotEmpty() && (!knownOwner(plan, response) || response == speaker)) ||
            !shortText(phrase->getProperty("gesture")) ||
            !shortText(phrase->getProperty("rhythm_shape")) ||
            !shortText(phrase->getProperty("contour")) ||
            !shortText(phrase->getProperty("silence_strategy")) ||
            !shortText(phrase->getProperty("relationship")) ||
            !shortText(phrase->getProperty("consequence"))) {
            error = "AI phrase " + juce::String(phraseNumber + 1) +
                " has unknown speaker/response or incomplete intention: speaker=" +
                speaker + " response=" + response;
            return false;
        }
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& part) { return speaker == juce::String::fromUTF8(part.id.c_str()); });
        if (owner == plan.instruments.end()) {
            error = "AI phrase speaker was not in the cast";
            return false;
        }
        const auto absoluteStart =
            plan.sections[static_cast<std::size_t>(sectionIndex)].startBar + start;
        const auto activeWithinPhrase = [&](const juce::String& id) {
            if (id.isEmpty()) return true;
            const auto part = std::find_if(plan.instruments.begin(),
                plan.instruments.end(), [&](const auto& candidate) {
                    return id == juce::String::fromUTF8(candidate.id.c_str());
                });
            if (part == plan.instruments.end()) return false;
            if (part->activeSections.empty()) return true;
            const auto endBar = absoluteStart + bars;
            return std::any_of(plan.sections.begin(), plan.sections.end(),
                [&](const auto& section) {
                    return section.startBar < endBar &&
                        section.startBar + section.bars > absoluteStart &&
                        std::find(part->activeSections.begin(),
                            part->activeSections.end(), section.name) !=
                            part->activeSections.end();
                });
        };
        if (enforceActiveSections &&
            (!activeWithinPhrase(speaker) || !activeWithinPhrase(response))) {
            error = "AI phrase " + juce::String(phraseNumber + 1) +
                " promises a speaker or response outside its active sections: " +
                speaker + " / " + response;
            return false;
        }
        if (low < std::max(0, owner->minimumPitch - 12) ||
            high > std::min(127, owner->maximumPitch + 12)) {
            error = "AI phrase " + juce::String(phraseNumber + 1) +
                " asks for register " + juce::String(low) + ".." +
                juce::String(high) + " outside speaker " + speaker +
                " preferred " + juce::String(owner->minimumPitch) + ".." +
                juce::String(owner->maximumPitch) + " plus one octave";
            return false;
        }
        if (!AiMelodicDiscourse::validate(value, bars * plan.beatsPerBar,
                std::max(0, owner->minimumPitch - 12),
                std::min(127, owner->maximumPitch + 12), error)) {
            error = "AI phrase " + juce::String(phraseNumber + 1) + ": " + error;
            return false;
        }
        if (!AiEnsembleDiscourse::validate(value, bars * plan.beatsPerBar, plan, error)) {
            error = "AI phrase " + juce::String(phraseNumber + 1) + ": " + error;
            return false;
        }
        const auto absoluteEnd = absoluteStart + bars;
        for (std::size_t index = 0; index < plan.sections.size(); ++index) {
            const auto& section = plan.sections[index];
            if (absoluteStart < section.startBar + section.bars &&
                absoluteEnd > section.startBar)
                covered.insert(static_cast<int>(index));
        }
    }
    if (covered.size() != plan.sections.size()) {
        error = "AI phrase architecture omits a section";
        return false;
    }
    if (!AiThematicSubject::validate(value, plan, error)) return false;
    if (!AiCompleteDialogue::validateArgumentOwners(value, plan, error)) return false;
    if (!AiMaterialDialogue::validate(value, plan, error)) return false;
    error.clear();
    return true;
}

juce::String AiPhraseArchitecture::windowBrief(const juce::String& json,
                                                const SongPlan& plan,
                                                std::size_t sectionIndex,
                                                int firstBar, int bars) {
    if (json.isEmpty() || sectionIndex >= plan.sections.size()) return {};
    const auto value = juce::JSON::parse(json);
    const auto* root = value.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr : root->getProperty("phrases").getArray();
    if (phrases == nullptr) return {};
    juce::String out;
    out << AiMaterialDialogue::accompanimentContext(value);
    if (AiMaterialDialogue::expectationLedEnabled()) return out;
    out << "\nAI-AUTHORED PHRASE ARCHITECTURE (intent, not invented notes): "
        << "motif clue=" << root->getProperty("motif_clue").toString()
        << "; question=" << root->getProperty("dramatic_question").toString()
        << "; final answer=" << root->getProperty("answer_principle").toString()
        << ". Realize these relationships in MIDI when they fit the actual harmony. "
        << "Do not add notes merely to satisfy the plan. "
        << "Bar indices below are zero-based and endpoints exclusive. "
        << "Window-local beats use the same coordinates as your MIDI output. "
        << "A declared speaker belongs inside its own phrase span. A respondent "
        << "may have its own subsequent phrase: preserve that handoff instead of "
        << "forcing its answer into the preceding statement. A held note counts "
        << "as sounding; an unfinished phrase may "
        << "continue through later windows.\n";
    for (const auto& value : *phrases) {
        const auto* phrase = value.getDynamicObject();
        if (phrase == nullptr) continue;
        const auto originSection = static_cast<int>(phrase->getProperty("section_index"));
        if (originSection < 0 || originSection >= static_cast<int>(plan.sections.size())) continue;
        const auto start = plan.sections[static_cast<std::size_t>(originSection)].startBar +
            static_cast<int>(phrase->getProperty("local_start_bar"));
        const auto stop = start + static_cast<int>(phrase->getProperty("bars"));
        const auto windowStart = plan.sections[sectionIndex].startBar + firstBar;
        const auto windowEnd = windowStart + bars;
        if (start >= windowEnd || stop <= windowStart) continue;
        out << "absolute bars " << start << ".." << stop
            << " window-local beats " << (start - windowStart) * plan.beatsPerBar
            << ".." << (stop - windowStart) * plan.beatsPerBar
            << " speaker=" << phrase->getProperty("speaker_id").toString()
            << " response=" << phrase->getProperty("response_id").toString()
            << " gesture=" << phrase->getProperty("gesture").toString()
            << " rhythm=" << phrase->getProperty("rhythm_shape").toString()
            << " contour=" << phrase->getProperty("contour").toString()
            << " register=" << phrase->getProperty("register_low").toString()
            << ".." << phrase->getProperty("register_high").toString()
            << " silence=" << phrase->getProperty("silence_strategy").toString()
            << " relationship=" << phrase->getProperty("relationship").toString()
            << " consequence=" << phrase->getProperty("consequence").toString()
            << "\n";
        out << AiMelodicDiscourse::phraseBrief(value, start * plan.beatsPerBar,
            windowStart * plan.beatsPerBar, windowEnd * plan.beatsPerBar, plan.bpm);
        out << AiEnsembleDiscourse::phraseBrief(value, start * plan.beatsPerBar,
            windowStart * plan.beatsPerBar, windowEnd * plan.beatsPerBar);
        if (AiMaterialDialogue::enabled())
            out << "MATERIAL DEVELOPMENT FOR THIS PHRASE: "
                << juce::JSON::toString(value.getProperty("material_development", {}), true) << "\n";
    }
    if (const auto* story = root->getProperty("grounded_story").getDynamicObject())
        out << "SCORE-GROUNDED STORY: This reading of the accepted MIDI takes "
               "precedence over any contradictory pre-MIDI phrase intention. "
               "Opening evidence="
            << story->getProperty("opening_evidence").toString()
            << "; related counteridea="
            << story->getProperty("related_counteridea").toString()
            << "; central conflict="
            << story->getProperty("central_conflict").toString()
            << "; necessary turn="
            << story->getProperty("necessary_turn").toString()
            << "; final consequence="
            << story->getProperty("final_consequence").toString() << "\n";
    if (const auto* links = root->getProperty("act_links").getArray()) {
        out << "LONG-FORM AI ARC: These are consequences between scenes, not note quotas. "
            << "The designated carrier can be any existing voice; let the other voices "
            << "answer or support it rather than restarting the motif at this window.\n";
        for (int offset = -1; offset <= 1; ++offset) {
            const auto index = static_cast<int>(sectionIndex) + offset;
            if (index < 0 || index >= links->size()) continue;
            const auto* link = (*links)[index].getDynamicObject();
            if (link == nullptr) continue;
            out << (offset < 0 ? "previous scene " : offset > 0 ? "next scene " : "current scene ")
                << index << " carrier=" << link->getProperty("carrier_id").toString()
                << " inherited=" << link->getProperty("inherited_question").toString()
                << " transformation=" << link->getProperty("audible_transformation").toString()
                << " consequence=" << link->getProperty("consequence").toString()
                << " left_open=" << link->getProperty("left_open").toString();
            if (link->hasProperty("score_evidence"))
                out << " score_evidence=" << link->getProperty("score_evidence").toString();
            out << "\n";
        }
    }
    return out;
}

void AiPhraseArchitecture::applyTerminalHandoff(const juce::String& json,
    SongPlan& plan) {
    plan.narrativeSpine.terminalSpeakerInstrumentId.clear();
    plan.narrativeSpine.terminalPhraseStartBar = -1;
    if (json.isEmpty() || plan.sections.empty()) return;
    const auto parsed = juce::JSON::parse(json);
    const auto* root = parsed.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr :
        root->getProperty("phrases").getArray();
    if (phrases == nullptr) return;
    const auto sectionIndex = static_cast<int>(plan.sections.size() - 1);
    const auto& lastSection = plan.sections.back();
    for (const auto& value : *phrases) {
        const auto* phrase = value.getDynamicObject();
        if (phrase == nullptr ||
            static_cast<int>(phrase->getProperty("section_index")) != sectionIndex)
            continue;
        const auto start = static_cast<int>(phrase->getProperty("local_start_bar"));
        const auto id = phrase->getProperty("speaker_id").toString().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(),
            plan.instruments.end(),
            [&](const auto& instrument) { return instrument.id == id; });
        // A pad, bass or FX layer can support the last chord but cannot stand
        // in for a promised melodic answer.
        if (start < 0 || start >= lastSection.bars ||
            owner == plan.instruments.end() ||
            voiceDefinition(owner->sourceVoice).family != VoiceFamily::Melodic)
            continue;
        const auto absolute = lastSection.startBar + start;
        if (absolute >= plan.narrativeSpine.terminalPhraseStartBar) {
            plan.narrativeSpine.terminalPhraseStartBar = absolute;
            plan.narrativeSpine.terminalSpeakerInstrumentId = id;
        }
    }
}

juce::String AiPhraseArchitecture::realizationBrief(const juce::String& json,
    const SongPlan& plan, const Pattern& midi) {
    if (json.isEmpty() || plan.sections.empty()) return {};
    const auto parsed = juce::JSON::parse(json);
    const auto* root = parsed.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr :
        root->getProperty("phrases").getArray();
    if (phrases == nullptr) return {};
    const auto ownerPart = [&](const juce::String& id) {
        for (std::size_t index = 0; index < plan.instruments.size(); ++index)
            if (id == juce::String::fromUTF8(plan.instruments[index].id.c_str()))
                return static_cast<std::uint16_t>(index + 1);
        return std::uint16_t{};
    };
    juce::String out;
    out << "\nPHRASE INTENTION VERSUS ACTUAL RENDERED MIDI. A planned rest "
        "can be expressive; decide from audible cause and response, not counts "
        "alone. A named answering voice that never enters is a discrepancy "
        "unless the AI deliberately revises its stated intention.\n";
    for (int index = 0; index < phrases->size(); ++index) {
        const auto* phrase = (*phrases)[index].getDynamicObject();
        if (phrase == nullptr) continue;
        const auto sectionIndex = static_cast<int>(
            phrase->getProperty("section_index"));
        if (sectionIndex < 0 || sectionIndex >=
                static_cast<int>(plan.sections.size())) continue;
        const auto firstBar = plan.sections[sectionIndex].startBar +
            static_cast<int>(phrase->getProperty("local_start_bar"));
        const auto lastBar = firstBar +
            static_cast<int>(phrase->getProperty("bars"));
        const auto start = firstBar * plan.beatsPerBar;
        const auto end = lastBar * plan.beatsPerBar;
        const auto speaker = phrase->getProperty("speaker_id").toString();
        const auto response = phrase->getProperty("response_id").toString();
        const auto sketch = [&](const juce::String& id) {
            const auto part = ownerPart(id);
            auto count = 0;
            juce::String notes;
            for (const auto& note : midi.notes) {
                if (note.partId != part || note.startBeat < start - .001 ||
                    note.startBeat >= end - .001) continue;
                if (count < 12)
                    notes << juce::String(note.startBeat, 2) << ":" << note.pitch
                        << "/" << juce::String(note.durationBeats, 2) << " ";
                ++count;
            }
            return juce::String(count) + " attacks [" + notes.trimEnd() +
                (count > 12 ? " ...]" : "]");
        };
        out << "phrase " << index << " bars " << firstBar << ".." << lastBar
            << " speaker=" << speaker << " " << sketch(speaker);
        if (response.isNotEmpty())
            out << " | promised answer=" << response << " " << sketch(response);
        out << " | intended consequence=" <<
            phrase->getProperty("consequence").toString() << "\n";
    }
    return out;
}

PhraseRealizationCounts AiPhraseArchitecture::measureRealization(
    const juce::String& json, const SongPlan& plan, const Pattern& midi) {
    PhraseRealizationCounts counts;
    if (json.isEmpty()) return counts;
    const auto parsed = juce::JSON::parse(json);
    const auto* root = parsed.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr :
        root->getProperty("phrases").getArray();
    if (phrases == nullptr) return counts;
    const auto audible = [&](const juce::String& id, double begin, double end) {
        const auto owner = std::find_if(plan.instruments.begin(),
            plan.instruments.end(), [&](const auto& part) {
                return id == juce::String::fromUTF8(part.id.c_str());
            });
        if (owner == plan.instruments.end()) return false;
        const auto partId = static_cast<std::uint16_t>(
            std::distance(plan.instruments.begin(), owner) + 1);
        return std::any_of(midi.notes.begin(), midi.notes.end(),
            [&](const auto& note) {
                return note.partId == partId && note.startBeat >= begin - .001 &&
                    note.startBeat < end - .001;
            });
    };
    for (const auto& value : *phrases) {
        const auto* phrase = value.getDynamicObject();
        if (phrase == nullptr) continue;
        const auto sectionIndex = static_cast<int>(
            phrase->getProperty("section_index"));
        if (sectionIndex < 0 || sectionIndex >=
            static_cast<int>(plan.sections.size())) continue;
        const auto firstBar = plan.sections[sectionIndex].startBar +
            static_cast<int>(phrase->getProperty("local_start_bar"));
        const auto lastBar = firstBar +
            static_cast<int>(phrase->getProperty("bars"));
        const auto begin = firstBar * plan.beatsPerBar;
        const auto end = lastBar * plan.beatsPerBar;
        if (!audible(phrase->getProperty("speaker_id").toString(), begin, end))
            ++counts.silentSpeakers;
        const auto response = phrase->getProperty("response_id").toString();
        if (response.isNotEmpty() && !audible(response, begin, end))
            ++counts.missingResponses;
    }
    return counts;
}

std::vector<std::string> AiPhraseArchitecture::missingCompletedPhraseOwners(
    const juce::String& json, const SongPlan& plan, const Pattern& midi,
    double windowStart, double windowEnd, juce::String* evidence) {
    std::vector<std::string> missing;
    if (evidence != nullptr) evidence->clear();
    const auto parsed = juce::JSON::parse(json);
    const auto* root = parsed.getDynamicObject();
    const auto* phrases = root == nullptr ? nullptr :
        root->getProperty("phrases").getArray();
    if (phrases == nullptr) return missing;
    for (const auto& value : *phrases) {
        const auto* phrase = value.getDynamicObject();
        if (phrase == nullptr) continue;
        const auto section = static_cast<int>(phrase->getProperty("section_index"));
        if (section < 0 || section >= static_cast<int>(plan.sections.size())) continue;
        const auto begin = (plan.sections[section].startBar +
            static_cast<int>(phrase->getProperty("local_start_bar"))) * plan.beatsPerBar;
        const auto end = begin + static_cast<int>(phrase->getProperty("bars")) * plan.beatsPerBar;
        if (end <= windowStart + .001 || end > windowEnd + .001) continue;
        // response_id names a relationship, not a separately timed obligation.
        // Its reply may be the next declared speaker phrase. Enforcing it in
        // this span would destroy legitimate call-and-response and add attacks.
        {
            const auto id = phrase->getProperty("speaker_id").toString().toStdString();
            if (id.empty()) continue;
            const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
                [&](const auto& part) { return part.id == id; });
            if (owner == plan.instruments.end()) continue;
            const auto partId = static_cast<std::uint16_t>(
                std::distance(plan.instruments.begin(), owner) + 1);
            const auto heard = std::any_of(midi.notes.begin(), midi.notes.end(),
                [&](const auto& note) {
                    return note.partId == partId && note.startBeat < end - .001 &&
                        note.endBeat() > begin + .001;
                });
            if (!heard) {
                if (std::find(missing.begin(), missing.end(), id) == missing.end())
                    missing.push_back(id);
                if (evidence != nullptr)
                    *evidence << " [" << juce::String::fromUTF8(id.c_str())
                        << " has no sounding note in its declared phrase, window-local beats "
                        << juce::String(begin - windowStart, 2) << ".."
                        << juce::String(end - windowStart, 2)
                        << "; notes outside this span cannot answer that phrase]";
            }
        }
    }
    return missing;
}

juce::String AiPhraseArchitecture::longFormArcSchema() {
    return R"json({"type":"object","additionalProperties":false,"required":["act_links"],
      "properties":{"act_links":{"type":"array","maxItems":64,"items":{
        "type":"object","additionalProperties":false,
        "required":["section_index","carrier_id","inherited_question",
          "audible_transformation","consequence","left_open"],
        "properties":{"section_index":{"type":"integer"},"carrier_id":{"type":"string"},
          "inherited_question":{"type":"string"},"audible_transformation":{"type":"string"},
          "consequence":{"type":"string"},"left_open":{"type":"string"}}}}}})json";
}

juce::String AiPhraseArchitecture::longFormArcPrompt(const SongPlan& plan,
    const juce::String& direction, const juce::String& phraseJson) {
    juce::String out;
    out << "You are editing the causal musical story of one long MIDI composition. "
        "The fixed form, harmony, cast and phrase intentions are below. Write exactly "
        "one act_link for each section in order. Each link must explain what audible "
        "pitch-rhythm identity is inherited, how it changes in an existing carrier "
        "voice, what it accomplishes and what question remains for the next section. "
        "Preserve a recognizable musical point of view across several adjacent "
        "scenes. A carrier may be lead, bass, arpeggio or inner voice, but changing "
        "carrier merely because the scene changed fractures the identity. Reuse "
        "the same carrier when its phrase can actually develop; hand off only when "
        "a specific earlier gesture makes the new voice's answer necessary, and "
        "bring that identity back later. Silence from the lead can be expressive, "
        "but the main question must remain perceptible in the ensemble. Carrier "
        "IDs must exactly match the instrument list. Do not fill rests. "
        "Do not restart a new theme at technical windows; the whole work should "
        "have one evolving question and an earned final answer. Specific gestures "
        "and silence matter more than adjectives. You are NOT writing notes, adding "
        "instruments, changing chords, or prescribing a fixed rhythm.\nUSER: "
        << direction << "\nMOTIF: "
        << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
        << "\nDEBT: " << juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str())
        << "\nRESOLUTION: " << juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str())
        << "\nINSTRUMENT IDS:\n";
    for (const auto& instrument : plan.instruments)
        out << juce::String::fromUTF8(instrument.id.c_str()) << " | "
            << juce::String::fromUTF8(instrument.role.c_str()) << "\n";
    out << "SECTIONS:\n";
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        out << index << " | " << juce::String::fromUTF8(section.name.c_str())
            << " | bars " << section.startBar << ".." << section.startBar + section.bars
            << " | " << juce::String::fromUTF8(section.function.c_str())
            << " | " << juce::String::fromUTF8(section.motifTreatment.c_str()) << "\n";
    }
    out << "EXISTING AI PHRASE INTENTIONS:\n" << phraseJson;
    return out;
}

bool AiPhraseArchitecture::attachLongFormArc(juce::String& phraseJson,
    const juce::String& arcJson, const SongPlan& plan, juce::String& error) {
    auto phrase = juce::JSON::parse(phraseJson);
    const auto arcValue = juce::JSON::parse(arcJson);
    const auto* arc = arcValue.getDynamicObject();
    auto* root = phrase.getDynamicObject();
    const auto* links = arc == nullptr ? nullptr : arc->getProperty("act_links").getArray();
    if (root == nullptr || links == nullptr ||
        links->size() != static_cast<int>(plan.sections.size())) {
        error = "Long-form arc must contain exactly one link per section";
        return false;
    }
    for (int index = 0; index < links->size(); ++index) {
        const auto* link = (*links)[index].getDynamicObject();
        int sectionIndex{};
        if (link == nullptr ||
            !integer(link->getProperty("section_index"), sectionIndex) ||
            sectionIndex != index ||
            !knownOwner(plan, link->getProperty("carrier_id").toString()) ||
            !shortText(link->getProperty("inherited_question"), 500) ||
            !shortText(link->getProperty("audible_transformation"), 500) ||
            !shortText(link->getProperty("consequence"), 500) ||
            !shortText(link->getProperty("left_open"), 500)) {
            error = "Long-form arc link " + juce::String(index) +
                " has invalid section, carrier or musical intention";
            return false;
        }
    }
    root->setProperty("act_links", arc->getProperty("act_links"));
    phraseJson = juce::JSON::toString(phrase);
    error.clear();
    return true;
}

juce::String AiPhraseArchitecture::groundedStorySchema() {
    return R"json({"type":"object","additionalProperties":false,
      "required":["grounded_story","act_links"],"properties":{
        "grounded_story":{"type":"object","additionalProperties":false,
          "required":["opening_evidence","related_counteridea","central_conflict",
            "necessary_turn","final_consequence"],"properties":{
            "opening_evidence":{"type":"string"},
            "related_counteridea":{"type":"string"},
            "central_conflict":{"type":"string"},
            "necessary_turn":{"type":"string"},
            "final_consequence":{"type":"string"}}},
        "act_links":{"type":"array","maxItems":64,"items":{
          "type":"object","additionalProperties":false,
          "required":["section_index","carrier_id","inherited_question",
            "audible_transformation","consequence","left_open","score_evidence"],
          "properties":{"section_index":{"type":"integer"},
            "carrier_id":{"type":"string"},"inherited_question":{"type":"string"},
            "audible_transformation":{"type":"string"},
            "consequence":{"type":"string"},"left_open":{"type":"string"},
            "score_evidence":{"type":"string"}}}}}})json";
}

juce::String AiPhraseArchitecture::groundedStoryPrompt(const SongPlan& plan,
    const juce::String& direction, const juce::String& phraseJson,
    const Pattern& openingMidi) {
    juce::String out;
    out << "You are the dramatic editor of an ORIGINAL complete MIDI work. "
        "The first technical window has ALREADY been composed; its notes are immutable. "
        "Ground the rest of the story in those ACTUAL notes, not in the plan's "
        "claims. The form, chord events and cast are fixed, but every later note "
        "will be authored by the AI writer. A long work cannot be sustained merely "
        "by withholding the fourth note of a four-note cell and finally adding it. "
        "Find the opening's strongest recognisable pitch-rhythm identity and a "
        "RELATED but distinct counteridea or answer that later voices can develop. "
        "The second idea must be caused by something actually heard in the first; "
        "do not invent an unrelated new theme. Make at least one middle scene "
        "change the meaning of both ideas through melodic dialogue, bass relation "
        "or voicing, then make the ending a consequence of that encounter. Stable "
        "ostinati and long harmony are welcome when their function evolves. Do not "
        "equate more notes, new instruments, louder dynamics or a higher register "
        "with a dramatic event. The result must be perceptible on neutral piano. "
        "For each scene, describe a specific audible score-level action and what "
        "it inherits, changes and leaves open. Avoid repeating the same instruction "
        "with different adjectives. Do not return MIDI notes or alter the opening.\n"
        "USER: " << direction << "\nKEY: "
        << juce::String::fromUTF8(plan.key.c_str())
        << "\nORIGINAL DECLARED MOTIF: "
        << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
        << "\nORIGINAL HARMONIC DEBT: "
        << juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str())
        << "\nOPENING MIDI (absolute beat, instrument ID, pitch, duration):\n";
    for (std::size_t owner = 0; owner < plan.instruments.size(); ++owner) {
        const auto& instrument = plan.instruments[owner];
        auto count = 0;
        out << juce::String::fromUTF8(instrument.id.c_str()) << " ["
            << juce::String::fromUTF8(instrument.role.c_str()) << "]: ";
        for (const auto& note : openingMidi.notes) {
            if (note.partId != owner + 1 || count >= 24) continue;
            out << juce::String(note.startBeat, 2) << ","
                << note.pitch << "," << juce::String(note.durationBeats, 2)
                << "; ";
            ++count;
        }
        if (count == 0) out << "rest";
        out << "\n";
    }
    out << "FIXED SECTIONS AND HARMONIC EVENTS:\n";
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        out << index << " " << juce::String::fromUTF8(section.name.c_str())
            << " bars " << section.startBar << ".."
            << section.startBar + section.bars << " function="
            << juce::String::fromUTF8(section.function.c_str())
            << " chords=";
        for (const auto& event : section.harmonicEvents)
            out << " [bar+" << event.barOffset << " "
                << juce::String::fromUTF8(event.chordId.c_str()) << "]";
        out << "\n";
    }
    out << "INSTRUMENT IDS AND ROLES:\n";
    for (const auto& instrument : plan.instruments)
        out << juce::String::fromUTF8(instrument.id.c_str()) << " | "
            << juce::String::fromUTF8(instrument.role.c_str()) << "\n";
    out << "PRE-EXISTING PHRASE PLAN (intentions, not proof of audible results):\n"
        << phraseJson;
    return out;
}

bool AiPhraseArchitecture::attachGroundedStory(juce::String& phraseJson,
    const juce::String& storyJson, const SongPlan& plan, juce::String& error) {
    auto phrase = juce::JSON::parse(phraseJson);
    const auto storyValue = juce::JSON::parse(storyJson);
    auto* root = phrase.getDynamicObject();
    const auto* story = storyValue.getDynamicObject();
    const auto* core = story == nullptr ? nullptr :
        story->getProperty("grounded_story").getDynamicObject();
    const auto* links = story == nullptr ? nullptr :
        story->getProperty("act_links").getArray();
    if (root == nullptr || core == nullptr || links == nullptr ||
        links->size() != static_cast<int>(plan.sections.size())) {
        error = "Grounded story must cover every scene";
        return false;
    }
    for (const auto& name : {"opening_evidence", "related_counteridea",
             "central_conflict", "necessary_turn", "final_consequence"})
        if (!shortText(core->getProperty(name), 1000)) {
            error = "Grounded story has an invalid musical premise: " +
                juce::String(name);
            return false;
        }
    for (int index = 0; index < links->size(); ++index) {
        const auto* link = (*links)[index].getDynamicObject();
        int sectionIndex{};
        if (link == nullptr ||
            !integer(link->getProperty("section_index"), sectionIndex) ||
            sectionIndex != index ||
            !knownOwner(plan, link->getProperty("carrier_id").toString()) ||
            !shortText(link->getProperty("inherited_question"), 700) ||
            !shortText(link->getProperty("audible_transformation"), 700) ||
            !shortText(link->getProperty("consequence"), 700) ||
            !shortText(link->getProperty("left_open"), 700) ||
            !shortText(link->getProperty("score_evidence"), 700)) {
            error = "Grounded story scene " + juce::String(index) +
                " has an invalid score-level link";
            return false;
        }
    }
    root->setProperty("grounded_story", story->getProperty("grounded_story"));
    root->setProperty("act_links", story->getProperty("act_links"));
    phraseJson = juce::JSON::toString(phrase);
    error.clear();
    return true;
}

} // namespace pulso::plugin
