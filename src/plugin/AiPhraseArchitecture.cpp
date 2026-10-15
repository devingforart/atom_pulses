#include "AiPhraseArchitecture.h"

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
    return R"json({"type":"object","additionalProperties":false,
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
      }})json";
}

juce::String AiPhraseArchitecture::prompt(const SongPlan& plan,
                                          const juce::String& direction) {
    juce::String out;
    out << "You are the phrase architect for a complete symbolic MIDI work. "
        "The form, harmony and instrument cast below are fixed. Plan audible phrases "
        "and relationships BEFORE any notes are written. This is a creative score "
        "intention, not a quota, algorithm or instruction to fill every bar. "
        "Return one or two significant phrase intentions per section. For "
        "long works with more than twelve sections, favor one intention per "
        "section and add a second only for essential dialogue. They may "
        "overlap or leave intentional silence. Choose their lengths and placements "
        "for this work; never restart the song at a technical writing boundary. "
        "Name a speaker and, where musically useful, a distinct answering voice. "
        "speaker_id and response_id MUST be exact instrument IDs from INSTRUMENTS; "
        "response_id is NEVER a phrase name/id. Use an empty string when no "
        "answering instrument is needed. Give the "
        "motif a memorable pitch-and-rhythm clue, a dramatic question, and a "
        "different final consequence. Specify rhythm, contour, register trajectory, "
        "rests and the causal relationship to the previous phrase. Register bounds "
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
                                    const SongPlan& plan, juce::String& error) {
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
        if (low < std::max(0, owner->minimumPitch - 12) ||
            high > std::min(127, owner->maximumPitch + 12)) {
            error = "AI phrase " + juce::String(phraseNumber + 1) +
                " asks for register " + juce::String(low) + ".." +
                juce::String(high) + " outside speaker " + speaker +
                " preferred " + juce::String(owner->minimumPitch) + ".." +
                juce::String(owner->maximumPitch) + " plus one octave";
            return false;
        }
        const auto absoluteStart =
            plan.sections[static_cast<std::size_t>(sectionIndex)].startBar + start;
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
    out << "\nAI-AUTHORED PHRASE ARCHITECTURE (intent, not invented notes): "
        << "motif clue=" << root->getProperty("motif_clue").toString()
        << "; question=" << root->getProperty("dramatic_question").toString()
        << "; final answer=" << root->getProperty("answer_principle").toString()
        << ". Realize these relationships in MIDI when they fit the actual harmony. "
        << "Do not add notes merely to satisfy the plan.\n";
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
    }
    return out;
}

} // namespace pulso::plugin
