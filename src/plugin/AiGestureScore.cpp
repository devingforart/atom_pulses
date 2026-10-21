#include "AiGestureScore.h"
#include "AiMaterialDialogue.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pulso::plugin {
bool AiGestureScore::enabled() {
    return AiMaterialDialogue::expectationLedEnabled() &&
        juce::SystemStats::getEnvironmentVariable("PULSO_AI_GESTURE_SCORE_EXPERIMENT", {}) == "1";
}
juce::String AiGestureScore::schema(const SongPlan& plan, const std::vector<std::size_t>& owners) {
    const auto old = juce::JSON::parse(AiCompleteDialogue::schema(plan, owners));
    const auto noteArrays = old.getProperty("properties", {}).getProperty("windows", {})
        .getProperty("items", {}).getProperty("properties", {}).getProperty("parts", {})
        .getProperty("properties", {});
    auto root = juce::JSON::parse(R"json({"type":"object","additionalProperties":false,
      "required":["parts"],"properties":{"parts":{"type":"object","additionalProperties":false,
      "required":[],"properties":{}}}})json");
    auto parts = root.getProperty("properties", {}).getProperty("parts", {});
    for (const auto owner : owners) {
        const auto id = juce::String::fromUTF8(plan.instruments[owner].id.c_str());
        auto gestures = juce::JSON::parse(R"json({"type":"array","maxItems":128,"items":{
          "type":"object","additionalProperties":false,
          "required":["start_beat","character","relationship","notes"],"properties":{
          "start_beat":{"type":"number","minimum":0,"multipleOf":0.25},
          "character":{"type":"string","description":"Concise audible character and role of this performed gesture."},
          "relationship":{"type":"string","description":"What actual preceding or simultaneous gesture this one receives, supports or changes."},
          "notes":{"type":"array","maxItems":768,"items":{}}}}})json");
        gestures.getProperty("items", {}).getProperty("properties", {}).getProperty("notes", {})
            .getDynamicObject()->setProperty("items", noteArrays.getProperty(juce::Identifier(id), {})
                .getProperty("items", {}));
        parts.getProperty("required", {}).getArray()->add(id);
        parts.getProperty("properties", {}).getDynamicObject()->setProperty(juce::Identifier(id), gestures);
    }
    return juce::JSON::toString(root, true);
}
juce::String AiGestureScore::prompt(const juce::String& direction, const SongPlan& plan,
    const std::vector<std::size_t>& owners, const std::vector<DialogueWindow>& windows,
    const Pattern& accepted) {
    return "PERFORMED GESTURE SCORE: Compose the entire selected ensemble as musical utterances, "
        "not bar-sized lists of permitted pitches. For each exact owner, parts contains a sequence "
        "of ACTUALLY PERFORMED gestures. Choose their lengths and boundaries from the music, not "
        "from the harmonic section boundaries. A gesture can cross several sections. It can be a "
        "complete singing phrase, its answering continuation, a harmonic body, a propulsive figure "
        "or a purposeful ornament; do not require every category. Keep character and relationship "
        "brief, and make their claim audible in the actual notes.\n"
        "Give the listener a hierarchy of weight and motion: which tones bear a sentence, which "
        "connect or decorate them, and what the other voices contribute at that moment. A held or "
        "broad line can carry the foreground while another moves; a connected flourish or arpeggio "
        "can gather energy into a consequential landing. These are possibilities, not patterns to "
        "insert. Do not turn every melody into equal-weight quarter-note steps or make all voices "
        "explain every beat together. Nor should every voice play fast ornaments to demonstrate "
        "variety. Do not mistake uniformly lengthening notes for expressive development either. "
        "Let a line have its own agogic shape within the shared tempo: a weighted tone, a connected "
        "continuation that gathers motion, and a meaningful landing rather than equal syllables. "
        "An instrumental run or arpeggio should be a shaped part of that utterance, not a pasted "
        "decoration between unrelated statements. Stable pulse, repeated figures, counterpoint and virtuosity remain available. "
        "Choose the actual rhythmic proportions and articulation of each thought.\n"
        "A change in surface should grow from the preceding thought and change its continuation. "
        "Let an arrival have weight without restarting the work at every cadence. Recall enough "
        "identity to make the ending conclude the lived opening. Judge all voices together on one "
        "piano; timbre alone cannot supply hierarchy. The real notes, not descriptive gesture names, "
        "must establish these relationships.\n"
        "OUTPUT: parts maps every requested exact ID to its gesture array (or [] for intentional "
        "rest). start_beat is ABSOLUTE from the work's beginning. Each note's beat is LOCAL to that "
        "gesture: absolute attack = start_beat + beat. duration is the actual sounding duration, "
        "not a gate ratio. All times are on the quarter-beat grid, velocities 1..127. Every attack "
        "must be before the work's end, every release at or before it. Explicitly write every note "
        "once: no repetition counts, motif instances, implicit patterns or generated filler. "
        "Gestures can overlap in genuine polyphony, but do not retrigger the same held pitch by "
        "accident. No code adds ornaments, repeats or transforms gestures.\n" +
        AiCompleteDialogue::prompt(direction, plan, owners, windows, accepted, false, true);
}
bool AiGestureScore::parse(const juce::String& json, const SongPlan& plan,
    const std::vector<std::size_t>& owners, const std::vector<DialogueWindow>& windows,
    PerformanceScore& score, juce::String& error) {
    score = {};
    const auto root = juce::JSON::parse(json);
    const auto parts = root.getProperty("parts", {});
    const auto fail = [&](const char* text) { error = text; return false; };
    if (!parts.isObject() || parts.getDynamicObject()->getProperties().size() != static_cast<int>(owners.size()))
        return fail("Gesture score has missing or unknown owners");
    juce::Array<juce::var> rows;
    for (const auto& window : windows) {
        auto* row = new juce::DynamicObject();
        row->setProperty("section_index", static_cast<int>(window.section));
        row->setProperty("first_bar", window.firstBar);
        auto* arrays = new juce::DynamicObject();
        for (const auto owner : owners)
            arrays->setProperty(juce::Identifier(juce::String::fromUTF8(plan.instruments[owner].id.c_str())), juce::Array<juce::var>{});
        row->setProperty("parts", juce::var(arrays));
        rows.add(juce::var(row));
    }
    const auto gridNumber = [](const juce::var& value, double& out) {
        if (!value.isInt() && !value.isInt64() && !value.isDouble()) return false;
        out = static_cast<double>(value);
        return std::isfinite(out) && out >= 0 && std::abs(out * 4 - std::round(out * 4)) < 1e-8;
    };
    for (const auto owner : owners) {
        const auto id = juce::Identifier(juce::String::fromUTF8(plan.instruments[owner].id.c_str()));
        std::set<std::pair<double, int>> attacks;
        const auto* gestures = parts.getProperty(id, {}).getArray();
        if (!gestures || gestures->size() > 128) return fail("Gesture score needs bounded explicit gesture arrays");
        for (const auto& gesture : *gestures) {
            double start{};
            if (!gridNumber(gesture.getProperty("start_beat", {}), start))
                return fail("Gesture start is not a valid absolute grid coordinate");
            for (const auto* field : {"character", "relationship"}) {
                const auto value = gesture.getProperty(field, {});
                if (!value.isString() || value.toString().trim().isEmpty() || value.toString().length() > 2400)
                    return fail("Gesture description is missing or unbounded");
            }
            const auto* notes = gesture.getProperty("notes", {}).getArray();
            if (!notes || notes->size() > 768) return fail("Gesture notes are missing or unbounded");
            for (const auto& note : *notes) {
                double offset{}, duration{};
                if (!note.isObject() || !gridNumber(note.getProperty("beat", {}), offset) ||
                    !gridNumber(note.getProperty("duration", {}), duration) || duration <= 0)
                    return fail("Gesture has invalid note timing; no notes were repaired or clipped");
                for (const auto* field : {"pitch", "velocity"}) {
                    const auto value = note.getProperty(field, {});
                    if ((!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                        !std::isfinite(static_cast<double>(value)) ||
                        static_cast<double>(value) != std::round(static_cast<double>(value)))
                        return fail("Gesture pitch and velocity must be explicit integers, never rounded");
                }
                const auto beat = start + offset;
                if (!attacks.emplace(beat, static_cast<int>(note.getProperty("pitch", {}))).second) {
                    error = "Gesture score repeats the same owner's pitch at the same attack: owner=" +
                        id.toString() + " absolute_beat=" + juce::String(beat, 2) +
                        " pitch=" + note.getProperty("pitch", {}).toString() +
                        ". Author one explicit attack with its intended duration/velocity; no duplicate will be merged.";
                    return false;
                }
                if (beat + duration > plan.totalBars * plan.beatsPerBar + 1e-8)
                {
                    error = "Gesture note releases after the work ends: owner=" + id.toString() +
                        " start=" + juce::String(beat, 2) + " duration=" + juce::String(duration, 2) +
                        " work_end=" + juce::String(plan.totalBars * plan.beatsPerBar, 2) +
                        ". Correct the explicit note; no clipping is applied.";
                    return false;
                }
                int found = -1;
                for (std::size_t i = 0; i < windows.size(); ++i) {
                    const auto& window = windows[i];
                    const auto begin = (plan.sections[window.section].startBar + window.firstBar) * plan.beatsPerBar;
                    if (beat >= begin && beat < begin + window.bars * plan.beatsPerBar) {
                        if (found != -1) return fail("Ambiguous gesture coordinate containers");
                        found = static_cast<int>(i);
                    }
                }
                if (found < 0) return fail("Gesture attack is outside the requested score span");
                auto local = juce::JSON::parse(juce::JSON::toString(note));
                const auto& window = windows[static_cast<std::size_t>(found)];
                local.getDynamicObject()->setProperty("beat", beat -
                    (plan.sections[window.section].startBar + window.firstBar) * plan.beatsPerBar);
                rows.getReference(found).getProperty("parts", {}).getProperty(id, {}).getArray()->add(local);
            }
        }
    }
    juce::var translated(new juce::DynamicObject());
    translated.getDynamicObject()->setProperty("windows", rows);
    return AiCompleteDialogue::parse(juce::JSON::toString(translated), plan, owners, windows, score, error);
}
juce::String AiGestureScore::reviewEvidence(const SongPlan& plan, const Pattern& actual,
    const std::vector<std::size_t>& owners) {
    if (!enabled()) return {};
    juce::String out = "\nREAD-ONLY MOVEMENT OBSERVATIONS: These describe attacks, not quality scores. "
        "A steady pulse or a uniform distribution can be intentional. Do not target ratios or "
        "manufacture fast notes. Inspect whether the actual phrasing has weight, connection and "
        "hierarchy, or whether its gestures merely describe an undifferentiated stream.\n";
    for (const auto owner : owners) {
        out << juce::String::fromUTF8(plan.instruments[owner].id.c_str()) << ":\n";
        for (const auto& section : plan.sections) {
            std::vector<double> attacks;
            for (const auto& note : actual.notes)
                if (note.partId == owner + 1 && note.startBeat >= section.startBar * plan.beatsPerBar &&
                    note.startBeat < (section.startBar + section.bars) * plan.beatsPerBar)
                    attacks.push_back(note.startBeat);
            std::sort(attacks.begin(), attacks.end());
            attacks.erase(std::unique(attacks.begin(), attacks.end()), attacks.end());
            std::map<double, int> gaps;
            for (std::size_t i = 1; i < attacks.size(); ++i) ++gaps[attacks[i] - attacks[i - 1]];
            out << "  absolute beats " << section.startBar * plan.beatsPerBar << ".."
                << (section.startBar + section.bars) * plan.beatsPerBar << " onset gaps (beats:count)=";
            for (const auto& [gap, count] : gaps) out << juce::String(gap, 2) << ":" << count << " ";
            out << "\n";
        }
    }
    return out;
}
}
