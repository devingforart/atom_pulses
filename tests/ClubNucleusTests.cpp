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

int main() {
    using namespace pulso;
    using namespace pulso::plugin;
    try {
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
