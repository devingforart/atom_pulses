#include "AiComposer.h"

#include "core/Scale.h"
#include "core/CoherentProofGate.h"
#include "core/CoherentProofRevision.h"
#include "core/EditorialSafety.h"
#include "core/ElectronicRoleContract.h"
#include "core/EnsembleReference.h"
#include "core/OrchestrationScore.h"
#include "core/SelectiveRepair.h"
#include "core/TonalContract.h"
#include "core/TrackViability.h"
#include "AiModelConfig.h"
#include "ApiCredentialStore.h"
#include "OperationalJournal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <future>
#include <map>
#include <limits>
#include <mutex>
#include <numeric>
#include <regex>
#include <set>
#include <thread>
#include <tuple>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <winhttp.h>
#endif

namespace pulso::plugin {
namespace {

constexpr auto model = ai_config::model;
constexpr auto reasoningEffort = ai_config::reasoningEffort;
constexpr std::size_t maximumInstruments = 64;
constexpr std::size_t instrumentsPerPerformanceBlock = 10;
constexpr std::size_t instrumentsPerCastShard = 10;
constexpr std::size_t maximumConcurrentCastShards = 3;
constexpr std::size_t instrumentsPerRepairShard = 2;
constexpr std::size_t instrumentsPerEditorialRepairShard = 1;
constexpr std::size_t maximumConcurrentRepairShards = 3;
constexpr std::array layerNames{"harmony", "melody", "bass", "drums"};
constexpr std::array layerChannels{3, 2, 1, 10};
constexpr std::array layerVoices{VoiceId::HarmonicFoundation, VoiceId::Lead,
                                 VoiceId::SubBass, VoiceId::CoreDrums};

bool localEditorialModeEnabled() {
    return juce::SystemStats::getEnvironmentVariable("PULSO_LOCAL_EDITORIAL_V2", {}) == "1";
}

std::size_t requestedInstrumentCountFromDirection(const juce::String& direction) noexcept {
    // The local coherence proof is an explicit three-owner contract. Enforce it
    // before requesting the cast, rather than rejecting an unconstrained cast
    // only after the paid manifest/detail calls have completed.
    if (direction.contains("LOCAL MUSICAL COHERENCE PROOF")) return 3;
    // The largest number explicitly attached to tracks/instruments is the global cast
    // request. This correctly distinguishes "50 tracks, 10 of them drums": 50 is the
    // ensemble size and 10 is a subset. Duration and tempo numbers are ignored.
    try {
        const auto text = direction.toLowerCase().toStdString();
        const std::array patterns{
            std::regex(R"((\d{1,2})\s*(?:midi\s*)?(?:tracks?|pistas?|instrumentos?|instruments?))",
                       std::regex::icase),
            std::regex(R"((?:tracks?|pistas?|instrumentos?|instruments?)\s*(?:midi\s*)?(?:de|of|:|-)?\s*(\d{1,2}))",
                       std::regex::icase)};
        auto requested = std::size_t{};
        for (const auto& pattern : patterns)
            for (auto match = std::sregex_iterator(text.begin(), text.end(), pattern);
                 match != std::sregex_iterator(); ++match)
                requested = std::max(requested,
                    static_cast<std::size_t>(std::stoul((*match)[1].str())));
        // A written-out count is just as explicit as a digit. If it is missed,
        // a prompt for "seis pistas" silently becomes an unconstrained large
        // cast, multiplying cost and thinning the musical content per voice.
        constexpr std::array<std::pair<const char*, int>, 24> spelledCounts{{
            std::pair{"uno", 1}, {"una", 1}, {"dos", 2}, {"tres", 3},
            {"cuatro", 4}, {"cinco", 5}, {"seis", 6}, {"siete", 7},
            {"ocho", 8}, {"nueve", 9}, {"diez", 10}, {"once", 11},
            {"doce", 12}, {"one", 1}, {"two", 2}, {"three", 3},
            {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
            {"eight", 8}, {"nine", 9}, {"ten", 10}, {"twelve", 12}}};
        for (const auto& [word, count] : spelledCounts) {
            const std::regex directPattern(std::string(R"(\b)") + word +
                R"(\s+(?:midi\s+)?(?:tracks?|pistas?|instrumentos?|instruments?)\b)",
                std::regex::icase);
            const std::regex qualifiedPattern(std::string(R"(\b)") + word +
                R"((?:\s+[a-z-]+){1,3}\s+(?:midi\s+)?(?:tracks?|pistas?|instrumentos?|instruments?)\b)",
                std::regex::icase);
            if (std::regex_search(text, directPattern) ||
                std::regex_search(text, qualifiedPattern))
                requested = std::max(requested, static_cast<std::size_t>(count));
        }
        return std::min(requested, maximumInstruments);
    } catch (...) {
        return 0;
    }
}

VoiceId rhythmVoiceForPitch(int pitch) noexcept {
    if (pitch == 35 || pitch == 36) return VoiceId::CoreDrums;
    if (pitch >= 37 && pitch <= 40) return VoiceId::SnareClap;
    if (pitch == 42 || pitch == 44) return VoiceId::ClosedHats;
    if (pitch == 46 || pitch >= 69) return VoiceId::OpenHatsShaker;
    if (pitch >= 41 && pitch <= 50) return VoiceId::LowPercussion;
    return VoiceId::HighPercussion;
}

bool explicitlyPercussionFreeDirection(const juce::String& direction) {
    const auto lower = direction.toLowerCase();
    constexpr std::array phrases{"no percussion", "without percussion", "sin percusion",
        "sin percusi", "no drums", "without drums", "sin bateria", "sin bater",
        "sin ritmica", "no rhythm", "no hace falta percusi", "no hacen falta percusi",
        "no hace falta bater", "no hacen falta bater", "no necesito percusi",
        "no necesito bater", "no quiero bateria", "no quiero bater",
        "no quiero percusion", "nicamente armon", "solo armon", "only harmony",
        "harmonies and melodies only", "harmony and melody only"};
    return std::any_of(phrases.begin(), phrases.end(), [&](const char* phrase) {
        return lower.contains(phrase);
    });
}

bool explicitlyRhythmicElectronicDirection(const juce::String& direction) {
    if (explicitlyPercussionFreeDirection(direction)) return false;
    const auto lower = direction.toLowerCase();
    constexpr std::array phrases{"techno", "house", "trance", "dancefloor",
        "club track", "club music", "musica de club", "música de club",
        "musica de boliche", "música de boliche", "four on the floor",
        "four-on-the-floor", "4x4", "bombo en negras"};
    return std::any_of(phrases.begin(), phrases.end(), [&](const char* phrase) {
        return lower.contains(phrase);
    });
}

bool validateExplicitRhythmCast(const juce::String& source,
                                const juce::String& direction,
                                juce::String& error) {
    if (!explicitlyRhythmicElectronicDirection(direction)) return true;
    const auto document = juce::JSON::parse(source);
    const auto* root = document.getDynamicObject();
    const auto* instruments = root == nullptr ? nullptr :
        root->getProperty("instruments").getArray();
    const auto* soundscape = root == nullptr ? nullptr :
        root->getProperty("electronic_soundscape").getDynamicObject();
    if (root == nullptr || instruments == nullptr || soundscape == nullptr) {
        error = "Cannot validate the explicit rhythmic electronic cast";
        return false;
    }
    if (static_cast<bool>(soundscape->getProperty("percussion_free"))) {
        error = "Explicit rhythmic electronic direction cannot be declared percussion_free";
        return false;
    }
    auto rhythmOwners = 0;
    auto hasCoreDrums = false;
    for (const auto& item : *instruments) {
        const auto* object = item.getDynamicObject();
        if (object == nullptr) continue;
        const auto voice = voiceIdFromKey(
            object->getProperty("source_voice").toString().toStdString());
        if (!voice || !isVoiceInFamily(*voice, VoiceFamily::Rhythm)) continue;
        ++rhythmOwners;
        hasCoreDrums = hasCoreDrums || *voice == VoiceId::CoreDrums;
    }
    if (hasCoreDrums && rhythmOwners >= 2) return true;
    error = "Explicit rhythmic electronic direction requires core drums and at least one complementary rhythm owner";
    return false;
}

bool enforceExplicitCastExclusionsImpl(const juce::String& source,
                                       const juce::String& direction,
                                       juce::String& sanitized,
                                       std::size_t& removed,
                                       juce::String& error) {
    removed = 0;
    sanitized = source;
    if (!explicitlyPercussionFreeDirection(direction)) return true;

    auto document = juce::JSON::parse(source);
    auto* root = document.getDynamicObject();
    auto* instruments = root == nullptr ? nullptr : root->getProperty("instruments").getArray();
    if (root == nullptr || instruments == nullptr) {
        error = "Cannot enforce explicit percussion exclusion on an invalid cast manifest";
        return false;
    }
    if (auto* soundscape = root->getProperty("electronic_soundscape").getDynamicObject())
        soundscape->setProperty("percussion_free", true);

    const auto protagonist = root->getProperty("protagonist_instrument_id").toString();
    auto protagonistRemoved = false;
    for (auto index = instruments->size(); --index >= 0;) {
        const auto* object = instruments->getReference(index).getDynamicObject();
        if (object == nullptr) continue;
        const auto* definition = instrumentDefinition(
            object->getProperty("instrument").toString().toStdString());
        const auto voice = voiceIdFromKey(
            object->getProperty("source_voice").toString().toStdString());
        const auto rhythm = (definition != nullptr &&
                definition->department == ScoreDepartment::Rhythm) ||
            (voice && isVoiceInFamily(*voice, VoiceFamily::Rhythm));
        if (!rhythm) continue;
        protagonistRemoved = protagonistRemoved ||
            object->getProperty("id").toString() == protagonist;
        instruments->remove(index);
        ++removed;
    }
    if (auto* voices = root->getProperty("voices").getArray()) {
        for (auto index = voices->size(); --index >= 0;) {
            const auto* object = voices->getReference(index).getDynamicObject();
            const auto voice = object == nullptr ? std::optional<VoiceId>{} :
                voiceIdFromKey(object->getProperty("id").toString().toStdString());
            if (voice && isVoiceInFamily(*voice, VoiceFamily::Rhythm)) voices->remove(index);
        }
    }
    if (protagonistRemoved) {
        const juce::DynamicObject* replacement = nullptr;
        for (const auto& item : *instruments) {
            const auto* object = item.getDynamicObject();
            if (object != nullptr && object->getProperty("source_voice").toString() == "lead") {
                replacement = object;
                break;
            }
        }
        if (replacement == nullptr) {
            error = "Explicit percussion exclusion removed an invalid rhythm protagonist and no melodic protagonist remains";
            return false;
        }
        root->setProperty("protagonist_instrument_id", replacement->getProperty("id"));
    }
    sanitized = juce::JSON::toString(document);
    return true;
}

bool repairMotionOwnerContract(SongPlan& plan) {
    if (!ElectronicRoleContract::requiresMotionOwner(plan) ||
        ElectronicRoleContract::motionOwnerCount(plan) == 1)
        return true;
    const auto elected = ElectronicRoleContract::electPrimaryMotionOwner(
        std::span<InstrumentAssignment>(plan.instruments.data(), plan.instruments.size()),
        plan.narrativeSpine.protagonistInstrumentId);
    if (!elected || ElectronicRoleContract::motionOwnerCount(plan) != 1) return false;
    OperationalJournal::write("WARN", "CAST",
        "repaired electronic motion owner locally after plan normalization; no musical material changed");
    return true;
}

bool repairCentralChordBedContract(SongPlan& plan) {
    std::vector<std::size_t> candidates;
    candidates.reserve(plan.instruments.size());
    for (std::size_t index = 0; index < plan.instruments.size(); ++index)
        if (plan.instruments[index].sourceVoice == VoiceId::HarmonicFoundation)
            candidates.push_back(index);
    if (candidates.empty()) return true;

    const auto marker = juce::String("primary_chord_bed");
    auto selected = candidates.front();
    auto best = -1.0;
    for (const auto index : candidates) {
        auto& instrument = plan.instruments[index];
        const auto role = juce::String::fromUTF8(instrument.role.c_str());
        const auto identity = juce::String::fromUTF8(
            (instrument.instrumentId + " " + instrument.name + " " +
             instrument.orchestralFunction).c_str());
        auto score = instrument.prominence * 2.0 + instrument.activity;
        if (role.containsIgnoreCase(marker)) score += 100.0;
        if (identity.containsIgnoreCase("chord") || identity.containsIgnoreCase("acorde") ||
            identity.containsIgnoreCase("pad") || identity.containsIgnoreCase("body") ||
            identity.containsIgnoreCase("colchon")) score += 8.0;
        if (const auto* definition = instrumentDefinition(instrument.instrumentId);
            definition != nullptr && definition->polyphonic) score += 4.0;
        if (score > best) {
            best = score;
            selected = index;
        }
    }

    auto changed = false;
    for (const auto index : candidates) {
        auto role = juce::String::fromUTF8(plan.instruments[index].role.c_str());
        while (true) {
            const auto markerIndex = role.indexOfIgnoreCase(marker);
            if (markerIndex < 0) break;
            role = role.replaceSection(markerIndex, marker.length(), {}).trim();
        }
        role = role.trimCharactersAtEnd(" |;,-").trim();
        if (index == selected) role = role + (role.isEmpty() ? "" : " | ") + marker;
        const auto normalized = role.toStdString();
        changed = changed || normalized != plan.instruments[index].role;
        plan.instruments[index].role = normalized;
    }
    // The central bed is the harmonic narrator, not a late-arriving colour. Keep
    // its explicit rests, but expose the essential dramatic stages to the writer.
    if (!plan.sections.empty()) {
        auto& bed = plan.instruments[selected];
        std::set<std::string> required{
            plan.sections.front().name,
            plan.sections[plan.sections.size() / 2].name,
            plan.sections.back().name,
        };
        for (const auto& act : plan.narrativeSpine.acts)
            if (act.stage == NarrativeStage::Transformation ||
                act.stage == NarrativeStage::Climax ||
                act.stage == NarrativeStage::Resolution)
                required.insert(act.sectionName);
        for (const auto& name : required)
            if (!name.empty() && std::find(bed.activeSections.begin(),
                    bed.activeSections.end(), name) == bed.activeSections.end()) {
                bed.activeSections.push_back(name);
                changed = true;
            }
    }
    if (changed)
        OperationalJournal::write("INFO", "CAST",
            "elected one primary polyphonic chord-bed owner; no MIDI material changed");
    return true;
}

void applyExplicitRhythmRequest(SongPlan& plan, const juce::String& direction) {
    const auto lower = direction.toLowerCase();
    const auto containsAny = [&](std::initializer_list<const char*> phrases) {
        return std::any_of(phrases.begin(), phrases.end(), [&](const char* phrase) {
            return lower.contains(phrase);
        });
    };
    const auto explicitlyBroken = containsAny({"breakbeat", "broken beat", "ritmo quebrado",
                                                "base break", "drum and bass", "dnb"});
    const auto explicitlyPercussionFree = containsAny({"no percussion", "without percussion",
        "sin percusion", "sin percusi", "sin percusiones", "no drums", "without drums",
        "sin bateria", "sin bater", "sin ritmica", "sin rítmica", "no rhythm",
        "no hace falta percusi", "no hacen falta percusi", "no hace falta bater",
        "no hacen falta bater", "no necesito percusi", "no necesito bater"});
    const auto explicitlyHarmonicOnly = containsAny({"no quiero bateria", "no quiero bater",
        "no quiero percusion", "nicamente armon", "solo armon", "only harmony",
        "harmonies and melodies only", "harmony and melody only"});
    if (explicitlyPercussionFree || explicitlyHarmonicOnly) {
        plan.percussionFreeIntent = true;
        plan.soundscape.percussionFree = true;
        return;
    }
    const auto rhythmicElectronic = explicitlyRhythmicElectronicDirection(direction);
    if (rhythmicElectronic) {
        plan.percussionFreeIntent = false;
        plan.soundscape.percussionFree = false;
    }
    const auto houseFoundation = !explicitlyBroken && containsAny({"progressive house", "deep house",
        "organic house", "four on the floor", "four-on-the-floor", "4x4", "guy j", "bombo en negras"}) ||
        (!explicitlyBroken && rhythmicElectronic);
    const auto constantKick = !explicitlyBroken && containsAny({"constant kick", "kick constante",
        "bombo constante", "bombo en negras constante", "four on the floor throughout"});
    if (!houseFoundation && !constantKick) return;

    for (auto& section : plan.sections) {
        if (constantKick) {
            section.rhythm.kickState = KickState::FourOnFloor;
            section.rhythm.continuity = KickContinuity::Required;
            section.rhythm.gestures.erase(std::remove_if(section.rhythm.gestures.begin(),
                section.rhythm.gestures.end(), [](const auto& gesture) {
                    return gesture.kind == RhythmGestureKind::DropLastKick ||
                           gesture.kind == RhythmGestureKind::HalfBarMute ||
                           gesture.kind == RhythmGestureKind::FullBarMute;
                }), section.rhythm.gestures.end());
        } else if (section.energy >= 0.40) {
            section.rhythm.kickState = KickState::FourOnFloor;
            section.rhythm.continuity = KickContinuity::Required;
        }
    }
}

const juce::String noteSchema = R"json({
  "type":"object",
  "properties":{
    "start":{"type":"number"},
    "duration":{"type":"number"},
    "pitch":{"type":"integer"},
    "velocity":{"type":"integer"}
  },
  "required":["start","duration","pitch","velocity"],
  "additionalProperties":false
})json";

juce::String schema() {
    juce::String result = R"json({
      "type":"object",
      "properties":{
        "title":{"type":"string"},
        "key":{"type":"string"},
        "summary":{"type":"string"},
        "bars":{"type":"integer"},
)json";
    for (std::size_t index = 0; index < layerNames.size(); ++index) {
        result += "\"" + juce::String(layerNames[index]) + "\":{";
        result += "\"type\":\"array\",\"items\":" + noteSchema + "}";
        if (index + 1 != layerNames.size()) result += ",";
    }
    result += R"json(},
      "required":["title","key","summary","bars","harmony","melody","bass","drums"],
      "additionalProperties":false
    })json";
    return result;
}

const juce::String songPlanSchema = juce::String(R"json({
  "type":"object",
  "properties":{
    "title":{"type":"string"},
    "key":{"type":"string"},
    "summary":{"type":"string"},
    "narrative_spine":{"type":"object","properties":{
      "premise":{"type":"string"},"question":{"type":"string"},
      "harmonic_debt":{"type":"string"},"protagonist_instrument_id":{"type":"string"},
      "motif_identity":{"type":"string"},"climax_consequence":{"type":"string"},
      "resolution":{"type":"string"},
      "acts":{"type":"array","minItems":3,"maxItems":20,"items":{"type":"object","properties":{
        "section_name":{"type":"string"},
        "stage":{"type":"string","enum":["premise","question","departure","transformation","climax","resolution","aftermath"]},
        "cause":{"type":"string"},"consequence":{"type":"string"},
        "unresolved_element":{"type":"string"},"resolution_target":{"type":"string"},
        "tension_target":{"type":"number"},"resolution_strength":{"type":"number"}
      },"required":["section_name","stage","cause","consequence","unresolved_element","resolution_target","tension_target","resolution_strength"],"additionalProperties":false}}
    },"required":["premise","question","harmonic_debt","protagonist_instrument_id","motif_identity","climax_consequence","resolution","acts"],"additionalProperties":false},
    "root_pitch_class":{"type":"integer"},
    "mode":{"type":"string","enum":["major","minor","dorian","mixolydian"]},
    "production_language":{"type":"object","properties":{
      "domain":{"type":"string","enum":["adaptive","club_electronic","hybrid","orchestral"]},
      "description":{"type":"string"},
      "electronic_intent":{"type":"number"},"club_focus":{"type":"number"},
      "low_end_interlock":{"type":"number"},"groove_evolution":{"type":"number"},
      "hook_economy":{"type":"number"},"automation_motion":{"type":"number"},
      "dj_utility":{"type":"number"},"spectral_restraint":{"type":"number"},
      "orchestral_allowance":{"type":"number"}
    },"required":["domain","description","electronic_intent","club_focus","low_end_interlock","groove_evolution","hook_economy","automation_motion","dj_utility","spectral_restraint","orchestral_allowance"],"additionalProperties":false},
    "rhythm_language":{"type":"object","properties":{
      "description":{"type":"string"},
      "pulse_stability":{"type":"number"},"backbeat_gravity":{"type":"number"},
      "syncopation":{"type":"number"},"ghost_density":{"type":"number"},
      "velocity_contrast":{"type":"number"},"timing_freedom":{"type":"number"},
      "orchestration_motion":{"type":"number"},"silence_bias":{"type":"number"},
      "call_response":{"type":"number"}
    },"required":["description","pulse_stability","backbeat_gravity","syncopation","ghost_density","velocity_contrast","timing_freedom","orchestration_motion","silence_bias","call_response"],"additionalProperties":false},
    "harmonic_language":{"type":"object","properties":{
      "description":{"type":"string"},
      "tonal_gravity":{"type":"number"},"modal_fluidity":{"type":"number"},
      "chromaticism":{"type":"number"},"extension_richness":{"type":"number"},
      "inversion_motion":{"type":"number"},"voice_leading_smoothness":{"type":"number"},
      "harmonic_rhythm_activity":{"type":"number"},"pedal_tone_affinity":{"type":"number"},
      "ambiguity":{"type":"number"},"cadence_strength":{"type":"number"}
    },"required":["description","tonal_gravity","modal_fluidity","chromaticism","extension_richness","inversion_motion","voice_leading_smoothness","harmonic_rhythm_activity","pedal_tone_affinity","ambiguity","cadence_strength"],"additionalProperties":false},
    "chord_palette":{"type":"array","minItems":4,"maxItems":24,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},"label":{"type":"string"},
        "root_pitch_class":{"type":"integer"},"bass_pitch_class":{"type":"integer"},
        "pitch_classes":{"type":"array","items":{"type":"integer"},"minItems":2,"maxItems":8},
        "function":{"type":"string","enum":["tonic","predominant","dominant","modal","chromatic","pedal","transitional","colour"]},
        "voicing":{"type":"string","enum":["close","open","drop_2","quartal","cluster","shell","mixed"]},
        "tension":{"type":"number"}
      },"required":["id","label","root_pitch_class","bass_pitch_class","pitch_classes","function","voicing","tension"],"additionalProperties":false
    }},
    "orchestration_language":{"type":"object","properties":{
      "description":{"type":"string"},
      "ensemble_scale":{"type":"number"},"timbral_motion":{"type":"number"},
      "foreground_rotation":{"type":"number"},"doubling_restraint":{"type":"number"},
      "register_separation":{"type":"number"},"chamber_contrast":{"type":"number"},
      "tutti_rarity":{"type":"number"},"harmonic_depth":{"type":"number"},
      "counterpoint_activity":{"type":"number"},"divisi_depth":{"type":"number"},
      "articulation_contrast":{"type":"number"},"family_dialogue":{"type":"number"},
      "hybrid_production":{"type":"number"}
    },"required":["description","ensemble_scale","timbral_motion","foreground_rotation","doubling_restraint","register_separation","chamber_contrast","tutti_rarity","harmonic_depth","counterpoint_activity","divisi_depth","articulation_contrast","family_dialogue","hybrid_production"],"additionalProperties":false},
    "timbre_palette":{"type":"object","properties":{
      "description":{"type":"string"},"material":{"type":"string"},"space":{"type":"string"},
      "warmth":{"type":"number"},"brightness":{"type":"number"},
      "transient_definition":{"type":"number"},"acoustic_electronic_balance":{"type":"number"},
      "cohesion":{"type":"number"},"contrast":{"type":"number"}
    },"required":["description","material","space","warmth","brightness","transient_definition","acoustic_electronic_balance","cohesion","contrast"],"additionalProperties":false},
)json") + R"json(    "electronic_soundscape":{"type":"object","properties":{
      "active":{"type":"boolean"},"percussion_free":{"type":"boolean"},
      "scene":{"type":"string"},"spatial_narrative":{"type":"string"},
      "target_median_active_layers":{"type":"number"},
      "layers":{"type":"array","maxItems":64,"items":{"type":"object","properties":{
        "instrument_id":{"type":"string"},
        "kind":{"type":"string","enum":["voice","environment","transition","one_shot"]},
        "time_scale":{"type":"string","enum":["fast","medium","slow","event"]},
        "narrative_role":{"type":"string"},"relationship":{"type":"string"},
        "evolution":{"type":"string"},"minimum_active_bars":{"type":"integer"},
        "minimum_phrases":{"type":"integer"},"maximum_static_bars":{"type":"integer"},
        "foreground_depth":{"type":"number"}
      },"required":["instrument_id","kind","time_scale","narrative_role","relationship","evolution","minimum_active_bars","minimum_phrases","maximum_static_bars","foreground_depth"],"additionalProperties":false}}
    },"required":["active","percussion_free","scene","spatial_narrative","target_median_active_layers","layers"],"additionalProperties":false},
)json" + R"json(    "motif_intervals":{"type":"array","items":{"type":"integer"},"minItems":3,"maxItems":8},
    "instruments":{"type":"array","minItems":8,"maxItems":64,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},
        "instrument":{"type":"string","enum":["kick_drum","snare_clap","hi_hats","timpani","taiko_ensemble","latin_percussion","shakers","cymbals","orchestral_percussion","piano","harp","violin_1","violin_2","viola","cello","contrabass","string_ensemble","chamber_strings","flute","piccolo","alto_flute","oboe","english_horn","clarinet","bass_clarinet","bassoon","contrabassoon","french_horns","trumpets","trombones","bass_trombone","tuba","brass_ensemble","woodwind_ensemble","choir","mallets","celesta","vibraphone","marimba","tubular_bells","electric_bass","sub_synth","rolling_mid_bass","reese_layer","analog_pad","poly_synth","dub_chord","filtered_stab","hypnotic_arp","lead_synth","deep_pluck","acid_line","fm_sequence","vocal_chop_texture","guitar","ambient_texture","granular_pad","spectral_drone","noise_riser","shimmer_tail"]},
        "name":{"type":"string"},
        "source_voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
        "role":{"type":"string"},
        "content_lane_id":{"type":"string"},
        "line_relationship":{"type":"string","enum":["independent","doubling","relay","call_response","octave_reinforcement","timbral_handoff"]},
        "minimum_pitch":{"type":"integer"},"maximum_pitch":{"type":"integer"},
        "octave_shift":{"type":"integer","enum":[-24,-12,0,12,24]},"activity":{"type":"number"},
        "prominence":{"type":"number"},"doubling":{"type":"number"},
        "orchestral_function":{"type":"string","enum":["foundation","body","extension","counterpoint","color","transition"]},
        "articulation_intent":{"type":"string","enum":["natural","legato","staccato","detached","sustained","swelling","tremolo","pizzicato","ostinato"]},
        "divisi_voices":{"type":"integer"},
        "live_device":{"type":"string","enum":["auto","Drum Rack","Instrument Rack","Simpler","Sampler","Drift","Meld","Wavetable","Operator","Analog","Electric","Tension","Collision","Granulator III"]},
        "live_preset_intent":{"type":"string"},
        "timbre_signature":{"type":"object","properties":{
          "source":{"type":"string","enum":["sine","triangle","saw","square","fm","noise","physical","sample","hybrid","acoustic"]},
          "envelope":{"type":"string","enum":["percussive","pluck","short","gated","natural","sustained","swelling"]},
          "spectrum":{"type":"string","enum":["dark","warm","neutral","bright","glassy"]},
          "motion":{"type":"string","enum":["static","subtle","evolving","rhythmic","chaotic"]},
          "space":{"type":"string","enum":["dry","close","wide","deep","wet"]},
          "texture":{"type":"string","enum":["clean","organic","metallic","gritty","airy","vocal"]},
          "uniqueness":{"type":"number"}
        },"required":["source","envelope","spectrum","motion","space","texture","uniqueness"],"additionalProperties":false},
        "active_sections":{"type":"array","maxItems":20,"items":{"type":"string"}}
      },"required":["id","instrument","name","source_voice","role","content_lane_id","line_relationship","minimum_pitch","maximum_pitch","octave_shift","activity","prominence","doubling","orchestral_function","articulation_intent","divisi_voices","live_device","live_preset_intent","timbre_signature","active_sections"],"additionalProperties":false
    }},
    "rhythm_motifs":{"type":"array","maxItems":6,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},"bars":{"type":"integer"},
        "steps_per_bar":{"type":"integer","enum":[8,16]},
        "kick":{"type":"string"},"snare_clap":{"type":"string"},
        "closed_hats":{"type":"string"},"open_hats_shaker":{"type":"string"},
        "low_percussion":{"type":"string"},"high_percussion":{"type":"string"},
        "ornaments":{"type":"array","maxItems":48,"items":{"type":"object","properties":{
          "step":{"type":"integer"},
          "instrument":{"type":"string","enum":["kick_deep","kick_alt","snare","sidestick","clap","tom_low","tom_mid","tom_high","closed_hat","pedal_hat","open_hat","ride","crash","shaker","tambourine","cowbell","conga_low","conga_high"]},
          "velocity":{"type":"integer"},"duration_steps":{"type":"number"}
        },"required":["step","instrument","velocity","duration_steps"],"additionalProperties":false}}
      },
      "required":["id","bars","steps_per_bar","kick","snare_clap","closed_hats","open_hats_shaker","low_percussion","high_percussion","ornaments"],
      "additionalProperties":false
    }},
    "voices":{"type":"array","minItems":7,"maxItems":15,"items":{
      "type":"object",
      "properties":{
        "id":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
        "function":{"type":"string"},
        "interaction":{"type":"string"},
        "activity":{"type":"number"},
        "syncopation":{"type":"number"},
        "minimum_pitch":{"type":"integer"},
        "maximum_pitch":{"type":"integer"},
        "performance_intent":{"type":"string"},
        "articulation":{"type":"string","enum":["percussive","staccato","detached","natural","legato","sustained","swelling"]},
        "dynamic_contour":{"type":"string","enum":["steady","phrase_arc","crescendo","decrescendo","swell","pulsing"]},
        "vibrato":{"type":"string","enum":["none","late_subtle","late_expressive","continuous_subtle"]},
        "pitch_gesture":{"type":"string","enum":["stable","approach","gentle_bends","portamento"]},
        "expression_depth":{"type":"number"},
        "brightness":{"type":"number"},
        "humanization":{"type":"number"},
        "sustain_pedal":{"type":"boolean"}
      },
      "required":["id","function","interaction","activity","syncopation","minimum_pitch","maximum_pitch","performance_intent","articulation","dynamic_contour","vibrato","pitch_gesture","expression_depth","brightness","humanization","sustain_pedal"],
      "additionalProperties":false
    }},
    "performance_score":{"type":"object","properties":{
      "cells":{"type":"array","minItems":1,"maxItems":512,"items":{
        "type":"object","properties":{
          "id":{"type":"string"},"theme_id":{"type":"string"},
          "narrative_function":{"type":"string","enum":["establish","question","answer","develop","withdraw","intensify","resolve","support"]},
          "length_beats":{"type":"number"},
          "owned_voices":{"type":"array","minItems":1,"maxItems":15,"items":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}},
          "notes":{"type":"array","maxItems":768,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"duration":{"type":"number"},"pitch":{"type":"integer"},
            "velocity":{"type":"integer"},"voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
            "instrument_id":{"type":"string"},
            "metric_intent":{"type":"string","enum":["strict_grid"]}
          },"required":["beat","duration","pitch","velocity","voice","instrument_id","metric_intent"],"additionalProperties":false}},
          "controls":{"type":"array","maxItems":384,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"controller":{"type":"integer"},"value":{"type":"integer"},
            "voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
            "instrument_id":{"type":"string"}
          },"required":["beat","controller","value","voice","instrument_id"],"additionalProperties":false}}
        },"required":["id","theme_id","narrative_function","length_beats","owned_voices","notes","controls"],"additionalProperties":false
      }},
      "placements":{"type":"array","maxItems":4096,"items":{"type":"object","properties":{
        "cell_id":{"type":"string"},"section_index":{"type":"integer"},"start_beat":{"type":"number"},
        "repeats":{"type":"integer"},"transpose":{"type":"integer"},
        "velocity_scale":{"type":"number"},"time_scale":{"type":"number"},"purpose":{"type":"string"},
        "voice_map":{"type":"array","maxItems":15,"items":{"type":"object","properties":{
          "from":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
          "to":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}
        },"required":["from","to"],"additionalProperties":false}},
        "retrograde":{"type":"boolean"},"invert_contour":{"type":"boolean"},
        "inversion_axis":{"type":"integer"},"fragment_start":{"type":"number"},"fragment_end":{"type":"number"},
        "metric_intent":{"type":"string","enum":["strict_grid"]}
      },"required":["cell_id","section_index","start_beat","repeats","transpose","velocity_scale","time_scale","purpose","voice_map","retrograde","invert_contour","inversion_axis","fragment_start","fragment_end","metric_intent"],"additionalProperties":false}}
    },"required":["cells","placements"],"additionalProperties":false},
    "sections":{"type":"array","minItems":3,"maxItems":20,"items":{
      "type":"object",
      "properties":{
        "name":{"type":"string"},
        "function":{"type":"string"},
        "harmonic_direction":{"type":"string"},
        "motif_treatment":{"type":"string"},
        "bars":{"type":"integer"},
        "energy":{"type":"number"},
        "tension":{"type":"number"},
        "density":{"type":"number"},
        "motif_variant":{"type":"integer"},
        "tonal_center_pitch_class":{"type":"integer"},
        "mode_hint":{"type":"string"},
        "harmonic_events":{"type":"array","minItems":2,"maxItems":64,"items":{
          "type":"object","properties":{
            "bar_offset":{"type":"integer"},"beat_offset":{"type":"number"},
            "chord_id":{"type":"string"},"emphasis":{"type":"number"},"purpose":{"type":"string"}
          },"required":["bar_offset","beat_offset","chord_id","emphasis","purpose"],"additionalProperties":false
        }},
        "active_voices":{"type":"array","items":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}},
        "kick_state":{"type":"string","enum":["muted","reduced","sparse","four_on_floor"]},
        "kick_continuity":{"type":"string","enum":["required","sectional","free"]},
        "percussion_density":{"type":"number"},
        "rhythmic_syncopation":{"type":"number"},
        "swing":{"type":"number"},
        "rhythm_motif_id":{"type":"string"},
        "rhythm_mutations":{"type":"array","maxItems":32,"items":{
          "type":"object","properties":{
            "bar_offset":{"type":"integer"},
            "lane":{"type":"string","enum":["kick","snare_clap","closed_hats","open_hats_shaker","low_percussion","high_percussion"]},
            "operation":{"type":"string","enum":["add","remove","shift","ratchet","velocity"]},
            "step":{"type":"integer"},"amount":{"type":"integer"},
            "velocity":{"type":"integer"},"purpose":{"type":"string"}
          },
          "required":["bar_offset","lane","operation","step","amount","velocity","purpose"],
          "additionalProperties":false
        }},
        "rhythm_gestures":{"type":"array","maxItems":16,"items":{
          "type":"object","properties":{
            "bar_offset":{"type":"integer"},
            "type":{"type":"string","enum":["drop_last_kick","double_kick","pickup_fill","half_bar_mute","full_bar_mute","percussion_fill"]},
            "beat":{"type":"number"},
            "intensity":{"type":"number"}
          },"required":["bar_offset","type","beat","intensity"],"additionalProperties":false
        }}
      },
      "required":["name","function","harmonic_direction","motif_treatment","bars","energy","tension","density","motif_variant","tonal_center_pitch_class","mode_hint","harmonic_events","active_voices","kick_state","kick_continuity","percussion_density","rhythmic_syncopation","swing","rhythm_motif_id","rhythm_mutations","rhythm_gestures"],
      "additionalProperties":false
    }}
  },
  "required":["title","key","summary","narrative_spine","root_pitch_class","mode","production_language","rhythm_language","harmonic_language","orchestration_language","timbre_palette","electronic_soundscape","chord_palette","motif_intervals","instruments","rhythm_motifs","voices","performance_score","sections"],
  "additionalProperties":false
})json";

// Phase one intentionally exposes only the fields needed to decide the long-form
// musical story. Reusing the full schema with maxItems=0 still made the model reason
// about every instrument and MIDI-note definition, defeating the point of splitting
// the work into bounded Responses.
juce::String macroBlueprintSchema() {
    const auto full = juce::JSON::parse(songPlanSchema);
    const auto* fullRoot = full.getDynamicObject();
    const auto* fullProperties = fullRoot == nullptr
        ? nullptr : fullRoot->getProperty("properties").getDynamicObject();
    if (fullProperties == nullptr) return {};

    auto root = new juce::DynamicObject();
    auto properties = new juce::DynamicObject();
    juce::Array<juce::var> required;
    for (const auto* name : {
             "title", "key", "summary", "narrative_spine", "root_pitch_class", "mode",
             "production_language", "rhythm_language", "harmonic_language", "chord_palette",
             "orchestration_language", "timbre_palette", "motif_intervals", "sections"}) {
        if (juce::String(name) == "sections") {
            const auto* fullSections = fullProperties->getProperty(name).getDynamicObject();
            const auto* fullItems = fullSections == nullptr
                ? nullptr : fullSections->getProperty("items").getDynamicObject();
            const auto* fullSectionProperties = fullItems == nullptr
                ? nullptr : fullItems->getProperty("properties").getDynamicObject();
            if (fullSectionProperties == nullptr) return {};
            auto compactSections = new juce::DynamicObject();
            auto compactItems = new juce::DynamicObject();
            auto compactProperties = new juce::DynamicObject();
            juce::Array<juce::var> compactRequired;
            for (const auto* field : {
                     "name", "function", "harmonic_direction", "motif_treatment", "bars",
                     "energy", "tension", "density", "motif_variant",
                     "tonal_center_pitch_class", "mode_hint", "harmonic_events",
                     "active_voices", "kick_state", "kick_continuity", "percussion_density",
                     "rhythmic_syncopation", "swing", "rhythm_motif_id"}) {
                compactProperties->setProperty(field, fullSectionProperties->getProperty(field));
                compactRequired.add(field);
            }
            compactItems->setProperty("type", "object");
            compactItems->setProperty("properties", juce::var(compactProperties));
            compactItems->setProperty("required", juce::var(compactRequired));
            compactItems->setProperty("additionalProperties", false);
            compactSections->setProperty("type", "array");
            compactSections->setProperty("minItems", 3);
            compactSections->setProperty("maxItems", 24);
            compactSections->setProperty("items", juce::var(compactItems));
            properties->setProperty(name, juce::var(compactSections));
        } else if (juce::String(name) == "narrative_spine") {
            // The macro phase cannot name an instrument that does not exist yet. Keep
            // the dramatic narrative here, then bind its definitive protagonist from
            // the subsequent global cast manifest.
            auto narrative = juce::JSON::parse(
                juce::JSON::toString(fullProperties->getProperty(name)));
            auto* narrativeObject = narrative.getDynamicObject();
            auto* narrativeProperties = narrativeObject == nullptr
                ? nullptr : narrativeObject->getProperty("properties").getDynamicObject();
            auto* narrativeRequired = narrativeObject == nullptr
                ? nullptr : narrativeObject->getProperty("required").getArray();
            if (narrativeProperties == nullptr || narrativeRequired == nullptr) return {};
            narrativeProperties->removeProperty("protagonist_instrument_id");
            narrativeRequired->removeAllInstancesOf(juce::var("protagonist_instrument_id"));
            properties->setProperty(name, narrative);
        } else {
            properties->setProperty(name, fullProperties->getProperty(name));
        }
        required.add(name);
    }
    root->setProperty("type", "object");
    root->setProperty("properties", juce::var(properties));
    root->setProperty("required", juce::var(required));
    root->setProperty("additionalProperties", false);
    return juce::JSON::toString(juce::var(root));
}

juce::String castBlueprintSchema() {
    const auto full = juce::JSON::parse(songPlanSchema);
    const auto* fullRoot = full.getDynamicObject();
    const auto* fullProperties = fullRoot == nullptr
        ? nullptr : fullRoot->getProperty("properties").getDynamicObject();
    if (fullProperties == nullptr) return {};

    auto root = new juce::DynamicObject();
    auto properties = new juce::DynamicObject();
    juce::Array<juce::var> required;
    for (const auto* name : {"electronic_soundscape", "instruments", "rhythm_motifs", "voices"}) {
        properties->setProperty(name, fullProperties->getProperty(name));
        required.add(name);
    }
    root->setProperty("type", "object");
    root->setProperty("properties", juce::var(properties));
    root->setProperty("required", juce::var(required));
    root->setProperty("additionalProperties", false);
    return juce::JSON::toString(juce::var(root));
}

juce::String mergeBlueprintPhases(const juce::String& macroText,
                                  const juce::String& castText) {
    auto macro = juce::JSON::parse(macroText);
    const auto cast = juce::JSON::parse(castText);
    auto* macroObject = macro.getDynamicObject();
    const auto* castObject = cast.getDynamicObject();
    if (macroObject == nullptr || castObject == nullptr) return {};
    const auto protagonistId = castObject->getProperty("protagonist_instrument_id").toString().trim();
    auto* narrative = macroObject->getProperty("narrative_spine").getDynamicObject();
    if (protagonistId.isEmpty() || narrative == nullptr) return {};
    narrative->setProperty("protagonist_instrument_id", protagonistId);
    for (const auto* name : {"electronic_soundscape", "instruments", "rhythm_motifs", "voices"})
        macroObject->setProperty(name, castObject->getProperty(name));
    auto performance = new juce::DynamicObject();
    performance->setProperty("cells", juce::Array<juce::var>{});
    performance->setProperty("placements", juce::Array<juce::var>{});
    macroObject->setProperty("performance_score", juce::var(performance));
    if (auto* sections = macroObject->getProperty("sections").getArray()) {
        for (auto& section : *sections) {
            if (auto* sectionObject = section.getDynamicObject()) {
                sectionObject->setProperty("rhythm_mutations", juce::Array<juce::var>{});
                sectionObject->setProperty("rhythm_gestures", juce::Array<juce::var>{});
            }
        }
    }
    return juce::JSON::toString(macro);
}

std::vector<juce::String> manifestInstrumentIds(const juce::String& manifestText,
                                                juce::String& error) {
    std::vector<juce::String> ids;
    const auto manifest = juce::JSON::parse(manifestText);
    const auto* object = manifest.getDynamicObject();
    const auto* instruments = object == nullptr
        ? nullptr : object->getProperty("instruments").getArray();
    if (instruments == nullptr || instruments->isEmpty()) {
        error = "Cast manifest contains no instruments";
        return {};
    }
    std::set<juce::String> unique;
    for (const auto& item : *instruments) {
        const auto* instrument = item.getDynamicObject();
        const auto id = instrument == nullptr ? juce::String{} : instrument->getProperty("id").toString().trim();
        if (id.isEmpty() || !unique.insert(id).second) {
            error = "Cast manifest contains an empty or duplicate instrument id";
            return {};
        }
        ids.push_back(id);
    }
    return ids;
}

bool validateIndependentCastManifest(const juce::String& manifestText,
                                     bool required, juce::String& error) {
    if (!required) return true;
    const auto manifest = juce::JSON::parse(manifestText);
    const auto* root = manifest.getDynamicObject();
    const auto* instruments = root == nullptr ? nullptr :
        root->getProperty("instruments").getArray();
    if (instruments == nullptr) {
        error = "Independent MIDI cast has no instrument roster";
        return false;
    }
    std::set<juce::String> lanes;
    for (const auto& item : *instruments) {
        const auto* instrument = item.getDynamicObject();
        const auto id = instrument == nullptr ? juce::String{} :
            instrument->getProperty("id").toString().trim();
        const auto lane = instrument == nullptr ? juce::String{} :
            instrument->getProperty("content_lane_id").toString().trim();
        const auto relation = instrument == nullptr ? juce::String{} :
            instrument->getProperty("line_relationship").toString().trim();
        if (lane.isEmpty() || !lanes.insert(lane).second ||
            (relation != "independent" && relation != "call_response")) {
            error = "Requested independent MIDI cast contains a shared or "
                "destination-only lane at instrument " + id;
            return false;
        }
    }
    return true;
}

bool validateProtagonistManifest(const juce::String& macroText,
                                 const juce::String& manifestText,
                                 juce::String& error) {
    const auto macro = juce::JSON::parse(macroText);
    const auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    const auto* manifestObject = manifest.getDynamicObject();
    const auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    const auto protagonistId = manifestObject == nullptr ? juce::String{} :
        manifestObject->getProperty("protagonist_instrument_id").toString().trim();
    if (macroObject == nullptr || instruments == nullptr || protagonistId.isEmpty()) {
        error = "Cast manifest omitted its authoritative protagonist_instrument_id";
        return false;
    }

    const juce::DynamicObject* protagonist = nullptr;
    auto matches = 0;
    for (const auto& item : *instruments) {
        const auto* instrument = item.getDynamicObject();
        if (instrument != nullptr &&
            instrument->getProperty("id").toString().trim() == protagonistId) {
            protagonist = instrument;
            ++matches;
        }
    }
    if (matches != 1 || protagonist == nullptr) {
        error = "Cast protagonist_instrument_id must match exactly one manifest instrument: " +
            protagonistId;
        return false;
    }
    if (protagonist->getProperty("source_voice").toString() != "lead") {
        error = "Cast protagonist must own the Lead source voice: " + protagonistId;
        return false;
    }

    std::set<juce::String> resolutionSections;
    if (const auto* spine = macroObject->getProperty("narrative_spine").getDynamicObject()) {
        if (const auto* acts = spine->getProperty("acts").getArray()) {
            for (const auto& item : *acts) {
                const auto* act = item.getDynamicObject();
                if (act != nullptr && act->getProperty("stage").toString() == "resolution") {
                    const auto section = act->getProperty("section_name").toString().trim();
                    if (section.isNotEmpty()) resolutionSections.insert(section);
                }
            }
        }
    }
    if (resolutionSections.empty()) {
        if (const auto* sections = macroObject->getProperty("sections").getArray();
            sections != nullptr && !sections->isEmpty()) {
            if (const auto* finalSection = sections->getLast().getDynamicObject()) {
                const auto name = finalSection->getProperty("name").toString().trim();
                if (name.isNotEmpty()) resolutionSections.insert(name);
            }
        }
    }
    if (resolutionSections.empty()) {
        error = "Macro blueprint contains no identifiable resolution section";
        return false;
    }

    const auto* activeSections = protagonist->getProperty("active_sections").getArray();
    const auto activeInResolution = activeSections != nullptr &&
        std::any_of(activeSections->begin(), activeSections->end(), [&](const auto& section) {
            return resolutionSections.contains(section.toString().trim());
        });
    if (!activeInResolution) {
        error = "Cast protagonist is not active in the declared resolution: " + protagonistId;
        return false;
    }
    return true;
}

bool reconcileProtagonistResolutionManifest(const juce::String& macroText,
                                             juce::String& manifestText,
                                             bool& changed,
                                             juce::String& error) {
    changed = false;
    const auto macro = juce::JSON::parse(macroText);
    auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    auto* manifestObject = manifest.getDynamicObject();
    auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    const auto protagonistId = manifestObject == nullptr ? juce::String{} :
        manifestObject->getProperty("protagonist_instrument_id").toString().trim();
    if (macroObject == nullptr || manifestObject == nullptr || instruments == nullptr ||
        protagonistId.isEmpty()) {
        error = "Cast manifest cannot reconcile its protagonist resolution metadata";
        return false;
    }

    std::set<juce::String> resolutionSections;
    if (const auto* spine = macroObject->getProperty("narrative_spine").getDynamicObject()) {
        if (const auto* acts = spine->getProperty("acts").getArray()) {
            for (const auto& item : *acts) {
                const auto* act = item.getDynamicObject();
                if (act != nullptr && act->getProperty("stage").toString() == "resolution") {
                    const auto section = act->getProperty("section_name").toString().trim();
                    if (section.isNotEmpty()) resolutionSections.insert(section);
                }
            }
        }
    }
    if (resolutionSections.empty()) {
        if (const auto* sections = macroObject->getProperty("sections").getArray();
            sections != nullptr && !sections->isEmpty()) {
            if (const auto* finalSection = sections->getLast().getDynamicObject()) {
                const auto name = finalSection->getProperty("name").toString().trim();
                if (name.isNotEmpty()) resolutionSections.insert(name);
            }
        }
    }
    if (resolutionSections.empty()) {
        error = "Macro blueprint contains no identifiable resolution section";
        return false;
    }

    juce::DynamicObject* protagonist = nullptr;
    auto matches = 0;
    for (auto& item : *instruments) {
        auto* instrument = item.getDynamicObject();
        if (instrument != nullptr &&
            instrument->getProperty("id").toString().trim() == protagonistId) {
            protagonist = instrument;
            ++matches;
        }
    }
    // Identity and voice errors are semantic cast errors and remain strict. Only
    // the mechanically missing section reference is safe to reconcile locally.
    if (matches != 1 || protagonist == nullptr ||
        protagonist->getProperty("source_voice").toString() != "lead") return true;

    juce::Array<juce::var> active;
    if (const auto* existing = protagonist->getProperty("active_sections").getArray())
        for (const auto& section : *existing) active.add(section);
    const auto activeInResolution = std::any_of(active.begin(), active.end(), [&](const auto& section) {
        return resolutionSections.contains(section.toString().trim());
    });
    if (activeInResolution) return true;

    // The macro is authoritative for form. Linking its unique resolution name to
    // the already-declared lead changes no cast identity and authors no music.
    active.add(*resolutionSections.begin());
    protagonist->setProperty("active_sections", juce::var(active));
    manifestText = juce::JSON::toString(manifest);
    changed = true;
    error.clear();
    return true;
}

juce::String foldedSectionReference(juce::String text) {
    text = text.toLowerCase()
        .replace(juce::String::fromUTF8("á"), "a")
        .replace(juce::String::fromUTF8("à"), "a")
        .replace(juce::String::fromUTF8("ä"), "a")
        .replace(juce::String::fromUTF8("é"), "e")
        .replace(juce::String::fromUTF8("è"), "e")
        .replace(juce::String::fromUTF8("ë"), "e")
        .replace(juce::String::fromUTF8("í"), "i")
        .replace(juce::String::fromUTF8("ì"), "i")
        .replace(juce::String::fromUTF8("ï"), "i")
        .replace(juce::String::fromUTF8("ó"), "o")
        .replace(juce::String::fromUTF8("ò"), "o")
        .replace(juce::String::fromUTF8("ö"), "o")
        .replace(juce::String::fromUTF8("ú"), "u")
        .replace(juce::String::fromUTF8("ù"), "u")
        .replace(juce::String::fromUTF8("ü"), "u")
        .replace(juce::String::fromUTF8("ñ"), "n");
    juce::String folded;
    auto previousWasSpace = true;
    for (auto index = 0; index < text.length(); ++index) {
        const auto character = text[index];
        if (juce::CharacterFunctions::isLetterOrDigit(character)) {
            folded += juce::String::charToString(character);
            previousWasSpace = false;
        } else if (!previousWasSpace) {
            folded += " ";
            previousWasSpace = true;
        }
    }
    return folded.trim();
}

int romanSectionOrdinal(const juce::String& token) noexcept {
    const auto roman = token.toUpperCase();
    if (roman == "I") return 1;
    if (roman == "II") return 2;
    if (roman == "III") return 3;
    if (roman == "IV") return 4;
    if (roman == "V") return 5;
    if (roman == "VI") return 6;
    if (roman == "VII") return 7;
    if (roman == "VIII") return 8;
    if (roman == "IX") return 9;
    if (roman == "X") return 10;
    return 0;
}

int explicitSectionOrdinal(const juce::String& reference) noexcept {
    auto tokens = juce::StringArray::fromTokens(foldedSectionReference(reference), " ", {});
    tokens.removeEmptyStrings();
    if (tokens.isEmpty()) return 0;
    auto offset = 0;
    if (tokens[0] == "section" || tokens[0] == "seccion" || tokens[0] == "act" ||
        tokens[0] == "acto" || tokens[0] == "part" || tokens[0] == "parte") ++offset;
    if (offset >= tokens.size()) return 0;
    if (tokens[offset].containsOnly("0123456789")) return tokens[offset].getIntValue();
    return romanSectionOrdinal(tokens[offset]);
}

juce::StringArray semanticSectionTokens(const juce::String& reference) {
    auto tokens = juce::StringArray::fromTokens(foldedSectionReference(reference), " ", {});
    tokens.removeEmptyStrings();
    if (!tokens.isEmpty() && (romanSectionOrdinal(tokens[0]) != 0 ||
        tokens[0].containsOnly("0123456789"))) tokens.remove(0);
    if (!tokens.isEmpty() && (tokens[0] == "section" || tokens[0] == "seccion" ||
        tokens[0] == "act" || tokens[0] == "acto" || tokens[0] == "part" ||
        tokens[0] == "parte")) tokens.remove(0);
    return tokens;
}

int sectionReferenceScore(const juce::String& reference,
                          const juce::String& authoritativeName) {
    const auto referenceFolded = foldedSectionReference(reference);
    const auto nameFolded = foldedSectionReference(authoritativeName);
    if (referenceFolded.isEmpty() || nameFolded.isEmpty()) return 0;
    if (referenceFolded == nameFolded) return 1000;

    const auto ordinal = explicitSectionOrdinal(reference);
    const auto authoritativeOrdinal = explicitSectionOrdinal(authoritativeName);
    auto score = ordinal != 0 && ordinal == authoritativeOrdinal ? 240 : 0;
    const auto referenceTokens = semanticSectionTokens(reference);
    const auto nameTokens = semanticSectionTokens(authoritativeName);
    if (referenceTokens.isEmpty() || nameTokens.isEmpty()) return score;

    auto overlap = 0;
    for (const auto& token : referenceTokens)
        if (nameTokens.contains(token)) ++overlap;
    if (overlap == 0) return score;
    const auto smaller = std::max(1, std::min(referenceTokens.size(), nameTokens.size()));
    const auto larger = std::max(referenceTokens.size(), nameTokens.size());
    score += (overlap * 600) / smaller + (overlap * 120) / std::max(1, larger);
    return score;
}

bool isHarmonicMatrixVoice(const juce::String& voice) {
    return voice == "harmonic_foundation" || voice == "harmonic_pulse" ||
        voice == "harmonic_upper" || voice == "atmosphere";
}

int requiredHarmonicMatrixOwners(std::size_t castSize) noexcept {
    return castSize <= 3 ? 1 : 2;
}

int requiredSectionMatrixOwners(double density, double energy,
                                std::size_t castSize) noexcept {
    if (castSize <= 2) return static_cast<int>(castSize);
    if (castSize <= 3)
        return std::clamp(static_cast<int>(std::lround(
            1.0 + density * 1.5 + energy * .5)), 2,
            static_cast<int>(castSize));
    return std::clamp(static_cast<int>(std::lround(
        4.0 + density * 4.0 + energy * 2.0)), 4,
        std::min(10, static_cast<int>(castSize)));
}

void markExplicitPromptInstrumentIdentities(SongPlan& plan,
                                            const juce::String& direction) {
    // Live's playback inventory is appended to the creative direction as execution
    // context. It must never be interpreted as something the user explicitly asked
    // to hear, otherwise every installed orchestral family becomes a hard identity
    // commitment and an optional empty lane can reject an otherwise valid score.
    auto userDirection = direction;
    const auto inventoryOffset = userDirection.indexOfIgnoreCase(
        "Ableton playback inventory.");
    if (inventoryOffset >= 0)
        userDirection = userDirection.substring(0, inventoryOffset);
    const auto foldedDirection = " " + foldedSectionReference(userDirection) + " ";
    const auto containsWord = [&](const juce::String& word) {
        return word.isNotEmpty() && foldedDirection.contains(" " + word + " ");
    };
    const std::array<std::pair<const char*, const char*>, 12> aliases{{
        {"piano", "piano"}, {"violin", "violin"},
        {"viola", "viola"}, {"cello", "cello"}, {"flauta", "flute"},
        {"arpa", "harp"}, {"guitarra", "guitar"}, {"coro", "choir"},
        {"trompeta", "trumpet"}, {"trombon", "trombone"},
        {"clarinete", "clarinet"}, {"oboe", "oboe"}}};
    const std::set<juce::String> concreteWords{
        "piano", "violin", "viola", "cello", "contrabass", "bassoon", "clarinet",
        "oboe", "flute", "harp", "guitar", "choir", "horn", "trumpet", "trombone",
        "tuba", "vibraphone", "marimba", "saxophone", "kick", "snare", "clap"};
    for (auto& instrument : plan.instruments) {
        const auto identity = foldedSectionReference(juce::String::fromUTF8(
            (instrument.instrumentId + " " + instrument.name).c_str()));
        auto tokens = juce::StringArray::fromTokens(identity, " ", {});
        tokens.removeEmptyStrings();
        instrument.explicitPromptIdentity = std::any_of(
            tokens.begin(), tokens.end(), [&](const auto& token) {
                return concreteWords.contains(token) && containsWord(token);
            });
        if (instrument.explicitPromptIdentity) continue;
        for (const auto& [requested, catalogWord] : aliases) {
            if (containsWord(requested) && tokens.contains(catalogWord)) {
                instrument.explicitPromptIdentity = true;
                break;
            }
        }
    }
}

bool reconcileOrchestrationMatrixManifest(const juce::String& macroText,
                                           juce::String& manifestText,
                                           bool& changed,
                                           juce::String& report,
                                           juce::String& error) {
    changed = false;
    report.clear();
    const auto macro = juce::JSON::parse(macroText);
    auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    auto* manifestObject = manifest.getDynamicObject();
    const auto* sections = macroObject == nullptr ? nullptr :
        macroObject->getProperty("sections").getArray();
    auto* instruments = manifestObject == nullptr ? nullptr :
        manifestObject->getProperty("instruments").getArray();
    if (sections == nullptr || instruments == nullptr || sections->isEmpty() || instruments->isEmpty()) {
        error = "Cast manifest cannot reconcile a global orchestration matrix";
        return false;
    }

    std::vector<juce::String> sectionNames;
    sectionNames.reserve(static_cast<std::size_t>(sections->size()));
    for (const auto& sectionItem : *sections) {
        const auto* section = sectionItem.getDynamicObject();
        const auto name = section == nullptr ? juce::String{} :
            section->getProperty("name").toString().trim();
        if (name.isEmpty()) {
            error = "Macro blueprint contains an unnamed section";
            return false;
        }
        sectionNames.push_back(name);
    }

    std::vector<juce::DynamicObject*> instrumentObjects;
    std::vector<std::set<int>> activeByInstrument;
    instrumentObjects.reserve(static_cast<std::size_t>(instruments->size()));
    activeByInstrument.reserve(static_cast<std::size_t>(instruments->size()));
    auto normalizedReferences = 0;
    auto unresolvedReferences = 0;
    for (auto& item : *instruments) {
        auto* instrument = item.getDynamicObject();
        if (instrument == nullptr) continue;
        instrumentObjects.push_back(instrument);
        std::set<int> active;
        const auto* declared = instrument->getProperty("active_sections").getArray();
        if (declared == nullptr || declared->isEmpty()) {
            for (auto sectionIndex = 0; sectionIndex < static_cast<int>(sectionNames.size()); ++sectionIndex)
                active.insert(sectionIndex);
        } else {
            for (const auto& sectionItem : *declared) {
                const auto reference = sectionItem.toString().trim();
                auto bestIndex = -1;
                auto bestScore = 0;
                auto runnerUp = 0;
                for (auto sectionIndex = 0; sectionIndex < static_cast<int>(sectionNames.size()); ++sectionIndex) {
                    const auto score = sectionReferenceScore(reference, sectionNames[sectionIndex]);
                    if (score > bestScore) {
                        runnerUp = bestScore;
                        bestScore = score;
                        bestIndex = sectionIndex;
                    } else if (score > runnerUp) {
                        runnerUp = score;
                    }
                }
                const auto ordinal = explicitSectionOrdinal(reference);
                if (bestScore < 350 && ordinal >= 1 &&
                    ordinal <= static_cast<int>(sectionNames.size())) {
                    bestIndex = ordinal - 1;
                    bestScore = 350;
                    runnerUp = 0;
                }
                if (bestIndex >= 0 && bestScore >= 350 && bestScore > runnerUp) {
                    active.insert(bestIndex);
                    if (reference != sectionNames[bestIndex]) ++normalizedReferences;
                } else {
                    ++unresolvedReferences;
                }
            }
        }
        activeByInstrument.push_back(std::move(active));
    }
    if (instrumentObjects.empty()) {
        error = "Cast manifest contains no usable instrument identities";
        return false;
    }

    const auto harmonicOwnerCount = static_cast<int>(std::count_if(
        instrumentObjects.begin(), instrumentObjects.end(), [](const auto* instrument) {
            return isHarmonicMatrixVoice(instrument->getProperty("source_voice").toString());
        }));
    const auto harmonicMinimum = requiredHarmonicMatrixOwners(instrumentObjects.size());
    if (harmonicOwnerCount < harmonicMinimum) {
        error = "Cast manifest cannot provide " + juce::String(harmonicMinimum) +
            " harmonic or atmospheric matrix owner(s)";
        return false;
    }

    const auto protagonistId = manifestObject->getProperty("protagonist_instrument_id")
        .toString();
    auto linksAdded = 0;
    auto linksRemoved = 0;
    for (auto sectionIndex = 0; sectionIndex < sections->size(); ++sectionIndex) {
        const auto* section = sections->getReference(sectionIndex).getDynamicObject();
        const auto density = std::clamp(static_cast<double>(section->getProperty("density")), 0.0, 1.0);
        const auto energy = std::clamp(static_cast<double>(section->getProperty("energy")), 0.0, 1.0);
        const auto target = requiredSectionMatrixOwners(
            density, energy, instrumentObjects.size());

        const auto activeCount = [&] {
            return static_cast<int>(std::count_if(activeByInstrument.begin(), activeByInstrument.end(),
                [&](const auto& active) { return active.contains(sectionIndex); }));
        };
        const auto harmonicCount = [&] {
            auto count = 0;
            for (auto index = 0; index < static_cast<int>(instrumentObjects.size()); ++index)
                if (activeByInstrument[index].contains(sectionIndex) &&
                    isHarmonicMatrixVoice(instrumentObjects[index]->getProperty("source_voice").toString())) ++count;
            return count;
        };
        const auto addBestCandidate = [&](bool harmonicOnly) {
            auto best = -1;
            auto bestScore = std::numeric_limits<int>::min();
            for (auto index = 0; index < static_cast<int>(instrumentObjects.size()); ++index) {
                if (activeByInstrument[index].contains(sectionIndex)) continue;
                const auto harmonic = isHarmonicMatrixVoice(
                    instrumentObjects[index]->getProperty("source_voice").toString());
                if (harmonicOnly && !harmonic) continue;
                auto score = harmonic ? 80 : 0;
                if (sectionIndex > 0 && activeByInstrument[index].contains(sectionIndex - 1)) score += 160;
                if (sectionIndex + 1 < sections->size() &&
                    activeByInstrument[index].contains(sectionIndex + 1)) score += 160;
                score -= static_cast<int>(activeByInstrument[index].size()) * 12;
                const auto function = instrumentObjects[index]->getProperty("orchestral_function").toString();
                if (function == "foundation" || function == "body") score += 45;
                if (score > bestScore) {
                    bestScore = score;
                    best = index;
                }
            }
            if (best < 0) return false;
            activeByInstrument[best].insert(sectionIndex);
            ++linksAdded;
            return true;
        };

        while (harmonicCount() < harmonicMinimum)
            if (!addBestCandidate(true)) break;
        while (activeCount() < target)
            if (!addBestCandidate(false)) break;
        if (activeCount() < target || harmonicCount() < harmonicMinimum) {
            error = "Cast orchestration matrix cannot be completed for section '" +
                sectionNames[sectionIndex] + "' from the existing ensemble";
            return false;
        }

        // Density and energy describe an orchestration window, not only a floor.
        // Keep a little expressive headroom while preventing an AI-declared near-tutti
        // from flattening a breakdown or making the climax indistinguishable from it.
        const auto maximum = std::min(static_cast<int>(instrumentObjects.size()), target + 2);
        while (activeCount() > maximum) {
            auto removable = -1;
            auto bestRemovalScore = std::numeric_limits<double>::lowest();
            for (auto index = 0; index < static_cast<int>(instrumentObjects.size()); ++index) {
                if (!activeByInstrument[index].contains(sectionIndex) ||
                    activeByInstrument[index].size() <= 1) continue;
                const auto* instrument = instrumentObjects[index];
                const auto id = instrument->getProperty("id").toString();
                if (!protagonistId.isEmpty() && id == protagonistId) continue;
                const auto harmonic = isHarmonicMatrixVoice(
                    instrument->getProperty("source_voice").toString());
                if (harmonic && harmonicCount() <= harmonicMinimum) continue;
                const auto prominence = std::clamp(
                    static_cast<double>(instrument->getProperty("prominence")), 0.0, 1.0);
                const auto activity = std::clamp(
                    static_cast<double>(instrument->getProperty("activity")), 0.0, 1.0);
                auto score = (1.0 - prominence) * 100.0 + (1.0 - activity) * 60.0 +
                    static_cast<double>(activeByInstrument[index].size()) * 5.0;
                const auto function = instrument->getProperty("orchestral_function").toString();
                if (function == "transition") score += 25.0;
                if (function == "foundation") score -= 55.0;
                else if (function == "body") score -= 30.0;
                if (harmonic) score -= 12.0;
                if (score > bestRemovalScore) {
                    bestRemovalScore = score;
                    removable = index;
                }
            }
            if (removable < 0) break;
            activeByInstrument[removable].erase(sectionIndex);
            ++linksRemoved;
        }
    }

    // An unrecognized non-empty declaration must not serialize as an empty array:
    // legacy readers interpret an empty array as "active everywhere". Give such a
    // member one real, least-populated home section instead of accidentally turning
    // a local colour into permanent tutti.
    for (auto instrumentIndex = 0;
         instrumentIndex < static_cast<int>(instrumentObjects.size()); ++instrumentIndex) {
        if (!activeByInstrument[instrumentIndex].empty()) continue;
        auto leastPopulatedSection = 0;
        auto leastPopulation = std::numeric_limits<int>::max();
        for (auto sectionIndex = 0; sectionIndex < sections->size(); ++sectionIndex) {
            const auto population = static_cast<int>(std::count_if(
                activeByInstrument.begin(), activeByInstrument.end(),
                [&](const auto& active) { return active.contains(sectionIndex); }));
            if (population < leastPopulation) {
                leastPopulation = population;
                leastPopulatedSection = sectionIndex;
            }
        }
        activeByInstrument[instrumentIndex].insert(leastPopulatedSection);
        ++linksAdded;
    }

    for (auto index = 0; index < static_cast<int>(instrumentObjects.size()); ++index) {
        juce::Array<juce::var> active;
        for (const auto sectionIndex : activeByInstrument[index])
            active.add(sectionNames[sectionIndex]);
        const auto before = juce::JSON::toString(
            instrumentObjects[index]->getProperty("active_sections"));
        const auto after = juce::JSON::toString(juce::var(active));
        if (before != after) changed = true;
        instrumentObjects[index]->setProperty("active_sections", juce::var(active));
    }
    if (changed) manifestText = juce::JSON::toString(manifest);
    report = "normalized references=" + juce::String(normalizedReferences) +
        " | unresolved references=" + juce::String(unresolvedReferences) +
        " | coverage links added=" + juce::String(linksAdded) +
        " | excess links removed=" + juce::String(linksRemoved) +
        " | cast identities preserved=" + juce::String(static_cast<int>(instrumentObjects.size()));
    error.clear();
    return true;
}

bool validateOrchestrationMatrixManifest(const juce::String& macroText,
                                          const juce::String& manifestText,
                                          juce::String& error) {
    const auto macro = juce::JSON::parse(macroText);
    const auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    const auto* manifestObject = manifest.getDynamicObject();
    const auto* sections = macroObject == nullptr
        ? nullptr : macroObject->getProperty("sections").getArray();
    const auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    if (sections == nullptr || instruments == nullptr || sections->isEmpty() || instruments->isEmpty()) {
        error = "Cast manifest cannot establish a global orchestration matrix";
        return false;
    }

    for (const auto& sectionItem : *sections) {
        const auto* section = sectionItem.getDynamicObject();
        if (section == nullptr) continue;
        const auto name = section->getProperty("name").toString().trim();
        const auto density = std::clamp(static_cast<double>(section->getProperty("density")), 0.0, 1.0);
        const auto energy = std::clamp(static_cast<double>(section->getProperty("energy")), 0.0, 1.0);
        const auto target = requiredSectionMatrixOwners(
            density, energy, static_cast<std::size_t>(instruments->size()));
        const auto harmonicMinimum = requiredHarmonicMatrixOwners(
            static_cast<std::size_t>(instruments->size()));
        auto active = 0;
        auto harmonic = 0;
        for (const auto& instrumentItem : *instruments) {
            const auto* instrument = instrumentItem.getDynamicObject();
            if (instrument == nullptr) continue;
            const auto* activeSections = instrument->getProperty("active_sections").getArray();
            const auto present = activeSections == nullptr || activeSections->isEmpty() ||
                std::any_of(activeSections->begin(), activeSections->end(), [&](const auto& item) {
                    return item.toString().trim() == name;
                });
            if (!present) continue;
            ++active;
            const auto voice = instrument->getProperty("source_voice").toString();
            if (voice == "harmonic_foundation" || voice == "harmonic_pulse" ||
                voice == "harmonic_upper" || voice == "atmosphere") ++harmonic;
        }
        const auto maximum = std::min(instruments->size(), target + 2);
        if (active < target || active > maximum || harmonic < harmonicMinimum) {
            error = "Cast orchestration matrix violates section window '" + name +
                "': planned owners " + juce::String(active) + "/" + juce::String(target) +
                "-" + juce::String(maximum) +
                ", harmonic or atmospheric responsibilities " + juce::String(harmonic) +
                "/" + juce::String(harmonicMinimum);
            return false;
        }
    }
    return true;
}

bool reconcileMotionManifest(const juce::String& macroText,
                             juce::String& manifestText,
                             const juce::String& direction,
                             juce::String& report,
                             juce::String& error) {
    const auto macro = juce::JSON::parse(macroText);
    auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    auto* manifestObject = manifest.getDynamicObject();
    const auto* production = macroObject == nullptr
        ? nullptr : macroObject->getProperty("production_language").getDynamicObject();
    const auto* soundscape = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("electronic_soundscape").getDynamicObject();
    auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    if (production == nullptr || soundscape == nullptr || instruments == nullptr) return true;
    const auto domain = production->getProperty("domain").toString();
    const auto electronic = (domain == "club_electronic" || domain == "hybrid") &&
        static_cast<double>(production->getProperty("electronic_intent")) >= .58;
    const auto text = direction.toLowerCase();
    const auto explicitlyStatic = text.contains("drone-only") || text.contains("drone only") ||
        text.contains("without pulse") || text.contains("without motion") ||
        text.contains("sin pulso") || text.contains("sin movimiento");
    std::vector<InstrumentAssignment> assignments;
    std::vector<juce::DynamicObject*> objects;
    assignments.reserve(instruments->size());
    objects.reserve(instruments->size());
    for (auto& item : *instruments) {
        auto* object = item.getDynamicObject();
        if (object == nullptr) continue;
        const auto voice = voiceIdFromKey(object->getProperty("source_voice").toString().toStdString());
        if (!voice) continue;
        InstrumentAssignment instrument;
        instrument.id = object->getProperty("id").toString().toStdString();
        instrument.instrumentId = object->getProperty("instrument").toString().toStdString();
        instrument.name = object->getProperty("name").toString().toStdString();
        instrument.sourceVoice = *voice;
        instrument.role = object->getProperty("role").toString().toStdString();
        instrument.contentLaneId = object->getProperty("content_lane_id").toString().toStdString();
        instrument.lineRelationship = object->getProperty("line_relationship").toString().toStdString();
        instrument.orchestralFunction = object->getProperty("orchestral_function").toString().toStdString();
        assignments.push_back(std::move(instrument));
        objects.push_back(object);
    }
    const auto percussionFree = static_cast<bool>(soundscape->getProperty("percussion_free")) ||
        explicitlyPercussionFreeDirection(direction);
    const auto authoredMotion = std::any_of(assignments.begin(), assignments.end(),
        [](const auto& part) { return ElectronicRoleContract::motionCandidate(part); });
    const auto required = electronic && !explicitlyStatic &&
        (percussionFree || authoredMotion);
    if (!required) return true;
    const auto explicitlyNoBass = text.contains("no bass") || text.contains("without bass") ||
        text.contains("sin bajo") || text.contains("sin bajos");
    const auto hasMovementBass = std::any_of(assignments.begin(), assignments.end(),
        [](const auto& instrument) {
            return instrument.sourceVoice == VoiceId::MovementBass;
        });
    if (percussionFree && !explicitlyNoBass && !hasMovementBass) {
        error = "Percussion-free electronic cast requires one authored movement_bass owner unless bass is explicitly excluded";
        return false;
    }
    const auto authoredCandidates = static_cast<std::size_t>(std::count_if(
        assignments.begin(), assignments.end(), [](const auto& part) {
            return ElectronicRoleContract::motionCandidate(part);
        }));
    const auto protagonist = manifestObject->getProperty("protagonist_instrument_id")
        .toString().toStdString();
    const auto primary = ElectronicRoleContract::electPrimaryMotionOwner(assignments, protagonist);
    if (!primary || *primary >= assignments.size()) {
        error = "Percussion-free electronic cast contains no pitched instrument eligible to conduct motion";
        return false;
    }
    for (std::size_t index = 0; index < assignments.size(); ++index)
        objects[index]->setProperty("role", juce::String::fromUTF8(assignments[index].role.c_str()));
    manifestText = juce::JSON::toString(manifest);
    report = "motion candidates=" + juce::String(static_cast<int>(authoredCandidates)) +
        " | primary=" + juce::String::fromUTF8(assignments[*primary].id.c_str()) +
        " | supporting=" + juce::String(static_cast<int>(authoredCandidates > 0
            ? authoredCandidates - 1 : 0));
    return true;
}

bool validateMotionManifest(const juce::String& macroText,
                             const juce::String& manifestText,
                             const juce::String& direction,
                            juce::String& error) {
    const auto macro = juce::JSON::parse(macroText);
    const auto manifest = juce::JSON::parse(manifestText);
    const auto* macroObject = macro.getDynamicObject();
    const auto* manifestObject = manifest.getDynamicObject();
    const auto* production = macroObject == nullptr
        ? nullptr : macroObject->getProperty("production_language").getDynamicObject();
    const auto* soundscape = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("electronic_soundscape").getDynamicObject();
    const auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    if (production == nullptr || soundscape == nullptr || instruments == nullptr) return true;
    const auto domain = production->getProperty("domain").toString();
    const auto electronic = (domain == "club_electronic" || domain == "hybrid") &&
        static_cast<double>(production->getProperty("electronic_intent")) >= .58;
    const auto text = direction.toLowerCase();
    const auto explicitlyStatic = text.contains("drone-only") || text.contains("drone only") ||
        text.contains("without pulse") || text.contains("without motion") ||
        text.contains("sin pulso") || text.contains("sin movimiento");
    auto owners = std::size_t{};
    auto candidates = std::size_t{};
    for (const auto& item : *instruments) {
        const auto* object = item.getDynamicObject();
        if (object == nullptr) continue;
        const auto voice = voiceIdFromKey(object->getProperty("source_voice").toString().toStdString());
        if (!voice) continue;
        InstrumentAssignment instrument;
        instrument.id = object->getProperty("id").toString().toStdString();
        instrument.instrumentId = object->getProperty("instrument").toString().toStdString();
        instrument.name = object->getProperty("name").toString().toStdString();
        instrument.sourceVoice = *voice;
        instrument.role = object->getProperty("role").toString().toStdString();
        instrument.orchestralFunction = object->getProperty("orchestral_function").toString().toStdString();
        if (ElectronicRoleContract::motionCandidate(instrument)) ++candidates;
        if (ElectronicRoleContract::motionOwner(instrument)) ++owners;
    }
    const auto percussionFree = static_cast<bool>(soundscape->getProperty("percussion_free")) ||
        explicitlyPercussionFreeDirection(direction);
    const auto required = electronic && !explicitlyStatic &&
        (percussionFree || candidates > 0);
    if (!required) return true;
    if (owners == 1) return true;
    error = "Electronic cast with authored motion requires exactly one elected primary motion owner; received " +
        juce::String(static_cast<int>(owners));
    return false;
}

juce::String manifestSubset(const juce::String& manifestText,
                            const std::vector<juce::String>& targetIds) {
    const auto source = juce::JSON::parse(manifestText);
    const auto* sourceObject = source.getDynamicObject();
    const auto* sourceInstruments = sourceObject == nullptr
        ? nullptr : sourceObject->getProperty("instruments").getArray();
    if (sourceInstruments == nullptr) return {};
    const std::set<juce::String> targets(targetIds.begin(), targetIds.end());
    juce::Array<juce::var> selected;
    for (const auto& item : *sourceInstruments) {
        const auto* object = item.getDynamicObject();
        if (object != nullptr && targets.contains(object->getProperty("id").toString())) selected.add(item);
    }
    return juce::JSON::toString(juce::var(selected));
}

bool mergeCastManifestSupplement(const juce::String& manifestText,
                                 const juce::String& supplementText,
                                 std::size_t requestedCount,
                                 juce::String& mergedText,
                                 juce::String& error) {
    auto manifest = juce::JSON::parse(manifestText);
    const auto supplement = juce::JSON::parse(supplementText);
    auto* manifestObject = manifest.getDynamicObject();
    const auto* supplementObject = supplement.getDynamicObject();
    auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    const auto* additions = supplementObject == nullptr
        ? nullptr : supplementObject->getProperty("instruments").getArray();
    if (instruments == nullptr || additions == nullptr) {
        error = "Cast reconciliation is not valid structured JSON";
        return false;
    }
    if (instruments->size() > static_cast<int>(requestedCount)) {
        error = "Cast reconciliation cannot remove an oversized AI manifest";
        return false;
    }
    const auto missing = requestedCount - static_cast<std::size_t>(instruments->size());
    if (additions->size() != static_cast<int>(missing)) {
        error = "Cast reconciliation returned " + juce::String(additions->size()) +
            " additions; expected " + juce::String(static_cast<int>(missing));
        return false;
    }

    std::set<juce::String> ids;
    for (const auto& item : *instruments) {
        const auto* object = item.getDynamicObject();
        const auto id = object == nullptr ? juce::String{} : object->getProperty("id").toString().trim();
        if (id.isEmpty() || !ids.insert(id).second) {
            error = "Original cast contains an empty or duplicate instrument id";
            return false;
        }
    }
    for (const auto& item : *additions) {
        const auto* object = item.getDynamicObject();
        const auto id = object == nullptr ? juce::String{} : object->getProperty("id").toString().trim();
        if (id.isEmpty() || !ids.insert(id).second) {
            error = "Cast reconciliation introduced an empty or duplicate instrument id";
            return false;
        }
        for (const auto* field : {"instrument", "name", "source_voice", "role",
                                  "content_lane_id", "line_relationship",
                                  "orchestral_function", "active_sections"}) {
            if (!object->hasProperty(field)) {
                error = "Cast reconciliation omitted required anchor " + juce::String(field);
                return false;
            }
        }
        instruments->add(item);
    }
    if (instruments->size() != static_cast<int>(requestedCount)) {
        error = "Cast reconciliation did not satisfy the explicit ensemble size";
        return false;
    }
    mergedText = juce::JSON::toString(manifest);
    return true;
}

juce::String voiceKey(VoiceId voice) {
    const auto key = voiceDefinition(voice).key;
    return juce::String::fromUTF8(key.data(), static_cast<int>(key.size()));
}

bool completeCastManifestFromCatalog(const juce::String& manifestText,
                                     const juce::String& macroText,
                                     std::size_t requestedCount,
                                     juce::String& completedText,
                                     juce::String& error) {
    auto manifest = juce::JSON::parse(manifestText);
    const auto macro = juce::JSON::parse(macroText);
    auto* manifestObject = manifest.getDynamicObject();
    const auto* macroObject = macro.getDynamicObject();
    auto* instruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    const auto* voices = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("voices").getArray();
    if (instruments == nullptr || voices == nullptr || instruments->isEmpty()) {
        error = "Cannot complete an invalid global cast";
        return false;
    }
    if (instruments->size() > static_cast<int>(requestedCount)) {
        error = "AI manifest exceeds the requested cast size";
        return false;
    }

    std::set<juce::String> ids;
    std::set<std::string> usedCatalog;
    for (const auto& item : *instruments) {
        if (const auto* object = item.getDynamicObject(); object != nullptr) {
            ids.insert(object->getProperty("id").toString());
            usedCatalog.insert(object->getProperty("instrument").toString().toStdString());
        }
    }
    std::set<std::string> availableVoices;
    for (const auto& item : *voices)
        if (const auto* object = item.getDynamicObject(); object != nullptr)
            availableVoices.insert(object->getProperty("id").toString().toStdString());

    juce::Array<juce::var> sectionNames;
    if (macroObject != nullptr)
        if (const auto* sections = macroObject->getProperty("sections").getArray())
            for (const auto& item : *sections)
                if (const auto* object = item.getDynamicObject(); object != nullptr)
                    sectionNames.add(object->getProperty("name").toString());

    const auto* soundscape = manifestObject->getProperty("electronic_soundscape").getDynamicObject();
    const auto percussionFree = soundscape != nullptr &&
        static_cast<bool>(soundscape->getProperty("percussion_free"));
    const auto* production = macroObject == nullptr
        ? nullptr : macroObject->getProperty("production_language").getDynamicObject();
    const auto domain = production == nullptr
        ? juce::String{} : production->getProperty("domain").toString();
    const auto electronic = domain == "club_electronic" || domain == "hybrid";
    std::vector<const InstrumentDefinition*> candidates;
    std::set<std::string> queuedCatalog;
    const auto queueDefinition = [&](std::string_view id) {
        if (const auto* definition = instrumentDefinition(id); definition != nullptr &&
            queuedCatalog.insert(std::string(id)).second)
            candidates.push_back(definition);
    };
    if (electronic) {
        for (const auto id : {"analog_pad", "granular_pad", "spectral_drone", "ambient_texture",
                              "shimmer_tail", "dub_chord", "filtered_stab", "poly_synth",
                              "deep_pluck", "lead_synth", "vocal_chop_texture", "reese_layer",
                              "rolling_mid_bass", "sub_synth", "noise_riser", "hypnotic_arp",
                              "fm_sequence", "acid_line", "kick_drum", "snare_clap", "hi_hats",
                              "shakers", "latin_percussion", "cymbals"})
            queueDefinition(id);
    }
    for (const auto& definition : instrumentCatalog()) queueDefinition(definition.id);
    const auto originalSize = static_cast<std::size_t>(instruments->size());
    const auto missing = requestedCount - originalSize;
    auto added = std::size_t{};

    // Prefer unused catalog identities. A second pass permits another instance of a useful
    // timbre when a 64-track request is larger than the compatible catalog subset. These are
    // orchestration anchors only: no note, phrase or rhythm is generated here.
    for (auto pass = 0; pass < 2 && added < missing; ++pass) {
        for (const auto* definitionPointer : candidates) {
            if (added >= missing) break;
            const auto& definition = *definitionPointer;
            if (percussionFree && definition.department == ScoreDepartment::Rhythm) continue;
            const auto sourceVoice = voiceKey(definition.preferredVoice);
            if (!availableVoices.contains(sourceVoice.toStdString())) continue;
            if (pass == 0 && usedCatalog.contains(std::string(definition.id))) continue;

            InstrumentAssignment candidate;
            candidate.instrumentId = std::string(definition.id);
            candidate.name = std::string(definition.name);
            candidate.sourceVoice = definition.preferredVoice;
            candidate.role = definition.department == ScoreDepartment::Rhythm
                ? "complementary rhythmic articulation"
                : definition.preferredVoice == VoiceId::Atmosphere
                    ? "complementary evolving depth"
                    : "complementary independent orchestration";
            candidate.orchestralFunction = definition.preferredVoice == VoiceId::Atmosphere
                ? "color" : definition.department == ScoreDepartment::Melody
                    ? "counterpoint" : definition.department == ScoreDepartment::Rhythm
                        ? "body" : "extension";
            if (percussionFree && ElectronicRoleContract::motionOwner(candidate)) continue;

            auto id = "reconciled_" + juce::String::fromUTF8(definition.id.data(),
                static_cast<int>(definition.id.size())) + "_" +
                juce::String(static_cast<int>(originalSize + added + 1));
            while (ids.contains(id)) id += "_x";
            ids.insert(id);
            usedCatalog.insert(std::string(definition.id));

            auto object = new juce::DynamicObject();
            object->setProperty("id", id);
            object->setProperty("instrument", juce::String::fromUTF8(definition.id.data(),
                static_cast<int>(definition.id.size())));
            object->setProperty("name", juce::String::fromUTF8(definition.name.data(),
                static_cast<int>(definition.name.size())) + " Complement " +
                juce::String(static_cast<int>(added + 1)));
            object->setProperty("source_voice", sourceVoice);
            object->setProperty("role", juce::String::fromUTF8(candidate.role.c_str()));
            object->setProperty("content_lane_id", id + "_lane");
            object->setProperty("line_relationship", "independent");
            object->setProperty("orchestral_function",
                                juce::String::fromUTF8(candidate.orchestralFunction.c_str()));
            juce::Array<juce::var> activeSections;
            if (!sectionNames.isEmpty()) {
                const auto first = static_cast<int>((originalSize + added) %
                    static_cast<std::size_t>(sectionNames.size()));
                activeSections.add(sectionNames[first]);
                if (sectionNames.size() > 2)
                    activeSections.add(sectionNames[(first + sectionNames.size() / 2) %
                                                     sectionNames.size()]);
            }
            object->setProperty("active_sections", juce::var(activeSections));
            instruments->add(juce::var(object));
            ++added;
        }
    }
    if (added != missing) {
        error = "Catalog reconciliation could add only " + juce::String(static_cast<int>(added)) +
            " of " + juce::String(static_cast<int>(missing)) + " missing cast identities";
        return false;
    }
    completedText = juce::JSON::toString(manifest);
    return true;
}

bool sameManifestAnchor(const juce::DynamicObject& manifest,
                        const juce::DynamicObject& detail) {
    for (const auto* field : {"id", "instrument", "name", "source_voice", "role",
                              "content_lane_id", "line_relationship", "orchestral_function"}) {
        if (manifest.getProperty(field).toString() != detail.getProperty(field).toString()) return false;
    }
    return juce::JSON::toString(manifest.getProperty("active_sections")) ==
           juce::JSON::toString(detail.getProperty("active_sections"));
}

bool validateCastDetailShard(const juce::String& manifestText,
                             const std::vector<juce::String>& targetIds,
                             const juce::String& detailText,
                             juce::String& error) {
    const auto manifest = juce::JSON::parse(manifestText);
    const auto detail = juce::JSON::parse(detailText);
    const auto* manifestObject = manifest.getDynamicObject();
    const auto* detailObject = detail.getDynamicObject();
    const auto* manifestInstruments = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("instruments").getArray();
    const auto* detailInstruments = detailObject == nullptr
        ? nullptr : detailObject->getProperty("instruments").getArray();
    const auto* layers = detailObject == nullptr
        ? nullptr : detailObject->getProperty("layers").getArray();
    if (manifestInstruments == nullptr || detailInstruments == nullptr || layers == nullptr) {
        error = "Cast detail shard is not a valid structured response";
        return false;
    }
    const std::set<juce::String> targets(targetIds.begin(), targetIds.end());
    std::map<juce::String, const juce::DynamicObject*> anchors;
    for (const auto& item : *manifestInstruments) {
        if (const auto* object = item.getDynamicObject(); object != nullptr)
            anchors[object->getProperty("id").toString()] = object;
    }
    std::set<juce::String> detailed;
    for (const auto& item : *detailInstruments) {
        const auto* object = item.getDynamicObject();
        const auto id = object == nullptr ? juce::String{} : object->getProperty("id").toString();
        const auto anchor = anchors.find(id);
        if (object == nullptr || !targets.contains(id) || anchor == anchors.end() ||
            !detailed.insert(id).second || !sameManifestAnchor(*anchor->second, *object)) {
            error = "Cast detail shard attempted to alter the global ensemble architecture";
            return false;
        }
    }
    std::set<juce::String> layered;
    for (const auto& item : *layers) {
        const auto* object = item.getDynamicObject();
        const auto id = object == nullptr ? juce::String{} : object->getProperty("instrument_id").toString();
        if (object == nullptr || !targets.contains(id) || !layered.insert(id).second) {
            error = "Cast detail shard contains an invalid soundscape layer";
            return false;
        }
    }
    if (detailed.size() != targets.size() || layered.size() != targets.size()) {
        error = "Cast detail shard did not complete every assigned instrument";
        return false;
    }
    return true;
}

juce::String mergeCastManifestAndDetails(const juce::String& manifestText,
                                         const std::vector<juce::String>& detailTexts,
                                         juce::String& error) {
    auto manifest = juce::JSON::parse(manifestText);
    auto* manifestObject = manifest.getDynamicObject();
    auto* soundscape = manifestObject == nullptr
        ? nullptr : manifestObject->getProperty("electronic_soundscape").getDynamicObject();
    if (manifestObject == nullptr || soundscape == nullptr) {
        error = "Could not assemble the detailed cast";
        return {};
    }
    juce::Array<juce::var> instruments;
    juce::Array<juce::var> layers;
    std::set<juce::String> ids;
    for (const auto& text : detailTexts) {
        const auto detail = juce::JSON::parse(text);
        const auto* object = detail.getDynamicObject();
        const auto* shardInstruments = object == nullptr
            ? nullptr : object->getProperty("instruments").getArray();
        const auto* shardLayers = object == nullptr ? nullptr : object->getProperty("layers").getArray();
        if (shardInstruments == nullptr || shardLayers == nullptr) {
            error = "Could not merge a cast detail shard";
            return {};
        }
        for (const auto& item : *shardInstruments) {
            const auto* instrument = item.getDynamicObject();
            const auto id = instrument == nullptr ? juce::String{} : instrument->getProperty("id").toString();
            if (id.isEmpty() || !ids.insert(id).second) {
                error = "Detailed cast contains a duplicate instrument";
                return {};
            }
            instruments.add(item);
        }
        for (const auto& layer : *shardLayers) layers.add(layer);
    }
    manifestObject->setProperty("instruments", juce::var(instruments));
    soundscape->setProperty("layers", juce::var(layers));
    return juce::JSON::toString(manifest);
}

const juce::String performanceBlockSchema = R"json({
  "type":"object",
  "properties":{
    "block_id":{"type":"string"},
    "covered_instrument_ids":{"type":"array","maxItems":12,"items":{"type":"string"}},
    "performance_score":{"type":"object","properties":{
      "cells":{"type":"array","minItems":1,"maxItems":12,"items":{
        "type":"object","properties":{
          "id":{"type":"string"},"theme_id":{"type":"string"},
          "narrative_function":{"type":"string","enum":["establish","question","answer","develop","withdraw","intensify","resolve","support"]},
          "length_beats":{"type":"number"},
          "owned_voices":{"type":"array","minItems":1,"maxItems":15,"items":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}},
          "notes":{"type":"array","minItems":1,"maxItems":768,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"duration":{"type":"number"},"pitch":{"type":"integer"},
            "velocity":{"type":"integer"},"voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
            "instrument_id":{"type":"string"},"metric_intent":{"type":"string","enum":["strict_grid"]}
          },"required":["beat","duration","pitch","velocity","voice","instrument_id","metric_intent"],"additionalProperties":false}},
          "controls":{"type":"array","maxItems":192,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"controller":{"type":"integer"},"value":{"type":"integer"},
            "voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
            "instrument_id":{"type":"string"}
          },"required":["beat","controller","value","voice","instrument_id"],"additionalProperties":false}}
        },"required":["id","theme_id","narrative_function","length_beats","owned_voices","notes","controls"],"additionalProperties":false
      }},
      "placements":{"type":"array","minItems":1,"maxItems":192,"items":{"type":"object","properties":{
        "cell_id":{"type":"string"},"section_index":{"type":"integer"},"start_beat":{"type":"number"},
        "repeats":{"type":"integer"},"transpose":{"type":"integer"},
        "velocity_scale":{"type":"number"},"time_scale":{"type":"number"},"purpose":{"type":"string"},
        "voice_map":{"type":"array","maxItems":15,"items":{"type":"object","properties":{
          "from":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
          "to":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}
        },"required":["from","to"],"additionalProperties":false}},
        "retrograde":{"type":"boolean"},"invert_contour":{"type":"boolean"},
        "inversion_axis":{"type":"integer"},"fragment_start":{"type":"number"},"fragment_end":{"type":"number"},
        "metric_intent":{"type":"string","enum":["strict_grid"]}
      },"required":["cell_id","section_index","start_beat","repeats","transpose","velocity_scale","time_scale","purpose","voice_map","retrograde","invert_contour","inversion_axis","fragment_start","fragment_end","metric_intent"],"additionalProperties":false}}
    },"required":["cells","placements"],"additionalProperties":false}
  },"required":["block_id","covered_instrument_ids","performance_score"],"additionalProperties":false
})json";

juce::String compositionBehaviorBrief(CompositionBehavior behavior) {
    if (behavior == CompositionBehavior::Adaptive) return {};
    if (behavior == CompositionBehavior::Narrative)
        return "STYLE: narrative development with thematic contrast, earned climax and resolution. ";
    return "STYLE: hypnotic evolution. Sustain a recognizable harmonic and rhythmic identity across "
        "long states, using staggered entries, complementary voicings, restrained motif variation and "
        "authored expression controls. Evolve one layer at a time when musically appropriate. "
        "Preserve complete phrases, instrumental responsibilities, thematic relationships and an "
        "intentional stable ending. This preference does not relax MIDI integrity, tonal compatibility, "
        "coverage or closure requirements. Follow explicit instrument and percussion exclusions. ";
}

juce::String boundedPerformanceSchema(
    const SongPlan& plan, const std::vector<std::size_t>& indices) {
    auto schema = juce::JSON::parse(performanceBlockSchema);
    juce::Array<juce::var> ids;
    for (const auto index : indices)
        if (index < plan.instruments.size())
            ids.add(juce::String::fromUTF8(plan.instruments[index].id.c_str()));
    if (ids.isEmpty()) return {};
    auto properties = schema.getProperty("properties", {});
    properties.getProperty("covered_instrument_ids", {}).getProperty("items", {})
        .getDynamicObject()->setProperty("enum", ids);
    auto cells = properties.getProperty("performance_score", {}).getProperty("properties", {})
        .getProperty("cells", {}).getProperty("items", {}).getProperty("properties", {});
    for (const auto* events : {"notes", "controls"})
        cells.getProperty(events, {}).getProperty("items", {}).getProperty("properties", {})
            .getProperty("instrument_id", {}).getDynamicObject()->setProperty("enum", ids);
    return juce::JSON::toString(schema, true);
}

juce::String describeReference(const Pattern* pattern, std::uint8_t lockedLayers) {
    if (pattern == nullptr || pattern->notes.empty() || lockedLayers == 0) return "None.";
    juce::String result;
    for (std::size_t layer = 0; layer < layerChannels.size(); ++layer) {
        if ((lockedLayers & (1u << layer)) == 0) continue;
        result += juce::String(layerNames[layer]).toUpperCase() + " LOCKED: ";
        auto count = 0;
        for (const auto& note : pattern->notes) {
            if (note.channel != layerChannels[layer]) continue;
            result += "[" + juce::String(note.startBeat, 3) + "," +
                      juce::String(note.durationBeats, 3) + "," + juce::String(note.pitch) +
                      "," + juce::String(note.velocity) + "] ";
            if (++count >= 256) break;
        }
        result += "\n";
    }
    return result;
}

juce::String extractOutputText(const juce::var& root) {
    const auto* object = root.getDynamicObject();
    if (object == nullptr) return {};
    const auto* output = object->getProperty("output").getArray();
    if (output == nullptr) return {};
    for (const auto& item : *output) {
        const auto* itemObject = item.getDynamicObject();
        if (itemObject == nullptr) continue;
        const auto* content = itemObject->getProperty("content").getArray();
        if (content == nullptr) continue;
        for (const auto& part : *content) {
            const auto* partObject = part.getDynamicObject();
            if (partObject != nullptr && partObject->getProperty("type").toString() == "output_text")
                return partObject->getProperty("text").toString();
        }
    }
    return {};
}

struct HttpResponse {
    juce::String body;
    int status{};
    bool connected{};
    bool cancelled{};
    bool timedOut{};
    bool reusedLocally{};
    unsigned long nativeError{};
};

bool responseIdentity(const juce::String& body, juce::String& id,
                      juce::String& status);

std::optional<juce::File> validatedResponseCacheFile(const juce::String& body) {
    const auto* configured = std::getenv("PULSO_AI_RESPONSE_CACHE_DIR");
    if (configured == nullptr || *configured == '\0') return std::nullopt;
    const auto directory = juce::File(juce::String::fromUTF8(configured));
    if (!juce::File::isAbsolutePath(directory.getFullPathName()))
        return std::nullopt;
    auto fingerprint = std::uint64_t{14695981039346656037ull};
    const auto* bytes = reinterpret_cast<const unsigned char*>(body.toRawUTF8());
    for (std::size_t index = 0; index < body.getNumBytesAsUTF8(); ++index) {
        fingerprint ^= bytes[index];
        fingerprint *= 1099511628211ull;
    }
    const auto digest = juce::String::toHexString(
        static_cast<juce::int64>(fingerprint)) + "-" +
        juce::String(static_cast<juce::int64>(body.getNumBytesAsUTF8()));
    return directory.getChildFile(digest + ".json");
}

void cacheValidatedResponse(const juce::String& requestBody,
                            const HttpResponse& response) {
    if (response.reusedLocally || !response.connected || response.status != 200 ||
        response.cancelled || response.timedOut) return;
    juce::String responseId;
    juce::String status;
    if (!responseIdentity(response.body, responseId, status) ||
        status != "completed" ||
        extractOutputText(juce::JSON::parse(response.body)).isEmpty()) return;
    const auto cacheFile = validatedResponseCacheFile(requestBody);
    if (!cacheFile || !cacheFile->getParentDirectory().createDirectory()) return;
    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("schema_version", 1);
    envelope->setProperty("request", requestBody);
    envelope->setProperty("response", response.body);
    (void) cacheFile->replaceWithText(
        juce::JSON::toString(juce::var(envelope), false), false, false, "\n");
}

void traceOpenAiRequest(const juce::String& requestBody, const HttpResponse& response,
                        std::chrono::steady_clock::time_point started,
                        const juce::String& knownResponseId) {
    const auto* destination = std::getenv("PULSO_TRACE_PATH");
    if (destination == nullptr || *destination == '\0') return;

    const auto request = juce::JSON::parse(requestBody);
    const auto* requestObject = request.getDynamicObject();
    const auto remote = juce::JSON::parse(response.body);
    const auto* remoteObject = remote.getDynamicObject();
    const auto* usage = remoteObject == nullptr ? nullptr :
        remoteObject->getProperty("usage").getDynamicObject();
    const auto* inputDetails = usage == nullptr ? nullptr :
        usage->getProperty("input_tokens_details").getDynamicObject();
    const auto* outputDetails = usage == nullptr ? nullptr :
        usage->getProperty("output_tokens_details").getDynamicObject();
    const auto* reasoning = requestObject == nullptr ? nullptr :
        requestObject->getProperty("reasoning").getDynamicObject();
    const auto* text = requestObject == nullptr ? nullptr :
        requestObject->getProperty("text").getDynamicObject();
    const auto* format = text == nullptr ? nullptr :
        text->getProperty("format").getDynamicObject();
    const auto* incomplete = remoteObject == nullptr ? nullptr :
        remoteObject->getProperty("incomplete_details").getDynamicObject();

    auto* event = new juce::DynamicObject();
    event->setProperty("schema_version", 1);
    event->setProperty("event", "api_call");
    event->setProperty("at", juce::Time::getCurrentTime().toISO8601(true));
    event->setProperty("elapsed_ms", static_cast<juce::int64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count()));
    event->setProperty("model", requestObject == nullptr ? juce::String{} :
        requestObject->getProperty("model").toString());
    event->setProperty("effort", reasoning == nullptr ? juce::String{} :
        reasoning->getProperty("effort").toString());
    event->setProperty("phase", format == nullptr ? juce::String{} :
        format->getProperty("name").toString());
    event->setProperty("max_output_tokens", requestObject == nullptr ? 0 :
        static_cast<int>(requestObject->getProperty("max_output_tokens")));
    event->setProperty("response_id", remoteObject == nullptr ? knownResponseId :
        remoteObject->getProperty("id").toString());
    event->setProperty("remote_status", remoteObject == nullptr ? juce::String{} :
        remoteObject->getProperty("status").toString());
    event->setProperty("http_status", response.status);
    event->setProperty("timed_out", response.timedOut);
    event->setProperty("cancelled", response.cancelled);
    event->setProperty("connected", response.connected);
    event->setProperty("incomplete_reason", incomplete == nullptr ? juce::String{} :
        incomplete->getProperty("reason").toString());
    event->setProperty("reused_locally", response.reusedLocally);
    event->setProperty("usage_available", usage != nullptr && !response.reusedLocally);
    event->setProperty("input_tokens", usage == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(usage->getProperty("input_tokens")));
    event->setProperty("cached_input_tokens", inputDetails == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(inputDetails->getProperty("cached_tokens")));
    event->setProperty("cache_write_tokens", inputDetails == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(inputDetails->getProperty("cache_write_tokens")));
    event->setProperty("output_tokens", usage == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(usage->getProperty("output_tokens")));
    event->setProperty("reasoning_tokens", outputDetails == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(outputDetails->getProperty("reasoning_tokens")));
    event->setProperty("total_tokens", usage == nullptr || response.reusedLocally ? 0 :
        static_cast<int>(usage->getProperty("total_tokens")));

    static std::mutex traceMutex;
    const std::scoped_lock lock(traceMutex);
    const juce::File file(juce::String::fromUTF8(destination));
    (void) file.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk()) return;
    stream.setPosition(file.getSize());
    stream.writeText(juce::JSON::toString(juce::var(event), true) + "\n", false, false, "\n");
    stream.flush();
}

#if JUCE_WINDOWS
HttpResponse performSingleRequest(const wchar_t* method, const juce::String& path,
                                  const juce::String& body, const juce::String& apiKey,
                                  std::stop_token token, std::chrono::milliseconds budget) {
    HttpResponse result;
    if (token.stop_requested()) {
        result.cancelled = true;
        return result;
    }

    const auto timeoutMs = std::clamp(static_cast<int>(budget.count()), 1000, 120000);
    const auto session = WinHttpOpen(L"PULSO/0.57.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        result.nativeError = GetLastError();
        return result;
    }
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    const auto connection = WinHttpConnect(session, L"api.openai.com",
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        result.nativeError = GetLastError();
        WinHttpCloseHandle(session);
        return result;
    }
    const auto request = WinHttpOpenRequest(connection, method, path.toWideCharPointer(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        result.nativeError = GetLastError();
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    std::atomic<bool> finished{};
    std::atomic<bool> deadlineReached{};
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::jthread watchdog([&](std::stop_token watchdogToken) {
        while (!watchdogToken.stop_requested() && !finished.load(std::memory_order_acquire)) {
            if (token.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                deadlineReached.store(!token.stop_requested(), std::memory_order_release);
                // Do not close a WinHTTP handle from a second thread while the owning
                // thread is inside WinHttpSendRequest/ReceiveResponse/ReadData. That
                // cross-thread close can block both threads indefinitely. WinHTTP already
                // has bounded connect/send/receive/read timeouts; the watchdog only records
                // the deadline and lets the owning call unwind safely.
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });

    const auto headers = juce::String("Content-Type: application/json\r\nAuthorization: Bearer ") +
                         apiKey + "\r\n";
    const auto utf8Body = body.toUTF8();
    const auto bodyBytes = static_cast<DWORD>(body.isEmpty() ? 0 : utf8Body.sizeInBytes() - 1);
    auto sent = WinHttpSendRequest(request, headers.toWideCharPointer(),
        static_cast<DWORD>(-1L), bodyBytes > 0 ? const_cast<char*>(utf8Body.getAddress()) : nullptr,
        bodyBytes, bodyBytes, 0) != FALSE;
    if (sent) sent = WinHttpReceiveResponse(request, nullptr) != FALSE;
    if (sent) {
        DWORD status{};
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            result.status = static_cast<int>(status);
            result.connected = true;
            juce::MemoryOutputStream response;
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    result.nativeError = GetLastError();
                    result.connected = false;
                    break;
                }
                if (available == 0) break;
                juce::HeapBlock<char> buffer(available);
                DWORD read{};
                if (!WinHttpReadData(request, buffer.getData(), available, &read)) {
                    result.nativeError = GetLastError();
                    result.connected = false;
                    break;
                }
                if (read > 0) response.write(buffer.getData(), read);
            }
            if (result.connected)
                result.body = juce::String::fromUTF8(static_cast<const char*>(response.getData()),
                                                      static_cast<int>(response.getDataSize()));
        } else {
            result.nativeError = GetLastError();
        }
    } else if (!token.stop_requested() && !deadlineReached.load(std::memory_order_acquire)) {
        result.nativeError = GetLastError();
    }

    finished.store(true, std::memory_order_release);
    watchdog.request_stop();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    result.cancelled = token.stop_requested();
    result.timedOut = deadlineReached.load(std::memory_order_acquire);
    return result;
}
#else
HttpResponse performSingleRequest(const char* method, const juce::String& path,
                                  const juce::String& body, const juce::String& apiKey,
                                  std::stop_token token, std::chrono::milliseconds budget) {
    HttpResponse result;
    if (token.stop_requested()) {
        result.cancelled = true;
        return result;
    }

    auto url = juce::URL("https://api.openai.com" + path);
    const bool isPost = juce::String(method) == "POST";
    if (isPost)
        url = url.withPOSTData(body.isEmpty() ? "{}" : body);
    juce::WebInputStream stream(url, isPost);
    stream.withExtraHeaders("Content-Type: application/json\r\nAuthorization: Bearer " + apiKey + "\r\n")
          .withConnectionTimeout(static_cast<int>(budget.count()));

    std::atomic<bool> finished{};
    std::atomic<bool> deadlineReached{};
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::jthread watchdog([&](std::stop_token watchdogToken) {
        while (!watchdogToken.stop_requested() && !finished.load(std::memory_order_acquire)) {
            if (token.stop_requested()) {
                stream.cancel();
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                deadlineReached.store(true, std::memory_order_release);
                stream.cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });

    result.connected = stream.connect(nullptr);
    if (result.connected && !token.stop_requested() &&
        !deadlineReached.load(std::memory_order_acquire)) {
        result.status = stream.getStatusCode();
        result.body = stream.readEntireStreamAsString();
    }
    finished.store(true, std::memory_order_release);
    watchdog.request_stop();
    result.cancelled = token.stop_requested();
    result.timedOut = deadlineReached.load(std::memory_order_acquire);
    return result;
}
#endif

bool responseIdentity(const juce::String& body, juce::String& id, juce::String& status) {
    const auto parsed = juce::JSON::parse(body);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) return false;
    id = object->getProperty("id").toString();
    status = object->getProperty("status").toString().toLowerCase();
    return id.isNotEmpty() && status.isNotEmpty();
}

HttpResponse singleRequest(const bool post, const juce::String& path, const juce::String& body,
                           const juce::String& apiKey, std::stop_token token,
                           std::chrono::milliseconds budget) {
#if JUCE_WINDOWS
    return performSingleRequest(post ? L"POST" : L"GET", path, body, apiKey, token, budget);
#else
    return performSingleRequest(post ? "POST" : "GET", path, body, apiKey, token, budget);
#endif
}

void cancelBackgroundResponse(const juce::String& responseId, const juce::String& apiKey) {
    if (responseId.isEmpty()) return;
    std::stop_source cancellationRequest;
    (void) singleRequest(true, "/v1/responses/" + responseId + "/cancel", "{}", apiKey,
                         cancellationRequest.get_token(), std::chrono::milliseconds(1500));
}

HttpResponse performRequest(const juce::String& body, const juce::String& apiKey,
                            std::stop_token token, std::chrono::milliseconds budget) {
    const auto started = std::chrono::steady_clock::now();
    juce::String knownResponseId;
    const auto finish = [&](HttpResponse result) {
        traceOpenAiRequest(body, result, started, knownResponseId);
        return result;
    };
    if (!token.stop_requested())
        if (const auto cacheFile = validatedResponseCacheFile(body);
            cacheFile && cacheFile->existsAsFile()) {
            HttpResponse cached;
            const auto envelope = juce::JSON::parse(cacheFile->loadFileAsString());
            const auto* entry = envelope.getDynamicObject();
            if (entry != nullptr && entry->getProperty("request").toString() == body)
                cached.body = entry->getProperty("response").toString();
            juce::String cachedId;
            juce::String cachedStatus;
            if (responseIdentity(cached.body, cachedId, cachedStatus) &&
                cachedStatus == "completed" &&
                extractOutputText(juce::JSON::parse(cached.body)).isNotEmpty()) {
                cached.connected = true;
                cached.status = 200;
                cached.reusedLocally = true;
                knownResponseId = cachedId;
                return finish(std::move(cached));
            }
        }
    const auto deadline = started + budget;
    const auto initialBudget = std::min(budget, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::seconds(30)));
    auto response = singleRequest(true, "/v1/responses", body, apiKey, token, initialBudget);
    const auto background = body.contains("\"background\":true");
    if (!background || !response.connected || response.status < 200 || response.status >= 300 ||
        response.cancelled || response.timedOut)
        return finish(std::move(response));

    juce::String responseId;
    juce::String state;
    if (!responseIdentity(response.body, responseId, state)) return finish(std::move(response));
    knownResponseId = responseId;

    int transientFailures{};
    while (state == "queued" || state == "in_progress") {
        for (int elapsed = 0; elapsed < 2000; elapsed += 25) {
            if (token.stop_requested()) {
                cancelBackgroundResponse(responseId, apiKey);
                response.cancelled = true;
                return finish(std::move(response));
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                cancelBackgroundResponse(responseId, apiKey);
                response.timedOut = true;
                return finish(std::move(response));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            cancelBackgroundResponse(responseId, apiKey);
            response.timedOut = true;
            return finish(std::move(response));
        }
        const auto pollBudget = std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::seconds(30)));
        auto polled = singleRequest(false, "/v1/responses/" + responseId, {}, apiKey, token, pollBudget);
        if (polled.cancelled) {
            cancelBackgroundResponse(responseId, apiKey);
            return finish(std::move(polled));
        }
        if (!polled.connected || polled.timedOut) {
            if (++transientFailures < 3) continue;
            return finish(std::move(polled));
        }
        transientFailures = 0;
        response = std::move(polled);
        if (response.status < 200 || response.status >= 300 ||
            !responseIdentity(response.body, responseId, state))
            return finish(std::move(response));
    }
    return finish(std::move(response));
}

juce::String apiErrorMessage(const HttpResponse& response) {
    auto error = response.timedOut ? juce::String("OpenAI request reached its time budget")
               : response.cancelled ? juce::String("Generation cancelled")
               : !response.connected ? juce::String("Could not connect to OpenAI") +
                    (response.nativeError != 0 ? " (transport error " +
                     juce::String(static_cast<int>(response.nativeError)) + ")" : juce::String{})
               : juce::String("OpenAI HTTP ") + juce::String(response.status);
    if (const auto parsed = juce::JSON::parse(response.body); !parsed.isVoid())
        if (const auto* object = parsed.getDynamicObject())
            if (const auto* apiError = object->getProperty("error").getDynamicObject())
                error += ": " + apiError->getProperty("message").toString();
    return error;
}

juce::String structuredResponseError(const juce::String& body,
                                     const juce::String& fallback) {
    const auto parsed = juce::JSON::parse(body);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) return fallback;
    auto result = fallback;
    const auto status = object->getProperty("status").toString();
    if (status.isNotEmpty()) result += " (response status: " + status + ")";
    if (const auto* details = object->getProperty("incomplete_details").getDynamicObject()) {
        const auto reason = details->getProperty("reason").toString();
        if (reason.isNotEmpty()) result += ": " + reason;
    }
    if (const auto* responseError = object->getProperty("error").getDynamicObject()) {
        const auto message = responseError->getProperty("message").toString();
        if (message.isNotEmpty()) result += ": " + message;
    }
    return result;
}

juce::String requestRevisedSongPlan(const juce::String& prompt, const juce::String& apiKey,
                                    std::stop_token token, std::chrono::milliseconds budget) {
    if (token.stop_requested()) return {};
    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"background\":true,\"reasoning\":{\"effort\":\"" + reasoningEffort + "\"},"
        "\"max_output_tokens\":100000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_song_plan_critic\","
        "\"strict\":true,\"schema\":" + songPlanSchema + "}}}";
    const auto response = performRequest(body, apiKey, token, budget);
    if (!response.connected || response.status < 200 || response.status >= 300 ||
        response.cancelled || response.timedOut) return {};
    return extractOutputText(juce::JSON::parse(response.body));
}

void normalizePattern(Pattern& pattern) {
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& left, const auto& right) {
        if (left.startBeat != right.startBeat) return left.startBeat < right.startBeat;
        if (left.channel != right.channel) return left.channel < right.channel;
        return left.pitch < right.pitch;
    });
    pattern.notes.erase(std::unique(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
                            return std::abs(a.startBeat - b.startBeat) < 0.0001 &&
                                   a.channel == b.channel && a.pitch == b.pitch;
                        }), pattern.notes.end());
}

} // namespace

bool AiComposer::hasApiKey() {
    return ApiCredentialStore::hasKey();
}

bool AiComposer::testApiConnection(const juce::String& candidate, std::stop_token token,
                                   juce::String& error) {
    auto apiKey = candidate.trim();
    if (apiKey.isEmpty()) apiKey = ApiCredentialStore::apiKey();
    if (!ApiCredentialStore::isPlausibleKey(apiKey)) {
        error = "The API key format is invalid";
        return false;
    }
#if JUCE_WINDOWS
    const auto response = performSingleRequest(L"GET", L"/v1/models", {}, apiKey, token,
                                               std::chrono::seconds(20));
#else
    const auto response = performSingleRequest("GET", "/v1/models", {}, apiKey, token,
                                               std::chrono::seconds(20));
#endif
    if (token.stop_requested() || response.cancelled) {
        error = "Connection test cancelled";
        return false;
    }
    if (!response.connected) {
        error = response.timedOut ? "OpenAI connection test timed out"
                                  : "Could not connect to OpenAI";
        return false;
    }
    if (response.status >= 200 && response.status < 300) {
        error.clear();
        return true;
    }
    if (response.status == 401) error = "OpenAI rejected the API key";
    else if (response.status == 403) error = "This API key lacks the required project permission";
    else if (response.status == 429) {
        error = "API key authenticated, but the project is currently rate limited";
        return true;
    } else error = "OpenAI connection test returned HTTP " + juce::String(response.status);
    return false;
}

juce::String instrumentBlockBrief(const SongPlan& plan,
                                  const std::vector<std::size_t>& indices,
                                  bool includeMinimums = true) {
    juce::String brief;
    for (const auto index : indices) {
        if (index >= plan.instruments.size()) continue;
        const auto& instrument = plan.instruments[index];
        const auto contract = TrackViability::contractFor(instrument, plan);
        brief << "- id=" << juce::String::fromUTF8(instrument.id.c_str())
              << "; catalog=" << juce::String::fromUTF8(instrument.instrumentId.c_str())
              << "; voice=" << juce::String::fromUTF8(
                    voiceDefinition(instrument.sourceVoice).key.data(),
                    static_cast<int>(voiceDefinition(instrument.sourceVoice).key.size()))
              << "; role=" << juce::String::fromUTF8(instrument.role.c_str())
              << "; register=" << instrument.minimumPitch << ".." << instrument.maximumPitch
              << "; relationship=" << juce::String::fromUTF8(instrument.lineRelationship.c_str());
        if (includeMinimums)
            brief << "; publication_minimums=" << static_cast<int>(contract.minimumNotes)
                  << " rendered notes, " << static_cast<int>(contract.minimumActiveBars)
                  << " active bars, " << static_cast<int>(contract.minimumPhrases) << " phrases";
        brief << "; active_sections=";
        for (const auto& section : instrument.activeSections)
            brief << juce::String::fromUTF8(section.c_str()) << ",";
        brief << "\n";
    }
    return brief;
}

juce::String orchestrationMatrixBrief(const SongPlan& plan,
                                     bool includeTargets = true) {
    juce::String matrix;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto target = std::clamp(static_cast<int>(std::lround(
            4.0 + section.density * 5.0 + section.energy * 2.0)), 4, 11);
        matrix << "- section_index=" << static_cast<int>(sectionIndex)
               << "; name=" << juce::String::fromUTF8(section.name.c_str())
               << "; function=" << juce::String::fromUTF8(section.function.c_str());
        if (includeTargets)
            matrix << "; target_complementary_responsibilities=" << target;
        matrix << "; planned_owners=";
        auto owners = 0;
        for (const auto& instrument : plan.instruments) {
            const auto active = instrument.activeSections.empty() ||
                std::find(instrument.activeSections.begin(), instrument.activeSections.end(),
                          section.name) != instrument.activeSections.end();
            if (!active || ElectronicCompositionFabric::rendererOwnedDestination(plan, instrument))
                continue;
            if (owners++ > 0) matrix << ",";
            matrix << juce::String::fromUTF8(instrument.id.c_str()) << "["
                   << juce::String::fromUTF8(
                        voiceDefinition(instrument.sourceVoice).key.data(),
                        static_cast<int>(voiceDefinition(instrument.sourceVoice).key.size()))
                   << ":" << juce::String::fromUTF8(instrument.orchestralFunction.c_str()) << "]";
        }
        matrix << "; planned_count=" << owners << "\n";
    }
    return matrix;
}

juce::String performanceBlockPrompt(const juce::String& direction,
                                    const juce::String& blueprintJson,
                                    const SongPlan& plan,
                                    const PerformanceScore& acceptedScore,
                                    const std::vector<std::size_t>& indices,
                                    std::size_t blockIndex, int attempt,
                                    bool localEditorial = false) {
    if (localEditorial) {
        juce::String compactBlueprint;
        compactBlueprint << "title=" << juce::String::fromUTF8(plan.title.c_str())
                         << " | key=" << juce::String::fromUTF8(plan.key.c_str())
                         << " | bars=" << plan.totalBars
                         << " | beats_per_bar=" << plan.beatsPerBar
                         << " | protagonist="
                         << juce::String::fromUTF8(
                                plan.narrativeSpine.protagonistInstrumentId.c_str())
                         << "\nMOTIF="
                         << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
                         << "\nHARMONIC_DEBT="
                         << juce::String::fromUTF8(plan.narrativeSpine.harmonicDebt.c_str())
                         << "\nRESOLUTION="
                         << juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str())
                         << "\nCHORD_PALETTE:\n";
        for (const auto& chord : plan.chordPalette) {
            compactBlueprint << juce::String::fromUTF8(chord.id.c_str()) << "="
                             << juce::String::fromUTF8(chord.label.c_str())
                             << " root_pc=" << chord.rootPitchClass
                             << " bass_pc=" << chord.bassPitchClass << " pcs=";
            for (const auto pitchClass : chord.pitchClasses)
                compactBlueprint << pitchClass << ",";
            compactBlueprint << " function="
                             << juce::String(harmonicFunctionKey(chord.function).data())
                             << "\n";
        }
        compactBlueprint << "FORM_AND_HARMONY:\n";
        for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
            const auto& section = plan.sections[sectionIndex];
            compactBlueprint << "section=" << static_cast<int>(sectionIndex) << " "
                             << juce::String::fromUTF8(section.name.c_str())
                             << " start_bar=" << section.startBar << " bars=" << section.bars
                             << " function=" << juce::String::fromUTF8(section.function.c_str())
                             << " harmonic_direction="
                             << juce::String::fromUTF8(section.harmonicDirection.c_str())
                             << " motif="
                             << juce::String::fromUTF8(section.motifTreatment.c_str())
                             << " energy=" << juce::String(section.energy, 2)
                             << " tension=" << juce::String(section.tension, 2)
                             << " density=" << juce::String(section.density, 2) << "\n";
            for (const auto& event : section.harmonicEvents)
                compactBlueprint << "  chord bar=" << event.barOffset
                                 << " beat=" << juce::String(event.beatOffset, 2)
                                 << " id=" << juce::String::fromUTF8(event.chordId.c_str())
                                 << " purpose="
                                 << juce::String::fromUTF8(event.purpose.c_str()) << "\n";
        }
        compactBlueprint << "NARRATIVE_CAUSALITY:\n";
        for (const auto& act : plan.narrativeSpine.acts)
            compactBlueprint << juce::String::fromUTF8(act.sectionName.c_str())
                             << " stage=" << juce::String(narrativeStageKey(act.stage).data())
                             << " cause=" << juce::String::fromUTF8(act.cause.c_str())
                             << " consequence="
                             << juce::String::fromUTF8(act.consequence.c_str())
                             << " unresolved="
                             << juce::String::fromUTF8(act.unresolvedElement.c_str())
                             << " target="
                             << juce::String::fromUTF8(act.resolutionTarget.c_str()) << "\n";
        juce::String prompt;
        prompt << "You are the sole composer of every MIDI note in this PULSO block. "
               "The shared blueprint fixes the song's form, harmony, cast and narrative, but you decide the "
               "actual pitches, chords, voicings, rhythms, silences, articulations and phrase lengths. "
               "Write ONLY the named instrument identities. Give each identity a distinct musical function; "
               "listen conceptually to the accepted ensemble and leave intentional space. Do not copy its "
               "attacks or fill silence mechanically. No numerical note quota is a musical goal: a sustained "
               "pedal may be sparse and a motif may develop through repetition with meaningful change. "
               "Use exact section chords at each placement and avoid semitone or tritone collisions with "
               "simultaneously sounding accepted parts unless the blueprint explicitly asks for that color. "
               "For a polyphonic chord bed, inspect every simultaneous pitch, not merely each pitch class. "
               "Below MIDI 55, do not sustain a minor or major second inside the same voicing; place a "
               "colour tone higher or omit it while retaining a complete playable chord. Bass and pad "
               "must express compatible roots and inversions in the same window. At the absolute ending, "
               "make the last bed, bass and protagonist notes support the declared resolution together. "
               "Give the ending an audible consequence, not just a chord label: let the protagonist "
               "return to a transformed contour from the opening and settle its final attack on the "
               "home tonic when the narrative promises tonic closure. Hold that arrival longer than "
               "the median note of its closing phrase. The protagonist's first premise statement must expose "
               "a memorable four-to-eight-attack nucleus. In the final eight bars, recall at least the first four "
               "attack directions and their recognizable rest pattern; transposition and one consequential interval "
               "change are welcome, but unrelated replacement material is not resolution. End with at least four "
               "connected protagonist attacks, place the last within the final two bars on tonic or a stable terminal "
               "chord tone, and sustain it longer than the phrase median. Let the climax be clearly higher and/or fuller "
               "than the final resolution, then release register and ensemble density in the coda. "
               "A listener should hear the accumulated question answered, not a new unrelated phrase. "
               "Author complete playable MIDI cells and section-relative placements, never prose or empty "
               "placeholder tracks. Treat active_sections as an audible performance contract: every listed "
               "section needs a meaningful statement unless the identity is explicitly a transition or one-shot. "
               "In the climax, the active protagonist and dialogue voices must enact the culmination rather than "
               "leave it entirely to accompaniment; in the resolution, the protagonist must audibly answer its "
               "opening question. Publication minima below are safety floors, never musical targets. "
               "For the primary chord bed, compose the actual harmonic floor rather than one isolated "
               "chord per section: write distinct AI-authored voicing cells for the harmonic states, "
               "then place and repeat those cells across the bars they support. Let chord durations "
               "carry to the next harmonic event or an intentional breath. Revoice the recurring "
               "progression at major narrative turns; do not stack the entire chord palette. "
               "For the movement bass, author a recognizable multi-bar pocket with rests and "
               "approaches, then place its developed versions through every active section; "
               "a single root marker in a section is not a bass performance. For the primary "
               "arpeggiated motion, author several related cells with changed rhythm or contour "
               "and place them in the sections where the story needs motion. The other voices "
               "should complement these anchors, not clone them. In a long hypnotic work, "
               "repetition is a deliberate AI-authored arrangement decision: specify both the "
               "actual notes AND their section-relative placements/repeats; nothing will be "
               "invented to fill gaps later. "
               "IMPORTANT: placement.start_beat is LOCAL to its section, never the "
               "absolute song beat. For a cell beginning at the start of section_index=2, set "
               "start_beat=0 even when two previous sections occupy 64 beats. "
               "fragment_start and fragment_end are LOCAL to the cell: for a 32-beat cell "
               "use fragment_start=0 and fragment_end=32, never the absolute song ending. "
               "Each placed note must satisfy start_beat + note.beat < section bars times beats_per_bar. "
               "Every note must use the exact instrument_id and source_voice, lie inside "
               "its register, and use strict-grid beat coordinates. Cells may recur, but different sections "
               "need musically consequential transformations. If one identity is the protagonist, give it "
               "a recognizable question, development, breathing room and final consequence. "
               "Return only the structured performance_score. Cell ids start with b"
               << static_cast<int>(blockIndex + 1) << "_. Attempt " << attempt
               << ". Original direction: " << direction << "\n"
               << compositionBehaviorBrief(plan.compositionBehavior)
               << "\nINSTRUMENTS IN THIS BLOCK:\n" << instrumentBlockBrief(plan, indices, true)
               << "\nSHARED ORCHESTRATION:\n" << orchestrationMatrixBrief(plan, true)
               << "\nIMMUTABLE COMPACT BLUEPRINT:\n" << compactBlueprint;
        std::set<std::string> targets;
        for (const auto index : indices)
            if (index < plan.instruments.size()) targets.insert(plan.instruments[index].id);
        const auto reference = EnsembleReference::summarize(plan, acceptedScore, targets, 4);
        if (!reference.empty())
            prompt << "\nACCEPTED ENSEMBLE MIDI (absolute beat,pitch,duration; immutable samples):\n"
                   << juce::String::fromUTF8(reference.c_str())
                   << "Compose in dialogue with these real notes, not just the role labels.\n";
        const auto ledger = EnsembleReference::harmonicLedger(
            plan, acceptedScore, targets, 96);
        if (!ledger.empty())
            prompt << "\nACCEPTED HARMONIC SPINE (absolute beat start-end:MIDI pitches; "
                      "complete grouped note events for the principal voices):\n"
                   << juce::String::fromUTF8(ledger.c_str())
                   << "Check the exact vertical intervals against your proposed notes. "
                      "Scale membership alone is insufficient. Keep low sustained semitones "
                      "out of the same chord and resolve deliberate suspensions.\n";
        return prompt;
    }
    const auto protagonistOnly = indices.size() == 1 &&
        indices.front() < plan.instruments.size() &&
        plan.instruments[indices.front()].id ==
            plan.narrativeSpine.protagonistInstrumentId;
    auto prompt = juce::String(
        "You are a PULSO performance orchestrator. The immutable JSON below is the shared song blueprint. "
        "Write MIDI performance cells only for the exact instrument ids listed in THIS BLOCK. Do not redesign the "
        "form, harmony, cast, key or narrative. Every pitch must agree with the blueprint's exact section chord at its "
        "placement. Use strict quarter-note beat coordinates relative to each reusable cell and section-relative "
        "placements. Write complete playable phrases, not token notes: beds and pulses need repeated but evolving "
        "coverage; leads need statement, answer, rests and transformed return; transitions may be rare. "
        "For a persistent pitched lane in a song of at least 64 bars, write at least eight source MIDI notes "
        "(twelve from 128 bars onward) across genuinely different sectional phrases. A four-note source cell "
        "repeated thirty times remains only four authored notes. Constant percussion, one-shots and true "
        "pedal/drone lanes are exempt. One cell may serve related instruments only when its notes retain each "
        "exact instrument_id. Use one principal motif cell per independent lane, with distinct development and "
        "return cells where needed. Placements extend complete phrases; they cannot replace authorship. "
        "The declared protagonist owns the complete leitmotif lineage. A call_response instrument may quote only a "
        "short clue and must answer in negative space with a different rhythmic sentence; harmonic, color, pedal, body "
        "and transition instruments must follow their own chordal or textural trajectories and must not receive copies "
        "of the protagonist cell. Relay and timbral_handoff members share one content lane and never sound the complete "
        "line simultaneously. Give protagonist, answerer and genuinely thematic counterpoint the same non-empty theme_id, "
        "but differentiate their onset grammar and interval contour; related must never mean cloned. The protagonist must "
        "have an authored transformed-return phrase in the complete song's final eight bars, containing at least "
        "four connected attacks; its last attack must occur inside the absolute final two bars and land on a stable pitch of the terminal "
        "harmony. Use the home tonic only when the blueprint explicitly requires tonic closure; suspended, modal and "
        "open endings remain valid creative decisions. Across every section where it is active, the protagonist must "
        "contain AI-written four-to-eight-bar sentences with at least four distinct connected attacks (no adjacent "
        "attack gap above three quarters of a bar) in at least 40 percent of eligible " ) +
        juce::String(plan.compositionBehavior == CompositionBehavior::Hypnotic ?
            "sixteen-bar states" : "eight-bar windows") + juce::String(
        ". Isolated marker "
        "notes never count as a phrase. "
        "Silence inside and between those phrases is welcome: ensemble continuity belongs to independent harmonic, "
        "atmospheric and movement voices, never to a permanently talking lead. Across four "
        "or more appearances, no single literal cell/placement state may own more than 70 percent of returns: write at "
        "least an anchor and a materially transformed statement through rhythm, contour, fragmentation or cadence. A declared "
        "primary non-percussive motion owner, identified by primary_motion_owner in its role, "
        "must receive its own evolving GPT-authored cell and placements. Instruments identified as supporting_motion "
        "remain independent complementary movement and must receive distinct cells rather than copies; the local "
        "renderer will not write principal, response or motion material for you. "
        "A movement_bass owner must write connected four-to-eight-bar pocket phrases with a real breath and at least one "
        "developed return; a percussion-free electronic arrangement still needs pitched low-end motion unless the user "
        "explicitly excluded bass. Melodic speakers must connect roughly 25-65 percent of adjacent within-phrase attacks "
        "by one or two semitones, using characteristic leaps as punctuation followed by a stepwise consequence rather than interval roulette; "
        "do not turn that connective tissue into an uninterrupted scale run. Every independent instrument must meet its publication_minimums after placement repetitions are rendered; later "
        "stages will neither develop nor merge an incomplete independent AI line. Regular instruments must appear in at "
        "least two structurally different sections; one_shot/transition material may be rare. "
        "The instrument whose role contains primary_chord_bed owns the listener's explicit chord track. It must be a "
        "self-contained polyphonic MIDI lane: at important harmonic changes write three-to-five distinct simultaneous "
        "pitches from the exact chord, use inversions and economical common-tone voice leading, and create recognisable "
        "sectional voicing states. Do not satisfy this by assigning one chord tone to several different tracks. The bed "
        "must carry premise, development, climax and the absolute final stage with distinct voicings; this is a narrative "
        "arc, not permission to sound continuously. The bed "
        "must withdraw for at least one intentional two-bar breath in a long form unless its role explicitly says constant "
        "or continuous; another independent harmonic layer may retain tonal memory during that breath. "
        "Rhythm motifs in the shared "
        "blueprint are context, not a substitute for this block: every assigned drum or percussion identity must still own "
        "at least one explicit note event with its exact instrument_id and a valid placement. "
        "A phrase minimum means distinct musical statements, not the same cell copied in every placement: for any lane "
        "longer than 64 bars, author at least three recognisably different 4-to-8-bar phrases, transform material every "
        "16-to-32 bars through contour, rhythm, register, harmony or orchestration, and leave at least one intentional "
        "phrase-level breath before each major return. A repeated ostinato counts as one phrase until it is genuinely "
        "varied. A persistent motion, inner or chordal lane needs three to six recognisable sectional phrase states across "
        "the complete form according to its role; transposing the same phrase does not count, but hypnotic repetition inside "
        "a state is intentional and valid. Independent lanes must not reproduce the exact MIDI pitch and onset of more "
        "than four fifths of the shorter line. Shared chord tones, coordinated cadential attacks and complementary voicings "
        "are normal orchestration, not duplication. Pads may sustain, but their entrances, inversions and releases must evolve. "
        "In a long-form cast of twelve or more parts, author at least three audible conversational lines across the form: "
        "one motif-derived answerer plus two genuinely independent counterlines, harmonic replies or register-separated "
        "responses. They must exchange phrases in negative space and may rotate by section; never satisfy this by stacking "
        "three simultaneous copies of the protagonist. "
        "The climax/hook section must sound categorically larger than its setup: add at least two independent authored "
        "foreground or harmonic lines, lift one important voice by register, introduce a new rhythmic/arpeggio grammar, "
        "and make the protagonist's hook arrive with a changed contour or cadence. Do not satisfy this by duplicating one "
        "cell across timbral destinations. In the breakdown, remove at least two active responsibilities and leave a "
        "sparse hook fragment plus harmonic memory; on the return, restore the hook with a changed timbre, register or "
        "voicing so the listener can hear the form's consequence. "
        "For relay, timbral_handoff, doubling and octave_reinforcement members, write the shared content owner only; "
        "do not spend output reproducing the same phrase for every destination. PULSO distributes complete phrase "
        "segments across every declared timbral destination after the global performance is assembled. The normalized "
        "INSTRUMENTS IN THIS BLOCK contract overrides stale content-lane labels in the immutable JSON blueprint. "
        "A transition is boundary "
        "punctuation, never the recurring motor: keep each transition within one-to-three-bar windows around structural "
        "changes and below one eighth of the complete form. Keep each note inside the declared register and use only the declared source_voice for that "
        "instrument. Return no instrument outside this block. Cell ids must start with b") +
        juce::String(static_cast<int>(blockIndex + 1)) + "_. Attempt " + juce::String(attempt) +
        ". Original direction: " + direction + "\n" +
        compositionBehaviorBrief(plan.compositionBehavior) +
        "\nINSTRUMENTS IN THIS BLOCK:\n" + instrumentBlockBrief(plan, indices) +
        "\nGLOBAL ORCHESTRATION MATRIX (authoritative for every block):\n" +
        orchestrationMatrixBrief(plan) +
        "Every planned owner in a section must contribute its declared independent responsibility there unless the "
        "matrix deliberately marks a breath through the section's low target. Coordinate with owners outside this block: "
        "write negative-space answers, inner motion, inversions, pedals, ostinati or evolving texture according to role; "
        "do not merely duplicate their attacks. In sections longer than sixteen bars, stagger at least two supporting "
        "owners through an exit and later re-entry, and place an audible ensemble breath before the consequential return; "
        "foundations may overlap that breath but may not turn it into an unchanging tutti plateau. A track may rest, but the ensemble must meet each section target through "
        "complementary ideas. Treat missing sectional participation as incomplete orchestration, not intentional silence.\n" +
        (protagonistOnly ? juce::String(
            "PROTAGONIST-FIRST CONTRACT: this request is deliberately isolated before the ensemble is written. "
            "Return this exact identity with multiple complete four-to-eight-bar melodic sentences, each containing "
            "at least four connected distinct attacks; cover premise, development, climax and the absolute final "
            "eight bars. The final sentence must end inside the song's final two bars on the declared terminal stable "
            "harmony. Isolated cue notes, one-note markers, empty cells and prose are invalid. Build recognisable "
            "motivic identity, breath, consequence and transformed recall now; no later lane may substitute for it.\n")
            : juce::String()) +
        "\nIMMUTABLE SHARED BLUEPRINT:\n" + blueprintJson;
    std::set<std::string> targets;
    for (const auto index : indices)
        if (index < plan.instruments.size()) targets.insert(plan.instruments[index].id);
    const auto reference = EnsembleReference::summarize(plan, acceptedScore, targets);
    if (!reference.empty())
        prompt += "\nACCEPTED ENSEMBLE MIDI (absolute beat,pitch,duration; immutable, sampled across the whole form):\n" +
            juce::String::fromUTF8(reference.c_str()) +
            "New material must answer, support or contrast these actual attacks and rests. "
            "Do not copy an existing lane's complete line or fill all of its negative space. "
            "These landmarks are a bounded reference, not the full MIDI; the shared blueprint remains authoritative.\n";
    return prompt;
}

juce::String jointWindowPrompt(const juce::String& direction, const SongPlan& plan,
                               const Pattern& accepted,
                               const std::vector<std::size_t>& owners,
                               std::size_t sectionIndex, int firstBar, int bars,
                               const juce::String& feedback, bool focusedRevision = false) {
    const auto& section = plan.sections[sectionIndex];
    const auto start = (section.startBar + firstBar) * plan.beatsPerBar;
    const auto end = start + bars * plan.beatsPerBar;
    const auto localStart = firstBar * plan.beatsPerBar;
    const auto localEnd = localStart + bars * plan.beatsPerBar;
    juce::String prompt;
    prompt << "Compose one complete ensemble passage for PULSO. You author every note, "
        "voicing, phrase, rest and repetition. This is one window of a longer song, "
        "not a separate loop or a collection of isolated solos. Keep the accepted "
        "previous MIDI immutable. Make the harmonic bed and bass establish the "
        "section's gravity, then let protagonist, arpeggio, texture and answers "
        "interlock around it. A lead may rest, but harmonic memory must normally "
        "remain audible; a breakdown may deliberately thin the ensemble. "
        "Choose several complementary 4-to-8-bar phrases. The protagonist must "
        "state a short recognisable PITCH-AND-RHYTHM question, not merely follow "
        "the chord roots; its later answer must transform that exact figure. "
        "Let the protagonist speak in complete, varied phrases with a felt "
        "arrival, not just isolated three-note tags separated by many bars. "
        "The response voice should answer or contradict a specific protagonist "
        "gesture, rather than restating its pitches as decorative fill. "
        "An arpeggio is a supporting kinetic layer, not a permanent substitute "
        "for melodic development: let it rest, change contour and register, "
        "or yield to the other voices when the story asks for space. "
        "Texture should evolve as a real complementary line or held colour, "
        "not merely repeat one isolated pitch at every bar line. "
        "The harmonic bed must voice at least a triad or a deliberate two-note "
        "shell over each harmonic event, above the bass register. Bass structural "
        "attacks must agree with the current event's bass pitch class; passing "
        "notes must be brief and resolve. Sustained chord tones must belong to "
        "the current chord, end at its next boundary, and must not form long "
        "semitone or whole-tone clashes with another sustained note. Chord "
        "membership alone does not make a low-register voicing safe: keep "
        "sevenths, flat ninths and other close colour tones well above a "
        "bass root, or leave them out of that attack. When a dominant chord "
        "repeats before resolving, sustain its functional leading tone as a "
        "planned debt; its real resolution belongs at the later tonic, not "
        "at the repeated dominant event. Account "
        "for what ALL instruments sound at once, including repeated cells. "
        "Write notes ONLY for the listed instrument IDs. Already accepted "
        "parts of this same window are immutable and must not be returned. "
        "Every listed instrument needs audible authored material within its "
        "declared active sections; a timbral handoff still needs its own MIDI. "
        "Choose several complementary 4-to-8-bar phrases. Write their actual "
        "MIDI note attacks directly, including intentional repetitions; develop "
        "voicing, rhythm or contour when the harmony or "
        "narrative changes. Do not leave a whole section represented by one chord "
        "or one bass root. Repetition can create hypnosis, but exact unchanging "
        "repetition across the entire form cannot create a story. "
        "Every note.beat is LOCAL to THIS WINDOW, starting at zero. Its end "
        "(beat+duration) must be at or before " << bars * plan.beatsPerBar
        << ". This window starts at section-local beat " << localStart
        << " and ends at " << localEnd << ". No note may cross a window "
        "or harmonic-event boundary. Do not output cells, placements, invented "
        "instruments or commentary. "
        "Use only the listed instrument IDs and source_voice. Their declared "
        "registers are preferred, with up to one octave of expressive latitude; "
        "strict quarter-beat grid, positive durations, no drums when excluded. "
        "Return direct MIDI notes grouped under each instrument ID. Every ID "
        "must be present as an array; an intentionally resting instrument "
        "has an empty array. Do not repeat instrument_id on individual notes.\n"
        "USER DIRECTION: " << direction << "\n"
        "GLOBAL IDENTITY: " << juce::String::fromUTF8(plan.key.c_str())
        << "; motif=" << juce::String::fromUTF8(
            plan.narrativeSpine.motifIdentity.c_str())
        << "; harmonic_debt=" << juce::String::fromUTF8(
            plan.narrativeSpine.harmonicDebt.c_str())
        << "; resolution=" << juce::String::fromUTF8(
            plan.narrativeSpine.resolution.c_str()) << "\n"
        "SECTION " << static_cast<int>(sectionIndex) << " "
        << juce::String::fromUTF8(section.name.c_str()) << " | function="
        << juce::String::fromUTF8(section.function.c_str()) << " | motif treatment="
        << juce::String::fromUTF8(section.motifTreatment.c_str())
        << " | density=" << juce::String(section.density, 2)
        << " | tension=" << juce::String(section.tension, 2)
        << " | absolute beats [" << start << "," << end << ")\n"
        "INSTRUMENT RESPONSIBILITIES:\n";
    for (const auto index : owners) {
        const auto& instrument = plan.instruments[index];
        const auto active = instrument.activeSections.empty() ||
            std::find(instrument.activeSections.begin(),
                      instrument.activeSections.end(), section.name) !=
                instrument.activeSections.end();
        const auto priorNotes = std::count_if(accepted.notes.begin(), accepted.notes.end(),
            [&](const auto& note) { return note.partId == index + 1; });
        prompt << juce::String::fromUTF8(instrument.id.c_str()) << " | "
            << juce::String::fromUTF8(instrument.role.c_str()) << " | voice="
            << juce::String(voiceDefinition(instrument.sourceVoice).key.data())
            << " | register=" << instrument.minimumPitch << "-"
            << instrument.maximumPitch << " | prior audible notes=" << priorNotes
            << " | this section="
            << (active ? "ACTIVE: write at least one audible note" :
                "WITHDRAWN: do not write notes") << "\n";
    }
    prompt << "CHORD PALETTE:\n";
    for (const auto& chord : plan.chordPalette) {
        prompt << juce::String::fromUTF8(chord.id.c_str()) << "="
            << juce::String::fromUTF8(chord.label.c_str()) << " pitches=";
        for (const auto pitch : chord.pitchClasses) prompt << pitch << ",";
        prompt << " bass=" << chord.bassPitchClass << "\n";
    }
    prompt << "HARMONIC EVENTS IN THIS SECTION (section-local beats; the active chord "
        "lasts until the next event, including across section boundaries):\n";
    for (const auto& event : section.harmonicEvents) {
        const auto beat = event.barOffset * plan.beatsPerBar + event.beatOffset;
        if (beat > localEnd + .001) break;
        if (beat < localStart - .001 &&
            std::any_of(section.harmonicEvents.begin(), section.harmonicEvents.end(),
                [&](const auto& later) {
                    const auto laterBeat = later.barOffset * plan.beatsPerBar + later.beatOffset;
                    return laterBeat > beat && laterBeat <= localStart;
                })) continue;
        prompt << beat << " " << juce::String::fromUTF8(event.chordId.c_str())
            << " " << juce::String::fromUTF8(event.purpose.c_str()) << "\n";
    }
    struct EventReference {
        double beat{};
        const HarmonicChord* chord{};
    };
    std::vector<EventReference> timeline;
    for (const auto& plannedSection : plan.sections)
        for (const auto& event : plannedSection.harmonicEvents) {
            const auto chord = std::find_if(plan.chordPalette.begin(),
                plan.chordPalette.end(), [&](const auto& item) {
                    return item.id == event.chordId;
                });
            if (chord != plan.chordPalette.end())
                timeline.push_back({(plannedSection.startBar + event.barOffset) *
                    plan.beatsPerBar + event.beatOffset, &*chord});
        }
    std::stable_sort(timeline.begin(), timeline.end(), [](const auto& left,
                                                          const auto& right) {
        return left.beat < right.beat;
    });
    prompt << "EXACT CHORD WINDOWS FOR THIS REQUEST (WINDOW-LOCAL start-end beats; "
        "sustained notes must end before the next boundary):\n";
    for (std::size_t index = 0; index < timeline.size(); ++index) {
        const auto eventStart = timeline[index].beat;
        const auto eventEnd = index + 1 < timeline.size() ? timeline[index + 1].beat :
            plan.totalBars * plan.beatsPerBar;
        if (eventEnd <= start + .001 || eventStart >= end - .001) continue;
        const auto relativeStart = std::max(start, eventStart) - start;
        const auto relativeEnd = std::min(end, eventEnd) - start;
        prompt << juce::String(relativeStart, 2) << "-"
            << juce::String(relativeEnd, 2) << " "
            << juce::String::fromUTF8(timeline[index].chord->label.c_str())
            << " | allowed sustained pitch classes=";
        for (const auto pitchClass : timeline[index].chord->pitchClasses)
            prompt << pitchClass << ",";
        prompt << " | structural bass pitch class="
            << timeline[index].chord->bassPitchClass << "\n";
    }
    prompt << "ACCEPTED MIDI IN THIS WINDOW (absolute beat,part,pitch,duration; "
        "immutable; place new notes in harmonic and rhythmic relation to these):\n";
    auto currentListed = 0;
    for (const auto& note : accepted.notes) {
        if (note.startBeat < start || note.startBeat >= end || currentListed >= 192)
            continue;
        prompt << note.startBeat << "," << note.partId << "," << note.pitch
            << "," << juce::String(note.durationBeats, 2) << "; ";
        ++currentListed;
    }
    prompt << "ACCEPTED LEAD-IN MIDI (absolute beat,part,pitch,duration; immutable):\n";
    auto listed = 0;
    for (const auto& note : accepted.notes) {
        if (note.startBeat < start - plan.beatsPerBar * 2 ||
            note.startBeat >= start || listed >= 48) continue;
        prompt << note.startBeat << "," << note.partId << "," << note.pitch
            << "," << juce::String(note.durationBeats, 2) << "; ";
        ++listed;
    }
    const auto protagonist = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& item) { return item.id == plan.narrativeSpine.protagonistInstrumentId; });
    if (protagonist != plan.instruments.end() && start > 0.0) {
        const auto protagonistPart = static_cast<std::uint16_t>(
            std::distance(plan.instruments.begin(), protagonist) + 1);
        prompt << "\nOPENING PROTAGONIST MOTIF (absolute beat,pitch,duration; immutable; "
            "recognise and transform its pitch/rhythm, do not copy blindly):\n";
        auto motifNotes = 0;
        for (const auto& note : accepted.notes) {
            if (note.partId != protagonistPart || note.startBeat >= start ||
                note.startBeat >= plan.beatsPerBar * 8 || motifNotes >= 24) continue;
            prompt << note.startBeat << "," << note.pitch << ","
                << juce::String(note.durationBeats, 2) << "; ";
            ++motifNotes;
        }
        if (motifNotes == 0) prompt << "No pitched opening motif has been authored yet; "
            "make this window's protagonist establish one now.";
    }
    prompt << "\nContinue the same motif and chord trajectory; close this passage so "
        "the next window can inherit a meaningful question or consequence.\n";
    if (end >= plan.totalBars * plan.beatsPerBar - .001)
        prompt << "This is the absolute ending: the protagonist's transformed motif, "
            "bass and chord bed must reach one audible, prepared consequence. "
            "Place the final attack inside the last two bars and let the arrival "
            "sustain; do not merely label a cadence in prose. Let the supporting "
            "arpeggio and response yield around that arrival so register and "
            "density release audibly instead of every part continuing at full "
            "activity to the last subdivision.\n";
    if (feedback.isNotEmpty()) {
        prompt << "PREVIOUS WINDOW ATTEMPT FAILED: " << feedback;
        if (focusedRevision)
            prompt << " The other instruments from that attempt are now accepted MIDI "
                "above. Revise ONLY the listed instrument against those exact sounding "
                "notes. Do not reproduce the listed pitch collision at the listed "
                "beat and duration: choose a different chord tone/register or a "
                "musically deliberate rest there. Preserve the useful phrase "
                "identity elsewhere; do not rewrite or return accepted parts.\n";
        else
            prompt << " Rewrite this instrument group coherently.\n";
    }
    return prompt;
}

juce::String directWindowSchema(const SongPlan& plan,
                                const std::vector<std::size_t>& owners) {
    auto schema = juce::JSON::parse(R"json({
      "type":"object","properties":{},"required":[],
      "additionalProperties":false})json");
    auto* root = schema.getDynamicObject();
    auto* properties = root->getProperty("properties").getDynamicObject();
    juce::Array<juce::var> required;
    for (const auto index : owners) {
        if (index >= plan.instruments.size()) continue;
        const auto& owner = plan.instruments[index];
        const auto id = juce::String::fromUTF8(owner.id.c_str());
        auto part = juce::JSON::parse(R"json({
          "type":"array","maxItems":256,"items":{"type":"object",
          "properties":{"beat":{"type":"number"},
          "duration":{"type":"number"},"pitch":{"type":"integer"},
          "velocity":{"type":"integer"}},
          "required":["beat","duration","pitch","velocity"],
          "additionalProperties":false}})json");
        part.getDynamicObject()->setProperty("description",
            "Direct MIDI notes for " + id + "; preferred pitches " +
            juce::String(owner.minimumPitch) + " through " +
            juce::String(owner.maximumPitch));
        properties->setProperty(id, part);
        required.add(id);
    }
    root->setProperty("required", required);
    return juce::JSON::toString(schema, true);
}

bool parseDirectWindow(const juce::String& text, const SongPlan& plan,
                       const std::vector<std::size_t>& owners,
                       std::size_t sectionIndex, int firstBar, int bars,
                       PerformanceScore& score, juce::String& error) {
    score = {};
    const auto parsed = juce::JSON::parse(text);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr) {
        error = "Direct ensemble window returned invalid structured MIDI";
        return false;
    }
    const auto length = bars * plan.beatsPerBar;
    std::map<std::size_t, PerformanceCell> byOwner;
    for (const auto index : owners) {
        if (index >= plan.instruments.size()) continue;
        const auto& owner = plan.instruments[index];
        const auto id = juce::String::fromUTF8(owner.id.c_str());
        const auto* notes = root->getProperty(id).getArray();
        if (notes == nullptr) {
            error = "Direct ensemble window omitted instrument array: " + id;
            return false;
        }
        for (const auto& item : *notes) {
        const auto* object = item.getDynamicObject();
        if (object == nullptr) {
            error = "Direct ensemble window has an invalid note for " + id;
            return false;
        }
        const auto beat = static_cast<double>(object->getProperty("beat"));
        const auto duration = static_cast<double>(object->getProperty("duration"));
        const auto pitch = static_cast<int>(object->getProperty("pitch"));
        const auto velocity = static_cast<int>(object->getProperty("velocity"));
        if (!std::isfinite(beat) || !std::isfinite(duration) || beat < 0.0 ||
            duration <= 0.0 || beat + duration > length + .001 ||
            std::abs(beat * 4.0 - std::round(beat * 4.0)) > .001 ||
            std::abs(duration * 4.0 - std::round(duration * 4.0)) > .001 ||
            pitch < std::max(0, owner.minimumPitch - 12) ||
            pitch > std::min(127, owner.maximumPitch + 12) ||
            velocity < 1 || velocity > 127) {
            error = "Direct ensemble note invalid for " + id + ": beat=" +
                juce::String(beat, 2) + " duration=" + juce::String(duration, 2) +
                " end=" + juce::String(beat + duration, 2) +
                " (window end=" + juce::String(length, 2) + "), pitch=" +
                juce::String(pitch) + " (preferred range " +
                juce::String(owner.minimumPitch) + ".." +
                juce::String(owner.maximumPitch) +
                ", permissible within one octave), velocity=" +
                juce::String(velocity) + ". Use quarter-beat values; no clipping is applied.";
            return false;
        }
        auto& cell = byOwner[index];
        if (cell.id.empty()) {
            cell.id = "w" + std::to_string(sectionIndex) + "_" +
                std::to_string(firstBar) + "_" + std::to_string(index);
            cell.lengthBeats = length;
            cell.ownedVoices = {owner.sourceVoice};
            cell.narrativeFunction = "develop";
            if (owner.id == plan.narrativeSpine.protagonistInstrumentId ||
                owner.lineRelationship == "call_response") cell.themeId = "main_motif";
        }
        cell.notes.push_back({beat, duration, pitch, velocity, owner.sourceVoice,
                              MetricIntent::StrictGrid, owner.id});
        }
    }
    if (byOwner.empty()) {
        error = "Direct ensemble window has no authored notes in any instrument";
        return false;
    }
    for (auto& [index, cell] : byOwner) {
        PerformancePlacement placement;
        placement.cellId = cell.id;
        placement.sectionIndex = static_cast<int>(sectionIndex);
        placement.startBeat = firstBar * plan.beatsPerBar;
        placement.purpose = "AI-authored direct ensemble performance";
        score.cells.push_back(std::move(cell));
        score.placements.push_back(std::move(placement));
    }
    return true;
}

struct DirectPitchTarget {
    std::string cellId;
    std::size_t noteIndex{};
    std::size_t ownerIndex{};
    double absoluteBeat{};
    int originalPitch{};
    std::string id;
};

std::vector<DirectPitchTarget> directPitchTargets(
    const PerformanceScore& score, const SongPlan& plan,
    const TonalAuditReport& tonal, const std::vector<std::size_t>& owners,
    std::size_t sectionIndex, int firstBar, int bars) {
    std::vector<DirectPitchTarget> targets;
    const auto windowStart = (plan.sections[sectionIndex].startBar + firstBar) *
        plan.beatsPerBar;
    const auto windowEnd = windowStart + bars * plan.beatsPerBar;
    for (const auto& issue : tonal.issues) {
        if (issue.beat < windowStart - .001 || issue.beat >= windowEnd - .001 ||
            !((issue.kind == "harsh_overlap" && issue.overlapBeats >= .5 - .001) ||
              issue.kind == "unsupported_chromatic" ||
              issue.kind == "strong_non_chord")) continue;
        for (const auto owner : owners) {
            const auto part = static_cast<std::uint16_t>(owner + 1);
            const auto primary = issue.partId == part;
            const auto secondary = issue.kind == "harsh_overlap" &&
                issue.otherPartId == part;
            if (!primary && !secondary) continue;
            const auto expectedCell = "w" + std::to_string(sectionIndex) + "_" +
                std::to_string(firstBar) + "_" + std::to_string(owner);
            const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
                [&](const auto& item) { return item.id == expectedCell; });
            if (cell == score.cells.end()) continue;
            const auto pitch = primary ? issue.pitch : issue.otherPitch;
            for (std::size_t index = 0; index < cell->notes.size(); ++index) {
                const auto& note = cell->notes[index];
                const auto start = windowStart + note.beat;
                if (note.pitch != pitch || start > issue.beat + .001 ||
                    start + note.durationBeats <= issue.beat + .001) continue;
                if (std::none_of(targets.begin(), targets.end(), [&](const auto& target) {
                        return target.cellId == cell->id && target.noteIndex == index;
                    }))
                    targets.push_back({cell->id, index, owner, start, pitch,
                        "n" + std::to_string(targets.size() + 1)});
                break;
            }
            // The first movable sounding note is the target. A second owner
            // may also be implicated, but the resulting full ensemble is
            // re-audited before any patch can be accepted.
            break;
        }
    }
    return targets;
}

bool applyDirectPitchRevision(const juce::String& text,
                              const std::vector<DirectPitchTarget>& targets,
                              const SongPlan& plan, PerformanceScore& score) {
    const auto parsed = juce::JSON::parse(text);
    const auto* root = parsed.getDynamicObject();
    const auto* patches = root == nullptr ? nullptr :
        root->getProperty("patches").getArray();
    if (patches == nullptr || patches->size() != static_cast<int>(targets.size()))
        return false;
    std::set<std::string> seen;
    auto changed = false;
    for (const auto& patch : *patches) {
        const auto* object = patch.getDynamicObject();
        if (object == nullptr || !object->getProperty("pitch").isInt()) return false;
        const auto id = object->getProperty("target_id").toString().toStdString();
        const auto target = std::find_if(targets.begin(), targets.end(),
            [&](const auto& item) { return item.id == id; });
        if (target == targets.end() || !seen.insert(id).second) return false;
        const auto pitch = static_cast<int>(object->getProperty("pitch"));
        const auto& owner = plan.instruments[target->ownerIndex];
        if (pitch < std::max(0, owner.minimumPitch - 12) ||
            pitch > std::min(127, owner.maximumPitch + 12)) return false;
        const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
            [&](const auto& item) { return item.id == target->cellId; });
        if (cell == score.cells.end() || target->noteIndex >= cell->notes.size() ||
            cell->notes[target->noteIndex].pitch != target->originalPitch)
            return false;
        changed = changed || pitch != target->originalPitch;
        cell->notes[target->noteIndex].pitch = pitch;
    }
    return changed;
}

std::vector<bool> renderedOwnerBars(const SongPlan& plan,
                                    const PerformanceScore& score,
                                    std::size_t instrumentIndex) {
    std::vector<bool> occupied(static_cast<std::size_t>(std::max(0, plan.totalBars)), false);
    if (instrumentIndex >= plan.instruments.size() || plan.beatsPerBar <= 0.0)
        return occupied;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        Pattern chunk;
        chunk.lengthBeats = section.bars * plan.beatsPerBar;
        PerformanceScoreEngine::replaceChunk(chunk, score, static_cast<int>(sectionIndex),
                                             0.0, chunk.lengthBeats, plan.instruments);
        for (const auto& note : chunk.notes) {
            if (note.partId != instrumentIndex + 1) continue;
            const auto first = section.startBar + static_cast<int>(std::floor(
                note.startBeat / plan.beatsPerBar));
            const auto last = section.startBar + static_cast<int>(std::floor(
                std::max(note.startBeat, note.endBeat() - .001) / plan.beatsPerBar));
            for (auto bar = first; bar <= last; ++bar)
                if (bar >= 0 && static_cast<std::size_t>(bar) < occupied.size())
                    occupied[static_cast<std::size_t>(bar)] = true;
        }
    }
    return occupied;
}

juce::String focusedProtagonistCoveragePrompt(const juce::String& direction,
                                              const SongPlan& plan,
                                              const PerformanceScore& score,
                                              const PerformanceCoverageDeficit& deficit) {
    const auto& owner = plan.instruments[deficit.instrumentIndex];
    const auto occupied = renderedOwnerBars(plan, score, deficit.instrumentIndex);
    const auto missingBars = deficit.minimumActiveBars - deficit.activeBars;
    juce::String prompt;
    prompt << "You are repairing ONLY the missing active-bar coverage of one already-authored melodic protagonist. "
        "Return a SMALL additive performance_score in the required JSON schema: two to four new, distinct "
        "four-to-eight-bar phrases with their own cells and section-relative placements. Add notes in at least "
        << static_cast<int>(missingBars) << " previously silent bars, preferably "
        << static_cast<int>(missingBars + 2) << " to provide a small margin. Existing cells, placements, "
        "motif, coda, section form and every other instrument are immutable. Do not rewrite or repeat the "
        "whole protagonist, fill every silence, clone another part, or return only prose. Leave meaningful "
        "gaps between statements. Give each new cell a unique id and the same theme_id as the accepted motif. "
        "Use the exact instrument_id and source_voice below, strict_grid timing, MIDI pitches inside its register, "
        "and section-relative placement start_beat. Each phrase must follow the exact harmonic event active "
        "at its position. Every required JSON field must be present, including empty controls and voice_map "
        "arrays. Return only the structured JSON.\n"
        "OWNER:\n" + instrumentBlockBrief(plan, {deficit.instrumentIndex}) +
        "KEY=" + juce::String::fromUTF8(plan.key.c_str()) +
        " root_pc=" + juce::String(plan.rootPitchClass) +
        " beats_per_bar=" + juce::String(plan.beatsPerBar, 2) +
        " total_bars=" + juce::String(plan.totalBars) +
        " accepted_active_bars=" + juce::String(static_cast<int>(deficit.activeBars)) +
        " required_active_bars=" + juce::String(static_cast<int>(deficit.minimumActiveBars)) +
        "\nMOTIF=" + juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str()) +
        "\nORIGINAL DIRECTION=" + direction.substring(0, 500) + "\n";
    for (const auto& chord : plan.chordPalette) {
        prompt << "CHORD " << juce::String::fromUTF8(chord.id.c_str())
               << " root_pc=" << chord.rootPitchClass << " pitch_classes=";
        for (const auto pitchClass : chord.pitchClasses) prompt << pitchClass << ",";
        prompt << "\n";
    }
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto assigned = owner.activeSections.empty() ||
            std::find(owner.activeSections.begin(), owner.activeSections.end(),
                      section.name) != owner.activeSections.end();
        if (!assigned) continue;
        prompt << "SECTION " << static_cast<int>(sectionIndex) << " "
               << juce::String::fromUTF8(section.name.c_str())
               << " absolute_start_bar=" << section.startBar
               << " bars=" << section.bars << " chord_events=";
        for (const auto& event : section.harmonicEvents)
            prompt << juce::String::fromUTF8(event.chordId.c_str()) << "@local_bar_"
                   << event.barOffset << ":beat_" << juce::String(event.beatOffset, 2) << ",";
        prompt << "\n";
        for (auto localBar = 0; localBar < section.bars;) {
            const auto absoluteBar = section.startBar + localBar;
            if (absoluteBar < 0 || static_cast<std::size_t>(absoluteBar) >= occupied.size() ||
                occupied[static_cast<std::size_t>(absoluteBar)]) {
                ++localBar;
                continue;
            }
            const auto start = localBar;
            while (localBar < section.bars) {
                const auto bar = section.startBar + localBar;
                if (bar < 0 || static_cast<std::size_t>(bar) >= occupied.size() ||
                    occupied[static_cast<std::size_t>(bar)]) break;
                ++localBar;
            }
            if (localBar - start >= 4)
                prompt << "EMPTY_RANGE section_index=" << static_cast<int>(sectionIndex)
                       << " local_bars=[" << start << "," << localBar
                       << ") start_beat=" << juce::String(start * plan.beatsPerBar, 2)
                       << "\n";
        }
    }
    prompt << "ACCEPTED MOTIF FRAGMENTS (reference only; do not output these cells):\n";
    auto shownCells = 0;
    for (const auto& cell : score.cells) {
        if (shownCells >= 4) break;
        auto shownNotes = 0;
        juce::String fragment;
        for (const auto& note : cell.notes) {
            if (note.instrumentId != owner.id) continue;
            if (shownNotes++ < 8)
                fragment << "(" << juce::String(note.beat, 2) << "," << note.pitch << ","
                         << juce::String(note.durationBeats, 2) << ")";
        }
        if (shownNotes == 0) continue;
        prompt << "theme_id=" << juce::String::fromUTF8(cell.themeId.c_str())
               << " phrase=" << fragment << "\n";
        ++shownCells;
    }
    return prompt;
}

struct PerformanceRoutingReport {
    std::map<std::string, std::size_t> received;
    std::map<std::string, std::size_t> accepted;
    std::map<std::string, std::size_t> reassigned;
    std::map<std::string, std::size_t> discarded;
    std::size_t foreignNotes{};
    std::size_t unknownInstrumentNotes{};
    std::size_t absolutePlacementsRelocalized{};

    [[nodiscard]] juce::String summary(const std::set<std::string>& assignedIds) const {
        juce::String text;
        for (const auto& id : assignedIds) {
            if (text.isNotEmpty()) text << "; ";
            const auto count = [&](const auto& values) {
                const auto found = values.find(id);
                return found == values.end() ? std::size_t{} : found->second;
            };
            text << juce::String::fromUTF8(id.c_str())
                 << " received=" << static_cast<int>(count(received))
                 << " accepted=" << static_cast<int>(count(accepted))
                 << " reassigned=" << static_cast<int>(count(reassigned))
                 << " discarded=" << static_cast<int>(count(discarded));
        }
        if (foreignNotes > 0) text << "; foreign=" << static_cast<int>(foreignNotes);
        if (unknownInstrumentNotes > 0)
            text << "; unknown_id=" << static_cast<int>(unknownInstrumentNotes);
        if (absolutePlacementsRelocalized > 0)
            text << "; absolute_placements_relocalized="
                 << static_cast<int>(absolutePlacementsRelocalized);
        return text.isEmpty() ? juce::String("no note events received") : text;
    }
};

bool parsePerformanceBlock(const juce::String& blockText, const SongPlan& blueprint,
                           const std::set<std::string>& assignedIds,
                           PerformanceScore& score, juce::String& error,
                           PerformanceRoutingReport* routingReport = nullptr) {
    PerformanceRoutingReport localRouting;
    auto& routing = routingReport == nullptr ? localRouting : *routingReport;
    routing = {};
    const auto block = juce::JSON::parse(blockText);
    const auto* blockObject = block.getDynamicObject();
    if (blockObject == nullptr) {
        error = "Performance-block JSON is invalid";
        return false;
    }
    const auto* performance = blockObject->getProperty("performance_score").getDynamicObject();
    if (performance == nullptr) {
        error = "Performance block contains no score";
        return false;
    }

    score = {};
    if (const auto* cells = performance->getProperty("cells").getArray()) {
        for (const auto& cellItem : *cells) {
            const auto* cell = cellItem.getDynamicObject();
            if (cell == nullptr) continue;
            PerformanceCell parsedCell;
            parsedCell.id = cell->getProperty("id").toString().trim().toStdString();
            parsedCell.themeId = cell->getProperty("theme_id").toString().trim().toStdString();
            parsedCell.narrativeFunction =
                cell->getProperty("narrative_function").toString().trim().toStdString();
            parsedCell.lengthBeats = static_cast<double>(cell->getProperty("length_beats"));
            if (const auto* owners = cell->getProperty("owned_voices").getArray())
                for (const auto& owner : *owners)
                    if (const auto voice = voiceIdFromKey(owner.toString().toStdString()))
                        parsedCell.ownedVoices.push_back(*voice);
            if (const auto* notes = cell->getProperty("notes").getArray()) {
                for (const auto& noteItem : *notes) {
                    const auto* note = noteItem.getDynamicObject();
                    if (note == nullptr) continue;
                    auto instrumentId = note->getProperty("instrument_id").toString().trim().toStdString();
                    ++routing.received[instrumentId];
                    const auto owner = std::find_if(blueprint.instruments.begin(), blueprint.instruments.end(),
                        [&](const auto& instrument) {
                            return instrument.id == instrumentId;
                        });
                    if (owner == blueprint.instruments.end()) {
                        ++routing.discarded[instrumentId];
                        ++routing.unknownInstrumentNotes;
                        continue;
                    }
                    if (!assignedIds.empty() && !assignedIds.contains(instrumentId)) {
                        ++routing.discarded[instrumentId];
                        ++routing.foreignNotes;
                        continue;
                    }
                    const auto reportedVoice = voiceIdFromKey(
                        note->getProperty("voice").toString().toStdString());
                    const auto voice = owner->sourceVoice;
                    if (!reportedVoice || *reportedVoice != voice)
                        ++routing.reassigned[instrumentId];
                    ++routing.accepted[instrumentId];
                    parsedCell.notes.push_back({
                        static_cast<double>(note->getProperty("beat")),
                        static_cast<double>(note->getProperty("duration")),
                        static_cast<int>(note->getProperty("pitch")),
                        static_cast<int>(note->getProperty("velocity")), voice,
                        metricIntentFromKey(note->getProperty("metric_intent").toString().toStdString()),
                        std::move(instrumentId)});
                }
            }
            if (const auto* controls = cell->getProperty("controls").getArray()) {
                for (const auto& controlItem : *controls) {
                    const auto* control = controlItem.getDynamicObject();
                    if (control == nullptr) continue;
                    auto instrumentId = control->getProperty("instrument_id").toString().trim().toStdString();
                    const auto owner = std::find_if(blueprint.instruments.begin(), blueprint.instruments.end(),
                        [&](const auto& instrument) {
                            return instrument.id == instrumentId;
                        });
                    if (owner == blueprint.instruments.end() ||
                        (!assignedIds.empty() && !assignedIds.contains(instrumentId))) continue;
                    const auto voice = owner->sourceVoice;
                    parsedCell.controls.push_back({
                        static_cast<double>(control->getProperty("beat")),
                        static_cast<int>(control->getProperty("controller")),
                        static_cast<int>(control->getProperty("value")), voice,
                        std::move(instrumentId)});
                }
            }
            std::set<VoiceId> routedVoices;
            for (const auto& note : parsedCell.notes) routedVoices.insert(note.voice);
            for (const auto& control : parsedCell.controls) routedVoices.insert(control.voice);
            if (!routedVoices.empty())
                parsedCell.ownedVoices.assign(routedVoices.begin(), routedVoices.end());
            score.cells.push_back(std::move(parsedCell));
        }
    }
    if (const auto* placements = performance->getProperty("placements").getArray()) {
        for (const auto& placementItem : *placements) {
            const auto* placement = placementItem.getDynamicObject();
            if (placement == nullptr) continue;
            PerformancePlacement parsedPlacement;
            parsedPlacement.cellId = placement->getProperty("cell_id").toString().trim().toStdString();
            parsedPlacement.sectionIndex = static_cast<int>(placement->getProperty("section_index"));
            parsedPlacement.startBeat = static_cast<double>(placement->getProperty("start_beat"));
            parsedPlacement.repeats = static_cast<int>(placement->getProperty("repeats"));
            parsedPlacement.transpose = static_cast<int>(placement->getProperty("transpose"));
            parsedPlacement.velocityScale = static_cast<double>(placement->getProperty("velocity_scale"));
            parsedPlacement.timeScale = static_cast<double>(placement->getProperty("time_scale"));
            parsedPlacement.purpose = placement->getProperty("purpose").toString().trim().toStdString();
            if (const auto* mappings = placement->getProperty("voice_map").getArray()) {
                for (const auto& mappingItem : *mappings) {
                    const auto* mapping = mappingItem.getDynamicObject();
                    if (mapping == nullptr) continue;
                    const auto from = voiceIdFromKey(mapping->getProperty("from").toString().toStdString());
                    const auto to = voiceIdFromKey(mapping->getProperty("to").toString().toStdString());
                    if (from && to) parsedPlacement.voiceMap.push_back({*from, *to});
                }
            }
            parsedPlacement.retrograde = static_cast<bool>(placement->getProperty("retrograde"));
            parsedPlacement.invertContour = static_cast<bool>(placement->getProperty("invert_contour"));
            parsedPlacement.inversionAxis = static_cast<int>(placement->getProperty("inversion_axis"));
            parsedPlacement.fragmentStart = static_cast<double>(placement->getProperty("fragment_start"));
            parsedPlacement.fragmentEnd = static_cast<double>(placement->getProperty("fragment_end"));
            parsedPlacement.metricIntent = metricIntentFromKey(
                placement->getProperty("metric_intent").toString().toStdString());
            // A recurring model mistake is to supply the absolute song position
            // where this schema expects a section-relative placement. The
            // transformation is unambiguous only when the supplied value lies
            // outside this section but inside its absolute span. It changes no
            // authored note, pitch, rhythm, velocity, or intended song position.
            if (parsedPlacement.sectionIndex >= 0 &&
                static_cast<std::size_t>(parsedPlacement.sectionIndex) < blueprint.sections.size()) {
                const auto& section = blueprint.sections[
                    static_cast<std::size_t>(parsedPlacement.sectionIndex)];
                const auto sectionBeats = section.bars * blueprint.beatsPerBar;
                const auto absoluteStart = section.startBar * blueprint.beatsPerBar;
                if (absoluteStart > 0.0 &&
                    parsedPlacement.startBeat >= sectionBeats - .001 &&
                    parsedPlacement.startBeat >= absoluteStart - .001 &&
                    parsedPlacement.startBeat < absoluteStart + sectionBeats - .001) {
                    parsedPlacement.startBeat -= absoluteStart;
                    const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
                        [&](const auto& candidate) {
                            return candidate.id == parsedPlacement.cellId;
                        });
                    if (cell != score.cells.end() &&
                        parsedPlacement.fragmentEnd > cell->lengthBeats + .001 &&
                        parsedPlacement.fragmentEnd - absoluteStart <= cell->lengthBeats + .001)
                        parsedPlacement.fragmentEnd -= absoluteStart;
                    ++routing.absolutePlacementsRelocalized;
                }
            }
            score.placements.push_back(std::move(parsedPlacement));
        }
    }
    std::vector<double> sectionLengths;
    sectionLengths.reserve(blueprint.sections.size());
    for (const auto& section : blueprint.sections)
        sectionLengths.push_back(section.bars * blueprint.beatsPerBar);
    const auto report = PerformanceScoreEngine::normalize(
        score, blueprint.sections.size(), sectionLengths);
    // Source notes can be valid while an AI-declared contour inversion turns
    // their rendered pitches negative or pushes them outside the concrete
    // instrument register. The sovereign renderer intentionally preserves AI
    // transformations, so reject an impossible placement as a unit and let the
    // bounded AI recovery author a playable replacement. Never clamp or rewrite
    // the composer's pitches locally.
    score.placements.erase(std::remove_if(score.placements.begin(), score.placements.end(),
        [&](const auto& placement) {
            const auto cell = std::find_if(score.cells.begin(), score.cells.end(),
                [&](const auto& candidate) { return candidate.id == placement.cellId; });
            if (cell == score.cells.end()) return true;
            return std::any_of(cell->notes.begin(), cell->notes.end(), [&](const auto& note) {
                const auto transformed = placement.invertContour
                    ? placement.inversionAxis * 2 - note.pitch : note.pitch;
                const auto renderedPitch = transformed + placement.transpose;
                const auto owner = std::find_if(blueprint.instruments.begin(),
                    blueprint.instruments.end(), [&](const auto& instrument) {
                        return instrument.id == note.instrumentId;
                    });
                const auto minimum = owner == blueprint.instruments.end()
                    ? 0 : std::max(0, owner->minimumPitch);
                const auto maximum = owner == blueprint.instruments.end()
                    ? 127 : std::min(127, owner->maximumPitch);
                return renderedPitch < minimum || renderedPitch > maximum;
            });
        }), score.placements.end());
    std::map<std::string, std::size_t> normalizedAccepted;
    for (const auto& cell : score.cells)
        for (const auto& note : cell.notes)
            ++normalizedAccepted[note.instrumentId];
    for (const auto& [id, parsedCount] : routing.accepted) {
        const auto finalCount = normalizedAccepted[id];
        if (parsedCount > finalCount) routing.discarded[id] += parsedCount - finalCount;
    }
    routing.accepted = std::move(normalizedAccepted);
    const auto acceptedNotes = std::accumulate(routing.accepted.begin(), routing.accepted.end(),
        std::size_t{}, [](auto total, const auto& entry) { return total + entry.second; });
    if (score.empty() || (!assignedIds.empty() && acceptedNotes == 0)) {
        error = "Performance block contains no usable cells and placements (raw contract parsed; accepted cells=" +
            juce::String(static_cast<int>(report.cellsAccepted)) + ", notes=" +
            juce::String(static_cast<int>(report.notesAccepted)) + "; routing=" +
            routing.summary(assignedIds) + ")";
        return false;
    }
    return true;
}

void mergePerformanceBlock(PerformanceScore& destination, PerformanceScore source,
                           std::size_t blockIndex) {
    std::set<std::string> usedIds;
    for (const auto& cell : destination.cells) usedIds.insert(cell.id);
    std::map<std::string, std::string> renamed;
    for (std::size_t index = 0; index < source.cells.size(); ++index) {
        auto& cell = source.cells[index];
        const auto old = cell.id;
        if (cell.id.empty() || usedIds.contains(cell.id))
            cell.id = "b" + std::to_string(blockIndex + 1) + "_cell_" +
                std::to_string(index + 1);
        while (usedIds.contains(cell.id)) cell.id += "_r";
        usedIds.insert(cell.id);
        renamed[old] = cell.id;
    }
    for (auto& placement : source.placements)
        if (const auto found = renamed.find(placement.cellId); found != renamed.end())
            placement.cellId = found->second;
    destination.cells.insert(destination.cells.end(),
                             std::make_move_iterator(source.cells.begin()),
                             std::make_move_iterator(source.cells.end()));
    destination.placements.insert(destination.placements.end(),
                                  std::make_move_iterator(source.placements.begin()),
                                  std::make_move_iterator(source.placements.end()));
}

std::vector<std::size_t> uncoveredInstruments(const SongPlan& plan,
                                              const PerformanceScore& score,
                                              const std::vector<std::size_t>& candidates,
                                              bool localEditorial = false) {
    auto missing = SelectiveRepair::incompleteTargets(plan, score, candidates);
    if (!localEditorial) return missing;
    const auto centralBed = SelectiveRepair::centralChordBedOwner(plan);
    const auto centralBedNeedsForm = centralBed &&
        std::find(candidates.begin(), candidates.end(), *centralBed) != candidates.end() &&
        !SelectiveRepair::chordBedFormCoverage(plan, score, *centralBed).ready();
    if (centralBedNeedsForm &&
        std::find(missing.begin(), missing.end(), *centralBed) == missing.end())
        missing.push_back(*centralBed);
    const auto findings = SelectiveRepair::performanceDeficits(plan, score, missing);
    missing.erase(std::remove_if(missing.begin(), missing.end(), [&](const auto index) {
        if (index >= plan.instruments.size()) return false;
        if (centralBedNeedsForm && index == *centralBed) return false;
        const auto finding = std::find_if(findings.begin(), findings.end(), [&](const auto& item) {
            return item.instrumentIndex == index;
        });
        if (plan.instruments[index].id == plan.narrativeSpine.protagonistInstrumentId)
            return finding != findings.end() &&
                SelectiveRepair::deferableLocalProtagonistEditorial(plan, *finding);
        // Local editorial mode treats quantitative development as critic feedback,
        // not as an excuse to generate filler or repeatedly rewrite a real part.
        return finding != findings.end() && finding->notes > 0;
    }), missing.end());
    return missing;
}

juce::String realizationBrief(const SongPlan& plan, const PerformanceScore& score,
                              const std::vector<std::size_t>& candidates) {
    std::vector<double> sectionLengths;
    sectionLengths.reserve(plan.sections.size());
    for (const auto& section : plan.sections)
        sectionLengths.push_back(section.bars * plan.beatsPerBar);
    juce::String result;
    for (const auto index : candidates) {
        if (index >= plan.instruments.size()) continue;
        const auto& id = plan.instruments[index].id;
        const auto audit = PerformanceScoreEngine::auditRealization(
            score, id, plan.instruments, sectionLengths);
        if (result.isNotEmpty()) result << "; ";
        result << juce::String::fromUTF8(id.c_str())
               << " placed_source=" << static_cast<int>(audit.placedSourceNotes)
               << " realizable=" << static_cast<int>(audit.realizableNotes)
               << " unplaced=" << static_cast<int>(audit.unplacedSourceNotes)
               << " clipped_by_fragment=" << static_cast<int>(audit.excludedByFragment)
               << " outside_section=" << static_cast<int>(audit.excludedBySection)
               << " voice_map_mismatch=" << static_cast<int>(audit.incompatibleVoiceMap);
    }
    return result;
}

juce::String performanceDeficitBrief(const SongPlan& plan,
                                     const PerformanceScore& score,
                                     const std::vector<std::size_t>& candidates) {
    juce::String result;
    for (const auto& deficit : SelectiveRepair::performanceDeficits(plan, score, candidates)) {
        if (result.isNotEmpty()) result << "; ";
        result << juce::String::fromUTF8(deficit.instrumentId.c_str())
               << " notes=" << static_cast<int>(deficit.notes) << "/"
               << static_cast<int>(deficit.minimumNotes);
        if (deficit.minimumAuthoredNotes > 0)
            result << " source_notes=" << static_cast<int>(deficit.authoredNotes) << "/"
                   << static_cast<int>(deficit.minimumAuthoredNotes);
        result << " bars=" << static_cast<int>(deficit.activeBars) << "/"
               << static_cast<int>(deficit.minimumActiveBars)
               << " phrases=" << static_cast<int>(deficit.phrases) << "/"
               << static_cast<int>(deficit.minimumPhrases);
        if (deficit.minimumSections > 0)
            result << " sections=" << static_cast<int>(deficit.sections) << "/"
                   << static_cast<int>(deficit.minimumSections);
        if (deficit.minimumSectionalStates > 0)
            result << " sectional_states=" << static_cast<int>(deficit.sectionalStates) << "/"
                   << static_cast<int>(deficit.minimumSectionalStates);
        if (deficit.minimumNarrativePhraseWindows > 0)
            result << " narrative_windows=" << static_cast<int>(deficit.narrativePhraseWindows) << "/"
                   << static_cast<int>(deficit.minimumNarrativePhraseWindows);
        if (deficit.missingNarrativePresence &&
            !deficit.missingNarrativeWindowStartBars.empty()) {
            result << " missing_window_start_bars=";
            for (const auto bar : deficit.missingNarrativeWindowStartBars)
                result << bar << ",";
        }
        if (deficit.missingCodaResolution) result << " coda=missing";
        if (deficit.missingThematicRelationship) result << " theme_relation=missing";
        if (deficit.missingAuthoredDevelopment)
            result << " source_phrase=underwritten_repetition";
        if (deficit.duplicatedIndependentLine)
            result << " duplicate_with="
                   << juce::String::fromUTF8(deficit.duplicatedWithInstrumentId.c_str())
                   << " overlap=" << juce::String(deficit.duplicateEventOverlap, 3);
        if (deficit.missingSectionalEvolution) result << " sectional_evolution=missing";
        if (deficit.missingNarrativePresence) result << " narrative_presence=missing";
        if (deficit.missingThematicDevelopment)
            result << " literal_return_ratio=" << juce::String(deficit.literalPlacementRatio, 3);
        if (deficit.missingMelodicSpeech)
            result << " melodic_step_ratio=" << juce::String(deficit.melodicStepRatio, 3)
                   << " intervals=" << static_cast<int>(deficit.melodicIntervals);
        if (deficit.missingCentralChordBed)
            result << " polyphonic_chord_attacks="
                   << static_cast<int>(deficit.polyphonicChordAttacks) << "/"
                   << static_cast<int>(deficit.minimumPolyphonicChordAttacks);
        if (deficit.missingChordBedBreath)
            result << " chord_bed_breath_bars="
                   << static_cast<int>(deficit.longestChordBedBreathBars) << "/2";
        if (deficit.missingChordBedNarrativeArc)
            result << " chord_bed_narrative_stages="
                   << static_cast<int>(deficit.chordBedNarrativeStages) << "/"
                   << static_cast<int>(deficit.minimumChordBedNarrativeStages);
    }
    return result.isEmpty() ? juce::String("none") : result;
}

juce::String marginalBarAcceptanceBrief(const SongPlan& plan,
                                        const PerformanceScore& score,
                                        const std::vector<std::size_t>& candidates) {
    juce::String result;
    for (const auto& accepted :
         SelectiveRepair::marginalBarAcceptances(plan, score, candidates)) {
        if (result.isNotEmpty()) result << "; ";
        result << juce::String::fromUTF8(accepted.instrumentId.c_str())
               << " notes=" << static_cast<int>(accepted.notes) << "/"
               << static_cast<int>(accepted.minimumNotes)
               << " bars=" << static_cast<int>(accepted.activeBars) << "/"
               << static_cast<int>(accepted.minimumActiveBars)
               << " phrases=" << static_cast<int>(accepted.phrases) << "/"
               << static_cast<int>(accepted.minimumPhrases);
    }
    return result;
}

bool incompleteStructuredResponse(const juce::String& body) {
    const auto parsed = juce::JSON::parse(body);
    const auto* object = parsed.getDynamicObject();
    return object != nullptr && object->getProperty("status").toString() == "incomplete";
}

juce::String performanceConstraintBrief(const SongPlan& plan,
                                        const PerformanceScore& score,
                                        const std::vector<std::size_t>& candidates,
                                        bool explicitCastCommitment) {
    juce::String result;
    for (const auto& constraint : SelectiveRepair::performanceConstraints(
             plan, score, candidates, explicitCastCommitment)) {
        const auto& evidence = constraint.evidence;
        if (result.isNotEmpty()) result << "\n";
        result << "- id=" << juce::String::fromUTF8(evidence.instrumentId.c_str())
               << "; authority=" << juce::String::fromUTF8(
                    constraintAuthorityKey(constraint.authority).data(),
                    static_cast<int>(constraintAuthorityKey(constraint.authority).size()))
               << "; blocking=" << (constraint.blocksPublication ? "true" : "false")
               << "; evidence=notes " << static_cast<int>(evidence.notes) << "/"
               << static_cast<int>(evidence.minimumNotes) << ", bars "
               << static_cast<int>(evidence.activeBars) << "/"
               << static_cast<int>(evidence.minimumActiveBars) << ", phrases "
               << static_cast<int>(evidence.phrases) << "/"
               << static_cast<int>(evidence.minimumPhrases) << ", sections "
               << static_cast<int>(evidence.sections) << "/"
               << static_cast<int>(evidence.minimumSections);
        if (evidence.missingAuthoredDevelopment)
            result << ", source_notes " << static_cast<int>(evidence.authoredNotes)
                   << "/" << static_cast<int>(evidence.minimumAuthoredNotes);
        result << "; operations=";
        for (const auto operation : constraint.operations) {
            const auto key = performanceRepairOperationKey(operation);
            result << juce::String::fromUTF8(key.data(), static_cast<int>(key.size())) << ",";
        }
        if (evidence.missingCodaResolution) result << "; resolution_evidence=missing";
        if (evidence.missingThematicRelationship) result << "; thematic_relation=missing";
        if (evidence.duplicatedIndependentLine)
            result << "; duplicate_with="
                   << juce::String::fromUTF8(evidence.duplicatedWithInstrumentId.c_str())
                   << "; event_overlap=" << juce::String(evidence.duplicateEventOverlap, 3);
        if (evidence.missingSectionalEvolution)
            result << "; sectional_states=" << static_cast<int>(evidence.sectionalStates)
                   << "/" << static_cast<int>(evidence.minimumSectionalStates);
        if (evidence.missingNarrativePresence)
            result << "; narrative_windows=" << static_cast<int>(evidence.narrativePhraseWindows)
                   << "/" << static_cast<int>(evidence.minimumNarrativePhraseWindows);
        if (evidence.missingNarrativePresence &&
            !evidence.missingNarrativeWindowStartBars.empty()) {
            result << "; missing_window_start_bars=";
            for (const auto bar : evidence.missingNarrativeWindowStartBars)
                result << bar << ",";
        }
        if (evidence.missingThematicDevelopment)
            result << "; literal_return_ratio=" << juce::String(evidence.literalPlacementRatio, 3);
        if (evidence.missingMelodicSpeech)
            result << "; melodic_step_ratio=" << juce::String(evidence.melodicStepRatio, 3)
                   << "; melodic_intervals=" << static_cast<int>(evidence.melodicIntervals);
        if (evidence.missingCentralChordBed)
            result << "; polyphonic_chord_attacks="
                   << static_cast<int>(evidence.polyphonicChordAttacks) << "/"
                   << static_cast<int>(evidence.minimumPolyphonicChordAttacks);
        if (evidence.missingChordBedBreath)
            result << "; chord_bed_breath_bars="
                   << static_cast<int>(evidence.longestChordBedBreathBars) << "/2";
        if (evidence.missingChordBedNarrativeArc)
            result << "; chord_bed_narrative_stages="
                   << static_cast<int>(evidence.chordBedNarrativeStages) << "/"
                   << static_cast<int>(evidence.minimumChordBedNarrativeStages);
    }
    return result.isEmpty() ? juce::String("none") : result;
}

juce::String independenceReferenceBrief(const SongPlan& plan,
                                        const PerformanceScore& score,
                                        const std::vector<std::size_t>& candidates) {
    std::set<std::string> counterpartIds;
    for (const auto& deficit : SelectiveRepair::performanceDeficits(plan, score, candidates))
        if (deficit.duplicatedIndependentLine &&
            !deficit.duplicatedWithInstrumentId.empty())
            counterpartIds.insert(deficit.duplicatedWithInstrumentId);
    if (counterpartIds.empty()) return {};

    Pattern rendered;
    rendered.lengthBeats = plan.totalBars * plan.beatsPerBar;
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        const auto sectionBeats = section.bars * plan.beatsPerBar;
        Pattern chunk;
        chunk.lengthBeats = sectionBeats;
        PerformanceScoreEngine::replaceChunk(chunk, score, static_cast<int>(sectionIndex),
                                              0.0, sectionBeats, plan.instruments);
        const auto offset = section.startBar * plan.beatsPerBar;
        for (auto note : chunk.notes) {
            note.startBeat += offset;
            rendered.notes.push_back(std::move(note));
        }
    }
    std::sort(rendered.notes.begin(), rendered.notes.end(), [](const auto& left, const auto& right) {
        return std::tie(left.startBeat, left.pitch, left.durationBeats) <
               std::tie(right.startBeat, right.pitch, right.durationBeats);
    });

    juce::String result;
    for (const auto& counterpartId : counterpartIds) {
        const auto instrument = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& candidate) { return candidate.id == counterpartId; });
        if (instrument == plan.instruments.end()) continue;
        const auto partId = static_cast<std::uint16_t>(
            std::distance(plan.instruments.begin(), instrument) + 1);
        result << "- id=" << juce::String::fromUTF8(counterpartId.c_str()) << "; events=";
        auto emitted = 0;
        for (std::size_t sectionIndex = 0;
             sectionIndex < plan.sections.size() && emitted < 64; ++sectionIndex) {
            const auto& section = plan.sections[sectionIndex];
            const auto start = section.startBar * plan.beatsPerBar;
            const auto end = (section.startBar + section.bars) * plan.beatsPerBar;
            auto emittedInSection = 0;
            for (const auto& note : rendered.notes) {
                if (note.partId != partId || note.startBeat < start || note.startBeat >= end) continue;
                result << "[" << juce::String(note.startBeat, 3) << ","
                       << note.pitch << "," << juce::String(note.durationBeats, 3) << "]";
                ++emitted;
                if (++emittedInSection == 6 || emitted == 64) break;
            }
        }
        result << "\n";
    }
    return result;
}

// The global manifest decides the complete ensemble once. It deliberately omits
// verbose implementation fields (preset descriptions and timbre signatures), so
// even a 64-part request remains a small architectural decision. Detail shards are
// not allowed to redesign these anchors; they only complete them.
juce::String castManifestSchema(std::size_t exactInstrumentCount = 0) {
    const auto full = juce::JSON::parse(songPlanSchema);
    const auto* root = full.getDynamicObject();
    const auto* fullProperties = root == nullptr
        ? nullptr : root->getProperty("properties").getDynamicObject();
    if (fullProperties == nullptr) return {};

    const auto* fullSoundscape = fullProperties->getProperty("electronic_soundscape").getDynamicObject();
    const auto* fullSoundscapeProperties = fullSoundscape == nullptr
        ? nullptr : fullSoundscape->getProperty("properties").getDynamicObject();
    const auto* fullInstruments = fullProperties->getProperty("instruments").getDynamicObject();
    const auto* fullInstrumentItems = fullInstruments == nullptr
        ? nullptr : fullInstruments->getProperty("items").getDynamicObject();
    const auto* fullInstrumentProperties = fullInstrumentItems == nullptr
        ? nullptr : fullInstrumentItems->getProperty("properties").getDynamicObject();
    if (fullSoundscapeProperties == nullptr || fullInstrumentProperties == nullptr) return {};

    auto manifestRoot = new juce::DynamicObject();
    auto properties = new juce::DynamicObject();
    auto soundscape = new juce::DynamicObject();
    auto soundscapeProperties = new juce::DynamicObject();
    juce::Array<juce::var> soundscapeRequired;
    for (const auto* name : {"active", "percussion_free", "scene", "spatial_narrative",
                             "target_median_active_layers"}) {
        soundscapeProperties->setProperty(name, fullSoundscapeProperties->getProperty(name));
        soundscapeRequired.add(name);
    }
    soundscape->setProperty("type", "object");
    soundscape->setProperty("properties", juce::var(soundscapeProperties));
    soundscape->setProperty("required", juce::var(soundscapeRequired));
    soundscape->setProperty("additionalProperties", false);
    properties->setProperty("electronic_soundscape", juce::var(soundscape));

    auto instruments = new juce::DynamicObject();
    auto instrumentItems = new juce::DynamicObject();
    auto instrumentProperties = new juce::DynamicObject();
    juce::Array<juce::var> instrumentRequired;
    for (const auto* name : {"id", "instrument", "name", "source_voice", "role",
                             "content_lane_id", "line_relationship", "orchestral_function",
                             "active_sections"}) {
        instrumentProperties->setProperty(name, fullInstrumentProperties->getProperty(name));
        instrumentRequired.add(name);
    }
    instrumentItems->setProperty("type", "object");
    instrumentItems->setProperty("properties", juce::var(instrumentProperties));
    instrumentItems->setProperty("required", juce::var(instrumentRequired));
    instrumentItems->setProperty("additionalProperties", false);
    instruments->setProperty("type", "array");
    const auto exactCount = exactInstrumentCount > 0
        ? std::min(exactInstrumentCount, maximumInstruments) : std::size_t{};
    instruments->setProperty("minItems", static_cast<int>(exactCount > 0 ? exactCount : 8));
    instruments->setProperty("maxItems", static_cast<int>(exactCount > 0
        ? exactCount : maximumInstruments));
    instruments->setProperty("items", juce::var(instrumentItems));
    properties->setProperty("instruments", juce::var(instruments));
    auto protagonist = new juce::DynamicObject();
    protagonist->setProperty("type", "string");
    properties->setProperty("protagonist_instrument_id", juce::var(protagonist));
    properties->setProperty("rhythm_motifs", fullProperties->getProperty("rhythm_motifs"));
    properties->setProperty("voices", fullProperties->getProperty("voices"));

    juce::Array<juce::var> required;
    for (const auto* name : {"electronic_soundscape", "protagonist_instrument_id",
                             "instruments", "rhythm_motifs", "voices"})
        required.add(name);
    manifestRoot->setProperty("type", "object");
    manifestRoot->setProperty("properties", juce::var(properties));
    manifestRoot->setProperty("required", juce::var(required));
    manifestRoot->setProperty("additionalProperties", false);
    return juce::JSON::toString(juce::var(manifestRoot));
}

juce::String castSupplementSchema(std::size_t exactCount) {
    const auto manifest = juce::JSON::parse(castManifestSchema());
    const auto* manifestRoot = manifest.getDynamicObject();
    const auto* manifestProperties = manifestRoot == nullptr
        ? nullptr : manifestRoot->getProperty("properties").getDynamicObject();
    const auto* manifestInstruments = manifestProperties == nullptr
        ? nullptr : manifestProperties->getProperty("instruments").getDynamicObject();
    if (manifestInstruments == nullptr || exactCount == 0 || exactCount > maximumInstruments)
        return {};

    auto root = new juce::DynamicObject();
    auto properties = new juce::DynamicObject();
    auto instruments = new juce::DynamicObject();
    instruments->setProperty("type", "array");
    instruments->setProperty("minItems", static_cast<int>(exactCount));
    instruments->setProperty("maxItems", static_cast<int>(exactCount));
    instruments->setProperty("items", manifestInstruments->getProperty("items"));
    properties->setProperty("instruments", juce::var(instruments));
    juce::Array<juce::var> required;
    required.add("instruments");
    root->setProperty("type", "object");
    root->setProperty("properties", juce::var(properties));
    root->setProperty("required", juce::var(required));
    root->setProperty("additionalProperties", false);
    return juce::JSON::toString(juce::var(root));
}

juce::String castDetailShardSchema() {
    const auto full = juce::JSON::parse(songPlanSchema);
    const auto* root = full.getDynamicObject();
    const auto* properties = root == nullptr
        ? nullptr : root->getProperty("properties").getDynamicObject();
    const auto* soundscape = properties == nullptr
        ? nullptr : properties->getProperty("electronic_soundscape").getDynamicObject();
    const auto* soundscapeProperties = soundscape == nullptr
        ? nullptr : soundscape->getProperty("properties").getDynamicObject();
    const auto* instruments = properties == nullptr
        ? nullptr : properties->getProperty("instruments").getDynamicObject();
    if (soundscapeProperties == nullptr || instruments == nullptr) return {};

    auto resultRoot = new juce::DynamicObject();
    auto resultProperties = new juce::DynamicObject();
    auto shardInstruments = new juce::DynamicObject();
    shardInstruments->setProperty("type", "array");
    shardInstruments->setProperty("minItems", 1);
    shardInstruments->setProperty("maxItems", static_cast<int>(instrumentsPerCastShard));
    shardInstruments->setProperty("items", instruments->getProperty("items"));
    auto layers = new juce::DynamicObject();
    const auto* fullLayers = soundscapeProperties->getProperty("layers").getDynamicObject();
    if (fullLayers == nullptr) return {};
    layers->setProperty("type", "array");
    layers->setProperty("minItems", 1);
    layers->setProperty("maxItems", static_cast<int>(instrumentsPerCastShard));
    layers->setProperty("items", fullLayers->getProperty("items"));
    resultProperties->setProperty("instruments", juce::var(shardInstruments));
    resultProperties->setProperty("layers", juce::var(layers));
    juce::Array<juce::var> required;
    required.add("instruments");
    required.add("layers");
    resultRoot->setProperty("type", "object");
    resultRoot->setProperty("properties", juce::var(resultProperties));
    resultRoot->setProperty("required", juce::var(required));
    resultRoot->setProperty("additionalProperties", false);
    return juce::JSON::toString(juce::var(resultRoot));
}

std::set<std::string> instrumentIdsFor(const SongPlan& plan,
                                       const std::vector<std::size_t>& indices) {
    std::set<std::string> result;
    for (const auto index : indices)
        if (index < plan.instruments.size()) result.insert(plan.instruments[index].id);
    return result;
}

void retainOnlyInstrumentMaterial(PerformanceScore& score,
                                  const std::set<std::string>& instrumentIds) {
    for (auto& cell : score.cells) {
        cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
            return !instrumentIds.contains(note.instrumentId);
        }), cell.notes.end());
        cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
            return !instrumentIds.contains(control.instrumentId);
        }), cell.controls.end());
        std::set<VoiceId> survivingVoices;
        for (const auto& note : cell.notes) survivingVoices.insert(note.voice);
        for (const auto& control : cell.controls) survivingVoices.insert(control.voice);
        cell.ownedVoices.assign(survivingVoices.begin(), survivingVoices.end());
    }
    std::set<std::string> emptyCells;
    for (const auto& cell : score.cells)
        if (cell.notes.empty()) emptyCells.insert(cell.id);
    score.cells.erase(std::remove_if(score.cells.begin(), score.cells.end(), [&](const auto& cell) {
        return emptyCells.contains(cell.id);
    }), score.cells.end());
    score.placements.erase(std::remove_if(score.placements.begin(), score.placements.end(),
        [&](const auto& placement) { return emptyCells.contains(placement.cellId); }),
        score.placements.end());
}

void eraseInstrumentMaterial(PerformanceScore& score,
                             const std::set<std::string>& instrumentIds) {
    for (auto& cell : score.cells) {
        cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
            return instrumentIds.contains(note.instrumentId);
        }), cell.notes.end());
        cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
            return instrumentIds.contains(control.instrumentId);
        }), cell.controls.end());
        std::set<VoiceId> survivingVoices;
        for (const auto& note : cell.notes) survivingVoices.insert(note.voice);
        for (const auto& control : cell.controls) survivingVoices.insert(control.voice);
        cell.ownedVoices.assign(survivingVoices.begin(), survivingVoices.end());
    }
    std::set<std::string> emptyCells;
    for (const auto& cell : score.cells)
        if (cell.notes.empty()) emptyCells.insert(cell.id);
    score.cells.erase(std::remove_if(score.cells.begin(), score.cells.end(), [&](const auto& cell) {
        return emptyCells.contains(cell.id);
    }), score.cells.end());
    score.placements.erase(std::remove_if(score.placements.begin(), score.placements.end(),
        [&](const auto& placement) { return emptyCells.contains(placement.cellId); }),
        score.placements.end());
}

juce::String existingTargetMaterial(const SongPlan& plan,
                                    const std::set<std::string>& instrumentIds) {
    juce::String result;
    std::set<std::string> includedCells;
    for (const auto& cell : plan.performanceScore.cells) {
        const auto relevant = std::any_of(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
            return instrumentIds.contains(note.instrumentId);
        });
        if (!relevant) continue;
        includedCells.insert(cell.id);
        result << "CELL " << juce::String::fromUTF8(cell.id.c_str()) << " length="
               << juce::String(cell.lengthBeats, 3) << " theme="
               << juce::String::fromUTF8(cell.themeId.c_str()) << " function="
               << juce::String::fromUTF8(cell.narrativeFunction.c_str()) << "\n";
        for (const auto& note : cell.notes) {
            if (!instrumentIds.contains(note.instrumentId)) continue;
            const auto key = voiceDefinition(note.voice).key;
            result << "  NOTE id=" << juce::String::fromUTF8(note.instrumentId.c_str())
                   << " voice=" << juce::String::fromUTF8(key.data(), static_cast<int>(key.size()))
                   << " beat=" << juce::String(note.beat, 3)
                   << " duration=" << juce::String(note.durationBeats, 3)
                   << " pitch=" << note.pitch << " velocity=" << note.velocity << "\n";
        }
    }
    for (const auto& placement : plan.performanceScore.placements) {
        if (!includedCells.contains(placement.cellId)) continue;
        result << "PLACEMENT cell=" << juce::String::fromUTF8(placement.cellId.c_str())
               << " section=" << placement.sectionIndex
               << " start=" << juce::String(placement.startBeat, 3)
               << " repeats=" << placement.repeats
               << " transpose=" << placement.transpose
               << " time_scale=" << juce::String(placement.timeScale, 3)
               << " purpose=" << juce::String::fromUTF8(placement.purpose.c_str()) << "\n";
    }
    return result;
}

juce::String sovereignCodaPrompt(const SongPlan& plan,
                                 const PerformanceScore& score,
                                 std::size_t protagonistIndex) {
    auto snapshot = plan;
    snapshot.performanceScore = score;
    const auto& owner = plan.instruments[protagonistIndex];
    const auto& last = plan.sections.back();
    const auto end = last.bars * plan.beatsPerBar;
    const auto voice = voiceDefinition(owner.sourceVoice).key;
    const HarmonicEvent* terminalEvent = nullptr;
    for (const auto& event : last.harmonicEvents)
        if (terminalEvent == nullptr ||
            std::tie(event.barOffset, event.beatOffset) >
                std::tie(terminalEvent->barOffset, terminalEvent->beatOffset))
            terminalEvent = &event;
    const HarmonicChord* terminalChord = nullptr;
    if (terminalEvent != nullptr)
        for (const auto& chord : plan.chordPalette)
            if (chord.id == terminalEvent->chordId) {
                terminalChord = &chord;
                break;
            }
    juce::String terminalHarmony;
    if (terminalChord != nullptr) {
        terminalHarmony << "Final chord "
            << juce::String::fromUTF8(terminalChord->label.c_str())
            << " (id=" << juce::String::fromUTF8(terminalChord->id.c_str())
            << ", root_pitch_class=" << terminalChord->rootPitchClass
            << ", bass_pitch_class=" << terminalChord->bassPitchClass
            << ", pitch_classes=[";
        for (std::size_t i = 0; i < terminalChord->pitchClasses.size(); ++i) {
            if (i) terminalHarmony << ",";
            terminalHarmony << terminalChord->pitchClasses[i];
        }
        terminalHarmony << "]). ";
    }
    const auto resolutionWords = juce::String::fromUTF8(
        plan.narrativeSpine.resolution.c_str()).toLowerCase();
    auto tonicEnding = resolutionWords.contains("tonic") ||
        resolutionWords.contains("root") ||
        resolutionWords.contains("home note") ||
        resolutionWords.contains("tonica");
    for (const auto& act : plan.narrativeSpine.acts) {
        if (act.stage != NarrativeStage::Resolution) continue;
        const auto target = juce::String::fromUTF8(act.resolutionTarget.c_str())
            .toLowerCase();
        tonicEnding = tonicEnding || target.contains("tonic") ||
            target.contains("root") || target.contains("home note") ||
            target.contains("tonica");
    }
    terminalHarmony << "The final note pitch class MUST be "
        << (tonicEnding ? juce::String(plan.rootPitchClass) :
            terminalChord != nullptr ? juce::String(terminalChord->rootPitchClass) :
            juce::String(plan.rootPitchClass))
        << " (MIDI pitch modulo 12). ";
    juce::String prompt =
        "Write ONLY the missing final coda for PULSO's already accepted AI-authored protagonist. "
        "You are the composer. Do not rewrite or replace any existing note, change the other instruments, "
        "add a filler ostinato, or continue endlessly. Return one short connected musical sentence "
        "(at least four distinct attacks), with several one- or two-semitone links "
        "between characteristic leaps rather than a mechanical scalar run, and a recognizable "
        "consequence of the existing motif, "
        "an intentional rest or held arrival, and a stable final pitch from the final chord. "
        "The last attack must be in the final TWO bars of the entire song. Write exactly one "
        "new cell and one placement for the existing instrument_id; no other cells or placements. "
        "Use the structured performance-block schema. Make one eight-beat cell with "
        "fragment_start=0, fragment_end=8, voice_map=[], repeats=1, time_scale=1, "
        "retrograde=false, and place it at the start of the final two bars. "
        "Keep at least four distinct connected attacks, no more than three beats apart, "
        "and place the final attack between local beats 4 and 7 of this cell. "
        "Place the cell in final section index " +
        juce::String(static_cast<int>(plan.sections.size() - 1)) +
        " with section-relative start_beat=" +
        juce::String(std::max(0.0, end - plan.beatsPerBar * 2.0), 2) +
        ". Section ends at beat " + juce::String(end, 2) +
        ". " + terminalHarmony +
        ". instrument_id=" + juce::String::fromUTF8(owner.id.c_str()) +
        " source_voice=" + juce::String::fromUTF8(voice.data(), static_cast<int>(voice.size())) +
        " key=" + juce::String::fromUTF8(plan.key.c_str()) +
        " resolution=" + juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str()) +
        " final_section=" + juce::String::fromUTF8(last.name.c_str()) +
        "\nEXISTING ACCEPTED PROTAGONIST MATERIAL (context only; never repeat it wholesale):\n" +
        existingTargetMaterial(snapshot, {owner.id}).substring(0, 16000);
    return prompt;
}

juce::String sovereignChordBedClosurePrompt(const SongPlan& plan,
                                             const PerformanceScore& score,
                                             std::size_t chordBedIndex) {
    if (chordBedIndex >= plan.instruments.size() || plan.sections.empty()) return {};
    auto snapshot = plan;
    snapshot.performanceScore = score;
    const auto& owner = plan.instruments[chordBedIndex];
    const auto& finalSection = plan.sections.back();
    const auto sectionBeats = finalSection.bars * plan.beatsPerBar;
    const auto voice = voiceDefinition(owner.sourceVoice).key;
    juce::String harmony;
    for (const auto& event : finalSection.harmonicEvents) {
        const auto chord = std::find_if(plan.chordPalette.begin(), plan.chordPalette.end(),
            [&](const auto& candidate) { return candidate.id == event.chordId; });
        harmony << "- local_beat="
                << juce::String(event.barOffset * plan.beatsPerBar + event.beatOffset, 2)
                << " chord_id=" << juce::String::fromUTF8(event.chordId.c_str());
        if (chord != plan.chordPalette.end()) {
            harmony << " root_pc=" << chord->rootPitchClass << " pcs=[";
            for (std::size_t index = 0; index < chord->pitchClasses.size(); ++index) {
                if (index) harmony << ",";
                harmony << chord->pitchClasses[index];
            }
            harmony << "]";
        }
        harmony << "\n";
    }
    return juce::String(
        "Write ONLY the missing closing-stage phrase for PULSO's already accepted AI-authored "
        "primary chord bed. You are the composer. Do not replace, repeat or summarize any existing "
        "material and do not write any other instrument. Return one new cell and one placement for "
        "the existing instrument_id. The cell spans the complete final section and must make the "
        "harmonic ending audible as a restrained coda: write three to five intentional polyphonic "
        "chord attacks, each containing three to five simultaneous pitches. Follow the exact local "
        "harmonic windows below, choose musical inversions and voice-leading, leave audible breaths "
        "between attacks, reduce register or density toward the final arrival, and end on a stable "
        "voicing of the terminal chord. This is not an ostinato and must not become constant block "
        "chords. Use strict-grid timing. Every note must end inside the cell. Write exactly one "
        "placement with section_index=") +
        juce::String(static_cast<int>(plan.sections.size() - 1)) +
        ", start_beat=0, repeats=1, transpose=0, time_scale=1, retrograde=false, "
        "fragment_start=0 and fragment_end=" + juce::String(sectionBeats, 2) +
        ". The cell length_beats must be " + juce::String(sectionBeats, 2) +
        ". instrument_id=" + juce::String::fromUTF8(owner.id.c_str()) +
        " source_voice=" +
        juce::String::fromUTF8(voice.data(), static_cast<int>(voice.size())) +
        " key=" + juce::String::fromUTF8(plan.key.c_str()) +
        "\nFINAL-SECTION HARMONIC WINDOWS:\n" + harmony +
        "EXISTING ACCEPTED CHORD-BED MATERIAL (context only; never copy it wholesale):\n" +
        existingTargetMaterial(snapshot, {owner.id}).substring(0, 14000);
}

std::vector<int> focusedProtagonistWindowTargets(
    const PerformanceCoverageDeficit& deficit) {
    const auto needed = deficit.minimumNarrativePhraseWindows >
            deficit.narrativePhraseWindows
        ? deficit.minimumNarrativePhraseWindows - deficit.narrativePhraseWindows : 0;
    const auto& missing = deficit.missingNarrativeWindowStartBars;
    const auto count = std::min({std::size_t{3}, needed, missing.size()});
    std::vector<int> targets;
    targets.reserve(count);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const auto index = count <= 1 ? std::size_t{} :
            sample * (missing.size() - 1) / (count - 1);
        targets.push_back(missing[index]);
    }
    return targets;
}

juce::String focusedProtagonistNarrativePrompt(
    const juce::String& direction, const SongPlan& plan,
    const PerformanceScore& score, const PerformanceCoverageDeficit& deficit) {
    const auto& owner = plan.instruments[deficit.instrumentIndex];
    const auto voice = voiceDefinition(owner.sourceVoice).key;
    const auto targets = focusedProtagonistWindowTargets(deficit);
    juce::String prompt;
    prompt << "You are completing an ALREADY ACCEPTED, AI-authored melodic protagonist. "
        "Return exactly one NEW additive cell and one section-relative placement for EACH TARGET_WINDOW "
        "below, and no other material; never replace, erase or restate accepted cells. Each cell "
        "needs at least four distinct connected attacks no more than three quarters of a bar apart, "
        "musical contour, an intentional breath and a "
        "recognizable transformation of the existing motif. Keep characteristic leaps but connect them "
        "with nearby steps; do not fill every bar or write mechanical scales. If the coda is missing, "
        "leave the coda to a separate request; do not add an unrequested ending here. "
        "Give each cell a unique id and a valid placement that actually renders its notes: "
        "use fragment_start=0, fragment_end equal to the cell length, voice_map=[], repeats=1, "
        "time_scale=1, retrograde=false, and section-relative start_beat equal to the "
        "TARGET_WINDOW local_start_beat. Keep each cell within its target window and section. "
        "Do not return notes outside the cell length or placements past a section boundary. "
        "Use strict_grid timing and include all required JSON fields. "
        "No other instrument may appear. Return only the structured score JSON.\n"
        "OWNER=" << juce::String::fromUTF8(owner.id.c_str())
        << " source_voice=" << juce::String::fromUTF8(
            voice.data(), static_cast<int>(voice.size()))
        << " key=" << juce::String::fromUTF8(plan.key.c_str())
        << " bars=" << plan.totalBars
        << " beats_per_bar=" << juce::String(plan.beatsPerBar, 2)
        << " missing_coda=" << static_cast<int>(deficit.missingCodaResolution)
        << " target_narrative_windows=" << static_cast<int>(targets.size())
        << "\nORIGINAL DIRECTION=" << direction.substring(0, 500) << "\n";
    for (const auto absoluteBar : targets) {
        for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
            const auto& section = plan.sections[sectionIndex];
            if (absoluteBar < section.startBar ||
                absoluteBar >= section.startBar + section.bars) continue;
            prompt << "TARGET_WINDOW absolute_start_bar=" << absoluteBar
                   << " section_index=" << static_cast<int>(sectionIndex)
                   << " section_name=" << juce::String::fromUTF8(section.name.c_str())
                   << " local_start_beat=" << juce::String(
                       (absoluteBar - section.startBar) * plan.beatsPerBar, 2)
                   << " section_length_beats=" << juce::String(
                       section.bars * plan.beatsPerBar, 2) << "\n";
            break;
        }
    }
    for (std::size_t sectionIndex = 0; sectionIndex < plan.sections.size(); ++sectionIndex) {
        const auto& section = plan.sections[sectionIndex];
        prompt << "SECTION " << static_cast<int>(sectionIndex)
               << " start_bar=" << section.startBar
               << " bars=" << section.bars << " chords=";
        for (const auto& event : section.harmonicEvents)
            prompt << juce::String::fromUTF8(event.chordId.c_str()) << "@"
                   << event.barOffset << ":" << juce::String(event.beatOffset, 2) << ",";
        prompt << "\n";
    }
    for (const auto& chord : plan.chordPalette) {
        prompt << "CHORD " << juce::String::fromUTF8(chord.id.c_str())
               << " pitch_classes=";
        for (const auto pitchClass : chord.pitchClasses) prompt << pitchClass << ",";
        prompt << "\n";
    }
    auto snapshot = plan;
    snapshot.performanceScore = score;
    prompt += "\nACCEPTED SOURCE MOTIF AND PLACEMENTS (immutable):\n" +
        existingTargetMaterial(snapshot, {owner.id}).substring(0, 12000);
    return prompt;
}

juce::String marginalSpeechPitchPrompt(const SongPlan& plan,
                                       const PerformanceScore& score,
                                       const PerformanceCoverageDeficit& deficit) {
    auto snapshot = plan;
    snapshot.performanceScore = score;
    juce::String prompt =
        "PULSO has accepted this protagonist's complete MIDI. Its only measured shortfall is ONE "
        "conjunct melodic interval. Make one musically meaningful pitch edit to an existing SOURCE note. "
        "Do not add notes, change timing, replace a phrase or change another instrument. Choose one cell_id "
        "and zero-based note_index from the exact source list below, and a new MIDI pitch within two semitones "
        "of that note. Preserve the motif and cadence. The edited pitch must belong to the declared key; "
        "it must make a neighboring interval of one or two semitones without creating a scalar run or an "
        "unresolved dissonance in any repeated placement. If no safe edit exists, return the original pitch "
        "so the complete ensemble can be judged without a forced change. Return only structured JSON. "
        "KEY=" + juce::String::fromUTF8(plan.key.c_str()) +
        " protagonist=" + juce::String::fromUTF8(deficit.instrumentId.c_str()) +
        " current_step_ratio=" + juce::String(deficit.melodicStepRatio, 4) + "\n";
    for (const auto& cell : score.cells) {
        auto relevant = false;
        for (std::size_t index = 0; index < cell.notes.size(); ++index) {
            const auto& note = cell.notes[index];
            if (note.instrumentId != deficit.instrumentId) continue;
            if (!relevant) {
                prompt << "CELL " << juce::String::fromUTF8(cell.id.c_str())
                       << " length=" << juce::String(cell.lengthBeats, 2) << "\n";
                relevant = true;
            }
            prompt << "  index=" << static_cast<int>(index)
                   << " beat=" << juce::String(note.beat, 2)
                   << " pitch=" << note.pitch
                   << " duration=" << juce::String(note.durationBeats, 2) << "\n";
        }
    }
    prompt += "PLACEMENTS:\n";
    for (const auto& placement : score.placements)
        prompt << "  cell=" << juce::String::fromUTF8(placement.cellId.c_str())
               << " section=" << placement.sectionIndex
               << " start=" << juce::String(placement.startBeat, 2)
               << " repeats=" << placement.repeats
               << " transpose=" << placement.transpose << "\n";
    return prompt;
}

constexpr auto marginalSpeechPitchSchema = R"json({
  "type":"object",
  "properties":{
    "cell_id":{"type":"string"},
    "note_index":{"type":"integer"},
    "pitch":{"type":"integer"}
  },
  "required":["cell_id","note_index","pitch"],
  "additionalProperties":false
})json";

juce::String selectiveRepairPrompt(const juce::String& direction,
                                   const SongPlan& plan,
                                   const SelectiveRepairPlan& diagnosis) {
    juce::String issues;
    for (const auto& issue : diagnosis.issues)
        issues << "- " << juce::String::fromUTF8(issue.c_str()) << "\n";
    const auto targetIds = instrumentIdsFor(plan, diagnosis.instrumentIndices);
    juce::String blueprintBrief;
    blueprintBrief << "key=" << juce::String::fromUTF8(plan.key.c_str())
                   << " root_pc=" << plan.rootPitchClass
                   << " bars=" << plan.totalBars
                   << " beats_per_bar=" << juce::String(plan.beatsPerBar, 3) << "\n";
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        blueprintBrief << "SECTION " << static_cast<int>(index)
                       << " name=" << juce::String::fromUTF8(section.name.c_str())
                       << " start_bar=" << section.startBar << " bars=" << section.bars
                       << " energy=" << juce::String(section.energy, 2)
                       << " tension=" << juce::String(section.tension, 2)
                       << " density=" << juce::String(section.density, 2) << " chords=";
        for (const auto& event : section.harmonicEvents)
            blueprintBrief << juce::String::fromUTF8(event.chordId.c_str()) << "@"
                           << event.barOffset << ":" << juce::String(event.beatOffset, 2) << ",";
        blueprintBrief << "\n";
    }
    blueprintBrief << "CHORDS ";
    for (const auto& chord : plan.chordPalette) {
        blueprintBrief << juce::String::fromUTF8(chord.id.c_str()) << "(root="
                       << chord.rootPitchClass << ",function="
                       << juce::String::fromUTF8(harmonicFunctionKey(chord.function).data())
                       << ",pcs=";
        for (const auto pitchClass : chord.pitchClasses) blueprintBrief << pitchClass << ".";
        blueprintBrief << ") ";
    }
    blueprintBrief << "\nprotagonist="
                   << juce::String::fromUTF8(plan.narrativeSpine.protagonistInstrumentId.c_str())
                   << " motif=" << juce::String::fromUTF8(plan.narrativeSpine.motifIdentity.c_str())
                   << " resolution=" << juce::String::fromUTF8(plan.narrativeSpine.resolution.c_str())
                   << "\n";
    for (const auto& act : plan.narrativeSpine.acts)
        blueprintBrief << "ACT section=" << juce::String::fromUTF8(act.sectionName.c_str())
                       << " stage=" << juce::String::fromUTF8(narrativeStageKey(act.stage).data())
                       << " target=" << juce::String::fromUTF8(act.resolutionTarget.c_str()) << "\n";
    const auto repairingProtagonist = targetIds.contains(
        plan.narrativeSpine.protagonistInstrumentId);
    return juce::String(
        "You are PULSO's final score editor. The macro form, section harmony, tonal policy, cast and every instrument "
        "not listed below are immutable and already accepted. Return replacement performance_score cells and placements "
        "ONLY for the listed instrument ids. Solve every audible critic finding as one coherent edit. Do not add tracks, "
        "change chords, rewrite unrelated ideas or increase global density. Preserve the song's recognisable motifs while "
        "giving underwritten lines complete phrases, contrasting returns and meaningful rests. If the source-note "
        "deficit is present, replace tiny repeated cells with distinct authored phrases in the same lane; "
        "renaming the same four notes or adding unplaced cells is not a repair. If a pulse or arpeggio is "
        "dominant, create subtraction, mutations and hand-offs instead of a continuous note stream. If closure is weak, "
        "make the final active phrases answer earlier material and resolve harmonic debt. All notes remain strict-grid. "
        "When the critic reports a flat density curve, do not add notes: author section-specific withdrawal, re-entry and "
        "register change in the listed sustained owners so setup, reduced memory, accumulation, climax and release have "
        "clearly different simultaneous weight while an untargeted harmonic owner preserves tonal context. "
        "Keep every note inside the declared registers and compatible with the exact section chords. "
        "For primary_chord_bed, write complete chord attacks, protect at least one intentional two-bar breath while another "
        "harmonic owner preserves context, and end on the declared tonic or on an explicitly intentional stable suspended "
        "settlement. Use compact reusable cells; this is "
        "a bounded repair, not a new composition.\nORIGINAL DIRECTION:\n") + direction +
        "\n" + compositionBehaviorBrief(plan.compositionBehavior) +
        (repairingProtagonist
            ? "\nPROTAGONIST REWRITE CONTRACT:\nThe promoted or transferred phrase is seed material only. Author this protagonist's own premise, question, development, transformed return and coda across multiple sections. Do not clone the flute, counterline or donor contour; establish a recognisable onset grammar, meaningful rests, an answered interval and a decisive final consequence.\n"
            : "") +
        "\nAUDIBLE CRITIC FINDINGS:\n" + issues +
        "TARGET INSTRUMENTS (replace these only):\n" +
        instrumentBlockBrief(plan, diagnosis.instrumentIndices) +
        "\nMEASURED SOURCE-MATERIAL DEFICITS:\n" +
        performanceDeficitBrief(plan, plan.performanceScore, diagnosis.instrumentIndices) +
        "\nCURRENT TARGET MATERIAL (retain its identity while improving it):\n" +
        existingTargetMaterial(plan, targetIds) +
        "\nCOMPACT IMMUTABLE BLUEPRINT:\n" + blueprintBrief +
        "\nIMMUTABLE ENSEMBLE MIDI LANDMARKS (absolute beat,pitch,duration):\n" +
        juce::String::fromUTF8(EnsembleReference::summarize(
            plan, plan.performanceScore, targetIds).c_str()) +
        "Write the replacement against these actual parts, not only their role descriptions.\n";
}

juce::String audibleAuditSummary(const CompositionRenderReport& report) {
    return "production=" + juce::String(report.production.score, 3) +
        " ready=" + juce::String(static_cast<int>(report.production.ready)) +
        " integrity[chromatic=" + juce::String(report.production.unsupportedChromaticNotes) +
        ",sustain=" + juce::String(report.production.invalidSustains) +
        ",overlap=" + juce::String(report.production.unintendedHarshOverlaps) +
        ",low_clash=" + juce::String(static_cast<int>(report.production.lowRegisterVerticalClashes)) +
        ",metric=" + juce::String(static_cast<int>(report.production.metricViolations)) +
        ",duration=" + juce::String(static_cast<int>(report.production.unsafeDurations)) +
        ",orphan=" + juce::String(static_cast<int>(report.production.orphanEvents)) + "]" +
        " | narrative=" + juce::String(report.narrative.score, 3) +
        " ready=" + juce::String(static_cast<int>(report.narrative.creativeReady)) +
        " resolution=" + juce::String(report.narrative.resolutionScore, 3) +
        " density=" + juce::String(report.narrative.densityControl, 3) +
        " | soundscape=" + juce::String(report.soundscape.score, 3) +
        " ready=" + juce::String(static_cast<int>(report.soundscape.ready)) +
        " static=" + juce::String(static_cast<int>(report.soundscape.staticLayerRuns)) +
        " | viability=" + juce::String(report.trackViability.score, 3) +
        " ready=" + juce::String(static_cast<int>(report.trackViability.ready)) +
        " token_tracks=" + juce::String(static_cast<int>(report.trackViability.tokenTracks)) +
        " | deficit=" + juce::String(SelectiveRepair::deficit(report), 3);
}

juce::String localTonalConflictBrief(const SongPlan& plan,
                                    const CompositionRenderReport& report,
                                    const std::set<std::string>& targetIds) {
    const auto& issues = report.finalTonalPass.after.issues;
    if (issues.empty()) return {};
    juce::String feedback;
    feedback << "\nMEASURED HARMONIC COLLISIONS BY PAIR AND BAR (bar numbers are 1-based):\n";
    const auto groups = SelectiveRepair::tonalConflictGroups(
        plan, report.finalTonalPass.after);
    auto shown = std::size_t{};
    for (const auto& group : groups) {
        const auto& firstId = plan.instruments[group.firstInstrument].id;
        const auto& secondId = plan.instruments[group.secondInstrument].id;
        if (!targetIds.empty() && !targetIds.contains(firstId) &&
            !targetIds.contains(secondId)) continue;
        feedback << "- bar " << group.bar + 1 << " "
                 << juce::String::fromUTF8(firstId.c_str()) << " pitch "
                 << group.exampleFirstPitch << " vs "
                 << juce::String::fromUTF8(secondId.c_str()) << " pitch "
                 << group.exampleSecondPitch << " | pair_events="
                 << static_cast<int>(group.events) << " | overlap_beats_total="
                 << juce::String(group.overlapBeats, 2) << " | longest_event_beats="
                 << juce::String(group.longestOverlapBeats, 2) << "\n";
        if (++shown >= 16) break;
    }
    if (shown == 0) feedback << "- No measured harsh-overlap pair touches this target.\n";
    feedback << "Pair events are not a count of bad notes. Verify the interval in its harmony and "
                "musical duration before editing. Preserve intentional suspensions and every "
                "unlisted instrument; address only collisions involving the selected target.\n";
    return feedback;
}

juce::String compactVoicingPatchSchema() {
    return R"json({"type":"object","properties":{"voicings":{"type":"array","items":{"type":"object","properties":{"section_index":{"type":"integer"},"start_beat":{"type":"number"},"duration_beats":{"type":"number"},"pitches":{"type":"array","items":{"type":"integer"}}},"required":["section_index","start_beat","duration_beats","pitches"],"additionalProperties":false}}},"required":["voicings"],"additionalProperties":false})json";
}

juce::String compactVoicingPatchPrompt(
    const SongPlan& plan, const Pattern& rendered,
    const TonalAuditReport& audit, std::size_t instrumentIndex,
    const std::vector<ChordVoicingTarget>& targets) {
    const auto& owner = plan.instruments[instrumentIndex];
    juce::String prompt;
    prompt << "You are correcting only specific chord voicings in an already AI-written "
              "MIDI performance. Return exactly one voicings object per listed target, "
              "with the identical section_index, start_beat and duration_beats. "
              "Choose two to six distinct MIDI pitches within the owner's register; "
              "the other notes, times, dynamics, tracks and sections are immutable. "
              "Do not respond with a full performance_score, cells, placements or prose. "
              "Only the measured unresolved pitch pairs listed for each target are blocking. "
              "Preserve declared chord-native major sevenths and dominant guide tones, "
              "but avoid prolonged close seconds in the bass register. Voice the actual "
              "chord against simultaneous bass and melody; resolve unsupported suspensions. "
              "Keep a complete, "
              "musical chord and smooth voice leading with neighbouring voicings.\n"
           << "KEY " << juce::String::fromUTF8(plan.key.c_str())
           << " | TARGET " << juce::String::fromUTF8(owner.id.c_str())
           << " | REGISTER " << owner.minimumPitch << ".." << owner.maximumPitch << "\n";
    for (const auto& target : targets) {
        const auto& section = plan.sections[static_cast<std::size_t>(target.sectionIndex)];
        const auto absoluteStart = section.startBar * plan.beatsPerBar + target.sectionBeat;
        const auto absoluteEnd = absoluteStart + target.durationBeats;
        prompt << "TARGET section_index=" << target.sectionIndex
               << " section=" << juce::String::fromUTF8(section.name.c_str())
               << " start_beat=" << juce::String(target.sectionBeat, 3)
               << " duration_beats=" << juce::String(target.durationBeats, 3)
               << " conflicts=" << static_cast<int>(target.conflictEvents)
               << " original_pitches=";
        for (const auto pitch : target.pitches) prompt << pitch << ",";
        prompt << " measured_unresolved_pairs=";
        auto measuredPairs = 0;
        for (const auto& issue : audit.issues) {
            if (issue.kind != "harsh_overlap" ||
                (issue.partId != instrumentIndex + 1 &&
                 issue.otherPartId != instrumentIndex + 1) ||
                issue.beat < absoluteStart - .001 ||
                issue.beat >= absoluteEnd - .001) continue;
            const auto ownerPitch = issue.partId == instrumentIndex + 1
                ? issue.pitch : issue.otherPitch;
            if (std::find(target.pitches.begin(), target.pitches.end(), ownerPitch) ==
                target.pitches.end()) continue;
            const auto otherPitch = issue.partId == instrumentIndex + 1
                ? issue.otherPitch : issue.pitch;
            const auto otherPart = issue.partId == instrumentIndex + 1
                ? issue.otherPartId : issue.partId;
            prompt << "[beat=" << juce::String(issue.beat, 2)
                   << ",owner=" << ownerPitch << ",other=" << otherPitch
                   << ",other_part=" << otherPart
                   << ",overlap=" << juce::String(issue.overlapBeats, 2) << "]";
            if (++measuredPairs >= 8) break;
        }
        if (measuredPairs == 0) prompt << "none";
        const HarmonicEvent* activeEvent = nullptr;
        for (const auto& event : section.harmonicEvents)
            if (event.barOffset * plan.beatsPerBar + event.beatOffset <=
                target.sectionBeat + .001 &&
                (activeEvent == nullptr ||
                 event.barOffset * plan.beatsPerBar + event.beatOffset >=
                     activeEvent->barOffset * plan.beatsPerBar + activeEvent->beatOffset))
                activeEvent = &event;
        if (activeEvent != nullptr)
            for (const auto& chord : plan.chordPalette)
                if (chord.id == activeEvent->chordId) {
                    prompt << " chord=" << juce::String::fromUTF8(chord.label.c_str())
                           << " root_pc=" << chord.rootPitchClass
                           << " bass_pc=" << chord.bassPitchClass
                           << " allowed_pc=";
                    for (const auto pitchClass : chord.pitchClasses)
                        prompt << pitchClass << ",";
                    break;
                }
        double previousStart = -1.0;
        double nextStart = std::numeric_limits<double>::infinity();
        for (const auto& note : rendered.notes) {
            if (note.partId != instrumentIndex + 1) continue;
            if (note.startBeat < absoluteStart - .001)
                previousStart = std::max(previousStart, note.startBeat);
            else if (note.startBeat > absoluteStart + .001)
                nextStart = std::min(nextStart, note.startBeat);
        }
        prompt << " neighboring_owner_MIDI=";
        auto neighboring = 0;
        for (const auto& note : rendered.notes) {
            if (note.partId != instrumentIndex + 1 ||
                (std::abs(note.startBeat - previousStart) >= .001 &&
                 std::abs(note.startBeat - nextStart) >= .001)) continue;
            prompt << "[" << juce::String(note.startBeat, 2) << "-"
                   << juce::String(note.endBeat(), 2) << ":" << note.pitch << "]";
            if (++neighboring >= 12) break;
        }
        prompt << "\n  simultaneous_other_MIDI=";
        auto shown = 0;
        for (const auto& note : rendered.notes) {
            if (note.partId == 0 || note.partId > plan.instruments.size() ||
                note.partId == instrumentIndex + 1 ||
                note.startBeat >= absoluteEnd || note.endBeat() <= absoluteStart) continue;
            const auto& other = plan.instruments[note.partId - 1];
            if (other.sourceVoice != VoiceId::SubBass &&
                other.sourceVoice != VoiceId::MovementBass &&
                other.sourceVoice != VoiceId::HarmonicFoundation &&
                other.id != plan.narrativeSpine.protagonistInstrumentId) continue;
            prompt << "[" << juce::String::fromUTF8(other.id.c_str()) << ":"
                   << juce::String(note.startBeat, 2) << "-"
                   << juce::String(note.endBeat(), 2) << ":" << note.pitch << "]";
            if (++shown >= 24) break;
        }
        if (shown == 0) prompt << "none_written_yet";
        prompt << "\n";
    }
    return prompt;
}

bool parseCompactVoicingPatches(
    const juce::String& text, const std::vector<ChordVoicingTarget>& targets,
    std::vector<ChordVoicingPatch>& patches, juce::String& error) {
    const auto parsed = juce::JSON::parse(text);
    const auto* root = parsed.getDynamicObject();
    const auto* values = root == nullptr ? nullptr : root->getProperty("voicings").getArray();
    if (values == nullptr || values->isEmpty() || values->size() > static_cast<int>(targets.size())) {
        error = "Compact voicing reply has no bounded voicings";
        return false;
    }
    for (const auto& value : *values) {
        const auto* item = value.getDynamicObject();
        const auto* pitches = item == nullptr ? nullptr : item->getProperty("pitches").getArray();
        if (item == nullptr || pitches == nullptr || pitches->size() < 2 || pitches->size() > 6) {
            error = "Compact voicing reply contains an invalid chord";
            return false;
        }
        ChordVoicingPatch patch;
        patch.sectionIndex = static_cast<int>(item->getProperty("section_index"));
        patch.sectionBeat = static_cast<double>(item->getProperty("start_beat"));
        patch.durationBeats = static_cast<double>(item->getProperty("duration_beats"));
        const auto requested = std::find_if(targets.begin(), targets.end(), [&](const auto& target) {
            return target.sectionIndex == patch.sectionIndex &&
                std::abs(target.sectionBeat - patch.sectionBeat) < .001 &&
                std::abs(target.durationBeats - patch.durationBeats) < .001;
        });
        if (requested == targets.end()) {
            error = "Compact voicing reply attempted to edit an unrequested location";
            return false;
        }
        // The prompt prints three decimals; bind the validated target back to
        // its full-precision measured MIDI coordinates before the splice.
        patch.sectionBeat = requested->sectionBeat;
        patch.durationBeats = requested->durationBeats;
        for (const auto& pitch : *pitches) patch.pitches.push_back(static_cast<int>(pitch));
        patches.push_back(std::move(patch));
    }
    return true;
}

std::vector<std::size_t> localTonalCulprits(const SongPlan& plan,
                                           const CompositionRenderReport& report) {
    std::vector<double> counts(plan.instruments.size());
    for (const auto& group : SelectiveRepair::tonalConflictGroups(
             plan, report.finalTonalPass.after)) {
        const auto weight = static_cast<double>(group.events) +
            std::min(8.0, group.overlapBeats * .25);
        counts[group.firstInstrument] += weight;
        if (group.secondInstrument != group.firstInstrument)
            counts[group.secondInstrument] += weight;
    }
    for (const auto& issue : report.finalTonalPass.after.issues) {
        if (issue.partId == 0 || issue.partId > counts.size()) continue;
        if (issue.kind == "invalid_sustain" ||
            issue.kind == "unsupported_chromatic") counts[issue.partId - 1] += 4.0;
    }
    std::vector<std::size_t> indices(counts.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::stable_sort(indices.begin(), indices.end(), [&](auto left, auto right) {
        return counts[left] > counts[right];
    });
    indices.erase(std::remove_if(indices.begin(), indices.end(), [&](auto index) {
        return counts[index] <= 0.0 ||
            ElectronicCompositionFabric::rendererOwnedDestination(plan, plan.instruments[index]);
    }), indices.end());
    return indices;
}

bool localUntouchedPassagesPreserved(
    const SongPlan& plan, const Pattern& before, const Pattern& after,
    const TonalAuditReport& audit, const std::set<std::string>& targetIds) {
    const auto groups = SelectiveRepair::tonalConflictGroups(plan, audit);
    std::map<std::uint16_t, std::set<int>> focusBars;
    for (const auto& group : groups) {
        for (const auto index : {group.firstInstrument, group.secondInstrument}) {
            if (index < plan.instruments.size() &&
                targetIds.contains(plan.instruments[index].id))
                focusBars[static_cast<std::uint16_t>(index + 1)].insert(group.bar);
        }
    }
    using Signature = std::tuple<std::uint16_t, long long, long long, int>;
    const auto signatures = [&](const Pattern& pattern) {
        std::vector<Signature> result;
        for (const auto& note : pattern.notes) {
            const auto found = focusBars.find(note.partId);
            if (found == focusBars.end()) continue;
            const auto firstBar = static_cast<int>(std::floor(
                note.startBeat / std::max(1.0, plan.beatsPerBar)));
            const auto lastBar = static_cast<int>(std::floor(
                (note.endBeat() - .0001) / std::max(1.0, plan.beatsPerBar)));
            auto focused = false;
            for (auto bar = firstBar; bar <= lastBar; ++bar)
                if (found->second.contains(bar)) { focused = true; break; }
            if (focused) continue;
            result.emplace_back(note.partId,
                std::llround(note.startBeat * 10000.0),
                std::llround(note.durationBeats * 10000.0), note.pitch);
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    return signatures(before) == signatures(after);
}

juce::String AiComposer::defaultModel() { return model; }

juce::String AiComposer::performanceSchemaFor(
    const SongPlan& plan, const std::vector<std::size_t>& indices) {
    return boundedPerformanceSchema(plan, indices);
}

juce::String AiComposer::directWindowSchemaFor(
    const SongPlan& plan, const std::vector<std::size_t>& indices) {
    return directWindowSchema(plan, indices);
}

bool AiComposer::parseDirectWindowJson(
    const juce::String& text, const SongPlan& plan,
    const std::vector<std::size_t>& indices,
    std::size_t sectionIndex, int firstBar, int bars,
    PerformanceScore& score, juce::String& error) {
    return parseDirectWindow(text, plan, indices, sectionIndex, firstBar, bars,
                             score, error);
}

juce::String AiComposer::defaultReasoningEffort() { return reasoningEffort; }

bool AiComposer::structuredOutputSchemaIsValid() {
    return !juce::JSON::parse(schema()).isVoid();
}

bool AiComposer::songPlanSchemaIsValid() {
    return !juce::JSON::parse(songPlanSchema).isVoid();
}

bool AiComposer::incrementalSchemasAreValid() {
    const auto macro = juce::JSON::parse(macroBlueprintSchema());
    const auto cast = juce::JSON::parse(castBlueprintSchema());
    const auto manifest = juce::JSON::parse(castManifestSchema());
    const auto exactManifest = juce::JSON::parse(castManifestSchema(50));
    const auto supplement = juce::JSON::parse(castSupplementSchema(3));
    const auto castDetail = juce::JSON::parse(castDetailShardSchema());
    const auto performance = juce::JSON::parse(performanceBlockSchema);
    return !macro.isVoid() && !cast.isVoid() && !manifest.isVoid() && !exactManifest.isVoid() &&
        AiComposer::castManifestUsesExactCount(50) &&
        !castDetail.isVoid() && !performance.isVoid() &&
        !macroBlueprintSchema().contains("performance_score") &&
        !macroBlueprintSchema().contains("instruments") &&
        !macroBlueprintSchema().contains("protagonist_instrument_id") &&
        castManifestSchema().contains("protagonist_instrument_id") &&
        !castManifestSchema().contains("timbre_signature") &&
        castDetailShardSchema().contains("timbre_signature") &&
        performanceBlockSchema.contains("covered_instrument_ids");
}

std::size_t AiComposer::maximumSongInstruments() noexcept { return maximumInstruments; }

std::size_t AiComposer::castDetailShardCount(std::size_t instruments) noexcept {
    instruments = std::min(instruments, maximumInstruments);
    return instruments == 0 ? 0 : (instruments + instrumentsPerCastShard - 1) /
        instrumentsPerCastShard;
}

std::size_t AiComposer::selectiveRepairShardCount(std::size_t instruments) noexcept {
    return instruments == 0 ? 0 :
        (instruments + instrumentsPerEditorialRepairShard - 1) /
            instrumentsPerEditorialRepairShard;
}

std::size_t AiComposer::performanceBlockCount(std::size_t instruments) noexcept {
    instruments = std::min(instruments, maximumInstruments);
    return instruments == 0 ? 0 : (instruments + instrumentsPerPerformanceBlock - 1) /
        instrumentsPerPerformanceBlock;
}

std::vector<std::vector<std::size_t>> AiComposer::performanceWritingBlocks(
    const SongPlan& plan, bool localEditorial) {
    std::vector<std::vector<std::size_t>> blocks;
    std::vector<std::size_t> orderedInstruments;
    orderedInstruments.reserve(plan.instruments.size());
    for (std::size_t index = 0; index < plan.instruments.size(); ++index)
        if (!ElectronicCompositionFabric::rendererOwnedDestination(
                plan, plan.instruments[index]))
            orderedInstruments.push_back(index);

    std::stable_sort(orderedInstruments.begin(), orderedInstruments.end(),
        [&](auto left, auto right) {
            const auto& a = plan.instruments[left];
            const auto& b = plan.instruments[right];
            const auto familyA = static_cast<int>(voiceDefinition(a.sourceVoice).family);
            const auto familyB = static_cast<int>(voiceDefinition(b.sourceVoice).family);
            if (familyA != familyB) return familyA < familyB;
            return a.contentLaneId < b.contentLaneId;
        });

    const auto takeFirst = [&](const auto& predicate) -> std::optional<std::size_t> {
        const auto owner = std::find_if(
            orderedInstruments.begin(), orderedInstruments.end(), predicate);
        if (owner == orderedInstruments.end()) return std::nullopt;
        const auto index = *owner;
        orderedInstruments.erase(owner);
        return index;
    };
    const auto isolateProtagonist = [&] {
        const auto owner = takeFirst([&](const auto index) {
            return plan.instruments[index].id ==
                plan.narrativeSpine.protagonistInstrumentId;
        });
        if (owner) blocks.push_back({*owner});
    };

    if (!localEditorial) {
        isolateProtagonist();
    } else {
        // The explicit chord lane and the principal low anchor are one musical
        // foundation. Asking for them in the same structured response gives the
        // model both sides of every vertical decision. A secondary movement
        // bass remains independent and writes afterwards against the accepted
        // foundation ledger.
        std::vector<std::size_t> foundationBlock;
        if (const auto bed = takeFirst([&](const auto index) {
                return plan.instruments[index].role.find("primary_chord_bed") !=
                    std::string::npos;
            }))
            foundationBlock.push_back(*bed);

        auto primaryBass = takeFirst([&](const auto index) {
            return plan.instruments[index].sourceVoice == VoiceId::SubBass;
        });
        if (!primaryBass)
            primaryBass = takeFirst([&](const auto index) {
                return plan.instruments[index].sourceVoice == VoiceId::MovementBass;
            });
        if (primaryBass) foundationBlock.push_back(*primaryBass);
        if (!foundationBlock.empty()) blocks.push_back(std::move(foundationBlock));

        for (const auto voice : {VoiceId::SubBass, VoiceId::MovementBass}) {
            const auto owner = takeFirst([&](const auto index) {
                return plan.instruments[index].sourceVoice == voice;
            });
            if (owner) blocks.push_back({*owner});
        }
        isolateProtagonist();
    }

    const auto performanceShardSize = localEditorial ? std::size_t{2} :
        plan.totalBars >= 128 ? std::size_t{6} : instrumentsPerPerformanceBlock;
    const auto narrativeShardSize = std::min(performanceShardSize, std::size_t{8});
    std::vector<std::size_t> narrativeOwners;
    std::vector<std::size_t> supportingOwners;
    for (const auto index : orderedInstruments) {
        const auto& instrument = plan.instruments[index];
        const auto narrative = instrument.lineRelationship == "call_response" ||
            instrument.sourceVoice == VoiceId::MovementBass ||
            instrument.role.find("primary_chord_bed") != std::string::npos ||
            ElectronicRoleContract::motionOwner(instrument);
        (narrative ? narrativeOwners : supportingOwners).push_back(index);
    }
    if (!narrativeOwners.empty()) {
        const auto companions = std::min<std::size_t>(
            supportingOwners.size(), narrativeOwners.size() < narrativeShardSize
                ? narrativeShardSize - narrativeOwners.size() : 0);
        narrativeOwners.insert(narrativeOwners.end(), supportingOwners.begin(),
            supportingOwners.begin() + static_cast<std::ptrdiff_t>(companions));
        supportingOwners.erase(supportingOwners.begin(),
            supportingOwners.begin() + static_cast<std::ptrdiff_t>(companions));
        for (std::size_t begin = 0; begin < narrativeOwners.size();
             begin += performanceShardSize) {
            const auto end = std::min(narrativeOwners.size(), begin + performanceShardSize);
            blocks.emplace_back(
                narrativeOwners.begin() + static_cast<std::ptrdiff_t>(begin),
                narrativeOwners.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    for (std::size_t begin = 0; begin < supportingOwners.size();
         begin += performanceShardSize) {
        const auto end = std::min(supportingOwners.size(), begin + performanceShardSize);
        blocks.emplace_back(
            supportingOwners.begin() + static_cast<std::ptrdiff_t>(begin),
            supportingOwners.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return blocks;
}

bool AiComposer::castManifestUsesExactCount(std::size_t instruments) noexcept {
    if (instruments == 0 || instruments > maximumInstruments) return false;
    const auto schema = juce::JSON::parse(castManifestSchema(instruments));
    const auto* root = schema.getDynamicObject();
    const auto* properties = root == nullptr
        ? nullptr : root->getProperty("properties").getDynamicObject();
    const auto* cast = properties == nullptr
        ? nullptr : properties->getProperty("instruments").getDynamicObject();
    return cast != nullptr &&
        static_cast<int>(cast->getProperty("minItems")) == static_cast<int>(instruments) &&
        static_cast<int>(cast->getProperty("maxItems")) == static_cast<int>(instruments);
}

bool AiComposer::reconcileCastManifest(const juce::String& acceptedManifest,
                                       const juce::String& supplement,
                                       std::size_t requestedCount,
                                       juce::String& reconciledManifest,
                                       juce::String& error) {
    return mergeCastManifestSupplement(acceptedManifest, supplement, requestedCount,
                                       reconciledManifest, error);
}

bool AiComposer::enforceExplicitCastExclusions(
    const juce::String& castManifest, const juce::String& creativeDirection,
    juce::String& sanitizedManifest, std::size_t& removedInstruments,
    juce::String& error) {
    return enforceExplicitCastExclusionsImpl(castManifest, creativeDirection,
                                             sanitizedManifest, removedInstruments, error);
}

bool AiComposer::bindCastProtagonist(const juce::String& macroBlueprint,
                                     const juce::String& castManifest,
                                     juce::String& mergedBlueprint,
                                    juce::String& error) {
    mergedBlueprint.clear();
    auto reconciledManifest = castManifest;
    bool resolutionLinked{};
    if (!reconcileProtagonistResolutionManifest(
            macroBlueprint, reconciledManifest, resolutionLinked, error) ||
        !validateProtagonistManifest(macroBlueprint, reconciledManifest, error)) return false;
    mergedBlueprint = mergeBlueprintPhases(macroBlueprint, reconciledManifest);
    if (mergedBlueprint.isEmpty()) {
        error = "Could not bind the authoritative cast protagonist into the macro blueprint";
        return false;
    }
    error.clear();
    return true;
}

bool AiComposer::reconcileOrchestrationMatrix(
    const juce::String& macroBlueprint, const juce::String& castManifest,
    juce::String& reconciledManifest, juce::String& report,
    juce::String& error) {
    reconciledManifest = castManifest;
    bool changed{};
    if (!reconcileOrchestrationMatrixManifest(
            macroBlueprint, reconciledManifest, changed, report, error)) return false;
    if (!validateOrchestrationMatrixManifest(
            macroBlueprint, reconciledManifest, error)) return false;
    error.clear();
    return true;
}

bool AiComposer::parsePerformanceBlockJson(
    const juce::String& text, const SongPlan& plan,
    const std::vector<std::size_t>& assignedInstruments,
    PerformanceScore& score, juce::String& error) {
    return parsePerformanceBlock(text, plan,
        instrumentIdsFor(plan, assignedInstruments), score, error);
}

bool AiComposer::parseCoherentProofJson(const juce::String& text, const SongPlan& plan,
                                        PerformanceScore& score, juce::String& error) {
    score = {};
    const auto parsed = juce::JSON::parse(text);
    const auto* root = parsed.getDynamicObject();
    const auto* parts = root == nullptr ? nullptr : root->getProperty("parts").getArray();
    if (parts == nullptr || parts->size() != 3 || plan.instruments.size() != 3 ||
        plan.sections.empty()) {
        error = "Coherent proof requires exactly three complete instrument parts";
        return false;
    }
    const auto bed = SelectiveRepair::centralChordBedOwner(plan);
    const auto lead = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [&](const auto& item) { return item.id == plan.narrativeSpine.protagonistInstrumentId; });
    const auto bass = std::find_if(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& item) { return isVoiceInFamily(item.sourceVoice, VoiceFamily::Bass); });
    if (!bed || lead == plan.instruments.end() || bass == plan.instruments.end() ||
        *bed >= plan.instruments.size() ||
        plan.instruments[*bed].id == lead->id || plan.instruments[*bed].id == bass->id ||
        lead->id == bass->id) {
        error = "Coherent proof cast must contain distinct chord-bed, bass and lead owners";
        return false;
    }
    const auto numeric = [](const juce::var& value) {
        return value.isInt() || value.isInt64() || value.isDouble();
    };
    std::set<std::string> seen;
    std::array<std::vector<std::pair<double, double>>, 3> noteSpans;
    std::array<int, 3> noteCounts{};
    const auto totalBeats = plan.totalBars * plan.beatsPerBar;
    for (const auto& partValue : *parts) {
        const auto* part = partValue.getDynamicObject();
        const auto id = part == nullptr ? std::string{} :
            part->getProperty("instrument_id").toString().trim().toStdString();
        const auto owner = std::find_if(plan.instruments.begin(), plan.instruments.end(),
            [&](const auto& item) { return item.id == id; });
        const auto* notes = part == nullptr ? nullptr : part->getProperty("notes").getArray();
        if (owner == plan.instruments.end() || notes == nullptr || notes->isEmpty() ||
            !seen.insert(id).second) {
            error = "Coherent proof has a missing, duplicate or unknown part";
            return false;
        }
        const auto ownerIndex = static_cast<std::size_t>(std::distance(plan.instruments.begin(), owner));
        if (notes->size() > 256) {
            error = "Coherent proof exceeds the bounded note budget";
            return false;
        }
        for (const auto& noteValue : *notes) {
            const auto* note = noteValue.getDynamicObject();
            if (note == nullptr || !numeric(note->getProperty("beat")) ||
                !numeric(note->getProperty("duration_beats")) ||
                !numeric(note->getProperty("pitch")) ||
                !numeric(note->getProperty("velocity"))) {
                error = "Coherent proof note has an invalid numeric field";
                return false;
            }
            const auto beat = static_cast<double>(note->getProperty("beat"));
            const auto duration = static_cast<double>(note->getProperty("duration_beats"));
            const auto pitch = static_cast<int>(note->getProperty("pitch"));
            const auto velocity = static_cast<int>(note->getProperty("velocity"));
            if (!std::isfinite(beat) || !std::isfinite(duration) || beat < 0.0 ||
                beat >= totalBeats || duration < 0.125 || beat + duration > totalBeats + 0.0001 ||
                std::abs(beat * 4.0 - std::round(beat * 4.0)) > 0.001 ||
                pitch < owner->minimumPitch || pitch > owner->maximumPitch ||
                velocity < 1 || velocity > 127) {
                error = "Coherent proof note is outside the timeline, grid or instrument register";
                return false;
            }
            const auto sectionIt = std::find_if(plan.sections.begin(), plan.sections.end(),
                [&](const auto& section) {
                    const auto start = section.startBar * plan.beatsPerBar;
                    const auto end = (section.startBar + section.bars) * plan.beatsPerBar;
                    return beat >= start && beat < end && beat + duration <= end + 0.0001;
                });
            if (sectionIt == plan.sections.end()) {
                error = "Coherent proof note crosses or misses an authored section";
                return false;
            }
            const auto sectionIndex = static_cast<int>(std::distance(plan.sections.begin(), sectionIt));
            const auto sectionStart = sectionIt->startBar * plan.beatsPerBar;
            const auto localBeat = beat - sectionStart;
            const auto chunkIndex = static_cast<int>(std::floor(localBeat / 64.0));
            const auto chunkStart = chunkIndex * 64.0;
            const auto chunkLength = std::min(64.0,
                sectionIt->bars * plan.beatsPerBar - chunkStart);
            if (localBeat + duration > chunkStart + chunkLength + 0.0001) {
                error = "Coherent proof note crosses a 64-beat score-cell boundary";
                return false;
            }
            const auto cellId = id + "_section_" + std::to_string(sectionIndex) +
                "_chunk_" + std::to_string(chunkIndex);
            auto cell = std::find_if(score.cells.begin(), score.cells.end(),
                [&](const auto& item) { return item.id == cellId; });
            if (cell == score.cells.end()) {
                PerformanceCell created;
                created.id = cellId;
                created.lengthBeats = chunkLength;
                created.ownedVoices = {owner->sourceVoice};
                created.themeId = id;
                created.narrativeFunction = sectionIt->function;
                score.cells.push_back(std::move(created));
                PerformancePlacement placement;
                placement.cellId = cellId;
                placement.sectionIndex = sectionIndex;
                placement.startBeat = chunkStart;
                placement.purpose = sectionIt->function;
                score.placements.push_back(std::move(placement));
                cell = std::prev(score.cells.end());
            }
            cell->notes.push_back({localBeat - chunkStart, duration, pitch, velocity,
                owner->sourceVoice, MetricIntent::StrictGrid, id});
            ++noteCounts[ownerIndex];
            noteSpans[ownerIndex].push_back({beat, beat + duration});
        }
    }
    const auto leadIndex = static_cast<std::size_t>(std::distance(plan.instruments.begin(), lead));
    if (seen.size() != 3) {
        error = "Coherent proof has a missing instrument part";
        return false;
    }
    auto leadSpans = noteSpans[leadIndex];
    std::sort(leadSpans.begin(), leadSpans.end());
    for (std::size_t index = 1; index < leadSpans.size(); ++index)
        if (leadSpans[index].first < leadSpans[index - 1].second - 0.001) {
            error = "Coherent proof protagonist is not monophonic";
            return false;
        }
    std::vector<double> sectionLengths;
    for (const auto& section : plan.sections)
        sectionLengths.push_back(section.bars * plan.beatsPerBar);
    const auto report = PerformanceScoreEngine::normalize(score, plan.sections.size(), sectionLengths);
    const auto authoredCount = std::accumulate(noteCounts.begin(), noteCounts.end(), 0);
    if (report.notesAccepted != static_cast<std::size_t>(authoredCount) ||
        report.notesRejected != 0 || report.cellsRejected != 0 ||
        report.placementsRejected != 0) {
        error = "Coherent proof normalization would modify or discard authored notes";
        return false;
    }
    error.clear();
    return true;
}

bool AiComposer::applyCoherentProofRevisionJson(
    const juce::String& original, const juce::String& revision,
    const SongPlan& plan, const std::vector<CoherentRevisionWindow>& windows,
    PerformanceScore& score, juce::String& error) {
    auto originalValue = juce::JSON::parse(original);
    const auto revisionValue = juce::JSON::parse(revision);
    auto* root = originalValue.getDynamicObject();
    auto* originalParts = root == nullptr ? nullptr : root->getProperty("parts").getArray();
    const auto* revisionRoot = revisionValue.getDynamicObject();
    const auto* patches = revisionRoot == nullptr ? nullptr :
        revisionRoot->getProperty("patches").getArray();
    if (originalParts == nullptr || patches == nullptr ||
        patches->size() != static_cast<int>(windows.size()) || windows.empty()) {
        error = "Coherent revision has an invalid patch count";
        return false;
    }
    const auto numeric = [](const juce::var& value) {
        return value.isInt() || value.isInt64() || value.isDouble();
    };
    std::set<std::size_t> used;
    for (const auto& patchValue : *patches) {
        const auto* patch = patchValue.getDynamicObject();
        if (patch == nullptr || !numeric(patch->getProperty("start_beat")) ||
            !numeric(patch->getProperty("end_beat"))) {
            error = "Coherent revision has invalid target coordinates";
            return false;
        }
        const auto id = patch->getProperty("instrument_id").toString().toStdString();
        const auto start = static_cast<double>(patch->getProperty("start_beat"));
        const auto end = static_cast<double>(patch->getProperty("end_beat"));
        const auto target = std::find_if(windows.begin(), windows.end(),
            [&](const auto& window) {
                return window.partId > 0 && window.partId <= plan.instruments.size() &&
                    plan.instruments[window.partId - 1].id == id &&
                    std::abs(window.startBeat - start) < 0.0001 &&
                    std::abs(window.endBeat - end) < 0.0001;
            });
        const auto* replacement = patch->getProperty("notes").getArray();
        if (target == windows.end() || replacement == nullptr ||
            !used.insert(static_cast<std::size_t>(target - windows.begin())).second) {
            error = "Coherent revision changed an unrequested or duplicate window";
            return false;
        }
        auto* part = [&]() -> juce::DynamicObject* {
            for (auto& partValue : *originalParts) {
                auto* candidate = partValue.getDynamicObject();
                if (candidate != nullptr &&
                    candidate->getProperty("instrument_id").toString().toStdString() == id)
                    return candidate;
            }
            return nullptr;
        }();
        auto* existing = part == nullptr ? nullptr : part->getProperty("notes").getArray();
        if (existing == nullptr) {
            error = "Coherent revision targets a missing part";
            return false;
        }
        juce::Array<juce::var> kept;
        for (const auto& noteValue : *existing) {
            const auto* note = noteValue.getDynamicObject();
            if (note == nullptr || !numeric(note->getProperty("beat")) ||
                !numeric(note->getProperty("duration_beats"))) {
                error = "Original coherent score is invalid";
                return false;
            }
            const auto beat = static_cast<double>(note->getProperty("beat"));
            const auto stop = beat + static_cast<double>(note->getProperty("duration_beats"));
            if ((beat < start - 0.0001 && stop > start + 0.0001) ||
                (beat < end - 0.0001 && stop > end + 0.0001)) {
                error = "Coherent revision would cut a sustained original note";
                return false;
            }
            if (beat < start - 0.0001 || beat >= end - 0.0001) kept.add(noteValue);
        }
        for (const auto& noteValue : *replacement) {
            const auto* note = noteValue.getDynamicObject();
            if (note == nullptr || !numeric(note->getProperty("beat")) ||
                !numeric(note->getProperty("duration_beats")) ||
                !numeric(note->getProperty("pitch")) ||
                !numeric(note->getProperty("velocity"))) {
                error = "Coherent revision has invalid note fields";
                return false;
            }
            const auto beat = static_cast<double>(note->getProperty("beat"));
            const auto duration = static_cast<double>(note->getProperty("duration_beats"));
            if (!std::isfinite(beat) || !std::isfinite(duration) ||
                beat < start - 0.0001 || beat >= end - 0.0001 ||
                beat + duration > end + 0.0001) {
                error = "Coherent revision note escapes its requested window";
                return false;
            }
            kept.add(noteValue);
        }
        part->setProperty("notes", kept);
    }
    return parseCoherentProofJson(juce::JSON::toString(originalValue), plan, score, error);
}

std::size_t AiComposer::requestedInstrumentCount(const juce::String& direction) noexcept {
    return requestedInstrumentCountFromDirection(direction);
}

void AiComposer::applyExplicitInstrumentCommitments(
    SongPlan& plan, const juce::String& creativeDirection) {
    markExplicitPromptInstrumentIdentities(plan, creativeDirection);
}

AiComposition AiComposer::compose(const juce::String& creativeDirection, int bars, double bpm,
                                  const Pattern* reference, std::uint8_t lockedLayers,
                                  std::stop_token token, juce::String& error) {
    AiComposition result;
    const auto apiKey = ApiCredentialStore::apiKey();
    if (apiKey.isEmpty()) {
        error = "OpenAI API key is not configured";
        return result;
    }
    if (!structuredOutputSchemaIsValid()) {
        error = "Internal structured-output schema is invalid";
        return result;
    }
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return result;
    }

    const auto direction = creativeDirection.trim().isEmpty()
                               ? "Create an emotionally clear, memorable contemporary instrumental idea."
                               : creativeDirection.trim();
    const auto prompt = juce::String(
        "You are the composition director for PULSO. Compose one coherent symbolic MIDI idea as a complete ensemble. "
        "Harmony is the source of truth: use intentional harmonic rhythm, voice leading, tension and cadence. "
        "Melody must have one recognisable motif with statement, answer, development and cadence. Bass must express "
        "the chord movement and drums must reinforce the same phrasing. Unless the request explicitly asks for broken "
        "rhythms, anchor the kick on every quarter note and let hats and percussion create syncopation. Avoid random scale runs, repetitive grids and "
        "meaningless density. Times and durations are quarter-note beats from zero. Use only MIDI pitches 0-127 and "
        "velocities 1-127. Drums use GM pitches. Every layer must be non-empty. The exact length is ") +
        juce::String(bars * 4) + " beats (" + juce::String(bars) + " bars of 4/4) at " +
        juce::String(bpm, 1) + " BPM. Creative direction: " + direction +
        "\nThe following layers are locked references. Compose all layers, but make unlocked layers support them exactly:\n" +
        describeReference(reference, lockedLayers);

    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"background\":true,\"reasoning\":{\"effort\":\"" + reasoningEffort +
        "\"},\"max_output_tokens\":16000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_composition\","
        "\"strict\":true,\"schema\":" + schema() + "}}}";

    const auto http = performRequest(body, apiKey, token, std::chrono::minutes(5));
    if (!http.connected || http.status < 200 || http.status >= 300 ||
        http.cancelled || http.timedOut) {
        error = apiErrorMessage(http);
        return result;
    }
    const auto response = juce::JSON::parse(http.body);
    const auto outputText = extractOutputText(response);
    if (outputText.isEmpty()) {
        error = "OpenAI returned no structured composition";
        return result;
    }
    if (!parseCompositionJson(outputText, bars, result, error)) return {};
    return result;
}

bool AiComposer::parseCompositionJson(const juce::String& text, int requestedBars,
                                      AiComposition& result, juce::String& error) {
    const auto parsed = juce::JSON::parse(text);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) {
        error = "Composition JSON is invalid";
        return false;
    }
    const auto bars = static_cast<int>(object->getProperty("bars"));
    if (bars != requestedBars || bars < 1 || bars > 16) {
        error = "Composition length does not match the request";
        return false;
    }
    result = {};
    result.title = object->getProperty("title").toString().trim();
    result.key = object->getProperty("key").toString().trim();
    result.summary = object->getProperty("summary").toString().trim();
    result.pattern.lengthBeats = bars * 4.0;

    for (std::size_t layer = 0; layer < layerNames.size(); ++layer) {
        const auto* notes = object->getProperty(layerNames[layer]).getArray();
        if (notes == nullptr || notes->isEmpty()) {
            error = juce::String(layerNames[layer]) + " layer is empty";
            return false;
        }
        for (const auto& item : *notes) {
            const auto* note = item.getDynamicObject();
            if (note == nullptr) continue;
            const auto start = static_cast<double>(note->getProperty("start"));
            const auto duration = static_cast<double>(note->getProperty("duration"));
            const auto pitch = static_cast<int>(note->getProperty("pitch"));
            const auto velocity = static_cast<int>(note->getProperty("velocity"));
            if (!std::isfinite(start) || !std::isfinite(duration) || start < 0.0 ||
                start >= result.pattern.lengthBeats || duration <= 0.0 ||
                pitch < 0 || pitch > 127 || velocity < 1 || velocity > 127)
                continue;
            const auto voice = layer == layerNames.size() - 1
                ? rhythmVoiceForPitch(pitch) : layerVoices[layer];
            result.pattern.notes.push_back({start,
                std::min(duration, result.pattern.lengthBeats - start), pitch, velocity,
                layerChannels[layer], voice});
        }
    }
    normalizePattern(result.pattern);
    for (const auto channel : layerChannels) {
        if (std::none_of(result.pattern.notes.begin(), result.pattern.notes.end(),
                         [channel](const auto& note) { return note.channel == channel; })) {
            error = "A required musical layer became empty after validation";
            return false;
        }
    }
    if (const auto signature = parseKeyName(result.key.toStdString())) {
        const auto [root, scale] = *signature;
        result.key = canonicalKeyName(root, scale);
        std::vector<std::vector<int>> harmony(static_cast<std::size_t>(bars));
        for (auto bar = 0; bar < bars; ++bar) {
            const auto barStart = bar * 4.0;
            for (const auto& note : result.pattern.notes) {
                if (note.voice != VoiceId::HarmonicFoundation || note.endBeat() <= barStart ||
                    note.startBeat >= barStart + 4.0) continue;
                const auto pitchClass = positiveModulo(note.pitch, 12);
                if (std::find(harmony[static_cast<std::size_t>(bar)].begin(),
                              harmony[static_cast<std::size_t>(bar)].end(), pitchClass) ==
                    harmony[static_cast<std::size_t>(bar)].end())
                    harmony[static_cast<std::size_t>(bar)].push_back(pitchClass);
            }
            if (harmony[static_cast<std::size_t>(bar)].empty()) {
                const auto intervals = intervalsFor(scale);
                for (const auto degree : {0, 2, 4})
                    harmony[static_cast<std::size_t>(bar)].push_back(
                        positiveModulo(root + intervals[static_cast<std::size_t>(degree)], 12));
            } else if (harmony[static_cast<std::size_t>(bar)].size() < 3) {
                const auto intervals = intervalsFor(scale);
                for (const auto degree : {0, 2, 4}) {
                    const auto pitchClass = positiveModulo(
                        root + intervals[static_cast<std::size_t>(degree)], 12);
                    if (std::find(harmony[static_cast<std::size_t>(bar)].begin(),
                                  harmony[static_cast<std::size_t>(bar)].end(), pitchClass) ==
                        harmony[static_cast<std::size_t>(bar)].end())
                        harmony[static_cast<std::size_t>(bar)].push_back(pitchClass);
                }
            }
        }
        [[maybe_unused]] const auto tonalReport = repairTonalContract(
            result.pattern, root, scale, 4.0, harmony);
    } else {
        result.key = "Unspecified key";
    }
    if (result.title.isEmpty()) result.title = "Untitled Idea";
    return true;
}

SongPlan AiComposer::planSong(const juce::String& creativeDirection, int targetSeconds,
                              int totalBars, double bpm, double beatsPerBar,
                              std::uint64_t seed, CompositionBehavior behavior,
                              std::stop_token token,
                              juce::String& error, const AiSongProgress& progress,
                              const AiSongCheckpoint& checkpoint, bool aiSovereign) {
    SongPlan result;
    result.sections.clear();
    result.compositionBehavior = behavior;
    result.aiSovereign = aiSovereign;
    const auto localEditorial = aiSovereign && localEditorialModeEnabled();
    if (localEditorial)
        OperationalJournal::write("INFO", "EDITORIAL",
            "local AI-only editorial profile enabled; Cloud and VST retain their existing pipeline");
    const auto apiKey = ApiCredentialStore::apiKey();
    if (apiKey.isEmpty()) {
        error = "OpenAI API key is not configured";
        return result;
    }
    if (!songPlanSchemaIsValid()) {
        error = "Internal song-plan schema is invalid";
        return result;
    }
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return result;
    }

    const auto direction = creativeDirection.trim().isEmpty()
        ? "Create a memorable instrumental song with a clear emotional narrative."
        : creativeDirection.trim();
    const auto requestedCastCount = requestedInstrumentCountFromDirection(direction);
    result.requestedCastCount = requestedCastCount;
    OperationalJournal::write("INFO", "GENERATION", "request started | seed=" +
        juce::String(static_cast<juce::int64>(seed)) + " | target_seconds=" +
        juce::String(targetSeconds) + " | bars=" + juce::String(totalBars) +
        " | requested_cast=" + juce::String(static_cast<int>(requestedCastCount)) +
        " | behavior=" + juce::String(compositionBehaviorKey(behavior).data()) +
        " | direction=" + direction.substring(0, 240));
    const auto directionLower = direction.toLowerCase();
    const auto independentCastRequested =
        directionLower.contains("pistas midi independientes") ||
        directionLower.contains("independent midi tracks") ||
        directionLower.contains("independent tracks");
    const auto hypnoticProgressiveReference = directionLower.contains("guy j");
    const auto percussionFreeIntent = explicitlyPercussionFreeDirection(direction);
    const auto rhythmicElectronicIntent = explicitlyRhythmicElectronicDirection(direction);
    const auto referenceBrief = hypnoticProgressiveReference
        ? percussionFreeIntent
            ? juce::String(
            "The named artist is a high-level reference, never a request to reproduce a recording. "
            "Translate only the compatible traits into percussion-free music: patient hypnotic harmony, "
            "pitched bass motion, a compact emotional hook, subtractive development, slow timbral change, "
            "long tension plateaus and a transformed return. Do not plan a kick, drum or percussion role. ")
            : juce::String(
            "The named artist is a high-level reference, never a request to reproduce a recording. Translate it into "
            "deep hypnotic progressive-house traits: an unwavering but breathing four-on-floor foundation, an "
            "eight-to-sixteen-bar kick/bass pocket, one compact emotional hook, patient subtractive development, "
            "long tension plateaus, delayed returns and a decisive transformed recall. Complexity must emerge from "
            "micro-development and automation, not melodic busyness or orchestral accumulation. ")
        : juce::String();
    const auto behaviorBrief = compositionBehaviorBrief(behavior);
    // Retained only as a historical comparison. The phased production path uses
    // macroPrompt, manifestPrompt and performanceBlockPrompt below. Compiling this
    // unused monolithic prompt hid which instructions actually reached the model.
#if 0
    const auto prompt = juce::String(
        "You are the long-form composition architect for PULSO. Design one complete song, not a loop. "
        "Create a narratively inevitable form with introduction, thematic statements, contrast, development, "
        "a true climax and a conclusive ending. ") + behaviorBrief +
        (percussionFreeIntent ? juce::String(
            "HARD USER CONSTRAINT: no drums or percussion. All rhythm fields in the schema are inert: "
            "kick_state=muted, no rhythm instruments, cells, motifs or events. Motion must come from "
            "pitched bass, harmonic rhythm, arpeggiation where justified and evolving texture. "
            "This constraint overrides all generic club-production examples below. ") : juce::String()) +
        referenceBrief + juce::String(
        "Author narrative_spine before writing notes. It is a causal contract, not a synopsis: premise introduces one "
        "recognisable identity; question creates a specific unfinished melodic or harmonic obligation; every act names "
        "what caused it and the audible consequence; transformation changes that identity because of the obligation; "
        "climax makes the accumulated debt unavoidable; resolution audibly repays it through motif closure, tonal arrival, "
        "register relaxation and reduced density. protagonist_instrument_id must exactly match one declared Lead instrument "
        "and that protagonist or its explicit handoff must speak in at least 40 percent of eligible ") +
        juce::String(behavior == CompositionBehavior::Hypnotic ?
            "sixteen-bar states" : "eight-bar phrase windows") + juce::String(
        ", using rests inside and between phrases rather than continuous note streams. Ensemble continuity must "
        "come from independent harmonic, atmospheric and movement voices. In electronic harmonic works, preserve tonal "
        "memory across roughly 70-90 percent of bars through complementary owners, but do not keep every floor layer "
        "active continuously. Author a perceptible density curve: at least one reduced-memory passage, one progressive "
        "accumulation, one withdrawal before a consequential return, and a climax whose added responsibilities are later "
        "released. A central chord bed may breathe while an independent pedal, upper memory or atmosphere retains context. "
        "The resolution act must end its foreground contour on tonic, lower register and reduced "
        "simultaneous density so the declared harmonic debt is audibly repaid. "
        "id. Map every act to an exact section_name and include premise, question, transformation, climax and resolution. "
        "Do not claim a consequence or closure that the performance_score MIDI notes do not enact. "
        "Recurring sections must share a recognisable motif while changing "
        "orchestration, register, harmony or rhythm. Design a variable ensemble rather than four fixed layers. "
        "Choose 7-15 execution voices from the supplied IDs; give every voice an independent function, interaction rule, "
        "activity and register. Give independent functions to complementary harmonic voices, bass, melodic dialogue, "
        "atmosphere and transitions; add rhythm voices only if the user permits percussion and the music needs them. "
        "Do not activate every voice in every section. "
        "First classify the requested production in production_language. club_electronic means the musical logic is "
        "that of a producer and DJ: coordinated motion, hook economy, automation, spectral space and energy "
        "over 4/8/16/32-bar horizons. It is not an instruction to imitate one fixed genre template. hybrid preserves "
        "electronic production logic while allowing requested acoustic families; orchestral is reserved for genuinely "
        "orchestral requests. All production dimensions are 0 to 1. For club_electronic, complexity comes from timbral "
        "evolution, rhythmic conversation, subtraction and structural returns rather than many simultaneous pitches. "
        "Use appropriate functional roles such as sub, bass groove, chord body, "
        "stab, hook, response, atmosphere and transitions. One foreground hook owns attention at a time. "
        "In club_electronic with low orchestral_allowance, do not cast marimba, vibraphone, celesta, tubular bells or "
        "generic pitched mallets unless the user explicitly requested that physical identity. Use a filtered synth pulse, "
        "restrained analog hook or evolving spectral bed instead; avoid toy, chiptune, game and novelty preset character. "
        "Above those execution voices, design a production cast of 8-64 instrument instances from the supplied catalog. "
        "Use the smallest cast that can realize the requested depth. Coordinate harmonic fabric with hooks and melodic "
        "speakers; add a rhythm/percussion department only when allowed. source_voice is the playable archetype feeding an instrument, not its identity, and MUST reference "
        "an id present in the voices array. Every execution voice used by performance_score MUST have at least one "
        "explicit instrument owner with a compatible source_voice; never rely on an unnamed or automatically invented "
        "orchestral owner. A guitar or synth may own countermelody when its orchestral_function is counterpoint. Assign each instance "
        "a distinct role, playable register, prominence, restrained doubling probability and optional named sections. "
        "When the user asks for no percussion, no drums, no rhythm, many pads, colchones armonicos, atmospheres or a "
        "harmonic sound symphony, switch to harmonic texture architecture: do not create rhythm instruments or rhythm "
        "performance cells, use at least twelve instrument instances, at least eight harmonic/texture/melodic-synth "
        "owners, and distribute responsibility across harmonic_foundation, harmonic_pulse, harmonic_upper, atmosphere, "
        "countermelody and non-drum transitions. Use electronic catalog colours such as dub_chord, filtered_stab, "
        "hypnotic_arp, granular_pad, spectral_drone, shimmer_tail, deep_pluck, fm_sequence, acid_line and "
        "vocal_chop_texture only when they serve the brief. Arpeggiation is optional and must never be inserted merely "
        "because the work is electronic. Preserve a complete harmonic foundation and ADD long pads, "
        "stabs, drones, swells, sparse phrase replies and transition breath around it. Never split one complete line "
        "among many tracks merely to increase track count. For casts of 24 or more pitched instruments, normally design "
        "18-24 genuinely independent pitched content lanes. Preserve every foundation, body, bass, motion owner, "
        "protagonist and real counterpoint as a complete independent line; only ornamental colour changes may become explicit timbral handoffs, "
        "relays, restrained doublings or octave reinforcement of those developed lines. Every instrument has "
        "content_lane_id: use a unique id for "
        "every genuinely independent musical line. Share a content_lane_id only when line_relationship explicitly "
        "declares doubling, relay, timbral_handoff or octave_reinforcement. call_response is a distinct line derived "
        "from, but never identical to, the speaker. "
        "Every declared instrument is an exportable DAW track and must justify that cost with role-complete MIDI. Write "
        "beds across multiple harmonic events, basses as connected phrases, pulses across several mutated cycles, and "
        "lead/reply voices as separated but complete statements. A transition may be rare and a true one_shot may occur "
        "once; do not use those labels to hide an unwritten pad, bass, pulse or countermelody. Before returning the score, "
        "verify that performance_score contains enough notes, active bars and separated phrases to satisfy every layer's "
        "minimum_active_bars and minimum_phrases. If an instrument has no independent trajectory, omit it or explicitly "
        "make it a relay/timbral_handoff sharing the source content_lane_id; never create a token track for track count. "
        "For every electronic or hybrid work, author electronic_soundscape as the causal production plan. scene defines "
        "one perceptual world and spatial_narrative explains how foreground, middle distance and background change over "
        "the form. Declare one soundscape layer for every non-rhythm instrument using its exact instruments[].id. kind "
        "is voice for developed arps, sequences, stabs, pads and melodic speakers; environment for drones, granular air "
        "and persistent spatial beds; transition for multi-section rises, reverses and withdrawals; one_shot only for a "
        "genuinely isolated impact or singular event. Never call an underwritten musical part a one_shot to evade the "
        "development contract. Give each layer a narrative_role, a relationship to another layer, a concrete evolution, "
        "its fast/medium/slow/event time scale, minimum active bars and phrases, and the longest number of literal static "
        "bars it may sustain. target_median_active_layers describes the complete rendered fabric, normally 7-10 for a "
        "percussion-free electronic journey and 7-11 with rhythm, while climaxes may reach 10-14 complementary layers. These are evolving layers, "
        "not a command for constant tutti. A voice must receive at least two separated phrases and enough authored notes "
        "to develop; an environment must evolve across sections; transitions must occur at multiple meaningful boundaries. "
        "Set percussion_free true exactly when the user prohibits drums/percussion, and in that case create no rhythm "
        "instruments, rhythm cells or rhythm notes. Electronic motion must then be evaluated through harmonic rhythm, "
        "arpeggiation, timbral evolution, spatial change and tension/release rather than a kick requirement. "
        "A role named upper, high, air or extension must actually begin at MIDI 60 or above unless it is an explicitly "
        "named alto instrument. Never describe a high spectral layer while assigning it to the bass register. "
        "For every instrument, author orchestral_function, articulation_intent and divisi_voices. The functions "
        "foundation, body, extension, counterpoint, color and transition are compositional responsibilities: distribute "
        "them across the families actually required by production_language. In club_electronic, prefer synth, drum, bass, "
        "texture and transition roles; do not add strings, winds or brass merely to create scale. In orchestral or hybrid "
        "directions, acoustic families may contribute independent voice-led material. Use harmonic_depth, counterpoint_activity, divisi_depth, "
        "articulation_contrast, family_dialogue and hybrid_production to define how that ensemble thinks. "
        "Choose live_device only from the supplied Ableton-native device enum and describe the desired installed sound "
        "in live_preset_intent with concise English browser-search nouns, even when the user writes in another language "
        "and author timbre_signature as the actual perceptual patch identity. Give featured parts clearly distinct "
        "source, envelope, spectrum, motion, space and texture combinations; uniqueness controls how far the sound "
        "may explore while preserving its catalog identity. Do not repeat one signature across unrelated parts. "
        "(for example: solo cello, closed hi-hat, chamber strings, warm analog pad). Never invent a factory preset name. "
        "The local Live resolver matches that intent against installed content and will never treat an empty Rack, "
        "Sampler or Simpler container as a playable sound. Never use generic intents such as balanced natural. The "
        "track name, role, articulation and live_preset_intent must describe one compatible audible identity; do not "
        "name a part glassy while requesting felt, or clean while requesting distorted. "
        "When the creative direction includes an Ableton playback inventory, treat it as an execution constraint: "
        "prefer exact installed identities, use family-only substitutions deliberately, and do not build important "
        "counterpoint around unavailable identities. Preserve creative freedom through roles, register and form, not "
        "by pretending an unavailable instrument exists. "
        "Use strings, winds, brass, keyboards, electronics and percussion only when the creative direction benefits. "
        "Rotate foreground ownership between instruments and families; a lead source may become flute, cello, violin, "
        "oboe or synth in different phrases without losing thematic identity. Build chamber reductions, antiphonal "
        "answers, divisi, octave doublings and rare tutti arrivals. Never make every instrument play continuously and "
        "never turn orchestration into indiscriminate unison doubling. Bass remains an independent bridge between "
        "harmony and rhythm. Write orchestration_language as the global orchestral argument and author one "
        "timbre_palette before selecting individual sounds. Its material and space define a single mix world; all "
        "Concrete instruments named in timbre_palette are binding: each must have a matching instruments entry and an audible role; never describe a flute, piano, string, brass or acoustic voice that the score does not instantiate. "
        "numeric palette dimensions are 0 to 1. Interpret every live_preset_intent as a relative role inside that "
        "palette, never as an unrelated sound search. Concrete perceptual descriptors such as felt, muted, breathy, "
        "glassy, dark, bright, high, low, dry, wet, short or sustained are binding audible requirements: choose only "
        "the few descriptors the role truly needs, because Live reports a character fallback when its installed sound "
        "does not realize them. Use active_sections to reserve colours for meaningful moments. "
        "Instrument names must be clear DAW track names. Complexity must come "
        "from coordinated independence, negative space and long-range development, never indiscriminate density. "
        "Negative space still needs dramatic continuity: outside an explicitly authored full silence, no eight-bar "
        "window may be carried by only one repeated texture. A breakdown should normally preserve at least three "
        "complementary responsibilities across the window, such as harmonic memory, a sparse motif fragment and "
        "atmospheric or low-frequency continuity, without making them play continuously. "
        "Treat active_voices as an available cast, not a command to play continuously: design implied entrances, "
        "responses, withdrawals, breath before arrivals, tension plateaus and genuine low-density descents. "
        "Harmonic tension must follow the dramatic curve. minimum_pitch and maximum_pitch are MIDI pitches. "
        "Audit the exact vertical voicing, not only scale membership: below MIDI 55, avoid sustained minor-second, "
        "major-seventh/minor-ninth and tritone collisions between sub, moving bass and harmonic support. Resolve them "
        "with inversion, register, voice leading or deliberate support gaps rather than adding more pitches. "
        "The key label, root_pitch_class and mode define the binding perceptual tonal centre for ordinary requests. "
        "Use consolidated tonality by default: structural notes, chord roots, basses and pitch-class sets remain in "
        "the home scale, while extensions, inversions and voice leading create richness inside it. A chromatic melodic "
        "passing or neighbour note must be short, weak, approached stepwise and immediately resolve stepwise. In minor, "
        "a raised leading tone is permitted only inside a declared dominant or transitional cadence and must resolve "
        "upward by semitone to the tonic within one beat. Never let "
        "a label such as colour, cluster or dominant legalise an otherwise unsupported pitch. Use limited modal "
        "interchange, secondary dominants or brief modulation only when the creative direction explicitly requests "
        "that harmonic device, and prepare and resolve every departure back to the home centre. Use free chromatic or "
        "atonal language only when the user explicitly asks for atonality, deliberate dissonance, serialism or "
        "polytonality. Invent one harmonic_language within that boundary. Its dimensions are 0 to 1 and describe "
        "gravity, modal mobility, structural chromaticism, extensions, inversion movement, voice-leading smoothness, "
        "harmonic rhythm, pedals, ambiguity and cadence force. Build a chord_palette of explicit pitch-class sets; "
        "pitch classes are integers 0-11. root_pitch_class identifies perceived root, while bass_pitch_class may differ "
        "for inversions, slash chords and pedal bass. pitch_classes define the actual sounding collection and may include "
        "extensions and omissions. Non-diatonic structures are unavailable under consolidated tonality and remain rare, "
        "prepared and resolved under an explicitly expanded request. Do not decorate every chord or modulate merely to appear sophisticated. "
        "Give each section its own tonal centre and mode hint, then write harmonic_events at exact zero-based bar and beat "
        "offsets. Events reference the palette and must cover the section from bar 0, with purposeful holds, anticipations, "
        "turns, pedals, departures and arrivals. Reuse chords for identity but transform ordering, bass, voicing and rhythm; "
        "do not repeat one four-chord cycle through the entire song. Every non-diatonic structural chord must have perceptual "
        "logic in its purpose. The final cadence should resolve the global argument without requiring a conventional V-I. "
        "Every Resolution act must place a terminal harmonic event in its final two-to-four bars. It must arrive on the "
        "home tonic/root, unless resolution and resolution_target explicitly declare an open, suspended or modal ending; "
        "that exception must settle on a low-tension stable modal or pedal chord with audible reduction. An arbitrary "
        "non-tonic loop chord is never a resolution. ") +
        (percussionFreeIntent ? juce::String(
        "Rhythmic motion is entirely pitched: set every kick_state to muted, kick_continuity to optional, "
        "percussion_density to zero, and rhythm_motifs, rhythm_mutations and rhythm_gestures to empty arrays. "
        "Use harmonic rhythm, bass phrasing, sparse synthesis and deliberate rests for momentum. ") : juce::String(
        "Invent the rhythmic language from the creative direction itself; there are no preset genre families and "
        "you must not default unrelated requests to house, four-on-the-floor or the same backbeat. Describe the "
        "language semantically, then set its continuous behavioural dimensions from 0 to 1. Write a deliberate "
        "rhythm score for every section. kick_state defines macro presence and kick_continuity says whether explicit "
        "quarter-note anchors are mandatory. Use four_on_floor only when the request or your musical reading truly "
        "calls for it. Create contrast through explicit rhythm_gestures: "
        "mute or remove kicks before transitions, add occasional double kicks or pickups, then restore the established "
        "groove after breaks. Every exception must have structural purpose and gestures must remain rare. Kick, "
        "snare/clap, closed hats, open hats/shaker and percussion are independent voices. Never choose a breakbeat "
        "unless the user explicitly requests one. percussion_density, rhythmic_syncopation and swing are 0 to 1; "
        "bar_offset is local to its section and beat is zero-based. Invent 2-6 reusable rhythm_motifs as open "
        "one-to-four-bar rhythmic DNA. Use genuinely different motifs when the form needs different rhythmic ideas; "
        "do not merely rename one mask. Each lane mask has exactly bars * steps_per_bar characters: 0 is silence, "
        "1 is a normal hit and 2 is an accent. Do not default every motif to a generic grid: internally consider "
        "at least three rhythm solutions, then choose the one whose kick, clap, hats and two percussion lanes form "
        "the clearest conversation. ornaments add freely chosen GM-kit articulations such as alternate kicks, "
        "sidestick, toms, ride, crash, shaker, tambourine, cowbell and congas; step spans the complete motif and "
        "duration_steps is measured in motif steps. Use ornaments purposefully, not as constant clutter. Sections "
        "develop a shared motif through sparse rhythm_mutations rather than "
        "replacing it arbitrarily. Every mutation needs an audible dramatic purpose. Preserve silence, asymmetry, "
        "call-and-response and recognizable lineage across the full song. ")) + juce::String(
        "Give every voice a distinct performance "
        "identity: articulation, dynamic contour, vibrato, pitch gesture, brightness, expression depth and "
        "humanization must serve its instrumental role. Pitch gestures belong only to monophonic bass or melodic "
        "voices; polyphonic harmony and drums remain pitch-stable. Sustain pedal is only for foundation, upper "
        "harmony or atmosphere when connected phrasing is intentional. Expression must breathe with the form and "
        "must never remain maximal or mechanically identical. "
        "The section bars MUST sum exactly to ") + juce::String(totalBars) + ". Use between 5 and 14 sections. Energy, tension "
        "and density are values from 0 to 1. Motif intervals are semitones relative to the "
        "tonic and form the immutable thematic DNA. The macro form must contain an audible arc: a restrained premise, "
        "a rising development, a low-density breakdown or withdrawal, an earned climax/hook and a transformed return or "
        "resolution. The climax is not a label: it must introduce at least two independent foreground or harmonic lines, "
        "a register lift, a materially different rhythmic or arpeggiated grammar, and a stronger harmonic arrival than the "
        "preceding section. The breakdown must remove at least two responsibilities while leaving harmonic memory and a "
        "fragment of the hook. The return must recall the hook with changed register, contour, timbre or harmony. Never let "
        "all sections share the same energy, density, register and role occupancy. Establish one compact primary statement, then make at least "
        "one of every two to four later foreground appearances preserve its recognisable onset rhythm and contour; "
        "transform register, harmony, instrumentation, dynamics or fragments around that memory instead of replacing "
        "it with unrelated material. Author the actual performance in performance_score. "
        "Compose by independent musical line and then by instrument, not by track quota. Every performance note and control has an "
        "instrument_id: set it to the exact instruments[].id that should own the event, or to an empty string "
        "only when orchestration is intentionally free to rotate that event. The named instrument must use the "
        "same source_voice as the event voice. In deep electronic production, explicitly assign at least 70 percent "
        "of non-rhythm performance notes to concrete instruments. Give each populated pad, stab, drone, pulse, "
        "texture and melodic speaker its own attacks, durations, register, rests and sectional responsibility; "
        "do not manufacture track count through unison, rotation or octave clones. A new track must contain a new "
        "musical responsibility or explicitly declare its relationship to a shared line. Preserve a continuously "
        "interlocking harmonic floor of at least two pad/body responsibilities across 80 percent of bars, one primary "
        "speaker with statement-question-answer-development-return phrases and at most one motif-derived answerer. "
        "Do not create separate Lead tracks named original, inverted, fragmented, recovered or returned versions of "
        "the same leitmotif: those are cells and placements owned by the protagonist, not new instruments. Other melodic "
        "parts must introduce genuinely independent counterpoint rather than another contour transformation. Add an "
        "arpeggio only when the creative direction or this song's specific production argument calls for one. Target "
        "18-24 populated independent instrument parts in a long-form arrangement unless the user explicitly requests "
        "minimal or sparse writing; "
        "across the complete arrangement. Target roughly 7-10 perceptually complementary responsibilities in normal "
        "sections and 10-14 at earned peaks; do not obey a raw simultaneous-track ceiling because a short hat, a narrow "
        "texture and a wide sustained pad consume very different perceptual space. "
        "This is the authoritative compositional layer, not an optional sketch. Give every cell a stable theme_id "
        "shared by its recognisable transformations and a narrative_function describing what it does. Across active "
        "lead, countermelody, bass and harmonic voices, placements must author at least 65 percent of their available "
        "timeline, concentrating that authorship in statements, answers, developments, breakdown memory, climax and "
        "resolution. In a GPT plan, lead, countermelody and movement bass are exclusively AI-authored: unfilled time "
        "is intentional silence and the local engine is forbidden to invent connecting notes. Write movement-bass "
        "cells as coherent four-to-eight-bar lines with pocket, breath and a "
        "recognisable developed return; do not represent a bass argument as unrelated isolated notes. "
        "The audible result, not the JSON labels, is graded: at least 85 percent of rendered lead/counter notes and 75 "
        "percent of rendered movement-bass notes must come from performance_score. Author at least 45 percent of the "
        "available groove timeline with reusable kick, clap, hat or percussion cells so GPT decides structural rhythm "
        "while the local engine only validates and fills non-defining continuity. A primary hook is a two-to-five-note "
        "rhythmic identity with rests, held consequence, one characteristic interval and a derived answer; never write "
        "an uninterrupted scale walk or decorate every beat with another nearby pitch. "
        "A cell is a compact reusable passage measured in quarter-note beats, not a genre template. Its "
        "owned_voices are authoritative: their notes and deliberate silences replace local procedural notes "
        "in every section where the cell is placed. Write independent attacks, releases, pitches and velocities "
        "for the important rhythm, bass, harmony, melody and texture voices. Use controls for intentional CC1, "
        "CC11, CC64 or timbral movement. Create contrasting cells for establishment, question, answer, development, "
        "withdrawal and arrival; never duplicate a cell under another name. placements use a zero-based section_index "
        "and start_beat relative to that section. A placement owns only its exact time interval; cover every interval "
        "that must be explicitly authored; uncovered foreground and movement-bass time remains silent. Never create an "
        "unmarked global silence longer than two bars. If complete silence is structurally intended, write the exact "
        "phrase full silence in that section's function; otherwise preserve sparse rhythmic, harmonic or atmospheric "
        "continuity. Repetition is permitted only when musically intentional: no "
        "foreground or bass cell may repeat verbatim more than twice, and a stable percussion cell no more than four "
        "times before a materially different companion cell answers it. "
        "The primary hook is the exception to novelty: state one identifiable two-to-four-bar nucleus, recall its onset pattern and contour in at least one quarter of later hook windows, and develop it by cadence, register, orchestration or one controlled interval change. Hook response must derive from that nucleus rather than introduce unrelated material. "
        "Use new cells and interleaved placements for "
        "structural variation. Build audible dialogue by reusing strong cells through voice_map: establish an idea in "
        "one voice, answer it in another, then transform it using time_scale, transpose, fragment boundaries, contour "
        "inversion or retrograde only when the dramatic purpose calls for it. purpose must state establish, answer, "
        "transform, withdraw, intensify or resolve in the language of this specific composition; these are relationships, "
        "not mandatory templates. In orchestral and hybrid domains, at least three instrument families should participate "
        "in a thematic relationship. In club_electronic, use at least three production families (low end, groove, harmonic "
        "or hook/texture) and do not inflate the cast with acoustic doublings. They must not all play simultaneously. "
        "A four-on-floor kick may add at most one non-quarter ornament per eight-bar phrase unless an explicit "
        "DoubleKick or PickupFill gesture states the structural reason. Percussion development must change onset phase, "
        "articulation, register, density or call-response ownership rather than merely changing velocity. A high-energy "
        "section longer than sixteen bars must rotate at least three support roles every four to eight bars while kick, "
        "low-end grammar and the current foreground remain legible. HarmonicPulse is punctuation: its individual notes "
        "must not exceed one quarter-note beat in club_electronic, and HarmonicFoundation must breathe or revoice before "
        "nine literal bars. Treat open hat, shaker, clap, rim, tom, hand percussion, metal and transition FX as distinct "
        "audible articulations. Every performance_score percussion pitch must match the GM articulation owned by its voice: never put kick pitches 35/36 into low_percussion, high_percussion, hats or transitions. High percussion must name concrete physical responsibilities such as ride, rim, tambourine or cowbell, never generic percussion, and should normally expose at least three purposeful articulations across a full song. "
        "If an instrument is named conga, use GM conga pitches 62-64 rather than tom pitches 41-50. Do not let claves, "
        "closed hats or any single articulation own more than roughly two thirds of a multi-articulation support lane. "
        "Keep each named percussion lane inside its declared acoustic family; request a separate tom or metal lane instead "
        "of hiding unrelated articulations inside a conga role. Never describe tempo-labelled loops or compound samples as "
        "isolated drum hits. A club reduction may withdraw the drums and hook for eight bars, but the next eight-bar window "
        "must restore a recognisable thematic foreground gesture even if the full drop is still delayed. A club song may "
        "sustain a breakdown, but unless full silence is explicitly declared it must not exceed twelve consecutive bars "
        "without at least one full pulse-anchor bar. Develop hats, clap and shaker in the final two bars of each eight-bar "
        "phrase through articulation, phase, subtraction or call-response; velocity-only changes do not count. Rotate the actual "
        "foreground instrument after at most two consecutive phrases; changing velocity alone is not rotation. Avoid tonal "
        "notes shorter than one eighth beat, and give bowed strings, winds and brass at least one quarter beat unless a "
        "physically intentional extended-technique articulation explicitly requires otherwise. "
        "Give timbrally independent pads, responses and textures contrasting live_preset_intent descriptions; never "
        "request the same preset identity for two different orchestration functions. "
        "Every declared instrument is a real exported MIDI track: write enough independent notes, active bars and phrase "
        "returns for its named function. A transition or one-shot may be brief; a pad, pulse, melodic speaker, dialogue "
        "or environment may not be a token track. If the direction requests a very large cast, either author a real "
        "trajectory for every member or declare fewer instruments - never split one gesture into nominal lanes. "
        "octave_shift is strictly an octave displacement: -24, -12, 0, 12 or 24 semitones. "
        "Every authored note and placement MUST declare metric_intent=strict_grid. Source MIDI timing is always exact; "
        "anticipation, feel and microtiming belong exclusively to PULSO's reversible Human Performance playback layer. "
        "Undeclared decimal timing is invalid. Keep the score sparse enough to fit, but never delegate the principal "
        "hook, response, movement bass, harmonic identity, cadence or defining groove to procedural fallback. Reuse "
        "authored cells through placements and purposeful transformations so primary authorship remains above 65 percent. "
        "For consolidated tonality, pitched performance notes must respect the chord and tonal narrative already authored. "
        "Target duration: " + juce::String(targetSeconds) +
        " seconds; tempo: " + juce::String(bpm, 1) + " BPM; meter: " + juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. Creative direction: " + direction +
        "\nPIPELINE MACRO PHASE: design only the shared form, narrative, tonal language, chord palette, section timeline "
        "and production direction requested by this compact schema. Do not choose instruments, soundscape layers, motifs, "
        "voices or MIDI. Later bounded phases will realize this immutable macro architecture.";
#endif

    const auto macroPrompt = juce::String(
        "You are PULSO's long-form musical architect. Design the immutable macro composition, not its instruments or "
        "MIDI performances. Return a coherent dramatic journey whose sections cover exactly ") +
        juce::String(totalBars) + " bars (the sum of section bars must equal that number), lasting " +
        juce::String(targetSeconds) + " seconds at " + juce::String(bpm, 1) + " BPM in " +
        juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. " + behaviorBrief + referenceBrief +
        (percussionFreeIntent ? juce::String(
            "HARD EXCLUSION: no drums or percussion anywhere in the form. Set every section kick_state=muted, "
            "kick_continuity=optional, percussion_density=0 and leave rhythmic gestures/mutations empty. "
            "Create motion using pitched bass, harmonic rhythm, sparse synth gestures and evolving texture; "
            "do not make a plan that depends on a kick which a later phase would remove. ") : juce::String()) +
        (rhythmicElectronicIntent ? juce::String(
            "HARD RHYTHMIC GENRE CONTRACT: the user explicitly requested club-oriented electronic music. "
            "Do not reinterpret it as percussion-free. Plan an AI-authored rhythm narrative with core drums, "
            "at least one complementary percussion or hat voice, low-end interlock, strategic withdrawals and "
            "a consequential return. A breakdown may remove the kick temporarily; the work as a whole may not. ") :
            juce::String()) +
        "Establish, develop, contrast, transform and resolve a recognisable musical identity. "
        "In narrative_spine.motif_identity describe a short pitched melodic contour AND "
        "its rhythm/rest shape so the protagonist can enact it in MIDI; harmonic "
        "progression alone is not a melodic motif. "
        "avoid interchangeable sections and arbitrary novelty. Make every harmonic event reference a declared chord ID, "
        "and keep tonal centers, modes, borrowed harmony and tension releases intentional under the declared tonal policy. "
        "Consolidated tonality may still declare a small number of functional chromatic or modal-color chords: mark their "
        "function honestly, keep every foreign pitch inside that exact chord window, and follow it with a chord whose "
        "scale tone resolves the foreign pitch by semitone or whole step. Never promise Neapolitan, Dorian, altered or "
        "chromatic color in prose unless chord_palette and harmonic_events actually materialize it. "
        "Honor exclusions literally (especially requests for no drums or percussion) and describe genre through musical "
        "behavior rather than artist imitation. Establish a specific motivic question and a later audible answer in "
        "the harmonic timeline itself; a change in energy or a new section label is not an answer. A long tonic pedal "
        "is valid for hypnotic music only if inversion, bass relation, harmonic colour or phrase rhythm transforms "
        "its meaning before the return. Let harmonic-event cadence follow the requested style: a hypnotic phrase may "
        "stay on one harmony for several bars while the bass relation, register or phrase rhythm evolves. Do not "
        "default to a new chord in every bar or interchangeable activity across all sections. Keep at least one "
        "shared tonal anchor while making the resolution consequential. Every complete song must contain an audible "
        "climax distinct from its final resolution. In a short three- or four-section form, the penultimate section "
        "may combine transformation and climax, but its narrative act must be climax and its energy, register or "
        "coordinated ensemble consequence must exceed the resolution. Scale the number of sections to the "
        "actual duration: a roughly one-minute sketch normally needs three or four distinct dramatic sections, "
        "not seven miniature scenes; a long work may earn more acts through developed musical consequences. "
        "Do not choose instruments, soundscape layers, rhythm motifs, voices, cells, "
        "placements or notes. Creative direction: " + direction;

    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"background\":true,\"reasoning\":{\"effort\":\"" + reasoningEffort + "\"},"
        "\"max_output_tokens\":20000,\"prompt_cache_key\":\"pulso-song-macro-v3\",\"input\":" +
        juce::JSON::toString(juce::var(macroPrompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_song_macro\","
        "\"strict\":true,\"schema\":" + macroBlueprintSchema() + "}}}";
    const juce::String configuredReasoningEffort(reasoningEffort);
    const auto executionReasoningEffort = configuredReasoningEffort == "max"
        ? juce::String("low") : configuredReasoningEffort;
    const auto realizationReasoningEffort = configuredReasoningEffort == "max"
        ? juce::String("low") : configuredReasoningEffort;
    const auto macroRecoveryBody = body.replace(
        "\"reasoning\":{\"effort\":\"" + configuredReasoningEffort + "\"}",
        "\"reasoning\":{\"effort\":\"" + executionReasoningEffort + "\"}")
        .replace("\"max_output_tokens\":20000", "\"max_output_tokens\":16000");

    // Full-song architecture can legitimately require several minutes of reasoning. Background
    // Responses keeps the UI cancellable while short, independently bounded polls avoid a single
    // fragile HTTP connection owning the full composition lifetime.
    constexpr auto totalAiBudget = std::chrono::minutes(45);
    // The first macro normally needs roughly a minute of background work. A
    // 30-second budget cancelled it on every local long-form run and paid the
    // latency of a fresh recovery request before the cast could even begin.
    constexpr auto architectureBudget = std::chrono::seconds(120);
    const auto aiStarted = std::chrono::steady_clock::now();
    if (progress) progress({AiSongStage::Blueprint, 0, 2, 1, "macro form, harmony and narrative"});
    auto http = performRequest(body, apiKey, token, architectureBudget);
    auto usedMacroRecovery = false;
    const auto transientBlueprintFailure = [&] {
        const auto response = juce::JSON::parse(http.body);
        const auto* responseObject = response.getDynamicObject();
        const auto incomplete = responseObject != nullptr &&
            responseObject->getProperty("status").toString() == "incomplete";
        return !http.connected || http.timedOut || http.status == 408 || http.status == 409 ||
            http.status == 429 || http.status >= 500 || incomplete;
    };
    if (!token.stop_requested() && transientBlueprintFailure()) {
        if (progress) progress({AiSongStage::Recovery, 0, 1, 2,
                                "blueprint transport recovery"});
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            aiStarted + totalAiBudget - std::chrono::steady_clock::now());
        if (remaining >= std::chrono::seconds(45)) {
            http = performRequest(macroRecoveryBody, apiKey, token,
                std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::minutes(3))));
            usedMacroRecovery = true;
        }
    }
    if (!http.connected || http.status < 200 || http.status >= 300 ||
        http.cancelled || http.timedOut) {
        error = apiErrorMessage(http);
        return result;
    }
    const auto macroText = extractOutputText(juce::JSON::parse(http.body));
    if (macroText.isEmpty()) {
        error = structuredResponseError(http.body,
            "OpenAI returned no structured macro blueprint");
        return result;
    }

    if (progress) progress({AiSongStage::Blueprint, 1, 2, 1,
                            "global instrument architecture"});
    const auto castCountContract = requestedCastCount == 0 ? juce::String{} :
        "NON-NEGOTIABLE CAST SIZE: return exactly " + juce::String(static_cast<int>(requestedCastCount)) +
        " instruments. Subset counts in the direction belong inside this total. ";
    const auto independentCastContract = independentCastRequested ? juce::String(
        "INDEPENDENT MIDI TRACK CONTRACT: each requested track is a genuinely "
        "independent note owner. Give every instrument a unique content_lane_id. "
        "line_relationship may be independent or call_response only; never "
        "doubling, relay, octave_reinforcement or timbral_handoff. A texture "
        "must have its own pitched evolution, not be a shadow destination. ") :
        juce::String{};
    const auto defaultCastContract = requestedCastCount == 0 && totalBars >= 96
        ? juce::String("DEFAULT LONG-FORM ENSEMBLE: return 18-24 independent authored MIDI owners "
                       "unless the user explicitly says minimal, sparse, drone-only or sketch. "
                       "Count only independent content owners, never relay or timbral destinations. ")
        : juce::String{};
    const auto compactCast = requestedCastCount == 3;
    const auto compactCastBrief = compactCast ? juce::String(
        "COMPACT THREE-TRACK PROOF: Assign exactly one harmonic_foundation central polyphonic chord bed "
        "(role includes primary_chord_bed), one independent movement_bass, and one source_voice=lead "
        "protagonist. These three owners are sufficient: do not add a second pad, atmosphere, "
        "percussion or token part. Keep the chord bed present in the opening, development and closing, "
        "while allowing the bass and lead to breathe and trade focus. ") : juce::String{};
    const auto matrixFloorBrief = compactCast ? juce::String(
        "at least one harmonic foundation, with bass and protagonist entering as the form needs. ") :
        juce::String("at least two harmonic or atmospheric responsibilities, deliberate inner motion, "
                     "and a foreground or counterline whenever the narrative requires speech. ");
    const auto longFormMatrixBrief = compactCast ? juce::String(
        "In this compact work, vary density through phrase-level entrances, durations and rests "
        "inside the three identities, not by adding more tracks or demanding a two-layer pad floor. ") :
        juce::String("Consecutive development sections must rotate at least two supporting responsibilities whenever "
                     "their combined span exceeds sixteen bars; preserve the two-layer floor while changing inner motion, color or "
                     "foreground ownership. ");
    const auto manifestPrompt = castCountContract + independentCastContract +
        defaultCastContract + compactCastBrief + juce::String(
        "You are PULSO's global orchestration director. Decide the complete ensemble once, before any detail is written. "
        "Return the compact cast manifest only. Every member needs a unique stable id, a distinct musical responsibility "
        "or an explicit complementary relationship, and an intentional section trajectory. The IDs, instrument types, "
        "source voices, roles, lane identities, relationships, orchestral functions and active sections are immutable in "
        "later phases. active_sections is the global orchestration matrix, not optional metadata: every section must name "
        "enough complementary owners to realize its declared density, with ") + matrixFloorBrief + juce::String(
        "Low-density sections may withdraw roles, but no ordinary section may become an accidental sketch. Climax sections "
        "must expand the matrix through independent functions rather than unison doubling. Satisfy any explicit requested "
        "instrument count up to 64 exactly; otherwise choose only the number "
        "that the story can support with independent material. Preserve exclusions literally, especially percussion-free "
        "requests. Avoid constant tutti and distribute foreground, harmonic floor, dialogue, movement and atmosphere over "
        "the whole arc. ") + longFormMatrixBrief + juce::String(
        "Declare exactly one protagonist and at most one motif-derived answerer. For a long-form "
        "cast of twelve or more owners, also declare at least two additional independent conversational partners using "
        "line_relationship=counterpoint or call_response. These partners must have separate sectional trajectories and "
        "must not share the protagonist content lane; together with the motif-derived answerer they create at least three "
        "audible dialogue lines without simultaneous unison stacking. Transformations such as "
        "original, inversion, fragmentation, recovery and return belong to that protagonist's later performance cells, "
        "not to separate instrument members. Use additional tracks for true orchestration: independent inner voices, "
        "pedals, chord bodies, contrary counterpoint, spectral color and transition functions. Permit an arpeggiator only "
        "when it is an intentional part of this particular production argument. In percussion-free electronic music, "
        "Elect exactly one harmonic_foundation instrument as the central polyphonic chord lane by including the literal "
        "marker primary_chord_bed in its role. That exported MIDI track must later contain complete three-to-five-note "
        "chords; other harmonic owners provide independent inner voices, pedals, pulses, upper memory or atmosphere and "
        "must never be used to fake the central chord by distributing one pitch per track. "
        "several complementary recurrence instruments (arp, sequence, pulse, orbit or ostinato) are valid when their "
        "trajectories are distinct. PULSO will elect exactly one primary_motion_owner locally and retain all remaining "
        "candidates as supporting_motion; never collapse or remove them. Unless the direction explicitly requests a "
        "static/drone-only work, this family supplies evolving hypnotic motion, not generic sixteenth-note filler. "
        "For club_electronic or hybrid percussion-free music, declare at least one independent source_voice=movement_bass "
        "owner with a sectional low-end trajectory unless the user explicitly says no bass or sin bajo. This is pitched "
        "harmonic propulsion, not percussion, and it must remain distinct from the sub pedal and protagonist. "
        "At the manifest root, protagonist_instrument_id must equal the exact stable id of that one protagonist. It must "
        "use source_voice=lead and include the macro's resolution section in active_sections so its authored transformed "
        "return can occur in the last sixteen bars. This manifest ID is authoritative and replaces any earlier narrative "
        "placeholder. Do not write registers, presets, timbre "
        "signatures, soundscape layers, notes or performances yet. " +
        (percussionFreeIntent ? juce::String(
            "The user's no-percussion instruction is absolute: do not cast, describe or reserve any drum or "
            "percussion identity. The ensemble's primary motion and narrative must survive without a kick. ")
            : rhythmicElectronicIntent ? juce::String(
            "The user's rhythmic electronic genre is explicit: set percussion_free=false and cast source_voice="
            "core_drums plus at least one independent hats, clap or percussion owner. The AI must compose their "
            "actual MIDI and sectional development; these are not local fallback tracks. ") : juce::String()) +
        "Original direction: ") + direction + "\nIMMUTABLE MACRO BLUEPRINT:\n" + macroText;
    const auto manifestBody = juce::String("{\"model\":\"") + model +
        "\",\"background\":true,\"reasoning\":{\"effort\":\"" + realizationReasoningEffort +
        "\"},\"max_output_tokens\":14000,\"prompt_cache_key\":\"pulso-cast-manifest-v1\",\"input\":" +
        juce::JSON::toString(juce::var(manifestPrompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_cast_manifest\","
        "\"strict\":true,\"schema\":" + castManifestSchema(requestedCastCount) + "}}}";
    auto manifestHttp = performRequest(manifestBody, apiKey, token, std::chrono::minutes(2));
    auto manifestText = extractOutputText(juce::JSON::parse(manifestHttp.body));
    juce::String manifestError;
    std::size_t excludedCastMembers{};
    juce::String sanitizedManifest;
    if (enforceExplicitCastExclusionsImpl(manifestText, direction, sanitizedManifest,
                                          excludedCastMembers, manifestError)) {
        manifestText = std::move(sanitizedManifest);
        if (excludedCastMembers != 0)
            OperationalJournal::write("WARN", "CAST",
                "removed " + juce::String(static_cast<int>(excludedCastMembers)) +
                " rhythm identities that contradicted the explicit percussion-free direction; replacements will preserve the requested cast size");
    }
    auto castIds = manifestInstrumentIds(manifestText, manifestError);
    juce::String motionReport;
    juce::String matrixReport;
    bool protagonistResolutionLinked{};
    bool matrixReconciled{};
    auto manifestContractsReady = !castIds.empty() &&
        validateIndependentCastManifest(manifestText, independentCastRequested, manifestError) &&
        validateExplicitRhythmCast(manifestText, direction, manifestError) &&
        reconcileProtagonistResolutionManifest(macroText, manifestText,
            protagonistResolutionLinked, manifestError) &&
        validateProtagonistManifest(macroText, manifestText, manifestError) &&
        reconcileMotionManifest(macroText, manifestText, direction, motionReport, manifestError) &&
        validateMotionManifest(macroText, manifestText, direction, manifestError) &&
        reconcileOrchestrationMatrixManifest(macroText, manifestText,
            matrixReconciled, matrixReport, manifestError) &&
        validateOrchestrationMatrixManifest(macroText, manifestText, manifestError);
    if (manifestContractsReady && motionReport.isNotEmpty())
        OperationalJournal::write("OK", "CAST", "local motion leadership elected | " + motionReport);
    if (manifestContractsReady && protagonistResolutionLinked)
        OperationalJournal::write("WARN", "CAST",
            "linked the declared protagonist to the authoritative macro resolution; no musical content changed");
    if (manifestContractsReady && matrixReconciled)
        OperationalJournal::write("WARN", "CAST",
            "reconciled orchestration matrix metadata | " + matrixReport +
            "; cast, roles and musical material unchanged");
    if (!token.stop_requested() &&
        (!manifestHttp.connected || manifestHttp.status < 200 || manifestHttp.status >= 300 ||
         manifestHttp.cancelled || manifestHttp.timedOut || !manifestContractsReady)) {
        if (progress) progress({AiSongStage::Recovery, 1, 2, 2,
                                "global cast manifest recovery"});
        manifestHttp = performRequest(manifestBody, apiKey, token, std::chrono::seconds(90));
        manifestText = extractOutputText(juce::JSON::parse(manifestHttp.body));
        manifestError.clear();
        excludedCastMembers = 0;
        sanitizedManifest.clear();
        if (enforceExplicitCastExclusionsImpl(manifestText, direction, sanitizedManifest,
                                              excludedCastMembers, manifestError))
            manifestText = std::move(sanitizedManifest);
        castIds = manifestInstrumentIds(manifestText, manifestError);
        motionReport.clear();
        matrixReport.clear();
        protagonistResolutionLinked = false;
        matrixReconciled = false;
        manifestContractsReady = !castIds.empty() &&
            validateIndependentCastManifest(manifestText, independentCastRequested,
                manifestError) &&
            validateExplicitRhythmCast(manifestText, direction, manifestError) &&
            reconcileProtagonistResolutionManifest(macroText, manifestText,
                protagonistResolutionLinked, manifestError) &&
            validateProtagonistManifest(macroText, manifestText, manifestError) &&
            reconcileMotionManifest(macroText, manifestText, direction, motionReport, manifestError) &&
            validateMotionManifest(macroText, manifestText, direction, manifestError) &&
            reconcileOrchestrationMatrixManifest(macroText, manifestText,
                matrixReconciled, matrixReport, manifestError) &&
            validateOrchestrationMatrixManifest(macroText, manifestText, manifestError);
        if (manifestContractsReady && motionReport.isNotEmpty())
            OperationalJournal::write("OK", "CAST", "local motion leadership elected | " + motionReport);
        if (manifestContractsReady && protagonistResolutionLinked)
            OperationalJournal::write("WARN", "CAST",
                "linked the declared protagonist to the authoritative macro resolution; no musical content changed");
        if (manifestContractsReady && matrixReconciled)
            OperationalJournal::write("WARN", "CAST",
                "reconciled orchestration matrix metadata | " + matrixReport +
                "; cast, roles and musical material unchanged");
    }
    if (!manifestHttp.connected || manifestHttp.status < 200 || manifestHttp.status >= 300 ||
        manifestHttp.cancelled || manifestHttp.timedOut || !manifestContractsReady) {
        error = "OpenAI global cast manifest failed: " +
            (manifestError.isNotEmpty() ? manifestError : apiErrorMessage(manifestHttp));
        return result;
    }
    cacheValidatedResponse(manifestBody, manifestHttp);

    if (requestedCastCount != 0 && castIds.size() < requestedCastCount &&
        !token.stop_requested()) {
        const auto missing = requestedCastCount - castIds.size();
        if (progress) progress({AiSongStage::Recovery, 1, 2, 2,
            "completing " + juce::String(static_cast<int>(missing)) +
            " missing cast identities"});
        OperationalJournal::write("WARN", "RECOVERY",
            "global cast returned " + juce::String(static_cast<int>(castIds.size())) + "/" +
            juce::String(static_cast<int>(requestedCastCount)) +
            "; preserving it and requesting only " + juce::String(static_cast<int>(missing)) +
            " missing identities");

        const auto supplementPrompt = juce::String(
            "You are PULSO's cast reconciler. The accepted global ensemble below is immutable but is short of its "
            "explicit size. Return exactly ") + juce::String(static_cast<int>(missing)) +
            " additional manifest members and nothing else. Do not repeat, rename, replace or revise any accepted member. "
            "Choose only genuinely complementary responsibilities missing from the existing architecture. Respect every "
            "exclusion and subset count in the original direction. Use only source_voice IDs already declared in the "
            "accepted manifest. Every new id and content_lane_id must be unique. These are orchestration identities only; "
            "do not write notes, patterns, registers, presets or performance material. ORIGINAL DIRECTION:\n" + direction +
            "\nIMMUTABLE MACRO BLUEPRINT:\n" + macroText +
            "\nACCEPTED GLOBAL CAST MANIFEST:\n" + manifestText;
        const auto supplementTokens = std::clamp(2200 + static_cast<int>(missing) * 550,
                                                  2800, 7000);
        const auto supplementBody = juce::String("{\"model\":\"") + model +
            "\",\"background\":true,\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":" +
            juce::String(supplementTokens) +
            ",\"prompt_cache_key\":\"pulso-cast-reconcile-v1\",\"input\":" +
            juce::JSON::toString(juce::var(supplementPrompt)) +
            ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_cast_supplement\"," +
            "\"strict\":true,\"schema\":" + castSupplementSchema(missing) + "}}}";
        const auto supplementHttp = performRequest(supplementBody, apiKey, token,
                                                    std::chrono::seconds(60));
        const auto supplementText = extractOutputText(juce::JSON::parse(supplementHttp.body));
        juce::String reconciledText;
        juce::String reconciliationError;
        auto reconciledByAi = supplementHttp.connected && supplementHttp.status >= 200 &&
            supplementHttp.status < 300 && !supplementHttp.cancelled && !supplementHttp.timedOut &&
            mergeCastManifestSupplement(manifestText, supplementText, requestedCastCount,
                                        reconciledText, reconciliationError);
        if (reconciledByAi) {
            std::size_t removedFromReconciliation{};
            juce::String exclusionSafeText;
            reconciledByAi = enforceExplicitCastExclusionsImpl(
                reconciledText, direction, exclusionSafeText,
                removedFromReconciliation, reconciliationError) &&
                removedFromReconciliation == 0;
            if (reconciledByAi) reconciledText = std::move(exclusionSafeText);
            else if (reconciliationError.isEmpty())
                reconciliationError = "AI cast supplement contradicted the explicit percussion exclusion";
        }
        juce::String reconciliationMotionReport;
        bool reconciliationResolutionLinked{};
        reconciledByAi = reconciledByAi &&
            reconcileProtagonistResolutionManifest(macroText, reconciledText,
                reconciliationResolutionLinked, reconciliationError) &&
            validateProtagonistManifest(macroText, reconciledText, reconciliationError) &&
            reconcileMotionManifest(macroText, reconciledText, direction,
                                    reconciliationMotionReport, reconciliationError) &&
            validateMotionManifest(macroText, reconciledText, direction, reconciliationError);
        if (reconciledByAi) {
            manifestText = std::move(reconciledText);
            OperationalJournal::write("OK", "CHECKPOINT",
                "AI cast reconciliation accepted; all " +
                juce::String(static_cast<int>(requestedCastCount)) + " identities preserved");
            if (reconciliationMotionReport.isNotEmpty())
                OperationalJournal::write("OK", "CAST",
                    "local motion leadership reconciled | " + reconciliationMotionReport);
        } else {
            if (reconciliationError.isEmpty()) reconciliationError = apiErrorMessage(supplementHttp);
            OperationalJournal::write("WARN", "RECOVERY",
                "bounded cast supplement unavailable (" + reconciliationError +
                "); completing identity metadata from the catalog without generating notes");
            reconciliationMotionReport.clear();
            if (!completeCastManifestFromCatalog(manifestText, macroText, requestedCastCount,
                                                 reconciledText, reconciliationError) ||
                !reconcileProtagonistResolutionManifest(macroText, reconciledText,
                    reconciliationResolutionLinked, reconciliationError) ||
                !validateProtagonistManifest(macroText, reconciledText, reconciliationError) ||
                !reconcileMotionManifest(macroText, reconciledText, direction,
                                         reconciliationMotionReport, reconciliationError) ||
                !validateMotionManifest(macroText, reconciledText, direction, reconciliationError)) {
                error = "OpenAI global cast reconciliation failed: " + reconciliationError;
                return result;
            }
            manifestText = std::move(reconciledText);
            OperationalJournal::write("OK", "CHECKPOINT",
                "catalog completed " + juce::String(static_cast<int>(missing)) +
                " cast identities only; no local notes were generated and performance remains assigned to AI writing");
            if (reconciliationMotionReport.isNotEmpty())
                OperationalJournal::write("OK", "CAST",
                    "local motion leadership reconciled | " + reconciliationMotionReport);
        }
        manifestError.clear();
        castIds = manifestInstrumentIds(manifestText, manifestError);
    }
    if (requestedCastCount != 0 && castIds.size() != requestedCastCount) {
        error = "OpenAI global cast manifest failed: explicit cast-size contract requested " +
            juce::String(static_cast<int>(requestedCastCount)) + " instruments; reconciled manifest contains " +
            juce::String(static_cast<int>(castIds.size()));
        return result;
    }
    if (!validateIndependentCastManifest(manifestText, independentCastRequested,
                                         manifestError)) {
        error = "OpenAI global cast manifest failed: " + manifestError;
        return result;
    }
    if (!validateExplicitRhythmCast(manifestText, direction, manifestError)) {
        error = "OpenAI global cast manifest failed: " + manifestError;
        return result;
    }
    manifestError.clear();
    protagonistResolutionLinked = false;
    matrixReconciled = false;
    matrixReport.clear();
    if (!reconcileProtagonistResolutionManifest(macroText, manifestText,
            protagonistResolutionLinked, manifestError) ||
        !validateProtagonistManifest(macroText, manifestText, manifestError) ||
        !reconcileOrchestrationMatrixManifest(macroText, manifestText,
            matrixReconciled, matrixReport, manifestError) ||
        !validateOrchestrationMatrixManifest(macroText, manifestText, manifestError)) {
        error = "OpenAI global cast manifest failed: " + manifestError;
        return result;
    }
    if (matrixReconciled)
        OperationalJournal::write("WARN", "CAST",
            "reconciled final orchestration matrix metadata | " + matrixReport +
            "; cast, roles and musical material unchanged");

    std::vector<std::vector<juce::String>> castShards;
    for (std::size_t begin = 0; begin < castIds.size(); begin += instrumentsPerCastShard) {
        castShards.emplace_back(castIds.begin() + static_cast<std::ptrdiff_t>(begin),
            castIds.begin() + static_cast<std::ptrdiff_t>(
                std::min(castIds.size(), begin + instrumentsPerCastShard)));
    }
    std::vector<juce::String> castDetailTexts(castShards.size());
    const auto castDetailBody = [&](std::size_t shardIndex) {
        const auto shardPrompt = juce::String(
            "You are a timbre and register specialist completing one shard of an already immutable global ensemble. "
            "Return exactly one full instrument definition and exactly one soundscape layer for every listed manifest "
            "member, and no others. Copy every manifest anchor literally: id, instrument, name, source_voice, role, "
            "content_lane_id, line_relationship, orchestral_function and active_sections. You may decide only register, "
            "octave, activity, prominence, doubling, articulation, divisi, Live device/preset intent, timbre signature and "
            "soundscape evolution. Make those decisions complementary to the complete global cast; do not create a new "
            "composition and do not write notes. ORIGINAL DIRECTION:\n") + direction +
            "\nIMMUTABLE MACRO BLUEPRINT:\n" + macroText +
            "\nCOMPLETE GLOBAL CAST MANIFEST:\n" + manifestText +
            "\nYOUR EXACT ASSIGNED MEMBERS:\n" + manifestSubset(manifestText, castShards[shardIndex]);
        return juce::String("{\"model\":\"") + model +
            "\",\"background\":true,\"reasoning\":{\"effort\":\"" + realizationReasoningEffort +
            "\"},\"max_output_tokens\":9000,\"prompt_cache_key\":\"pulso-cast-detail-v1\",\"input\":" +
            juce::JSON::toString(juce::var(shardPrompt)) +
            ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_cast_detail\","
            "\"strict\":true,\"schema\":" + castDetailShardSchema() + "}}}";
    };
    const auto requestCastShard = [&](std::size_t shardIndex, std::chrono::milliseconds budget) {
        return performRequest(castDetailBody(shardIndex), apiKey, token, budget);
    };

    std::vector<std::size_t> unresolved(castShards.size());
    std::iota(unresolved.begin(), unresolved.end(), 0);
    std::vector<juce::String> shardFailures(castShards.size());
    for (int attempt = 1; attempt <= 3 && !unresolved.empty() && !token.stop_requested(); ++attempt) {
        // A third, longer attempt is reserved for transport/time-budget failures. Completed
        // but invalid answers get one retry; permanent HTTP errors are never retried.
        const auto budget = std::chrono::seconds(attempt == 1 ? 90 : attempt == 2 ? 75 : 120);
        if (attempt > 1 && progress)
            progress({AiSongStage::Recovery, castShards.size() - unresolved.size(),
                      castShards.size(), attempt, "recovering only unresolved cast detail shards"});
        std::vector<std::size_t> retryable;
        for (std::size_t batchBegin = 0; batchBegin < unresolved.size();
             batchBegin += maximumConcurrentCastShards) {
            const auto batchEnd = std::min(unresolved.size(), batchBegin + maximumConcurrentCastShards);
            std::vector<std::future<HttpResponse>> requests;
            for (auto offset = batchBegin; offset < batchEnd; ++offset)
                requests.push_back(std::async(std::launch::async, requestCastShard,
                    unresolved[offset], std::chrono::duration_cast<std::chrono::milliseconds>(budget)));
            for (auto offset = batchBegin; offset < batchEnd; ++offset) {
                const auto index = unresolved[offset];
                auto response = requests[offset - batchBegin].get();
                juce::String detailError;
                const auto httpOk = response.connected && response.status >= 200 &&
                    response.status < 300 && !response.cancelled && !response.timedOut;
                auto detailText = httpOk ? extractOutputText(juce::JSON::parse(response.body)) : juce::String{};
                if (httpOk && validateCastDetailShard(manifestText, castShards[index],
                                                       detailText, detailError)) {
                    cacheValidatedResponse(castDetailBody(index), response);
                    castDetailTexts[index] = std::move(detailText);
                    shardFailures[index].clear();
                    if (progress) progress({AiSongStage::Blueprint, index + 1, castShards.size(), attempt,
                                            attempt == 1 ? "validated cast detail shard" :
                                            "recovered cast detail shard; prior shards preserved"});
                    continue;
                }
                const auto transient = response.timedOut || !response.connected ||
                    response.status == 408 || response.status == 429 || response.status >= 500;
                const auto reason = response.cancelled ? juce::String("Generation cancelled") :
                    !httpOk ? apiErrorMessage(response) :
                    structuredResponseError(response.body, detailError.isNotEmpty() ? detailError :
                                            "Cast detail response contained no usable output");
                shardFailures[index] = reason;
                OperationalJournal::write("WARN", "CAST",
                    "detail shard " + juce::String(static_cast<int>(index + 1)) + "/" +
                    juce::String(static_cast<int>(castShards.size())) + " attempt " +
                    juce::String(attempt) + " failed | budget=" +
                    juce::String(static_cast<int>(budget.count())) + "s | " + reason);
                if (!response.cancelled && attempt < 3 && (transient || (attempt == 1 && httpOk)))
                    retryable.push_back(index);
            }
        }
        // Validated texts remain in their original slots and are never requested again.
        unresolved = std::move(retryable);
    }
    unresolved.clear();
    for (std::size_t index = 0; index < castDetailTexts.size(); ++index)
        if (castDetailTexts[index].isEmpty()) {
            OperationalJournal::write("ERROR", "CAST",
                "detail shard " + juce::String(static_cast<int>(index + 1)) + "/" +
                juce::String(static_cast<int>(castShards.size())) + " unresolved | " +
                shardFailures[index]);
            unresolved.push_back(index);
        }
    if (!unresolved.empty() || token.stop_requested()) {
        error = token.stop_requested() ? "Generation cancelled" :
            "OpenAI cast detail phase failed after bounded shard recovery; accepted cast shards were preserved";
        return result;
    }
    juce::String castMergeError;
    const auto castText = mergeCastManifestAndDetails(manifestText, castDetailTexts, castMergeError);
    juce::String outputText;
    juce::String protagonistBindingError;
    if (castText.isEmpty() ||
        !AiComposer::bindCastProtagonist(macroText, castText, outputText,
                                         protagonistBindingError)) {
        error = castMergeError.isNotEmpty() ? castMergeError :
            protagonistBindingError.isNotEmpty() ? protagonistBindingError :
            "Could not merge AI macro and instrument cast";
        return result;
    }
    if (!parseSongPlanJson(outputText, targetSeconds, totalBars, bpm, beatsPerBar,
                           seed, result, error,
                           tonalPolicyForDirection(direction.toStdString()))) return {};
    cacheValidatedResponse(usedMacroRecovery ? macroRecoveryBody : body, http);
    result.compositionBehavior = behavior;
    result.aiSovereign = aiSovereign;
    applyExplicitInstrumentCommitments(result, direction);
    // A percussion-free request can still be explicitly electronic. Promote that
    // intent before normalization so the production planner supplies synth pads,
    // arps, sequences and independent electronic layers instead of falling back to
    // a small orchestral cast merely because drums were excluded.
    const auto electronicDirection = directionLower.contains("synth") ||
        directionLower.contains("sintet") || directionLower.contains("electronic") ||
        directionLower.contains("electronica") || directionLower.contains("techno") ||
        directionLower.contains("progressive") || directionLower.contains("house") ||
        directionLower.contains("arpeggio") || directionLower.contains("arp") ||
        directionLower.contains("sequencer");
    if (electronicDirection) {
        result.productionLanguage.electronicIntent =
            std::max(result.productionLanguage.electronicIntent, 0.82);
        if (result.productionLanguage.domain == ProductionDomain::Adaptive ||
            result.productionLanguage.domain == ProductionDomain::Orchestral)
            result.productionLanguage.domain = ProductionDomain::Hybrid;
        result.orchestrationLanguage.hybridProduction =
            std::max(result.orchestrationLanguage.hybridProduction, 0.82);
    }
    applyExplicitRhythmRequest(result, direction);
    SongComposer::normalizePlan(result);
    OperationalJournal::write("OK", "CHECKPOINT",
        "authoritative protagonist bound to cast identity " +
        juce::String::fromUTF8(result.narrativeSpine.protagonistInstrumentId.c_str()));
    if (!repairMotionOwnerContract(result)) {
        error = "OpenAI cast violated the electronic motion-owner contract";
        return {};
    }
    repairCentralChordBedContract(result);
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return {};
    }

    // The local short proof has one musical authoring decision, not three
    // independent writers followed by patch/recovery passes. Absolute beats are
    // converted to section-local cells only after the whole score validates.
    if (localEditorial && direction.contains("LOCAL MUSICAL COHERENCE PROOF") &&
        targetSeconds <= 60) {
        static const juce::String proofSchema = R"json({
          "type":"object","additionalProperties":false,"required":["parts"],
          "properties":{"parts":{"type":"array","items":{"type":"object",
            "additionalProperties":false,"required":["instrument_id","notes"],
            "properties":{"instrument_id":{"type":"string"},"notes":{"type":"array",
              "items":{"type":"object","additionalProperties":false,
                "required":["beat","duration_beats","pitch","velocity"],
                "properties":{"beat":{"type":"number"},"duration_beats":{"type":"number"},
                  "pitch":{"type":"integer"},"velocity":{"type":"integer"}}}}}}}}
        })json";
        if (result.instruments.size() != 3 || juce::JSON::parse(proofSchema).isVoid()) {
            error = "The local coherent proof requires a three-part AI cast";
            return {};
        }
        juce::String scoreBrief;
        scoreBrief << "Write ONE coordinated three-part MIDI score. Each note is authored by you. "
            "Use absolute zero-based beat positions, quarter-beat grid, no loops, no repeats, "
            "no implied material, and no notes crossing section boundaries or 64-beat "
            "boundaries within a section. Return exactly "
            "one part for each instrument ID; no extra parts. Chord bed must state full "
            "polyphonic chords in opening, development and ending; bass roots/approaches "
            "must support them; a single monophonic protagonist must form a memorable "
            "question, development and resolving answer with actual rests. Compose these "
            "three lines as one arrangement, not three independent solos. The chord "
            "palette is a shared harmonic vocabulary, NOT a command to play every pitch. "
            "Choose clear voicings and leave bass foundations to the bass. Dissonance "
            "can be expressive when the exact simultaneous pitches, register, duration "
            "and later release make its role audible. Keep close clashes out of the low "
            "register unless the brief explicitly calls for them; an upper major seventh "
            "or dominant guide-tone tritone can be intentional. For chords held for "
            "several beats, audition the actual voicing against the bass and lead: "
            "adjacent pitch classes or a minor-ninth spacing should be an intentional "
            "colour, not an accidental consequence of playing every available chord "
            "tone. Prefer an omission, inversion or register change when it makes the "
            "harmonic function clearer. Do not ban tension globally. Do not sustain an old "
            "chord tone through a new harmony unless it remains supported or resolves. "
            "Write the actual pitches and silences of the full ensemble; keep the note "
            "budget concise.\n"
            "Creative brief: " << direction << "\n"
            "Timeline: " << result.totalBars << " bars, " << result.beatsPerBar
            << " beats per bar, total beats " << result.totalBars * result.beatsPerBar << ".\n"
            "Key: " << juce::String::fromUTF8(result.key.c_str()) <<
            "; tonal policy: " << juce::String(tonalPolicyKey(
                result.harmonicLanguage.tonalPolicy).data()) << ".\n"
            "Narrative premise: " << juce::String::fromUTF8(result.narrativeSpine.premise.c_str())
            << "; motif identity: " << juce::String::fromUTF8(
                result.narrativeSpine.motifIdentity.c_str())
            << "; harmonic debt: " << juce::String::fromUTF8(
                result.narrativeSpine.harmonicDebt.c_str()) << ".\n"
            "Narrative question: " << juce::String::fromUTF8(result.narrativeSpine.question.c_str())
            << "; resolution: " << juce::String::fromUTF8(result.narrativeSpine.resolution.c_str())
            << ".\nNarrative acts:\n";
        for (const auto& act : result.narrativeSpine.acts)
            scoreBrief << act.sectionName << " stage=" <<
                juce::String(narrativeStageKey(act.stage).data()) <<
                " cause=" << act.cause << " consequence=" << act.consequence <<
                " unresolved=" << act.unresolvedElement <<
                " resolution_target=" << act.resolutionTarget << "\n";
        scoreBrief << "Instruments:\n";
        for (const auto& instrument : result.instruments)
            scoreBrief << instrument.id << " | " << instrument.name << " | "
                << instrument.role << " | register " << instrument.minimumPitch << "-"
                << instrument.maximumPitch << " | voice "
                << juce::String(voiceDefinition(instrument.sourceVoice).key.data()) << "\n";
        scoreBrief << "Chord palette:\n";
        for (const auto& chord : result.chordPalette) {
            scoreBrief << chord.id << " label=" << chord.label <<
                " root=" << chord.rootPitchClass << " bass="
                << chord.bassPitchClass << " function=" <<
                juce::String(harmonicFunctionKey(chord.function).data()) << " voicing=" <<
                juce::String(voicingStrategyKey(chord.voicing).data()) <<
                " tension=" << juce::String(chord.tension, 2) << " pitches=";
            for (const auto pitchClass : chord.pitchClasses) scoreBrief << pitchClass << ",";
            scoreBrief << "\n";
        }
        scoreBrief << "Section/harmony timeline (absolute beats):\n";
        for (const auto& section : result.sections) {
            scoreBrief << section.name << " ["
                << section.startBar * result.beatsPerBar << ","
                << (section.startBar + section.bars) * result.beatsPerBar << ") "
                << section.function << " | harmonic motion " << section.harmonicDirection
                << " | motif treatment " << section.motifTreatment
                << " | energy " << juce::String(section.energy, 2)
                << " tension " << juce::String(section.tension, 2)
                << " density " << juce::String(section.density, 2) << "\n";
            for (const auto& event : section.harmonicEvents)
                scoreBrief << "  beat=" << (section.startBar + event.barOffset) *
                    result.beatsPerBar + event.beatOffset << " chord=" << event.chordId <<
                    " purpose=" << event.purpose << "\n";
        }
        scoreBrief << "Performance arc: make the section functions audible in actual notes, "
            "not just labels. Preserve a recognizable motif, then alter its register, "
            "rhythm, phrase length or answer as the narrative develops. Let the chord "
            "bed sustain a harmonic floor while bass and lead sometimes pause or hand "
            "off; avoid identical attack counts in every bar merely to fill space. "
            "Hypnosis may hold a harmony and return to a figure without restarting the "
            "same phrase each bar. Give the ending a musically prepared consequence. "
            "All variations, pauses and notes must be authored in this score.\n";
        for (int attempt = 1; attempt <= 2; ++attempt) {
            if (token.stop_requested()) {
                error = "Generation cancelled";
                return {};
            }
            const auto body = juce::String("{\"model\":\"") + model +
                "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                realizationReasoningEffort +
                "\"},\"max_output_tokens\":10000,\"input\":" +
                juce::JSON::toString(juce::var(scoreBrief)) +
                ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_coherent_proof\","
                "\"strict\":true,\"schema\":" + proofSchema + "}}}";
            if (progress) progress({attempt == 1 ? AiSongStage::PerformanceBlock :
                AiSongStage::Recovery, 0, 1, attempt,
                attempt == 1 ? "writing coordinated chord bed, bass and melody" :
                    "rewriting complete three-part score after validation"});
            const auto response = performRequest(body, apiKey, token, std::chrono::minutes(3));
            if (!response.connected || response.status < 200 || response.status >= 300 ||
                response.cancelled || response.timedOut) {
                error = apiErrorMessage(response);
                return {};
            }
            const auto scoreText = extractOutputText(juce::JSON::parse(response.body));
            if (scoreText.isEmpty()) {
                error = structuredResponseError(response.body, "OpenAI returned no coherent score");
            } else if (parseCoherentProofJson(
                    scoreText, result, result.performanceScore, error)) {
                GenerationContext foundation;
                foundation.role = Role::Ensemble;
                foundation.rootPitchClass = result.rootPitchClass;
                foundation.scale = result.scale;
                foundation.beatsPerBar = result.beatsPerBar;
                foundation.seed = result.seed;
                foundation.humanize = 0.0;
                CompositionRenderReport audit;
                const auto rendered = SongComposer{}.render(result, foundation, {}, &audit);
                const auto authoredNotes = std::accumulate(result.performanceScore.cells.begin(),
                    result.performanceScore.cells.end(), std::size_t{},
                    [](std::size_t count, const auto& cell) { return count + cell.notes.size(); });
                const auto gate = CoherentProofGate::evaluate(
                    result, rendered, audit, authoredNotes);
                if (gate.ready) {
                    auto acceptedGate = gate;
                    auto acceptedNotes = authoredNotes;
                    const auto windows = CoherentProofRevision::selectWindows(
                        result, rendered, gate);
                    result.coherentReview.riskBefore =
                        result.coherentReview.riskAfter = CoherentProofRevision::risk(gate);
                    result.coherentReview.targetedWindows =
                        static_cast<int>(windows.size());
                    if (token.stop_requested()) {
                        error = "Generation cancelled";
                        return {};
                    }
                    auto checkpointed = false;
                    if (!windows.empty() && !token.stop_requested()) {
                        result.coherentReview.attempted = true;
                        if (checkpoint) {
                            checkpoint(result, 1, false, false);
                            checkpointed = true;
                        }
                        static const juce::String revisionSchema = R"json({
                          "type":"object","additionalProperties":false,"required":["patches"],
                          "properties":{"patches":{"type":"array","items":{"type":"object",
                            "additionalProperties":false,
                            "required":["instrument_id","start_beat","end_beat","notes"],
                            "properties":{"instrument_id":{"type":"string"},
                              "start_beat":{"type":"number"},"end_beat":{"type":"number"},
                              "notes":{"type":"array","items":{"type":"object",
                                "additionalProperties":false,
                                "required":["beat","duration_beats","pitch","velocity"],
                                "properties":{"beat":{"type":"number"},
                                  "duration_beats":{"type":"number"},
                                  "pitch":{"type":"integer"},
                                  "velocity":{"type":"integer"}}}}}}}}}
                        )json";
                        juce::String revisionBrief;
                        revisionBrief << "You are the same composer reviewing your finished MIDI score. "
                            "Return ONLY replacement notes for the exact requested windows. "
                            "All other notes, instruments, motif identity, form and chord timeline "
                            "are immutable. These are contextual listening concerns, not forbidden "
                            "intervals: preserve intentional expressive tension when it has a clear "
                            "release, but reconsider long close clashes, congested registers and "
                            "simultaneous notes that obscure the harmonic floor. Do not solve every "
                            "concern by deleting notes or flattening the harmony. Maintain the "
                            "protagonist's question and resolving answer. Each patch must contain "
                            "exactly the requested instrument_id and start/end beats; every note "
                            "must lie wholly inside that window on the quarter-beat grid. Return "
                            "structured JSON only.\nOriginal direction: " << direction <<
                            "\nKey: " << juce::String::fromUTF8(result.key.c_str()) <<
                            "\nMotif: " << juce::String::fromUTF8(
                                result.narrativeSpine.motifIdentity.c_str()) <<
                            "\nResolution: " << juce::String::fromUTF8(
                                result.narrativeSpine.resolution.c_str()) <<
                            "\nHarmonic timeline (absolute beats):\n";
                        for (const auto& section : result.sections)
                            for (const auto& event : section.harmonicEvents)
                                revisionBrief << (section.startBar + event.barOffset) *
                                    result.beatsPerBar + event.beatOffset << " " <<
                                    juce::String::fromUTF8(event.chordId.c_str()) << "\n";
                        for (const auto& window : windows) {
                            const auto& instrument = result.instruments[window.partId - 1];
                            revisionBrief << "\nPATCH instrument_id=" <<
                                juce::String::fromUTF8(instrument.id.c_str()) <<
                                " start_beat=" << window.startBeat <<
                                " end_beat=" << window.endBeat <<
                                " allowed_pitch=" << instrument.minimumPitch << "-" <<
                                instrument.maximumPitch << "\nReasons:\n";
                            for (const auto& issue : gate.issues)
                                if (issue.partId == window.partId &&
                                    issue.beat >= window.startBeat &&
                                    issue.beat < window.endBeat &&
                                    CoherentProofRevision::issuePriority(issue) >= 1.8)
                                    revisionBrief << "beat " << issue.beat << " pitches " <<
                                        issue.pitch << "/" << issue.otherPitch <<
                                        " overlap " << issue.overlapBeats <<
                                        " beats, register " <<
                                        juce::String::fromUTF8(issue.registerName.c_str()) <<
                                        ", chord " <<
                                        juce::String::fromUTF8(issue.chordId.c_str()) <<
                                        ", observed_resolution=" <<
                                        juce::String(issue.resolutionObserved ? "yes" : "no") <<
                                        "\n";
                            revisionBrief << "Existing ensemble notes in and around this window "
                                "(part_id, beat, duration, pitch, velocity):\n";
                            for (const auto& note : rendered.notes)
                                if (note.startBeat < window.endBeat + result.beatsPerBar &&
                                    note.endBeat() > window.startBeat - result.beatsPerBar)
                                    revisionBrief << note.partId << " " << note.startBeat <<
                                        " " << note.durationBeats << " " << note.pitch <<
                                        " " << note.velocity << "\n";
                        }
                        if (progress) progress({AiSongStage::Recovery, 0, 1, 1,
                            "AI reviewing contextual voicings in bounded passages"});
                        const auto revisionBody = juce::String("{\"model\":\"") + model +
                            "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                            realizationReasoningEffort +
                            "\"},\"max_output_tokens\":4500,\"input\":" +
                            juce::JSON::toString(juce::var(revisionBrief)) +
                            ",\"text\":{\"format\":{\"type\":\"json_schema\","
                            "\"name\":\"pulso_coherent_revision\",\"strict\":true,\"schema\":" +
                            revisionSchema + "}}}";
                        const auto revisionHttp = performRequest(revisionBody, apiKey, token,
                            std::chrono::seconds(110));
                        if (token.stop_requested() || revisionHttp.cancelled) {
                            error = "Generation cancelled";
                            return {};
                        }
                        auto revisionAccepted = false;
                        if (revisionHttp.connected && revisionHttp.status >= 200 &&
                            revisionHttp.status < 300 && !revisionHttp.cancelled &&
                            !revisionHttp.timedOut) {
                            const auto revisionText = extractOutputText(
                                juce::JSON::parse(revisionHttp.body));
                            PerformanceScore revisedScore;
                            juce::String revisionError;
                            if (!revisionText.isEmpty() && applyCoherentProofRevisionJson(
                                    scoreText, revisionText, result, windows,
                                    revisedScore, revisionError)) {
                                auto candidate = result;
                                candidate.performanceScore = std::move(revisedScore);
                                CompositionRenderReport revisedAudit;
                                const auto revisedMidi = SongComposer{}.render(
                                    candidate, foundation, {}, &revisedAudit);
                                const auto revisedNotes = std::accumulate(
                                    candidate.performanceScore.cells.begin(),
                                    candidate.performanceScore.cells.end(), std::size_t{},
                                    [](std::size_t count, const auto& cell) {
                                        return count + cell.notes.size();
                                    });
                                const auto revisedGate = CoherentProofGate::evaluate(
                                    candidate, revisedMidi, revisedAudit, revisedNotes);
                                const auto narrativePreserved =
                                    revisedAudit.narrative.resolutionScore + 0.05 >=
                                        audit.narrative.resolutionScore &&
                                    revisedAudit.narrative.score + 0.05 >=
                                        audit.narrative.score;
                                if (narrativePreserved && CoherentProofRevision::accepts(
                                        gate, revisedGate, authoredNotes, revisedNotes)) {
                                    result.performanceScore = std::move(candidate.performanceScore);
                                    acceptedGate = revisedGate;
                                    acceptedNotes = revisedNotes;
                                    revisionAccepted = true;
                                    result.coherentReview.accepted = true;
                                    result.coherentReview.riskAfter =
                                        CoherentProofRevision::risk(revisedGate);
                                }
                            }
                        }
                        OperationalJournal::write(revisionAccepted ? "OK" : "WARN",
                            "MUSICAL_REVISION", revisionAccepted ?
                                "AI passage revision accepted; untouched MIDI retained exactly" :
                                "AI passage revision unavailable or not measurably safer; original AI score retained");
                        if (progress) progress({AiSongStage::Validation, 1, 1, 1,
                            revisionAccepted ? "contextual AI revision accepted" :
                                "original AI score preserved after contextual review"});
                        if (revisionAccepted && checkpoint) checkpoint(result, 2, false, false);
                    }
                    if (!checkpointed && checkpoint) checkpoint(result, 1, false, false);
                    if (progress) progress({AiSongStage::Validation, 1, 1, attempt,
                        "coordinated AI score validated with bounded contextual review"});
                    OperationalJournal::write("OK", "COHERENT_PROOF",
                        "accepted exact AI score | notes=" +
                        juce::String(static_cast<int>(acceptedNotes)));
                    if (acceptedGate.musicalReviewRequired)
                        OperationalJournal::write("WARN", "COHERENT_PROOF",
                            "musical review still recommended | harsh=" +
                            juce::String(acceptedGate.unintendedHarshOverlaps) + " low=" +
                            juce::String(static_cast<int>(acceptedGate.lowRegisterVerticalClashes)) +
                            " chromatic=" + juce::String(acceptedGate.unsupportedChromaticNotes));
                    if (acceptedGate.uniformActivity)
                        OperationalJournal::write("WARN", "COHERENT_PROOF",
                            "lead and chord-bed attack counts are uniform across bars; "
                            "editorial audition recommended");
                    error.clear();
                    return result;
                }
                error = "Exact AI authorship or MIDI integrity failed: authored=" +
                    juce::String(static_cast<int>(authoredNotes)) + " rendered=" +
                    juce::String(static_cast<int>(rendered.notes.size())) +
                    " exact_ai=" + juce::String(gate.exactAiNotes ? 1 : 0) +
                    " [metric=" + juce::String(static_cast<int>(audit.production.metricViolations)) +
                    " duration=" + juce::String(static_cast<int>(audit.production.unsafeDurations)) +
                    " orphan=" + juce::String(static_cast<int>(audit.production.orphanEvents)) + "]";
                // Preserve the rejected MIDI as an explicitly diagnostic artifact.
                // It never becomes the accepted composition.
                if (checkpoint) checkpoint(result, static_cast<std::size_t>(attempt),
                    true, true);
                if (attempt == 1)
                    scoreBrief << "\nThe previous score failed objective MIDI integrity. "
                        "Keep the musical design, but author a fresh complete score with "
                        "valid quarter-beat attacks, positive note durations inside the "
                        "work, and every note assigned to its declared part.\n";
            }
            OperationalJournal::write("WARN", "COHERENT_PROOF",
                "whole-score attempt " + juce::String(attempt) + " rejected: " + error);
            if (attempt == 1)
                scoreBrief << "\nThe previous complete score was rejected: " << error
                    << ". Rewrite ALL THREE PARTS together from first beat to last. "
                       "Do not merely patch one note or one instrument.\n";
        }
        result.performanceScore = {};
        return {};
    }

    // In the local sovereign studio, write the ensemble in bounded time windows.
    // Every window is a whole-arrangement AI decision. The previous window is
    // immutable and supplies real MIDI context; no renderer invents missing
    // notes or fills rests. This scales with song length without asking any
    // single model response to write one instrument for ten entire minutes.
    std::vector<std::size_t> jointOwners;
    if (localEditorial && result.totalBars >= 24) {
        // The sovereign renderer never synthesizes notes for a shared timbral
        // destination. It must therefore be an explicit AI owner too.
        for (std::size_t index = 0; index < result.instruments.size(); ++index)
            jointOwners.push_back(index);
    }
    if (localEditorial && !jointOwners.empty() && jointOwners.size() <= 8) {
        constexpr auto barsPerWindow = 16;
        std::vector<std::size_t> harmonicAnchors;
        std::vector<std::size_t> expressiveVoices;
        for (const auto index : jointOwners) {
            const auto voice = result.instruments[index].sourceVoice;
            if (voice == VoiceId::HarmonicFoundation ||
                isVoiceInFamily(voice, VoiceFamily::Bass))
                harmonicAnchors.push_back(index);
            else
                expressiveVoices.push_back(index);
        }
        std::vector<std::vector<std::size_t>> writingGroups;
        if (!harmonicAnchors.empty() && !expressiveVoices.empty())
            writingGroups = {harmonicAnchors, expressiveVoices};
        else
            writingGroups = {jointOwners};
        std::size_t windowCount = 0;
        for (const auto& section : result.sections)
            windowCount += static_cast<std::size_t>(
                (section.bars + barsPerWindow - 1) / barsPerWindow);
        PerformanceScore score;
        std::vector<double> sectionLengths;
        for (const auto& section : result.sections)
            sectionLengths.push_back(section.bars * result.beatsPerBar);
        auto completedWindows = std::size_t{};
        auto serial = std::size_t{};
        for (std::size_t sectionIndex = 0; sectionIndex < result.sections.size(); ++sectionIndex) {
            const auto& section = result.sections[sectionIndex];
            for (auto firstBar = 0; firstBar < section.bars; firstBar += barsPerWindow) {
                const auto bars = std::min(barsPerWindow, section.bars - firstBar);
                const auto localStart = firstBar * result.beatsPerBar;
                const auto localEnd = (firstBar + bars) * result.beatsPerBar;
                const auto absoluteStart = (section.startBar + firstBar) * result.beatsPerBar;
                const auto absoluteEnd = absoluteStart + bars * result.beatsPerBar;
                for (std::size_t groupIndex = 0; groupIndex < writingGroups.size(); ++groupIndex) {
                const auto& stageOwners = writingGroups[groupIndex];
                juce::String feedback;
                auto accepted = false;
                PerformanceScore bestRejectedScore;
                auto bestRejectedRisk = std::numeric_limits<int>::max();
                juce::String bestRejectedFeedback;
                std::optional<PerformanceScore> focusedBase;
                auto requestedOwners = stageOwners;
                juce::String originalWindowRequest;
                const auto reportWindowFailure = [&](int attempt) {
                    OperationalJournal::write("WARN", "JOINT_WINDOW", feedback);
                    if (progress) progress({AiSongStage::Recovery, completedWindows,
                        windowCount, attempt, "ensemble window " +
                        juce::String(static_cast<int>(completedWindows + 1)) +
                        " group " + juce::String(static_cast<int>(groupIndex + 1)) +
                        "/" + juce::String(static_cast<int>(writingGroups.size())) +
                        " review: " + feedback});
                };
                // One ensemble response plus bounded owner-specific revisions.
                // A larger expressive group may expose a different offending
                // owner only after the first clash is repaired; five total
                // calls prematurely exhausted that progression in longer works.
                const auto maximumWindowAttempts = std::min<std::size_t>(
                    7, std::max<std::size_t>(5, stageOwners.size() + 3));
                for (auto attempt = 1; attempt <= maximumWindowAttempts && !accepted; ++attempt) {
                    if (token.stop_requested()) {
                        error = "Generation cancelled";
                        return {};
                    }
                    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                        aiStarted + totalAiBudget - std::chrono::steady_clock::now());
                    if (remaining < std::chrono::seconds(45)) {
                        error = "Joint AI score reached its global time budget; accepted MIDI checkpoints preserved";
                        return {};
                    }
                    auto snapshot = result;
                    snapshot.performanceScore = focusedBase ? *focusedBase : score;
                    GenerationContext foundation;
                    foundation.role = Role::Ensemble;
                    foundation.rootPitchClass = result.rootPitchClass;
                    foundation.scale = result.scale;
                    foundation.beatsPerBar = result.beatsPerBar;
                    foundation.seed = result.seed;
                    foundation.humanize = 0.0;
                    Pattern acceptedPattern;
                    if (!snapshot.performanceScore.empty())
                        acceptedPattern = SongComposer{}.render(snapshot, foundation);
                    const auto prompt = jointWindowPrompt(direction, result, acceptedPattern,
                        requestedOwners, sectionIndex, firstBar, bars, feedback,
                        focusedBase.has_value());
                    const auto body = juce::String("{\"model\":\"") + model +
                        "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                        realizationReasoningEffort +
                        "\"},\"max_output_tokens\":14000,\"input\":" +
                        juce::JSON::toString(juce::var(prompt)) +
                        ",\"text\":{\"format\":{\"type\":\"json_schema\","
                        "\"name\":\"pulso_direct_window\",\"strict\":true,\"schema\":" +
                        directWindowSchema(result, requestedOwners) + "}}}";
                    if (attempt == 1) originalWindowRequest = body;
                    if (progress) progress({attempt == 1 ? AiSongStage::PerformanceBlock :
                        AiSongStage::Recovery, completedWindows, windowCount, attempt,
                        "AI composing ensemble window " +
                        juce::String(static_cast<int>(completedWindows + 1)) + "/" +
                        juce::String(static_cast<int>(windowCount)) + " group " +
                        juce::String(static_cast<int>(groupIndex + 1)) + "/" +
                        juce::String(static_cast<int>(writingGroups.size()))});
                    const auto response = performRequest(body, apiKey, token,
                        std::min<std::chrono::seconds>(remaining, std::chrono::seconds(165)));
                    if (!response.connected || response.status < 200 || response.status >= 300 ||
                        response.cancelled || response.timedOut) {
                        feedback = apiErrorMessage(response);
                        reportWindowFailure(attempt);
                        continue;
                    }
                    const auto output = extractOutputText(juce::JSON::parse(response.body));
                    PerformanceScore passage;
                    if (output.isEmpty() || !parseDirectWindow(
                            output, result, requestedOwners, sectionIndex, firstBar, bars,
                            passage, feedback)) {
                        if (feedback.isEmpty()) feedback = "The joint score response was empty";
                        reportWindowFailure(attempt);
                        continue;
                    }
                    auto contained = !passage.cells.empty() && !passage.placements.empty();
                    juce::String containmentDetail;
                    for (const auto& placement : passage.placements) {
                        const auto cell = std::find_if(passage.cells.begin(), passage.cells.end(),
                            [&](const auto& item) { return item.id == placement.cellId; });
                        const auto fragmentEnd = cell == passage.cells.end() ? 0.0 :
                            placement.fragmentEnd > placement.fragmentStart ?
                                placement.fragmentEnd : cell->lengthBeats;
                        const auto audibleEnd = cell == passage.cells.end() ? 0.0 :
                            placement.startBeat +
                            ((std::max(1, placement.repeats) - 1) * cell->lengthBeats +
                             fragmentEnd) * placement.timeScale;
                        if (cell == passage.cells.end() ||
                            placement.sectionIndex != static_cast<int>(sectionIndex) ||
                            placement.startBeat < localStart - .001 ||
                            audibleEnd > localEnd + .001) {
                            contained = false;
                            if (containmentDetail.isEmpty())
                                containmentDetail = " cell=" +
                                    juce::String::fromUTF8(placement.cellId.c_str()) +
                                    " section=" + juce::String(placement.sectionIndex) +
                                    " start=" + juce::String(placement.startBeat, 2) +
                                    " audible_end=" + juce::String(audibleEnd, 2) +
                                    " allowed=[" + juce::String(localStart, 2) + "," +
                                    juce::String(localEnd, 2) + ")";
                        }
                    }
                    if (!contained) {
                        feedback = "Placements crossed the assigned section-local window:" +
                            containmentDetail;
                        reportWindowFailure(attempt);
                        continue;
                    }
                    auto candidate = snapshot.performanceScore;
                    mergePerformanceBlock(candidate, std::move(passage), serial++);
                    const auto normalized = PerformanceScoreEngine::normalize(
                        candidate, result.sections.size(), sectionLengths, true);
                    if (normalized.cellsRejected != 0 || normalized.placementsRejected != 0) {
                        feedback = "The authored cells or placements were outside the valid MIDI score";
                        reportWindowFailure(attempt);
                        continue;
                    }
                    snapshot.performanceScore = candidate;
                    CompositionRenderReport audition;
                    const auto rendered = SongComposer{}.render(snapshot, foundation, {}, &audition);
                    if (!EditorialSafety::technicallySafeAiScore(rendered, audition)) {
                        feedback = "MIDI timing, ownership or duration was technically invalid";
                        reportWindowFailure(attempt);
                        continue;
                    }
                    const auto windowNotes = std::count_if(rendered.notes.begin(),
                        rendered.notes.end(), [&](const auto& note) {
                            return note.startBeat >= absoluteStart - .001 &&
                                note.startBeat < absoluteEnd - .001;
                        });
                    if (windowNotes == 0) {
                        feedback = "The window rendered no audible MIDI notes";
                        reportWindowFailure(attempt);
                        continue;
                    }
                    // Review the actually sounding ensemble, not the JSON's
                    // claims. A locally valid MIDI passage may still contain
                    // sustained semitone clashes or omit an active owner.
                    int sustainedClashes = 0;
                    int strongNonChord = 0;
                    int unsupported = 0;
                    juce::String sustainedExamples;
                    juce::String nonChordExample;
                    juce::String chromaticExamples;
                    const auto issuePosition = [&](double beat) {
                        juce::String chordName = "unassigned harmony";
                        auto latestEventBeat = -1.0;
                        for (const auto& plannedSection : result.sections)
                            for (const auto& event : plannedSection.harmonicEvents) {
                                const auto eventBeat =
                                    (plannedSection.startBar + event.barOffset) *
                                    result.beatsPerBar + event.beatOffset;
                                if (eventBeat > beat + .001 ||
                                    eventBeat < latestEventBeat) continue;
                                const auto chord = std::find_if(
                                    result.chordPalette.begin(), result.chordPalette.end(),
                                    [&](const auto& item) { return item.id == event.chordId; });
                                if (chord == result.chordPalette.end()) continue;
                                latestEventBeat = eventBeat;
                                chordName = juce::String::fromUTF8(chord->label.c_str());
                            }
                        return "window-local beat " +
                            juce::String(beat - absoluteStart, 2) +
                            " (absolute " + juce::String(beat, 2) +
                            ", chord " + chordName + ")";
                    };
                    const auto issueOwner = [&](std::uint16_t partId) {
                        return partId > 0 && partId <= result.instruments.size() ?
                            juce::String::fromUTF8(result.instruments[partId - 1].id.c_str()) :
                            juce::String("unknown instrument");
                    };
                    for (const auto& issue : audition.finalTonalPass.after.issues) {
                        if (issue.beat < absoluteStart - .001 ||
                            issue.beat >= absoluteEnd - .001) continue;
                        // At 120 BPM a half-beat overlap lasts 250 ms: clearly
                        // audible, particularly against a sustained chord bed.
                        // The old one-beat cutoff missed every clash in the
                        // short-form proof score, including low bass/beds.
                        if (issue.kind == "harsh_overlap" &&
                            issue.overlapBeats >= .5 - .001) {
                            ++sustainedClashes;
                            if (sustainedClashes <= 3) {
                                if (sustainedExamples.isNotEmpty()) sustainedExamples << "; ";
                                sustainedExamples << issueOwner(issue.partId) << " pitch "
                                    << issue.pitch << " against " <<
                                    issueOwner(issue.otherPartId) << " pitch " <<
                                    issue.otherPitch << " at " << issuePosition(issue.beat)
                                    << " for "
                                    << juce::String(issue.overlapBeats, 2) << " beats";
                            }
                        } else if (issue.kind == "strong_non_chord") {
                            ++strongNonChord;
                            if (nonChordExample.isEmpty())
                                nonChordExample = "strong non-chord " +
                                    issueOwner(issue.partId) + " pitch " +
                                    juce::String(issue.pitch) + " at " +
                                    issuePosition(issue.beat);
                        } else if (issue.kind == "unsupported_chromatic") {
                            ++unsupported;
                            if (unsupported <= 3) {
                                if (chromaticExamples.isNotEmpty())
                                    chromaticExamples << "; ";
                                chromaticExamples << issueOwner(issue.partId) << " pitch "
                                    << issue.pitch << " at " << issuePosition(issue.beat);
                            }
                        }
                    }
                    juce::String missingOwner;
                    for (const auto index : stageOwners) {
                        const auto& instrument = result.instruments[index];
                        const auto activeHere = instrument.activeSections.empty() ||
                            std::find(instrument.activeSections.begin(),
                                      instrument.activeSections.end(), section.name) !=
                                instrument.activeSections.end();
                        const auto futureActiveSection = std::any_of(
                            result.sections.begin() + static_cast<std::ptrdiff_t>(sectionIndex + 1),
                            result.sections.end(), [&](const auto& future) {
                                return instrument.activeSections.empty() ||
                                    std::find(instrument.activeSections.begin(),
                                              instrument.activeSections.end(), future.name) !=
                                        instrument.activeSections.end();
                            });
                        const auto laterWindowHere = activeHere &&
                            firstBar + bars < section.bars;
                        const auto lastOpportunity = activeHere && !futureActiveSection &&
                            !laterWindowHere;
                        if (!activeHere) continue;
                        const auto notes = std::count_if(rendered.notes.begin(),
                            rendered.notes.end(), [&](const auto& note) {
                                return note.partId == index + 1 &&
                                    note.startBeat >= absoluteStart - .001 &&
                                    note.startBeat < absoluteEnd - .001;
                            });
                        const auto fullScoreNotes = std::count_if(rendered.notes.begin(),
                            rendered.notes.end(), [&](const auto& note) {
                                return note.partId == index + 1;
                            });
                        const auto indispensableOpeningOwner = completedWindows == 0 && activeHere &&
                            (instrument.sourceVoice == VoiceId::HarmonicFoundation ||
                             isVoiceInFamily(instrument.sourceVoice, VoiceFamily::Bass) ||
                             instrument.id == result.narrativeSpine.protagonistInstrumentId);
                        if ((indispensableOpeningOwner && notes == 0) ||
                            (lastOpportunity && fullScoreNotes < 2)) {
                            missingOwner = juce::String::fromUTF8(instrument.id.c_str());
                            break;
                        }
                    }
                    // A chord palette states harmonic gravity, not every
                    // permitted in-key passing or bass approach. Preserve a
                    // small amount of AI-authored modal movement when it is
                    // free of sustained clashes and chromatic accidents.
                    const auto nonChordBudget = std::max(4, bars / 4);
                    if (sustainedClashes > 0 || strongNonChord > nonChordBudget || unsupported > 1 ||
                        missingOwner.isNotEmpty()) {
                        const auto risk = sustainedClashes * 10 + unsupported * 4 +
                            strongNonChord + (missingOwner.isNotEmpty() ? 100 : 0);
                        const auto improved = risk < bestRejectedRisk;
                        if (improved) bestRejectedScore = candidate;
                        // Keep safe AI-authored voices from this attempt. The next
                        // response is a focused AI rewrite of only one offending
                        // owner, not another full-group composition. No note is
                        // procedurally retuned, inserted, or silently discarded
                        // from an accepted window.
                        auto repairPart = std::uint16_t{};
                        juce::String rejectedOwnerMidi;
                        if (missingOwner.isNotEmpty()) {
                            for (const auto owner : stageOwners)
                                if (result.instruments[owner].id == missingOwner.toStdString())
                                    repairPart = static_cast<std::uint16_t>(owner + 1);
                        }
                        if (repairPart == 0) {
                            std::vector<int> ownerConflictCounts(result.instruments.size());
                            for (const auto& issue : audition.finalTonalPass.after.issues) {
                                if (issue.beat < absoluteStart - .001 ||
                                    issue.beat >= absoluteEnd - .001) continue;
                                const auto relevant =
                                    (issue.kind == "harsh_overlap" &&
                                     issue.overlapBeats >= .5 - .001) ||
                                    issue.kind == "unsupported_chromatic" ||
                                    issue.kind == "strong_non_chord";
                                if (!relevant) continue;
                                for (const auto owner : stageOwners) {
                                    const auto part = static_cast<std::uint16_t>(owner + 1);
                                    if (issue.partId == part ||
                                        (issue.kind == "harsh_overlap" &&
                                         issue.otherPartId == part))
                                        ++ownerConflictCounts[owner];
                                }
                            }
                            const auto offender = std::max_element(stageOwners.begin(),
                                stageOwners.end(), [&](const auto left, const auto right) {
                                    return ownerConflictCounts[left] < ownerConflictCounts[right];
                                });
                            if (offender != stageOwners.end() &&
                                ownerConflictCounts[*offender] > 0)
                                repairPart = static_cast<std::uint16_t>(*offender + 1);
                        }
                        if (improved && repairPart != 0 &&
                            attempt < maximumWindowAttempts) {
                            const auto repairOwner = static_cast<std::size_t>(repairPart - 1);
                            auto listedNotes = 0;
                            for (const auto& note : rendered.notes) {
                                if (note.partId != repairPart ||
                                    note.startBeat < absoluteStart - .001 ||
                                    note.startBeat >= absoluteEnd - .001 ||
                                    listedNotes >= 96) continue;
                                rejectedOwnerMidi << juce::String(
                                    note.startBeat - absoluteStart, 2) << "," <<
                                    juce::String(note.durationBeats, 2) << "," <<
                                    note.pitch << "," << note.velocity << "; ";
                                ++listedNotes;
                            }
                            std::set<std::string> removeCells;
                            for (const auto& cell : candidate.cells)
                                if (std::any_of(cell.notes.begin(), cell.notes.end(),
                                        [&](const auto& note) {
                                            return note.instrumentId ==
                                                result.instruments[repairOwner].id;
                                        }))
                                    for (const auto& placement : candidate.placements)
                                        if (placement.cellId == cell.id &&
                                            placement.sectionIndex ==
                                                static_cast<int>(sectionIndex) &&
                                            std::abs(placement.startBeat - localStart) < .001)
                                            removeCells.insert(cell.id);
                            if (!removeCells.empty()) {
                                candidate.cells.erase(std::remove_if(candidate.cells.begin(),
                                    candidate.cells.end(), [&](const auto& cell) {
                                        return removeCells.contains(cell.id);
                                    }), candidate.cells.end());
                                candidate.placements.erase(std::remove_if(
                                    candidate.placements.begin(), candidate.placements.end(),
                                    [&](const auto& placement) {
                                        return removeCells.contains(placement.cellId);
                                    }), candidate.placements.end());
                                focusedBase = std::move(candidate);
                                requestedOwners = {repairOwner};
                            }
                        }
                        feedback = "The audible ensemble failed harmonic/ownership review: " +
                            juce::String(sustainedClashes) + " sustained clashes, " +
                            juce::String(strongNonChord) + " structural non-chord tones, " +
                            juce::String(unsupported) + " unsupported chromatic tones" +
                            (missingOwner.isNotEmpty() ? "; missing active instrument " +
                                missingOwner : juce::String()) +
                            (sustainedExamples.isNotEmpty() ?
                                "; sustained overlap examples: " + sustainedExamples :
                                juce::String()) +
                            (nonChordExample.isNotEmpty() ? "; " + nonChordExample :
                                juce::String()) +
                            (chromaticExamples.isNotEmpty() ?
                                "; unsupported chromatic examples: " +
                                chromaticExamples : juce::String()) +
                            ". Repair the offending voicing against the accepted MIDI; "
                            "the macro harmony and accepted notes remain immutable.";
                        if (rejectedOwnerMidi.isNotEmpty())
                            feedback << " Rejected owner phrase (window-local "
                                "beat,duration,pitch,velocity): " << rejectedOwnerMidi <<
                                " Keep its safe notes and rhythmic identity where possible; "
                                "change the explicitly conflicting or unsupported pitches.";
                        reportWindowFailure(attempt);
                        if (improved) {
                            bestRejectedRisk = risk;
                            bestRejectedFeedback = feedback;
                        } else if (bestRejectedFeedback.isNotEmpty()) {
                            // The next AI revision starts from the best
                            // measured candidate, not from a worse retry.
                            feedback = bestRejectedFeedback;
                        }
                        continue;
                    }
                    score = std::move(candidate);
                    cacheValidatedResponse(body, response);
                    if (attempt > 1 && originalWindowRequest.isNotEmpty()) {
                        // A focused revision may have taken several paid AI
                        // responses. Cache the validated *combined* authored
                        // passage under the initial whole-group request so a
                        // replay can reuse the exact MIDI without re-enacting
                        // the unsuccessful attempts. It is validated again by
                        // the normal parser and ensemble audit on every hit.
                        auto* combined = new juce::DynamicObject();
                        for (const auto owner : stageOwners) {
                            juce::Array<juce::var> notes;
                            for (const auto& note : rendered.notes) {
                                if (note.partId != owner + 1 ||
                                    note.startBeat < absoluteStart - .001 ||
                                    note.startBeat >= absoluteEnd - .001) continue;
                                auto* item = new juce::DynamicObject();
                                item->setProperty("beat", note.startBeat - absoluteStart);
                                item->setProperty("duration", note.durationBeats);
                                item->setProperty("pitch", note.pitch);
                                item->setProperty("velocity", note.velocity);
                                notes.add(juce::var(item));
                            }
                            combined->setProperty(juce::String::fromUTF8(
                                result.instruments[owner].id.c_str()), notes);
                        }
                        auto* outputText = new juce::DynamicObject();
                        outputText->setProperty("type", "output_text");
                        outputText->setProperty("text", juce::JSON::toString(
                            juce::var(combined), false));
                        juce::Array<juce::var> content;
                        content.add(juce::var(outputText));
                        auto* message = new juce::DynamicObject();
                        message->setProperty("content", content);
                        juce::Array<juce::var> outputItems;
                        outputItems.add(juce::var(message));
                        auto* envelope = new juce::DynamicObject();
                        envelope->setProperty("id", "pulso-local-validated-" +
                            juce::Uuid().toString());
                        envelope->setProperty("status", "completed");
                        envelope->setProperty("output", outputItems);
                        HttpResponse assembled;
                        assembled.body = juce::JSON::toString(juce::var(envelope), false);
                        assembled.connected = true;
                        assembled.status = 200;
                        cacheValidatedResponse(originalWindowRequest, assembled);
                    }
                    accepted = true;
                    OperationalJournal::write("OK", "JOINT_WINDOW",
                        "accepted ensemble window group " +
                        juce::String(static_cast<int>(groupIndex + 1)) + "/" +
                        juce::String(static_cast<int>(writingGroups.size())) +
                        " | rendered window notes=" +
                        juce::String(windowNotes));
                }
                if (!accepted) {
                    // The long-window writer can be musically sound except for
                    // a handful of exact vertical collisions. Rewriting a
                    // whole 16-bar owner repeatedly is both expensive and
                    // likely to move the clash elsewhere. Ask the AI for only
                    // those authored pitches; every other MIDI field stays
                    // byte-for-byte identical and the full score is audited.
                    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                        aiStarted + totalAiBudget - std::chrono::steady_clock::now());
                    if (!bestRejectedScore.empty() && bestRejectedRisk <= 80 &&
                        remaining > std::chrono::seconds(45) && !token.stop_requested()) {
                        auto bestPlan = result;
                        bestPlan.performanceScore = bestRejectedScore;
                        GenerationContext foundation;
                        foundation.role = Role::Ensemble;
                        foundation.rootPitchClass = result.rootPitchClass;
                        foundation.scale = result.scale;
                        foundation.beatsPerBar = result.beatsPerBar;
                        foundation.seed = result.seed;
                        foundation.humanize = 0.0;
                        CompositionRenderReport before;
                        const auto bestMidi = SongComposer{}.render(
                            bestPlan, foundation, {}, &before);
                        const auto targets = directPitchTargets(bestRejectedScore,
                            result, before.finalTonalPass.after, stageOwners,
                            sectionIndex, firstBar, bars);
                        if (!targets.empty() && targets.size() <= 8) {
                            juce::String request;
                            request << "You are the composer making a microscopic MIDI "
                                "voicing revision. Keep every accepted note, instrument, "
                                "attack, duration, velocity, rhythm, rest, motif and chord "
                                "unchanged. Only choose a new PITCH for each exact target "
                                "below. These are simultaneous non-functional clashes "
                                "against the actual ensemble. Preserve the target's "
                                "melodic role and register while resolving them. Return "
                                "exactly one patch per target_id; no new or deleted notes. "
                                "A pitch revision is unsafe if it forms a minor second, "
                                "tritone, or major seventh against another note sounding "
                                "for at least half a beat. Check all sounding parts, "
                                "including bass and previously accepted notes. "
                                "Return structured JSON only.\nKey=" <<
                                juce::String::fromUTF8(result.key.c_str()) <<
                                "\nSection=" << juce::String::fromUTF8(section.name.c_str()) <<
                                "\nChord events (absolute beats):\n";
                            for (const auto& event : section.harmonicEvents)
                                request << (section.startBar + event.barOffset) *
                                    result.beatsPerBar + event.beatOffset << " " <<
                                    juce::String::fromUTF8(event.chordId.c_str()) << "\n";
                            for (const auto& chord : result.chordPalette) {
                                request << "CHORD " << juce::String::fromUTF8(chord.id.c_str()) <<
                                    " " << juce::String::fromUTF8(chord.label.c_str()) <<
                                    " pitch_classes=";
                                for (const auto pitchClass : chord.pitchClasses)
                                    request << pitchClass << ",";
                                request << "\n";
                            }
                            for (const auto& target : targets) {
                                const auto& source = bestRejectedScore.cells;
                                const auto cell = std::find_if(source.begin(), source.end(),
                                    [&](const auto& item) { return item.id == target.cellId; });
                                const auto& note = cell->notes[target.noteIndex];
                                request << "TARGET " << juce::String::fromUTF8(target.id.c_str()) <<
                                    " owner=" << juce::String::fromUTF8(
                                        result.instruments[target.ownerIndex].id.c_str()) <<
                                    " beat=" << juce::String(target.absoluteBeat, 2) <<
                                    " duration=" << juce::String(note.durationBeats, 2) <<
                                    " pitch=" << target.originalPitch <<
                                    " preferred_register=" <<
                                    result.instruments[target.ownerIndex].minimumPitch << ".." <<
                                    result.instruments[target.ownerIndex].maximumPitch <<
                                    "\nSOUNDING MIDI nearby (part,beat,duration,pitch):\n";
                                for (const auto& sounding : bestMidi.notes)
                                    if (sounding.startBeat < target.absoluteBeat + 1.0 &&
                                        sounding.endBeat() > target.absoluteBeat - 1.0)
                                        request << sounding.partId << "," <<
                                            juce::String(sounding.startBeat, 2) << "," <<
                                            juce::String(sounding.durationBeats, 2) << "," <<
                                            sounding.pitch << "; ";
                                request << "\n";
                            }
                            static const juce::String patchSchema = R"json({
                              "type":"object","additionalProperties":false,
                              "required":["patches"],"properties":{"patches":{
                                "type":"array","maxItems":8,"items":{
                                  "type":"object","additionalProperties":false,
                                  "required":["target_id","pitch"],
                                  "properties":{"target_id":{"type":"string"},
                                                "pitch":{"type":"integer"}}}}}}
                            )json";
                            juce::String revisionFeedback;
                            for (auto microAttempt = 1; microAttempt <= 2 && !accepted &&
                                 !token.stop_requested(); ++microAttempt) {
                            const auto microRemaining = std::chrono::duration_cast<
                                std::chrono::seconds>(aiStarted + totalAiBudget -
                                    std::chrono::steady_clock::now());
                            if (microRemaining <= std::chrono::seconds(20)) break;
                            const auto body = juce::String("{\"model\":\"") + model +
                                "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                                "low" +
                                "\"},\"max_output_tokens\":2400,\"input\":" +
                                juce::JSON::toString(juce::var(request + revisionFeedback)) +
                                ",\"text\":{\"format\":{\"type\":\"json_schema\","
                                "\"name\":\"pulso_direct_pitch_revision\",\"strict\":true,\"schema\":" +
                                patchSchema + "}}}";
                            if (progress) progress({AiSongStage::Recovery,
                                completedWindows, windowCount,
                                static_cast<int>(maximumWindowAttempts + microAttempt),
                                "AI reviewing only the remaining conflicting pitches"});
                            const auto response = performRequest(body, apiKey, token,
                                std::min<std::chrono::seconds>(microRemaining,
                                    std::chrono::seconds(55)));
                            auto revised = bestRejectedScore;
                            const auto output = response.connected && response.status == 200 ?
                                extractOutputText(juce::JSON::parse(response.body)) : juce::String{};
                            if (output.isEmpty() || !applyDirectPitchRevision(
                                    output, targets, result, revised)) {
                                revisionFeedback = "\nThe previous patch was missing, invalid, or "
                                    "outside the allowed register. Return exactly one valid "
                                    "changed pitch for every target_id.\n";
                                OperationalJournal::write("WARN", "JOINT_WINDOW",
                                    "AI pitch revision invalid or unavailable | attempt=" +
                                    juce::String(microAttempt));
                                continue;
                            }
                            {
                                auto revisedPlan = result;
                                revisedPlan.performanceScore = revised;
                                CompositionRenderReport after;
                                const auto revisedMidi = SongComposer{}.render(
                                    revisedPlan, foundation, {}, &after);
                                auto remainingClashes = 0;
                                auto remainingChromatic = 0;
                                auto remainingNonChord = 0;
                                for (const auto& issue : after.finalTonalPass.after.issues) {
                                    if (issue.beat < absoluteStart - .001 ||
                                        issue.beat >= absoluteEnd - .001) continue;
                                    if (issue.kind == "harsh_overlap" &&
                                        issue.overlapBeats >= .5 - .001)
                                        ++remainingClashes;
                                    else if (issue.kind == "unsupported_chromatic")
                                        ++remainingChromatic;
                                    else if (issue.kind == "strong_non_chord")
                                        ++remainingNonChord;
                                }
                                if (EditorialSafety::technicallySafeAiScore(
                                        revisedMidi, after) && remainingClashes == 0 &&
                                    remainingChromatic <= 1 &&
                                    remainingNonChord <= std::max(4, bars / 4)) {
                                    score = std::move(revised);
                                    accepted = true;
                                    OperationalJournal::write("OK", "JOINT_WINDOW",
                                        "accepted AI-authored note-level revision | targets=" +
                                        juce::String(static_cast<int>(targets.size())));
                                    // The exact combined passage is cached under
                                    // the original request, like a normal focused
                                    // window revision. Future replays pay nothing.
                                    auto* combined = new juce::DynamicObject();
                                    for (const auto owner : stageOwners) {
                                        juce::Array<juce::var> notes;
                                        for (const auto& note : revisedMidi.notes) {
                                            if (note.partId != owner + 1 ||
                                                note.startBeat < absoluteStart - .001 ||
                                                note.startBeat >= absoluteEnd - .001) continue;
                                            auto* item = new juce::DynamicObject();
                                            item->setProperty("beat", note.startBeat - absoluteStart);
                                            item->setProperty("duration", note.durationBeats);
                                            item->setProperty("pitch", note.pitch);
                                            item->setProperty("velocity", note.velocity);
                                            notes.add(juce::var(item));
                                        }
                                        combined->setProperty(juce::String::fromUTF8(
                                            result.instruments[owner].id.c_str()), notes);
                                    }
                                    auto* outputText = new juce::DynamicObject();
                                    outputText->setProperty("type", "output_text");
                                    outputText->setProperty("text", juce::JSON::toString(
                                        juce::var(combined), false));
                                    juce::Array<juce::var> content;
                                    content.add(juce::var(outputText));
                                    auto* message = new juce::DynamicObject();
                                    message->setProperty("content", content);
                                    juce::Array<juce::var> outputItems;
                                    outputItems.add(juce::var(message));
                                    auto* envelope = new juce::DynamicObject();
                                    envelope->setProperty("id", "pulso-local-validated-" +
                                        juce::Uuid().toString());
                                    envelope->setProperty("status", "completed");
                                    envelope->setProperty("output", outputItems);
                                    HttpResponse assembled;
                                    assembled.body = juce::JSON::toString(
                                        juce::var(envelope), false);
                                    assembled.connected = true;
                                    assembled.status = 200;
                                    cacheValidatedResponse(originalWindowRequest, assembled);
                                } else {
                                    revisionFeedback = "\nThe previous pitch patch still failed "
                                        "the full MIDI audit. Choose DIFFERENT pitches for "
                                        "the same target IDs. Remaining issues:\n";
                                    for (const auto& issue : after.finalTonalPass.after.issues) {
                                        if (issue.beat < absoluteStart - .001 ||
                                            issue.beat >= absoluteEnd - .001) continue;
                                        if (issue.kind != "harsh_overlap" ||
                                            issue.overlapBeats < .5 - .001) continue;
                                        revisionFeedback << "beat=" << juce::String(issue.beat, 2) <<
                                            " part=" << issue.partId << " pitch=" << issue.pitch <<
                                            " versus part=" << issue.otherPartId <<
                                            " pitch=" << issue.otherPitch << " overlap=" <<
                                            juce::String(issue.overlapBeats, 2) << "\n";
                                    }
                                    OperationalJournal::write("WARN", "JOINT_WINDOW",
                                        "AI pitch revision rejected by full audit | attempt=" +
                                        juce::String(microAttempt) + " clashes=" +
                                        juce::String(remainingClashes) + " chromatic=" +
                                        juce::String(remainingChromatic) + " nonchord=" +
                                        juce::String(remainingNonChord));
                                }
                            }
                            }
                        }
                    }
                }
                if (!accepted) {
                    if (!bestRejectedScore.empty() && checkpoint) {
                        auto rejectedSnapshot = result;
                        rejectedSnapshot.performanceScore =
                            std::move(bestRejectedScore);
                        checkpoint(rejectedSnapshot, completedWindows + 1, true, true);
                    }
                    error = "Joint AI score could not validate window " +
                        juce::String(static_cast<int>(completedWindows + 1)) + "/" +
                        juce::String(static_cast<int>(windowCount)) + " group " +
                        juce::String(static_cast<int>(groupIndex + 1)) + "/" +
                        juce::String(static_cast<int>(writingGroups.size())) + ": " + feedback;
                    return {};
                }
                }
                ++completedWindows;
                if (checkpoint) {
                    auto snapshot = result;
                    snapshot.performanceScore = score;
                    checkpoint(snapshot, completedWindows, true, false);
                }
            }
        }
        // One optional AI editorial pass on the final expressive window. It
        // cannot invalidate a technically accepted composition: only an
        // objectively safer, more conclusive authored replacement is kept.
        // Earlier MIDI, chord bed and bass are immutable; there is no local
        // algorithm inventing the ending on the model's behalf.
        if (!expressiveVoices.empty() && !result.sections.empty()) {
            result.performanceScore = score;
            GenerationContext foundation;
            foundation.role = Role::Ensemble;
            foundation.rootPitchClass = result.rootPitchClass;
            foundation.scale = result.scale;
            foundation.beatsPerBar = result.beatsPerBar;
            foundation.seed = result.seed;
            foundation.humanize = 0.0;
            CompositionRenderReport before;
            (void) SongComposer{}.render(result, foundation, {}, &before);
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                aiStarted + totalAiBudget - std::chrono::steady_clock::now());
            if ((!before.narrative.narrativeSpineReady ||
                 before.narrative.resolutionScore < .68) &&
                remaining > std::chrono::seconds(65)) {
                const auto sectionIndex = result.sections.size() - 1;
                const auto& lastSection = result.sections.back();
                const auto firstBar = ((lastSection.bars - 1) / barsPerWindow) * barsPerWindow;
                const auto bars = lastSection.bars - firstBar;
                const auto localStart = firstBar * result.beatsPerBar;
                const auto absoluteStart = (lastSection.startBar + firstBar) *
                    result.beatsPerBar;
                const auto absoluteEnd = absoluteStart + bars * result.beatsPerBar;
                std::vector<std::size_t> revisionOwners;
                for (const auto owner : expressiveVoices) {
                    const auto& instrument = result.instruments[owner];
                    // The motion lane is part of the accepted rhythmic and
                    // harmonic floor. Rewriting it in a narrative coda review
                    // can introduce new vertical clashes without improving
                    // the protagonist's ending. Keep it as audible context.
                    if (instrument.sourceVoice == VoiceId::HarmonicPulse)
                        continue;
                    if (instrument.activeSections.empty() ||
                        std::find(instrument.activeSections.begin(),
                                  instrument.activeSections.end(), lastSection.name) !=
                            instrument.activeSections.end())
                        revisionOwners.push_back(owner);
                }
                auto base = score;
                std::set<std::string> removed;
                for (const auto& cell : base.cells) {
                    const auto revisedOwner = std::any_of(
                        cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                            return std::any_of(revisionOwners.begin(), revisionOwners.end(),
                                [&](const auto owner) {
                                    return note.instrumentId == result.instruments[owner].id;
                                });
                        });
                    if (!revisedOwner) continue;
                    for (const auto& placement : base.placements)
                        if (placement.cellId == cell.id &&
                            placement.sectionIndex == static_cast<int>(sectionIndex) &&
                            std::abs(placement.startBeat - localStart) < .001)
                            removed.insert(cell.id);
                }
                if (!removed.empty()) {
                    base.cells.erase(std::remove_if(base.cells.begin(), base.cells.end(),
                        [&](const auto& cell) { return removed.contains(cell.id); }),
                        base.cells.end());
                    base.placements.erase(std::remove_if(base.placements.begin(),
                        base.placements.end(), [&](const auto& placement) {
                            return removed.contains(placement.cellId);
                        }), base.placements.end());
                    auto basePlan = result;
                    basePlan.performanceScore = base;
                    const auto immutable = SongComposer{}.render(basePlan, foundation);
                    auto editorialPrompt = jointWindowPrompt(direction, result, immutable,
                        revisionOwners, sectionIndex, firstBar, bars, {}, false);
                    editorialPrompt << "\nFINAL EDITORIAL REVIEW: The complete current MIDI "
                        "is technically safe but its measured musical resolution is "
                        << juce::String(before.narrative.resolutionScore, 2)
                        << ", motif closure " << juce::String(before.narrative.motifClosure, 2)
                        << ", density release " << juce::String(before.narrative.densityRelease, 2)
                        << ", register release " << juce::String(before.narrative.registerRelease, 2)
                        << ", complete protagonist ending " <<
                            (before.narrative.narrativeSpineReady ? "yes" : "no")
                        << ". Rewrite only the listed final expressive parts so their "
                        "motif, answers, textural withdrawals and final arrival have a "
                        "clear audible consequence. The protagonist must finish a "
                        "connected phrase, not a two-note tag, and land on the promised "
                        "stable final harmony. Honour the original musical direction "
                        "and preserve every accepted bass, chord and earlier note exactly. "
                        "A rest can be more expressive than another decorative note.\n";
                    const auto body = juce::String("{\"model\":\"") + model +
                        "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                        realizationReasoningEffort +
                        "\"},\"max_output_tokens\":14000,\"input\":" +
                        juce::JSON::toString(juce::var(editorialPrompt)) +
                        ",\"text\":{\"format\":{\"type\":\"json_schema\","
                        "\"name\":\"pulso_direct_window\",\"strict\":true,\"schema\":" +
                        directWindowSchema(result, revisionOwners) + "}}}";
                    if (progress) progress({AiSongStage::Recovery, windowCount,
                        windowCount, 1, "AI reviewing the final narrative passage"});
                    const auto response = performRequest(body, apiKey, token,
                        std::min<std::chrono::seconds>(remaining, std::chrono::seconds(125)));
                    PerformanceScore replacement;
                    juce::String editorialError;
                    const auto output = response.connected && response.status == 200 ?
                        extractOutputText(juce::JSON::parse(response.body)) : juce::String{};
                    if (output.isNotEmpty() && parseDirectWindow(output, result,
                            revisionOwners, sectionIndex, firstBar, bars,
                            replacement, editorialError)) {
                        auto revised = base;
                        mergePerformanceBlock(revised, std::move(replacement), serial++);
                        const auto normalized = PerformanceScoreEngine::normalize(revised,
                            result.sections.size(), sectionLengths, true);
                        if (normalized.cellsRejected == 0 &&
                            normalized.placementsRejected == 0) {
                            auto candidate = result;
                            candidate.performanceScore = revised;
                            CompositionRenderReport after;
                            const auto performance = SongComposer{}.render(
                                candidate, foundation, {}, &after);
                            const auto harmonicSafe =
                                after.finalTonalPass.after.unintendedHarshOverlaps <=
                                    before.finalTonalPass.after.unintendedHarshOverlaps &&
                                after.finalTonalPass.after.unsupportedChromaticNotes <=
                                    before.finalTonalPass.after.unsupportedChromaticNotes;
                            const auto narrativeImproved =
                                (!before.narrative.narrativeSpineReady &&
                                 after.narrative.narrativeSpineReady &&
                                 after.narrative.resolutionScore + .02 >=
                                     before.narrative.resolutionScore) ||
                                after.narrative.resolutionScore >=
                                    before.narrative.resolutionScore + .035;
                            if (EditorialSafety::technicallySafeAiScore(performance, after) &&
                                harmonicSafe && narrativeImproved &&
                                after.narrative.motifClosure + .05 >=
                                    before.narrative.motifClosure) {
                                score = std::move(revised);
                                cacheValidatedResponse(body, response);
                                OperationalJournal::write("OK", "AI_EDITORIAL",
                                    "final AI-authored narrative revision accepted | resolution " +
                                    juce::String(before.narrative.resolutionScore, 2) +
                                    " -> " + juce::String(after.narrative.resolutionScore, 2));
                                if (checkpoint) {
                                    candidate.performanceScore = score;
                                    checkpoint(candidate, windowCount + 1, true, false);
                                }
                            } else {
                                OperationalJournal::write("WARN", "AI_EDITORIAL",
                                    "final revision retained original | technical=" +
                                    juce::String(EditorialSafety::technicallySafeAiScore(
                                        performance, after) ? 1 : 0) +
                                    " harmonic_safe=" + juce::String(harmonicSafe ? 1 : 0) +
                                    " spine " +
                                    juce::String(before.narrative.narrativeSpineReady ? 1 : 0) +
                                    "->" +
                                    juce::String(after.narrative.narrativeSpineReady ? 1 : 0) +
                                    " resolution " +
                                    juce::String(before.narrative.resolutionScore, 3) +
                                    "->" + juce::String(after.narrative.resolutionScore, 3) +
                                    " motif " + juce::String(before.narrative.motifClosure, 3) +
                                    "->" + juce::String(after.narrative.motifClosure, 3) +
                                    "; original AI notes retained");
                                if (checkpoint) checkpoint(candidate, windowCount + 1,
                                    true, true);
                            }
                        }
                    }
                }
            }
        }
        result.performanceScore = std::move(score);
        error.clear();
        return result;
    }

    // Phase two authors the concrete MIDI in bounded, independently retryable blocks.
    // A completed block is merged immediately, so a later transport failure never
    // discards already validated musical work.
    const auto authoredLaneOwners = static_cast<std::size_t>(std::count_if(
        result.instruments.begin(), result.instruments.end(), [&](const auto& instrument) {
            return !ElectronicCompositionFabric::rendererOwnedDestination(result, instrument);
        }));
    const auto rendererDestinations = result.instruments.size() - authoredLaneOwners;
    OperationalJournal::write("INFO", "WRITING",
        "content architecture | " +
        juce::String(static_cast<int>(authoredLaneOwners)) +
        " authored lane owner(s) | " +
        juce::String(static_cast<int>(rendererDestinations)) +
        " renderer-owned timbral destination(s) | " +
        juce::String(static_cast<int>(result.instruments.size())) + " exported tracks");
    auto blocks = performanceWritingBlocks(result, localEditorial);
    if (blocks.empty()) {
        error = "AI blueprint contains no instrument cast";
        return {};
    }

    PerformanceScore assembledScore;
    auto requestSerial = std::size_t{};
    auto completedBlocks = std::size_t{};
    std::map<std::string, std::size_t> localBlockRewrites;
    auto localVoicingPatches = std::size_t{};
    auto rejectedCandidateCaptured = false;
    std::function<bool(const PerformanceScore&)> localCheckpointAudit;
    const auto saveAcceptedCheckpoint = [&](bool provisional) {
        if (!checkpoint || assembledScore.empty()) return;
        if (localEditorial && localCheckpointAudit &&
            !localCheckpointAudit(assembledScore)) return;
        if (localEditorial) {
            const auto bed = SelectiveRepair::centralChordBedOwner(result);
            if (bed) {
                const auto& bedId = result.instruments[*bed].id;
                const auto authoredBed = std::any_of(assembledScore.cells.begin(),
                    assembledScore.cells.end(), [&](const auto& cell) {
                        return std::any_of(cell.notes.begin(), cell.notes.end(),
                            [&](const auto& note) { return note.instrumentId == bedId; });
                    });
                if (authoredBed && !SelectiveRepair::chordBedFormCoverage(
                        result, assembledScore, *bed).ready())
                    return;
            }
        }
        auto snapshot = result;
        snapshot.performanceScore = assembledScore;
        checkpoint(snapshot, completedBlocks + 1, provisional, false);
    };
    const auto saveRejectedCandidate = [&](const PerformanceScore& score) {
        if (!checkpoint || score.empty()) return;
        auto snapshot = result;
        snapshot.performanceScore = score;
        checkpoint(snapshot, completedBlocks + 1, true, true);
        rejectedCandidateCaptured = true;
    };
    std::set<std::string> retiredInstrumentIds;
    std::set<std::string> deferredConstraintInstrumentIds;
    std::set<std::string> reportedMarginalBarAcceptances;
    juce::String lastBlockError;
    const auto overallDeadline = aiStarted + totalAiBudget;
    const auto tonalPolicy = tonalPolicyForDirection(direction.toStdString());
    const auto normalizeAssembledScore = [&](PerformanceScore& score,
                                             const juce::String& context) {
        const auto cellsBefore = score.cells.size();
        const auto placementsBefore = score.placements.size();
        std::vector<double> sectionLengths;
        sectionLengths.reserve(result.sections.size());
        for (const auto& section : result.sections)
            sectionLengths.push_back(section.bars * result.beatsPerBar);
        const auto report = PerformanceScoreEngine::normalize(
            score, result.sections.size(), sectionLengths);
        OperationalJournal::write(
            report.cellsDroppedByCapacity == 0 && report.placementsDroppedByCapacity == 0
                ? "INFO" : "ERROR",
            "NORMALIZATION",
            context + " | cells " + juce::String(static_cast<int>(cellsBefore)) + " -> " +
                juce::String(static_cast<int>(score.cells.size())) +
                " | placements " + juce::String(static_cast<int>(placementsBefore)) + " -> " +
                juce::String(static_cast<int>(score.placements.size())) +
                " | invalid cells=" + juce::String(static_cast<int>(report.cellsRejected -
                    report.cellsDroppedByCapacity)) +
                " placements=" + juce::String(static_cast<int>(report.placementsRejected -
                    report.placementsDroppedByCapacity)) +
                " | capacity cells=" + juce::String(static_cast<int>(report.cellsDroppedByCapacity)) +
                " placements=" + juce::String(static_cast<int>(report.placementsDroppedByCapacity)));
        return report.cellsDroppedByCapacity == 0 &&
            report.placementsDroppedByCapacity == 0;
    };
    const auto auditLocalScore = [&](const PerformanceScore& score) {
        auto snapshot = result;
        snapshot.performanceScore = score;
        GenerationContext foundation;
        foundation.role = Role::Ensemble;
        foundation.rootPitchClass = result.rootPitchClass;
        foundation.scale = result.scale;
        foundation.beatsPerBar = result.beatsPerBar;
        foundation.seed = result.seed;
        foundation.humanize = 0.0;
        CompositionRenderReport report;
        [[maybe_unused]] const auto rendered = SongComposer{}.render(
            snapshot, foundation, {}, &report);
        return report;
    };
    if (localEditorial)
        localCheckpointAudit = [&](const PerformanceScore& score) {
            auto snapshot = result;
            snapshot.performanceScore = score;
            GenerationContext foundation;
            foundation.role = Role::Ensemble;
            foundation.rootPitchClass = result.rootPitchClass;
            foundation.scale = result.scale;
            foundation.beatsPerBar = result.beatsPerBar;
            foundation.seed = result.seed;
            foundation.humanize = 0.0;
            CompositionRenderReport audit;
            const auto rendered = SongComposer{}.render(snapshot, foundation, {}, &audit);
            return EditorialSafety::technicallySafeAiScore(rendered, audit);
        };
    const auto reportMarginalBarAcceptances = [&](const PerformanceScore& score,
                                                   const std::vector<std::size_t>& candidates,
                                                   const juce::String& context) {
        std::vector<std::size_t> newlyAccepted;
        for (const auto& acceptance :
             SelectiveRepair::marginalBarAcceptances(result, score, candidates)) {
            if (reportedMarginalBarAcceptances.insert(acceptance.instrumentId).second)
                newlyAccepted.push_back(acceptance.instrumentIndex);
        }
        if (newlyAccepted.empty()) return;
        OperationalJournal::write("WARN", "VALIDATION",
            context + " accepted one-bar coverage margin: " +
            marginalBarAcceptanceBrief(result, score, newlyAccepted));
    };
    const auto essentialInstrument = [&](std::size_t index) {
        if (index >= result.instruments.size()) return false;
        const auto& instrument = result.instruments[index];
        return instrument.id == result.narrativeSpine.protagonistInstrumentId ||
            (ElectronicRoleContract::requiresMotionOwner(result) &&
             ElectronicRoleContract::motionOwner(instrument));
    };
    const auto containsEssential = [&](const std::vector<std::size_t>& indices) {
        return std::any_of(indices.begin(), indices.end(), essentialInstrument);
    };
    const auto isProtagonistOnly = [&](const std::vector<std::size_t>& indices) {
        return indices.size() == 1 && indices.front() < result.instruments.size() &&
            result.instruments[indices.front()].id ==
                result.narrativeSpine.protagonistInstrumentId;
    };
    const auto containsExplicitIdentity = [&](const std::vector<std::size_t>& indices) {
        return std::any_of(indices.begin(), indices.end(), [&](const auto index) {
            return index < result.instruments.size() &&
                result.instruments[index].explicitPromptIdentity;
        });
    };
    const auto deferConstraints = [&](const std::vector<std::size_t>& indices,
                                      const juce::String& reason,
                                      std::size_t displayBlock) {
        for (const auto index : indices)
            if (index < result.instruments.size())
                deferredConstraintInstrumentIds.insert(result.instruments[index].id);
        OperationalJournal::write("WARN", "CONSTRAINT",
            "block " + juce::String(static_cast<int>(displayBlock + 1)) +
            " deferred unresolved objectives; remaining blocks will continue | reason=" + reason +
            " | descriptors=" + performanceConstraintBrief(
                result, assembledScore, indices, requestedCastCount > 0));
    };

    const auto recoverGranular = [&](const std::vector<std::size_t>& pending,
                                     int recoveryAttempt,
                                     std::size_t displayBlock) {
        std::vector<std::size_t> unresolved;
        const auto pendingDeficits = SelectiveRepair::performanceDeficits(
            result, assembledScore, pending);
        const auto focusedSourceRewrite = std::any_of(
            pendingDeficits.begin(), pendingDeficits.end(),
            [](const auto& finding) { return finding.missingAuthoredDevelopment; });
        const auto shardWidth = focusedSourceRewrite ? std::size_t{1} : instrumentsPerRepairShard;
        for (std::size_t batchBegin = 0; batchBegin < pending.size();
             batchBegin += shardWidth * maximumConcurrentRepairShards) {
            const auto batchEnd = std::min(pending.size(), batchBegin +
                shardWidth * maximumConcurrentRepairShards);
            struct PendingRequest {
                std::vector<std::size_t> indices;
                std::set<std::string> replacementIds;
                std::size_t serial{};
                std::future<HttpResponse> response;
            };
            std::vector<PendingRequest> requests;
            for (auto begin = batchBegin; begin < batchEnd; begin += shardWidth) {
                const auto end = std::min(batchEnd, begin + shardWidth);
                std::vector<std::size_t> shard(pending.begin() + static_cast<std::ptrdiff_t>(begin),
                                               pending.begin() + static_cast<std::ptrdiff_t>(end));
                std::set<std::string> replacementIds;
                for (const auto& deficit :
                     SelectiveRepair::performanceDeficits(result, assembledScore, shard))
                    if (SelectiveRepair::requiresReplacement(deficit))
                        replacementIds.insert(deficit.instrumentId);
                std::vector<std::string> emptyInstrumentIds;
                for (const auto& deficit :
                     SelectiveRepair::performanceDeficits(result, assembledScore, shard))
                    if (deficit.notes == 0)
                        emptyInstrumentIds.push_back(deficit.instrumentId);
                const auto serial = requestSerial++;
                auto shardPrompt = performanceBlockPrompt(
                    direction, outputText, result, assembledScore, shard, serial,
                    recoveryAttempt, localEditorial) +
                    "\nGENERIC CONSTRAINT RECOVERY: return only these unresolved instruments. Preserve every accepted "
                    "cell elsewhere and perform only the listed operations. supply_missing_identity creates concrete MIDI "
                    "for an empty planned lane; extend_coverage adds placements in missing active regions; develop_phrase "
                    "authors separated musical statements; resolve_narrative adds or transforms material inside the "
                    "blueprint's resolution window and ends on a stable terminal-harmony pitch; "
                    "develop_sectional_evolution replaces literal repetition with the measured three-to-six phrase states "
                    "distributed across setup, development, climax and return; "
                    "When source_notes is below minimum, replace the target's repeated tiny cell with the measured "
                    "number of genuinely authored notes across distinct phrases; duplicate or unplaced cells do not count. "
                    "develop_narrative_presence writes distinct protagonist statements into the measured missing " +
                    juce::String(result.compositionBehavior == CompositionBehavior::Hypnotic ?
                        "sixteen-bar states" : "eight-bar windows") +
                    " without continuous filler; transform_thematic_returns keeps the recognisable nucleus but "
                    "replaces dominant literal copies through contour, onset, fragmentation or cadential consequence; "
                    "shape_melodic_speech combines singable stepwise connection with characteristic leaps, rests and "
                    "held consequences; avoid both disconnected interval roulette and continuous scalar walking; "
                    "author_central_chord_bed replaces the target with one self-contained polyphonic MIDI performance: "
                    "three-to-five distinct simultaneous pitches at meaningful harmonic changes, smooth voice leading, "
                    "inversions and sectional revoicing across premise, development, climax and the absolute final stage; "
                    "never distribute the required chord across other tracks; "
                    "shape_harmonic_breath preserves that bed's identity but writes at least one audible two-bar "
                    "withdrawal before a consequential return, coordinated with independent pads that retain tonal memory; "
                    "separate_independent_line rewrites the target so it no longer shares exact MIDI pitch and attack "
                    "with the named counterpart, while preserving harmony and its declared role; "
                    "establish_thematic_relationship uses the protagonist theme_id while retaining an independent contour. "
                    "Do not add density unrelated to the measured evidence. A musical_objective improves the score but is "
                    "not permission to redesign it. The following descriptors are authoritative:\n" +
                    performanceConstraintBrief(result, assembledScore, shard, requestedCastCount > 0);
                if (localEditorial)
                    shardPrompt += "\nACTUAL MIDI REALIZATION (not just source-cell counts): " +
                        realizationBrief(result, assembledScore, shard) +
                        "\nIf source notes exist but realizable is zero, repair the placement fragments, "
                        "section-relative starts or voice mapping with your own valid cells. "
                        "Do not claim coverage from unrendered notes.\n";
                if (localEditorial) {
                    const auto bed = SelectiveRepair::centralChordBedOwner(result);
                    if (bed && std::find(shard.begin(), shard.end(), *bed) != shard.end()) {
                        const auto coverage = SelectiveRepair::chordBedFormCoverage(
                            result, assembledScore, *bed);
                        shardPrompt += "\nPRIMARY CHORD-BED FORM REPAIR: use additive cells "
                            "for missing structural thirds unless a measured replacement operation "
                            "is explicitly listed below. Supply polyphonic chord attacks "
                            "(at least three simultaneous pitches). Opening=" +
                            juce::String(static_cast<int>(coverage.opening)) +
                            " development=" +
                            juce::String(static_cast<int>(coverage.development)) +
                            " closing=" +
                            juce::String(static_cast<int>(coverage.closing)) +
                            ". Sustain tonal memory but allow deliberate breaths; "
                            "the primary bed must not vanish after the introduction. "
                            "Place notes in valid section-relative coordinates, coordinate "
                            "against the immutable protagonist and bass, and return no other part.\n";
                    }
                }
                const auto counterpartReference = independenceReferenceBrief(
                    result, assembledScore, shard);
                if (counterpartReference.isNotEmpty())
                    shardPrompt += "\nCOUNTERPART MIDI REFERENCE (absolute beat, exact pitch, duration):\n" +
                        counterpartReference +
                        "Write complementary material around these events; do not copy them.\n";
                if (!replacementIds.empty()) {
                    shardPrompt += "\nSURGICAL REPLACEMENT MODE: the following instrument material will be replaced "
                        "transactionally because adding notes cannot correct cloned counterpoint, literal sectional "
                        "repetition or missing phrase boundaries: ";
                    for (const auto& id : replacementIds)
                        shardPrompt << juce::String::fromUTF8(id.c_str()) << " ";
                    shardPrompt += "\nReturn a complete performance for each replacement identity, preserving its role, "
                        "register, tonal function and relationship to the form. Do not reuse another instrument's exact "
                        "onset and MIDI-pitch sequence. Shared chord tones and coordinated cadences remain valid. "
                        "Author section-specific variants for premise, development, climax and "
                        "return, and at least the measured phrase minimum "
                        "as musically complete statements separated by literal gaps of at least three quarters of a bar. "
                        "Use rests, withdrawal and re-entry; do not cover the entire timeline with touching or overlapping notes. "
                        "Other listed identities remain additive repairs. No other instrument may appear.";
                }
                if (!emptyInstrumentIds.empty()) {
                    shardPrompt += "\nZERO-EVENT RECOVERY: each following planned identity currently has no accepted MIDI and "
                        "must receive at least one concrete note inside a cell plus at least one valid placement: ";
                    for (const auto& id : emptyInstrumentIds)
                        shardPrompt << juce::String::fromUTF8(id.c_str()) << " ";
                    shardPrompt += "\nUse the exact instrument_id shown above. Do not answer with rhythm_motifs, prose, "
                        "controls alone, another track's ID or an unplaced cell.";
                }
                const auto cacheKey = "pulso-performance-" + juce::String::toHexString(
                    static_cast<juce::int64>(seed));
                const auto shardBody = juce::String("{\"model\":\"") + model +
                    "\",\"background\":true,\"reasoning\":{\"effort\":\"" + realizationReasoningEffort +
                    "\"},\"max_output_tokens\":" +
                    juce::String(localEditorial ? 10000 : 7000) +
                    ",\"prompt_cache_key\":" +
                    juce::JSON::toString(juce::var(cacheKey)) + ",\"input\":" +
                    juce::JSON::toString(juce::var(shardPrompt)) +
                    ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_performance_block\","
                    "\"strict\":true,\"schema\":" + performanceSchemaFor(result, shard) + "}}}";
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    overallDeadline - std::chrono::steady_clock::now());
                if (remaining < std::chrono::seconds(30)) {
                    unresolved.insert(unresolved.end(), shard.begin(), shard.end());
                    continue;
                }
                const auto budget = std::min(remaining,
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(90)));
                requests.push_back({std::move(shard), std::move(replacementIds), serial,
                    std::async(std::launch::async, [&, shardBody, budget] {
                        return performRequest(shardBody, apiKey, token, budget);
                    })});
            }
            for (auto& request : requests) {
                auto response = request.response.get();
                const auto responseText = extractOutputText(juce::JSON::parse(response.body));
                PerformanceScore shardScore;
                juce::String shardError;
                const auto assignedIds = instrumentIdsFor(result, request.indices);
                PerformanceRoutingReport routing;
                const auto valid = response.connected && response.status >= 200 && response.status < 300 &&
                    !response.cancelled && !response.timedOut && !responseText.isEmpty() &&
                    parsePerformanceBlock(responseText, result, assignedIds,
                                          shardScore, shardError, &routing);
                OperationalJournal::write(valid ? "INFO" : "WARN", "ROUTING",
                    "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                    " recovery routing: " + routing.summary(assignedIds));
                if (!valid) {
                    unresolved.insert(unresolved.end(), request.indices.begin(), request.indices.end());
                    const auto failure = incompleteStructuredResponse(response.body)
                        ? structuredResponseError(response.body,
                            "OpenAI recovery was truncated before complete structured output") :
                        shardError.isNotEmpty() ? shardError :
                        responseText.isEmpty() && response.connected &&
                                response.status >= 200 && response.status < 300
                            ? juce::String("OpenAI returned an empty recovery response")
                            : apiErrorMessage(response);
                    lastBlockError = "Recovery for " +
                        (assignedIds.empty() ? juce::String("unknown instrument") :
                            juce::String::fromUTF8(assignedIds.begin()->c_str())) +
                        " returned no usable notes: " + failure;
                    OperationalJournal::write("WARN", "RECOVERY", lastBlockError +
                        " | measured deficit=" + performanceDeficitBrief(
                            result, assembledScore, request.indices));
                    continue;
                }
                retainOnlyInstrumentMaterial(shardScore, assignedIds);
                auto candidateScore = assembledScore;
                if (!request.replacementIds.empty())
                    eraseInstrumentMaterial(candidateScore, request.replacementIds);
                mergePerformanceBlock(candidateScore, std::move(shardScore), request.serial);
                if (!normalizeAssembledScore(candidateScore,
                        "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                        " recovery merge")) {
                    unresolved.insert(unresolved.end(), request.indices.begin(), request.indices.end());
                    lastBlockError = "Global performance capacity was exceeded during recovery; accepted score preserved";
                    continue;
                }
                if (localEditorial) {
                    const auto candidateAudit = auditLocalScore(candidateScore);
                    if (!localCheckpointAudit || !localCheckpointAudit(candidateScore)) {
                        unresolved.insert(unresolved.end(), request.indices.begin(),
                                          request.indices.end());
                        saveRejectedCandidate(candidateScore);
                        lastBlockError = "AI recovery failed objective MIDI integrity; "
                            "the previous checkpoint was preserved";
                        OperationalJournal::write("WARN", "RECOVERY", lastBlockError);
                        continue;
                    }
                    if (SelectiveRepair::measuredTonalDebt(candidateAudit) >
                        SelectiveRepair::measuredTonalDebt(auditLocalScore(assembledScore)))
                        OperationalJournal::write("WARN", "RECOVERY",
                            "AI recovery adds contextual harmonic findings; authored MIDI "
                            "remains eligible for complete-score listening and selective revision");
                }
                const auto missing = uncoveredInstruments(result, candidateScore, request.indices,
                                                          localEditorial);
                const auto replacementStillMissing = std::any_of(missing.begin(), missing.end(),
                    [&](const auto index) {
                        return index < result.instruments.size() &&
                            request.replacementIds.contains(result.instruments[index].id);
                    });
                if (replacementStillMissing) {
                    unresolved.insert(unresolved.end(), request.indices.begin(), request.indices.end());
                    lastBlockError = "Surgical replacement did not satisfy its measured phrase contract";
                    OperationalJournal::write("WARN", "VALIDATION",
                        "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                        " rejected a non-convergent replacement; original target material preserved");
                    continue;
                }
                assembledScore = std::move(candidateScore);
                saveAcceptedCheckpoint(false);
                reportMarginalBarAcceptances(
                    assembledScore, request.indices,
                    "block " + juce::String(static_cast<int>(displayBlock + 1)) + " recovery");
                if (!request.replacementIds.empty())
                    OperationalJournal::write("OK", "CHECKPOINT",
                        "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                        " committed surgical replacement for " +
                        juce::String(static_cast<int>(request.replacementIds.size())) +
                        " instrument(s); unrelated material preserved");
                unresolved.insert(unresolved.end(), missing.begin(), missing.end());
                if (!missing.empty())
                    OperationalJournal::write("WARN", "VALIDATION",
                        "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                        " remaining deficits: " +
                        performanceDeficitBrief(result, assembledScore, missing));
                if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), recoveryAttempt,
                    "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                    " granular recovery " + juce::String(static_cast<int>(request.indices.size())) +
                    " part(s); " + juce::String(static_cast<int>(missing.size())) + " unresolved"});
            }
        }
        std::sort(unresolved.begin(), unresolved.end());
        unresolved.erase(std::unique(unresolved.begin(), unresolved.end()), unresolved.end());
        return unresolved;
    };

    std::function<bool(const std::vector<std::size_t>&, int, std::size_t)> authorBlock;
    authorBlock = [&](const std::vector<std::size_t>& indices, int attempt,
                      std::size_t displayBlock) -> bool {
        if (indices.empty()) return true;
        if (token.stop_requested()) {
            lastBlockError = "Generation cancelled";
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            overallDeadline - std::chrono::steady_clock::now());
        if (remaining < std::chrono::seconds(45)) {
            lastBlockError = "Incremental AI pipeline exhausted its total time budget";
            return false;
        }

        const auto stage = attempt == 1 ? AiSongStage::PerformanceBlock : AiSongStage::Recovery;
        if (progress) progress({stage, completedBlocks, blocks.size(), attempt,
            "instrument block " + juce::String(static_cast<int>(displayBlock + 1)) + "/" +
            juce::String(static_cast<int>(blocks.size())) + " - " +
            juce::String(static_cast<int>(indices.size())) + " parts"});

        const auto serial = requestSerial++;
        auto blockPrompt = performanceBlockPrompt(direction, outputText, result, assembledScore, indices,
                                                  serial, attempt, localEditorial);
        if (localEditorial) {
            const auto bed = SelectiveRepair::centralChordBedOwner(result);
            if (bed && std::find(indices.begin(), indices.end(), *bed) != indices.end())
                blockPrompt += "\nPRIMARY CHORD BED: author complete polyphonic chords with "
                    "at least three simultaneous pitches in the opening, development "
                    "and closing thirds of the whole song. These are structural "
                    "arrivals, not a demand for constant notes; leave intentional "
                    "breathing room. Never write only the introduction. Coordinate "
                    "each chord against the protagonist, bass and active harmony.\n";
        }
        const auto blockOutputTokens = localEditorial ? 10000 :
            isProtagonistOnly(indices) ? 9000 : 16000;
        const auto cacheKey = "pulso-performance-" + juce::String::toHexString(
            static_cast<juce::int64>(seed));
        const auto blockBody = juce::String("{\"model\":\"") + model +
            "\",\"background\":true,\"reasoning\":{\"effort\":\"" + realizationReasoningEffort +
            "\"},\"max_output_tokens\":" + juce::String(blockOutputTokens) +
            ",\"prompt_cache_key\":" +
            juce::JSON::toString(juce::var(cacheKey)) + ",\"input\":" +
            juce::JSON::toString(juce::var(blockPrompt)) +
            ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_performance_block\","
            "\"strict\":true,\"schema\":" + performanceSchemaFor(result, indices) + "}}}";
        const auto requestBudget = std::min(remaining,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(3)));
        const auto blockHttp = performRequest(blockBody, apiKey, token, requestBudget);
        if (!blockHttp.connected || blockHttp.status < 200 || blockHttp.status >= 300 ||
            blockHttp.cancelled || blockHttp.timedOut) {
            lastBlockError = apiErrorMessage(blockHttp);
            if (indices.size() > 1 && attempt < 3) {
                const auto middle = indices.begin() + static_cast<std::ptrdiff_t>(indices.size() / 2);
                const std::vector<std::size_t> left(indices.begin(), middle);
                const std::vector<std::size_t> right(middle, indices.end());
                return authorBlock(left, attempt + 1, displayBlock) &&
                       authorBlock(right, attempt + 1, displayBlock);
            }
            if (attempt < 3) return authorBlock(indices, attempt + 1, displayBlock);
            if (isProtagonistOnly(indices)) {
                lastBlockError = "OpenAI could not author the required protagonist before ensemble writing";
                return false;
            }
            if (containsEssential(indices) || containsExplicitIdentity(indices)) {
                lastBlockError = containsExplicitIdentity(indices)
                    ? "OpenAI could not complete a user-named instrument identity"
                    : "OpenAI could not author an essential protagonist or motion part";
                deferConstraints(indices, lastBlockError, displayBlock);
                return true;
            }
            for (const auto index : indices)
                if (index < result.instruments.size())
                    retiredInstrumentIds.insert(result.instruments[index].id);
            if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), attempt,
                "retiring an unavailable nominal part; preserving completed music"});
            return true;
        }

        const auto blockText = extractOutputText(juce::JSON::parse(blockHttp.body));
        PerformanceScore parsedScore;
        juce::String parseError;
        const auto assignedIds = instrumentIdsFor(result, indices);
        PerformanceRoutingReport routing;
        const auto parsedBlock = !blockText.isEmpty() && parsePerformanceBlock(
            blockText, result, assignedIds, parsedScore, parseError, &routing);
        OperationalJournal::write(parsedBlock ? "INFO" : "WARN", "ROUTING",
            "block " + juce::String(static_cast<int>(displayBlock + 1)) +
            " initial routing: " + routing.summary(assignedIds));
        if (!parsedBlock) {
            lastBlockError = incompleteStructuredResponse(blockHttp.body)
                ? structuredResponseError(blockHttp.body,
                    "OpenAI performance was truncated before complete structured output") :
                blockText.isEmpty()
                ? structuredResponseError(blockHttp.body, "OpenAI returned no performance block")
                : parseError;
            if (indices.size() > 1 && attempt < 3) {
                const auto middle = indices.begin() + static_cast<std::ptrdiff_t>(indices.size() / 2);
                const std::vector<std::size_t> left(indices.begin(), middle);
                const std::vector<std::size_t> right(middle, indices.end());
                return authorBlock(left, attempt + 1, displayBlock) &&
                       authorBlock(right, attempt + 1, displayBlock);
            }
            if (attempt < 3) return authorBlock(indices, attempt + 1, displayBlock);
            if (isProtagonistOnly(indices)) {
                lastBlockError = "OpenAI returned no valid protagonist performance before ensemble writing";
                return false;
            }
            if (containsEssential(indices) || containsExplicitIdentity(indices)) {
                lastBlockError = containsExplicitIdentity(indices)
                    ? "OpenAI returned invalid music for a user-named instrument identity"
                    : "OpenAI returned invalid music for an essential protagonist or motion part";
                deferConstraints(indices, lastBlockError, displayBlock);
                return true;
            }
            for (const auto index : indices)
                if (index < result.instruments.size())
                    retiredInstrumentIds.insert(result.instruments[index].id);
            if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), attempt,
                "retiring an invalid nominal part; preserving completed music"});
            return true;
        }

        const auto scoreBeforeBlock = localEditorial ? assembledScore : PerformanceScore{};
        mergePerformanceBlock(assembledScore, std::move(parsedScore), serial);
        if (!normalizeAssembledScore(assembledScore,
                "block " + juce::String(static_cast<int>(displayBlock + 1)) + " initial merge")) {
            lastBlockError = "Global performance capacity was exceeded while assembling authored blocks";
            return false;
        }
        if (localEditorial && isProtagonistOnly(indices)) {
            const auto detail = "protagonist source-to-MIDI audit: " +
                realizationBrief(result, assembledScore, indices);
            OperationalJournal::write("INFO", "REALIZATION", detail);
            if (progress) progress({AiSongStage::Validation, completedBlocks,
                blocks.size(), attempt, detail});
        }
        // AI-sovereign composition is evaluated as one score. Mid-block
        // surgery on raw overlap counts fragments the authored narrative and
        // can spend several full rewrites before the other voices even exist.
        if (localEditorial) {
            const auto beforeAudit = auditLocalScore(scoreBeforeBlock);
            auto afterAudit = auditLocalScore(assembledScore);
            const auto tonalDebt = SelectiveRepair::measuredTonalDebt;
            const auto introduced = tonalDebt(afterAudit) - tonalDebt(beforeAudit);
            const auto priorLowSeconds = SelectiveRepair::sustainedLowChordBedSeconds(
                result, beforeAudit.finalTonalPass.after).size();
            auto lowSeconds = SelectiveRepair::sustainedLowChordBedSeconds(
                result, afterAudit.finalTonalPass.after);
            const auto newLowSeconds = lowSeconds.size() > priorLowSeconds;
            if ((introduced > 0 || newLowSeconds) && !token.stop_requested()) {
                const auto culprits = localTonalCulprits(result, afterAudit);
                const auto selected = std::find_if(culprits.begin(), culprits.end(),
                    [&](const auto index) {
                        if (std::find(indices.begin(), indices.end(), index) == indices.end())
                            return false;
                        return !newLowSeconds || std::any_of(lowSeconds.begin(),
                            lowSeconds.end(), [&](const auto& issue) {
                                return issue.partId == index + 1;
                            });
                    });
                if (selected == culprits.end()) {
                    OperationalJournal::write("WARN", "EDITORIAL",
                        "new tonal conflicts had no attributable instrument in this block; "
                        "preserving authored MIDI for final audition");
                } else {
                const auto& selectedId = result.instruments[*selected].id;
                if (result.instruments[*selected].sourceVoice == VoiceId::HarmonicFoundation) {
                    OperationalJournal::write("WARN", "EDITORIAL",
                        "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                        " introduced " + juce::String(introduced) +
                        " tonal conflicts; requesting compact AI chord voicings, not a full-track rewrite");
                    auto unusableVoicingReplies = 0;
                    while (localVoicingPatches < 4 && !token.stop_requested() &&
                           (tonalDebt(afterAudit) - tonalDebt(beforeAudit) > 0 ||
                            lowSeconds.size() > priorLowSeconds)) {
                        auto snapshot = result;
                        snapshot.performanceScore = assembledScore;
                        GenerationContext context;
                        context.role = Role::Ensemble;
                        context.rootPitchClass = result.rootPitchClass;
                        context.scale = result.scale;
                        context.beatsPerBar = result.beatsPerBar;
                        context.seed = result.seed;
                        context.humanize = 0.0;
                        const auto rendered = SongComposer{}.render(snapshot, context);
                        const auto targetCount = localVoicingPatches == 0 ?
                            std::size_t{12} : std::size_t{10};
                        const auto targets = SelectiveRepair::chordVoicingTargets(
                            result, rendered, afterAudit.finalTonalPass.after,
                            *selected, targetCount);
                        if (targets.empty()) {
                            OperationalJournal::write("WARN", "EDITORIAL",
                                "no complete chord attacks match the measured conflicts");
                            break;
                        }
                        const auto remaining = std::chrono::duration_cast<
                            std::chrono::milliseconds>(overallDeadline -
                                std::chrono::steady_clock::now());
                        if (remaining < std::chrono::seconds(25)) break;
                        if (progress) progress({AiSongStage::Recovery,
                            completedBlocks, blocks.size(),
                            static_cast<int>(localVoicingPatches + 1),
                            "AI refining " + juce::String(static_cast<int>(targets.size())) +
                            " measured chord attacks; accepted MIDI preserved"});
                        const auto prompt = compactVoicingPatchPrompt(
                            result, rendered, afterAudit.finalTonalPass.after,
                            *selected, targets);
                        const auto body = juce::String("{\"model\":\"") + model +
                            "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                            realizationReasoningEffort +
                            "\"},\"max_output_tokens\":4000,\"input\":" +
                            juce::JSON::toString(juce::var(prompt)) +
                            ",\"text\":{\"format\":{\"type\":\"json_schema\"," +
                            "\"name\":\"pulso_compact_voicing_patch\",\"strict\":true,\"schema\":" +
                            compactVoicingPatchSchema() + "}}}";
                        ++localVoicingPatches;
                        const auto response = performRequest(body, apiKey, token,
                            std::min(remaining, std::chrono::duration_cast<
                                std::chrono::milliseconds>(std::chrono::seconds(75))));
                        std::vector<ChordVoicingPatch> patches;
                        juce::String patchError;
                        const auto text = extractOutputText(juce::JSON::parse(response.body));
                        if (!response.connected || response.status < 200 ||
                            response.status >= 300 || response.cancelled || response.timedOut ||
                            incompleteStructuredResponse(response.body) ||
                            !parseCompactVoicingPatches(text, targets, patches, patchError)) {
                            OperationalJournal::write("WARN", "EDITORIAL",
                                "compact voicing response unavailable or incomplete; "
                                "accepted MIDI unchanged");
                            if (++unusableVoicingReplies >= 2) break;
                            continue;
                        }
                        PerformanceScore candidate;
                        std::string spliceError;
                        if (!SelectiveRepair::applyChordVoicingPatches(
                                result, assembledScore, *selected, patches,
                                candidate, spliceError) ||
                            !normalizeAssembledScore(candidate, "compact AI voicing patch")) {
                            OperationalJournal::write("WARN", "EDITORIAL",
                                "compact voicing splice invalid: " +
                                juce::String::fromUTF8(spliceError.c_str()));
                            if (++unusableVoicingReplies >= 2) break;
                            continue;
                        }
                        auto candidateSnapshot = result;
                        candidateSnapshot.performanceScore = candidate;
                        const auto candidatePattern = SongComposer{}.render(
                            candidateSnapshot, context);
                        if (!SelectiveRepair::preservesUntouchedMidi(
                                result, rendered, candidatePattern, *selected, patches)) {
                            OperationalJournal::write("WARN", "EDITORIAL",
                                "compact voicing splice changed MIDI outside requested chords; "
                                "candidate discarded");
                            if (++unusableVoicingReplies >= 2) break;
                            continue;
                        }
                        const auto candidateAudit = auditLocalScore(candidate);
                        const auto candidateLowSeconds =
                            SelectiveRepair::sustainedLowChordBedSeconds(
                                result, candidateAudit.finalTonalPass.after);
                        if (tonalDebt(candidateAudit) < tonalDebt(afterAudit) &&
                            candidateLowSeconds.size() <= lowSeconds.size() &&
                            candidateAudit.production.unsupportedChromaticNotes <=
                                afterAudit.production.unsupportedChromaticNotes) {
                            assembledScore = std::move(candidate);
                            afterAudit = candidateAudit;
                            lowSeconds = candidateLowSeconds;
                            unusableVoicingReplies = 0;
                            OperationalJournal::write("OK", "EDITORIAL",
                                "compact AI voicing patch improved the complete MIDI | " +
                                juce::String(static_cast<int>(patches.size())) +
                                " chord attacks edited | remaining new tonal events=" +
                                juce::String(tonalDebt(afterAudit) - tonalDebt(beforeAudit)));
                        } else {
                            OperationalJournal::write("WARN", "EDITORIAL",
                                "compact voicing patch did not improve the complete MIDI; "
                                "accepted notes preserved");
                            if (++unusableVoicingReplies >= 2) break;
                        }
                    }
                } else if (!aiSovereign && localBlockRewrites[selectedId] < 2) {
                    const std::vector<std::size_t> rewriteIndices{*selected};
                    const auto rewriteIds = instrumentIdsFor(result, rewriteIndices);
                    OperationalJournal::write("WARN", "EDITORIAL",
                    "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                    " introduced " + juce::String(introduced) +
                    " tonal conflicts; asking AI to revise only " +
                    juce::String::fromUTF8(result.instruments[*selected].id.c_str()));
                auto feedback = performanceBlockPrompt(direction, outputText, result,
                    assembledScore, rewriteIndices, requestSerial, 2, true);
                feedback += localTonalConflictBrief(result, afterAudit, rewriteIds) +
                    "REWRITE ONLY THE SELECTED INSTRUMENT. Preserve every other instrument, "
                    "repair the measured collisions through your own note, chord and release choices, "
                    "and return a complete performance for this one instrument. A sustained low second "
                    "inside one chord is not repaired by selecting individually diatonic pitches: "
                    "revoice the simultaneous chord and verify the bass register against the immutable "
                    "harmonic ledger. Keep its unaffected "
                    "motifs and passages recognisable; do not solve pair conflicts by merely adding notes.\n";
                const auto body = juce::String("{\"model\":\"") + model +
                    "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                    realizationReasoningEffort +
                    "\"},\"max_output_tokens\":6500,\"input\":" +
                    juce::JSON::toString(juce::var(feedback)) +
                    ",\"text\":{\"format\":{\"type\":\"json_schema\","
                    "\"name\":\"pulso_performance_block\",\"strict\":true,\"schema\":" +
                    performanceSchemaFor(result, rewriteIndices) + "}}}";
                const auto budget = std::min(std::chrono::duration_cast<std::chrono::milliseconds>(
                    overallDeadline - std::chrono::steady_clock::now()),
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::seconds(90)));
                if (budget >= std::chrono::seconds(30)) {
                    ++localBlockRewrites[selectedId];
                    const auto response = performRequest(body, apiKey, token, budget);
                    PerformanceScore replacement;
                    juce::String rewriteError;
                    const auto text = extractOutputText(juce::JSON::parse(response.body));
                    if (response.connected && response.status >= 200 && response.status < 300 &&
                        !response.cancelled && !response.timedOut &&
                        !incompleteStructuredResponse(response.body) &&
                        parsePerformanceBlock(text, result, rewriteIds,
                            replacement, rewriteError)) {
                        retainOnlyInstrumentMaterial(replacement, rewriteIds);
                        auto candidate = assembledScore;
                        eraseInstrumentMaterial(candidate, rewriteIds);
                        mergePerformanceBlock(candidate, std::move(replacement), requestSerial++);
                        if (normalizeAssembledScore(candidate, "local block-only rewrite")) {
                            const auto candidateAudit = auditLocalScore(candidate);
                            const auto candidateLowSeconds =
                                SelectiveRepair::sustainedLowChordBedSeconds(
                                    result, candidateAudit.finalTonalPass.after).size();
                            GenerationContext preservationContext;
                            preservationContext.role = Role::Ensemble;
                            preservationContext.rootPitchClass = result.rootPitchClass;
                            preservationContext.scale = result.scale;
                            preservationContext.beatsPerBar = result.beatsPerBar;
                            preservationContext.seed = result.seed;
                            preservationContext.humanize = 0.0;
                            auto originalSnapshot = result;
                            originalSnapshot.performanceScore = assembledScore;
                            auto candidateSnapshot = result;
                            candidateSnapshot.performanceScore = candidate;
                            const auto untouchedPreserved = localUntouchedPassagesPreserved(
                                result,
                                SongComposer{}.render(originalSnapshot, preservationContext),
                                SongComposer{}.render(candidateSnapshot, preservationContext),
                                afterAudit.finalTonalPass.after, rewriteIds);
                            if (uncoveredInstruments(result, candidate, rewriteIndices,
                                                     localEditorial).empty() &&
                                untouchedPreserved &&
                                candidateLowSeconds <= lowSeconds.size() &&
                                tonalDebt(candidateAudit) < tonalDebt(afterAudit) &&
                                candidateAudit.production.unsupportedChromaticNotes <=
                                    afterAudit.production.unsupportedChromaticNotes) {
                                assembledScore = std::move(candidate);
                                afterAudit = candidateAudit;
                                lowSeconds = SelectiveRepair::sustainedLowChordBedSeconds(
                                    result, afterAudit.finalTonalPass.after);
                                OperationalJournal::write("OK", "EDITORIAL",
                                    "AI one-instrument rewrite improved tonal integrity; every other MIDI lane preserved");
                            }
                        }
                    } else {
                        OperationalJournal::write("WARN", "EDITORIAL",
                            "block-only rewrite unavailable; retaining original AI block for final audition");
                    }
                } else if (aiSovereign) {
                    OperationalJournal::write("INFO", "EDITORIAL",
                        "sovereign block retained for complete-score tonal review; "
                        "no speculative mid-composition rewrite requested");
                }
                }
                }
            }
            const auto remainingIntroduced = tonalDebt(afterAudit) - tonalDebt(beforeAudit);
            if (lowSeconds.size() > priorLowSeconds || remainingIntroduced > 0) {
                if (localCheckpointAudit && localCheckpointAudit(assembledScore)) {
                    OperationalJournal::write("WARN", "EDITORIAL",
                        "block retains " + juce::String(remainingIntroduced) +
                        " contextual tonal findings after bounded AI revision; "
                        "technically valid authored MIDI kept for full-song listening and targeted review");
                    if (progress) progress({AiSongStage::Validation, completedBlocks,
                        blocks.size(), attempt,
                        "AI block retained with musical observations; exact MIDI checkpoint preserved"});
                    saveAcceptedCheckpoint(true);
                } else {
                const auto technicalEvents =
                    afterAudit.production.metricViolations +
                    afterAudit.production.unsafeDurations +
                    afterAudit.production.orphanEvents;
                if (technicalEvents > 0) {
                    lastBlockError = "AI block failed MIDI integrity after normalization: off-grid=" +
                        juce::String(static_cast<int>(afterAudit.production.metricViolations)) +
                        " unsafe_durations=" +
                        juce::String(static_cast<int>(afterAudit.production.unsafeDurations)) +
                        " orphan_events=" +
                        juce::String(static_cast<int>(afterAudit.production.orphanEvents));
                } else if (lowSeconds.size() > priorLowSeconds) {
                    const auto& issue = lowSeconds.front();
                    lastBlockError = "AI chord-bed block retained a sustained internal voicing clash "
                        "after bounded revision: bar " +
                        juce::String(static_cast<int>(std::floor(
                            issue.beat / std::max(1.0, result.beatsPerBar))) + 1) +
                        " pitches " + juce::String(issue.otherPitch) + "/" +
                        juce::String(issue.pitch);
                } else {
                    lastBlockError = "AI block retained " + juce::String(remainingIntroduced) +
                        " new measured tonal conflicts after bounded revision";
                }
                lastBlockError += "; preserving the previous checkpoint instead of "
                    "spending on later blocks";
                OperationalJournal::write("ERROR", "EDITORIAL", lastBlockError);
                if (progress) progress({AiSongStage::Validation, completedBlocks,
                    blocks.size(), attempt, lastBlockError +
                        localTonalConflictBrief(result, afterAudit,
                            instrumentIdsFor(result, indices)).substring(0, 1800)});
                saveRejectedCandidate(assembledScore);
                assembledScore = scoreBeforeBlock;
                saveAcceptedCheckpoint(true);
                return false;
                }
            }
        }
        saveAcceptedCheckpoint(true);
        reportMarginalBarAcceptances(
            assembledScore, indices,
            "block " + juce::String(static_cast<int>(displayBlock + 1)) + " initial");
        // A complete protagonist can satisfy every narrative requirement yet omit
        // only its absolute-boundary placement. Repair that debt from its own
        // connected AI-authored phrase before spending another remote call. The
        // transaction is independently rendered and audited; failure restores the
        // original score and falls through to normal bounded recovery.
        if (isProtagonistOnly(indices)) {
            const auto findings = SelectiveRepair::performanceDeficits(
                result, assembledScore, indices);
            const auto missingOnlyTerminalPlacement = std::any_of(
                findings.begin(), findings.end(), [&](const auto& finding) {
                    return finding.instrumentId ==
                               result.narrativeSpine.protagonistInstrumentId &&
                        finding.notes > 0 && finding.missingCodaResolution;
                });
            if (!aiSovereign && missingOnlyTerminalPlacement &&
                SelectiveRepair::ensureAuthoredProtagonistCoda(
                    result, assembledScore)) {
                if (!normalizeAssembledScore(assembledScore,
                        "protagonist-first transactional coda")) {
                    lastBlockError =
                        "Global performance capacity was exceeded while placing the authored protagonist coda";
                    return false;
                }
                OperationalJournal::write("OK", "CHECKPOINT",
                    "protagonist-first block received a verified absolute-boundary coda from its own connected AI phrase; no remote rewrite or procedural notes used");
            }
        }
        auto missing = uncoveredInstruments(result, assembledScore, indices,
                                             localEditorial);
        // A complete central chord bed may be missing only its final dramatic
        // stage. Rewriting the whole lane is expensive and frequently omits the
        // very coda it was asked to add. Ask the AI for one bounded closing phrase
        // instead, merge it transactionally, and keep every accepted chord intact.
        // This remains sovereign authorship: the engine supplies no pitches,
        // voicings, durations or attacks.
        if (aiSovereign && localEditorial) {
            const auto bed = SelectiveRepair::centralChordBedOwner(result);
            const auto bedMissing = bed &&
                std::find(missing.begin(), missing.end(), *bed) != missing.end();
            const auto findings = bedMissing
                ? SelectiveRepair::performanceDeficits(result, assembledScore, {*bed})
                : std::vector<PerformanceCoverageDeficit>{};
            const auto closureOnly = findings.size() == 1 &&
                findings.front().notes >= findings.front().minimumNotes &&
                findings.front().polyphonicChordAttacks >=
                    findings.front().minimumPolyphonicChordAttacks &&
                !findings.front().missingCentralChordBed &&
                !findings.front().missingAuthoredDevelopment &&
                findings.front().missingChordBedNarrativeArc;
            if (closureOnly) {
                if (progress) progress({AiSongStage::Recovery, completedBlocks,
                    blocks.size(), 1, "AI composing the missing chord-bed coda"});
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    overallDeadline - std::chrono::steady_clock::now());
                juce::String focusedError;
                if (remaining >= std::chrono::seconds(25)) {
                    const auto bedIndices = std::vector<std::size_t>{*bed};
                    const auto body = juce::String("{\"model\":\"") + model +
                        "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                        realizationReasoningEffort +
                        "\"},\"max_output_tokens\":5000,\"input\":" +
                        juce::JSON::toString(juce::var(sovereignChordBedClosurePrompt(
                            result, assembledScore, *bed))) +
                        ",\"text\":{\"format\":{\"type\":\"json_schema\","
                        "\"name\":\"pulso_chord_bed_closure\",\"strict\":true,\"schema\":" +
                        performanceSchemaFor(result, bedIndices) + "}}}";
                    const auto response = performRequest(body, apiKey, token,
                        std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::seconds(75))));
                    const auto responseText = extractOutputText(juce::JSON::parse(response.body));
                    PerformanceScore addition;
                    juce::String parseError;
                    const auto ownerIds = instrumentIdsFor(result, bedIndices);
                    const auto parsed = response.connected && response.status >= 200 &&
                        response.status < 300 && !response.cancelled && !response.timedOut &&
                        !incompleteStructuredResponse(response.body) &&
                        parsePerformanceBlock(responseText, result, ownerIds,
                            addition, parseError);
                    if (parsed) {
                        retainOnlyInstrumentMaterial(addition, ownerIds);
                        auto candidate = assembledScore;
                        mergePerformanceBlock(candidate, std::move(addition), requestSerial++);
                        if (normalizeAssembledScore(candidate,
                                "AI-authored chord-bed coda") &&
                            SelectiveRepair::chordBedFormCoverage(
                                result, candidate, *bed).ready() &&
                            localCheckpointAudit && localCheckpointAudit(candidate)) {
                            assembledScore = std::move(candidate);
                            saveAcceptedCheckpoint(false);
                            missing = uncoveredInstruments(
                                result, assembledScore, indices, localEditorial);
                            OperationalJournal::write("OK", "RECOVERY",
                                "focused AI chord-bed coda accepted without rewriting earlier harmony");
                        } else {
                            focusedError = "focused chord-bed coda did not improve the complete MIDI";
                        }
                    } else {
                        focusedError = parseError.isNotEmpty() ? parseError :
                            apiErrorMessage(response);
                    }
                } else {
                    focusedError = "no time remained for a focused chord-bed coda";
                }
                if (focusedError.isNotEmpty())
                    OperationalJournal::write("WARN", "RECOVERY",
                        "focused AI chord-bed coda retained the prior checkpoint: " + focusedError);
            }
        }
        if (localEditorial && isProtagonistOnly(indices) && missing.empty()) {
            const auto findings = SelectiveRepair::performanceDeficits(
                result, assembledScore, indices);
            if (findings.size() == 1 &&
                SelectiveRepair::deferableLocalProtagonistEditorial(
                    result, findings.front())) {
                deferConstraints(indices,
                    "authored protagonist retained; narrative presence and melodic speech await complete-score audition",
                    displayBlock);
            }
        }
        if (!missing.empty()) {
            lastBlockError = juce::String(static_cast<int>(missing.size())) +
                " instruments were underwritten in block " +
                juce::String(static_cast<int>(displayBlock + 1));
            OperationalJournal::write("WARN", "VALIDATION",
                "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                " initial deficits: " + performanceDeficitBrief(result, assembledScore, missing) +
                (localEditorial ? " | realization: " +
                    realizationBrief(result, assembledScore, missing) : juce::String()));
            // A populated protagonist with only a modest active-bar shortage does
            // not need another full performance-block rewrite. Request a few new
            // phrases in its genuinely empty bars, once, and keep the accepted
            // material untouched. If transport fails, the completed ensemble and
            // independent audible gate will decide whether this is a real defect.
            if (isProtagonistOnly(indices)) {
                const auto findings = SelectiveRepair::performanceDeficits(
                    result, assembledScore, indices);
                if (aiSovereign && findings.size() == 1 &&
                    findings.front().missingCodaResolution &&
                    findings.front().notes >= findings.front().minimumNotes &&
                    !findings.front().missingAuthoredDevelopment &&
                    !findings.front().missingNarrativePresence) {
                    if (progress) progress({AiSongStage::Recovery, completedBlocks,
                        blocks.size(), 1, "AI composing a focused protagonist coda"});
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        overallDeadline - std::chrono::steady_clock::now());
                    if (remaining < std::chrono::seconds(25)) {
                        lastBlockError = "No time remained for an AI-authored protagonist coda";
                        return false;
                    }
                    const auto body = juce::String("{\"model\":\"") + model +
                        "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                        realizationReasoningEffort +
                        "\"},\"max_output_tokens\":4500,\"input\":" +
                        juce::JSON::toString(juce::var(sovereignCodaPrompt(
                            result, assembledScore, indices.front()))) +
                        ",\"text\":{\"format\":{\"type\":\"json_schema\","
                        "\"name\":\"pulso_performance_block\",\"strict\":true,\"schema\":" +
                        performanceSchemaFor(result, indices) + "}}}";
                    const auto response = performRequest(body, apiKey, token,
                        std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::seconds(90))));
                    const auto responseText = extractOutputText(juce::JSON::parse(response.body));
                    PerformanceScore addition;
                    juce::String parseError;
                    const auto ownerIds = instrumentIdsFor(result, indices);
                    const auto parsed = response.connected && response.status >= 200 &&
                        response.status < 300 && !response.cancelled && !response.timedOut &&
                        !incompleteStructuredResponse(response.body) &&
                        parsePerformanceBlock(responseText, result, ownerIds,
                            addition, parseError);
                    if (parsed) {
                        retainOnlyInstrumentMaterial(addition, ownerIds);
                        auto candidate = assembledScore;
                        mergePerformanceBlock(candidate, std::move(addition), requestSerial++);
                        if (normalizeAssembledScore(candidate, "AI-authored focused coda")) {
                            const auto remaining = uncoveredInstruments(
                                result, candidate, indices, localEditorial);
                            const auto remainingFindings = SelectiveRepair::performanceDeficits(
                                result, candidate, indices);
                            const auto codaStillMissing = std::any_of(
                                remainingFindings.begin(), remainingFindings.end(),
                                [](const auto& finding) { return finding.missingCodaResolution; });
                            if (remaining.empty() ||
                                (localEditorial && !codaStillMissing &&
                                 localCheckpointAudit && localCheckpointAudit(candidate))) {
                                assembledScore = std::move(candidate);
                                saveAcceptedCheckpoint(false);
                                if (!remaining.empty())
                                    deferConstraints(remaining,
                                        "focused coda is audible; residual phrase observations await complete-score audition",
                                        displayBlock);
                                OperationalJournal::write("OK", "RECOVERY",
                                    "focused AI coda accepted without discarding earlier protagonist MIDI");
                                return true;
                            }
                        }
                        parseError = "Focused AI coda did not pass complete protagonist validation";
                    }
                    lastBlockError = "Focused AI coda failed: " +
                        (parseError.isNotEmpty() ? parseError :
                         incompleteStructuredResponse(response.body)
                            ? structuredResponseError(response.body,
                                "OpenAI coda output was incomplete")
                            : apiErrorMessage(response));
                    OperationalJournal::write("WARN", "RECOVERY", lastBlockError);
                    if (localEditorial && localCheckpointAudit &&
                        localCheckpointAudit(assembledScore)) {
                        deferConstraints(indices,
                            "populated protagonist preserved after non-convergent coda; final audit must flag the ending",
                            displayBlock);
                        saveAcceptedCheckpoint(true);
                        return true;
                    }
                    return false;
                }
                if (findings.size() == 1 &&
                    SelectiveRepair::deferableMarginalMelodicSpeech(result, findings.front())) {
                    // One missing step in an otherwise complete phrase is not a reason to
                    // rewrite 112 accepted notes. Ask for one bounded pitch edit once.
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        overallDeadline - std::chrono::steady_clock::now());
                    auto pitchAccepted = false;
                    if (remaining >= std::chrono::seconds(15)) {
                        const auto body = juce::String("{\"model\":\"") + model +
                            "\",\"background\":true,\"reasoning\":{\"effort\":\"low\"},"
                            "\"max_output_tokens\":1200,\"input\":" +
                            juce::JSON::toString(juce::var(marginalSpeechPitchPrompt(
                                result, assembledScore, findings.front()))) +
                            ",\"text\":{\"format\":{\"type\":\"json_schema\","
                            "\"name\":\"pulso_marginal_pitch\",\"strict\":true,\"schema\":" +
                            marginalSpeechPitchSchema + "}}}";
                        const auto response = performRequest(body, apiKey, token,
                            std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::seconds(45))));
                        const auto edit = juce::JSON::parse(extractOutputText(
                            juce::JSON::parse(response.body)));
                        if (response.connected && response.status >= 200 &&
                            response.status < 300 && !response.cancelled && !response.timedOut &&
                            !incompleteStructuredResponse(response.body)) {
                            if (const auto* object = edit.getDynamicObject()) {
                                const auto cellId = object->getProperty("cell_id").toString().toStdString();
                                const auto noteIndex = static_cast<int>(object->getProperty("note_index"));
                                const auto newPitch = static_cast<int>(object->getProperty("pitch"));
                                auto candidate = assembledScore;
                                for (auto& cell : candidate.cells) {
                                    if (cell.id != cellId || noteIndex < 0 ||
                                        static_cast<std::size_t>(noteIndex) >= cell.notes.size()) continue;
                                    auto& note = cell.notes[static_cast<std::size_t>(noteIndex)];
                                    const auto& owner = result.instruments[indices.front()];
                                    if (note.instrumentId != owner.id ||
                                        std::abs(newPitch - note.pitch) > 2 ||
                                        newPitch == note.pitch || newPitch < owner.minimumPitch ||
                                        newPitch > owner.maximumPitch ||
                                        !isPitchClassInScale(newPitch % 12,
                                            result.rootPitchClass, result.scale)) break;
                                    note.pitch = newPitch;
                                    if (uncoveredInstruments(result, candidate, indices,
                                                             localEditorial).empty()) {
                                        assembledScore = std::move(candidate);
                                        saveAcceptedCheckpoint(false);
                                        pitchAccepted = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    OperationalJournal::write(pitchAccepted ? "OK" : "WARN", "RECOVERY",
                        pitchAccepted ? "single authored protagonist pitch edit passed full coverage validation" :
                        "one-interval melodic shortfall deferred to complete-score audit; no full-track rewrite");
                    if (!pitchAccepted)
                        deferConstraints(missing,
                            "one-interval melodic observation; complete-score audit retains authority",
                            displayBlock);
                    return true;
                }
                if (findings.size() == 1 &&
                    SelectiveRepair::deferableProtagonistCoverage(result, findings.front())) {
                    if (progress) progress({AiSongStage::Recovery, completedBlocks,
                        blocks.size(), attempt + 1,
                        "focused protagonist coverage repair; accepted phrases preserved"});
                    const auto beforeBars = renderedOwnerBars(
                        result, assembledScore, indices.front());
                    const auto focusedPrompt = focusedProtagonistCoveragePrompt(
                        direction, result, assembledScore, findings.front());
                    const auto focusedSerial = requestSerial++;
                    const auto focusedBody = juce::String("{\"model\":\"") + model +
                        "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                        realizationReasoningEffort +
                        "\"},\"max_output_tokens\":4500,\"input\":" +
                        juce::JSON::toString(juce::var(focusedPrompt)) +
                        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_performance_block\","
                        "\"strict\":true,\"schema\":" +
                        performanceSchemaFor(result, indices) + "}}}";
                    const auto focusedRemaining = std::chrono::duration_cast<
                        std::chrono::milliseconds>(overallDeadline -
                            std::chrono::steady_clock::now());
                    juce::String focusedFailure;
                    if (focusedRemaining >= std::chrono::seconds(30)) {
                        const auto focusedBudget = std::min(focusedRemaining,
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::seconds(90)));
                        const auto response = performRequest(
                            focusedBody, apiKey, token, focusedBudget);
                        const auto responseText = extractOutputText(
                            juce::JSON::parse(response.body));
                        PerformanceScore addition;
                        PerformanceRoutingReport focusedRouting;
                        const auto focusedIds = instrumentIdsFor(result, indices);
                        const auto parsed = response.connected && response.status >= 200 &&
                            response.status < 300 && !response.cancelled && !response.timedOut &&
                            !responseText.isEmpty() && parsePerformanceBlock(
                                responseText, result, focusedIds, addition,
                                focusedFailure, &focusedRouting);
                        OperationalJournal::write(parsed ? "INFO" : "WARN", "ROUTING",
                            "focused protagonist coverage routing: " + focusedRouting.summary(focusedIds));
                        if (parsed) {
                            retainOnlyInstrumentMaterial(addition, focusedIds);
                            const auto addedBars = renderedOwnerBars(
                                result, addition, indices.front());
                            auto hasOverlap = false;
                            for (std::size_t bar = 0; bar < addedBars.size(); ++bar)
                                if (addedBars[bar] && beforeBars[bar]) {
                                    hasOverlap = true;
                                    break;
                                }
                            // Evaluate the rendered additional material, not just
                            // its cell declarations. A malformed placement must
                            // never overwrite an already accepted protagonist bar.
                            if (hasOverlap) {
                                focusedFailure = "Focused addition occupied an accepted protagonist bar";
                            } else {
                                auto candidateScore = assembledScore;
                                mergePerformanceBlock(candidateScore, std::move(addition),
                                                      focusedSerial);
                                if (!normalizeAssembledScore(candidateScore,
                                        "focused protagonist coverage merge")) {
                                    focusedFailure = "Focused addition exceeded score capacity";
                                } else {
                                    const auto afterBars = renderedOwnerBars(
                                        result, candidateScore, indices.front());
                                    const auto oldCount = static_cast<std::size_t>(
                                        std::count(beforeBars.begin(), beforeBars.end(), true));
                                    const auto newCount = static_cast<std::size_t>(
                                        std::count(afterBars.begin(), afterBars.end(), true));
                                    auto originalBarsPreserved = true;
                                    for (std::size_t bar = 0; bar < beforeBars.size(); ++bar)
                                        if (beforeBars[bar] && !afterBars[bar]) {
                                            originalBarsPreserved = false;
                                            break;
                                        }
                                    const auto remainingFindings =
                                        SelectiveRepair::performanceDeficits(
                                            result, candidateScore, indices);
                                    const auto safeRemainder = remainingFindings.empty() ||
                                        (remainingFindings.size() == 1 &&
                                         SelectiveRepair::deferableProtagonistCoverage(
                                             result, remainingFindings.front()));
                                    if (newCount > oldCount && originalBarsPreserved &&
                                        safeRemainder) {
                                        assembledScore = std::move(candidateScore);
                                        saveAcceptedCheckpoint(false);
                                        OperationalJournal::write("OK", "CHECKPOINT",
                                            "focused protagonist coverage added " +
                                            juce::String(static_cast<int>(newCount - oldCount)) +
                                            " previously silent bars; accepted notes preserved");
                                    } else {
                                        focusedFailure = "Focused addition did not safely improve active-bar coverage";
                                    }
                                }
                            }
                        } else if (focusedFailure.isEmpty()) {
                            focusedFailure = responseText.isEmpty() && response.connected &&
                                response.status >= 200 && response.status < 300
                                ? juce::String("OpenAI returned an empty focused response")
                                : apiErrorMessage(response);
                        }
                    } else {
                        focusedFailure = "No time remained for focused recovery";
                    }
                    if (focusedFailure.isNotEmpty())
                        OperationalJournal::write("WARN", "RECOVERY",
                            "focused protagonist coverage did not converge: " + focusedFailure);
                    const auto remainder = uncoveredInstruments(
                        result, assembledScore, indices, localEditorial);
                    if (remainder.empty()) return true;
                    const auto residualFindings = SelectiveRepair::performanceDeficits(
                        result, assembledScore, indices);
                    if (residualFindings.size() == 1 &&
                        SelectiveRepair::deferableProtagonistCoverage(
                            result, residualFindings.front())) {
                        deferConstraints(remainder,
                            "substantial authored protagonist has only an optional coverage shortfall; complete-score audible gate retains final authority",
                            displayBlock);
                        return true;
                    }
                }
            }
            auto unresolved = missing;
            std::vector<std::size_t> boundedMusicalObservations;
            const auto bedOwner = SelectiveRepair::centralChordBedOwner(result);
            const auto bedBlock = localEditorial && bedOwner &&
                std::find(indices.begin(), indices.end(), *bedOwner) != indices.end();
            const auto lastRecoveryAttempt = bedBlock ? attempt + 1 : 3;
            for (auto recoveryAttempt = attempt + 1;
                 !unresolved.empty() && recoveryAttempt <= lastRecoveryAttempt;
                 ++recoveryAttempt) {
                unresolved = recoverGranular(unresolved, recoveryAttempt, displayBlock);
                // Independence, phrase-state evolution and coda findings receive one
                // focused rewrite with concrete evidence. Repeating the same expensive
                // request cannot improve its information and previously amplified one
                // false positive into many calls. Coverage/identity recovery retains
                // the existing bounded retries.
                if (recoveryAttempt == attempt + 1 && !unresolved.empty()) {
                    const auto constraints = SelectiveRepair::performanceConstraints(
                        result, assembledScore, unresolved, requestedCastCount > 0);
                    std::set<std::size_t> singleAttemptTargets;
                    for (const auto& constraint : constraints)
                        if (constraint.evidence.duplicatedIndependentLine ||
                            constraint.evidence.missingAuthoredDevelopment ||
                            constraint.evidence.missingSectionalEvolution ||
                            constraint.evidence.missingCodaResolution)
                            singleAttemptTargets.insert(constraint.evidence.instrumentIndex);
                    for (const auto index : unresolved)
                        if (singleAttemptTargets.contains(index))
                            boundedMusicalObservations.push_back(index);
                    unresolved.erase(std::remove_if(unresolved.begin(), unresolved.end(),
                        [&](const auto index) { return singleAttemptTargets.contains(index); }),
                        unresolved.end());
                }
            }
            unresolved.insert(unresolved.end(), boundedMusicalObservations.begin(),
                              boundedMusicalObservations.end());
            std::sort(unresolved.begin(), unresolved.end());
            unresolved.erase(std::unique(unresolved.begin(), unresolved.end()), unresolved.end());
            if (localEditorial && isProtagonistOnly(indices) && !unresolved.empty()) {
                auto findings = SelectiveRepair::performanceDeficits(
                    result, assembledScore, indices);
                if (findings.size() == 1 && findings.front().notes > 0 &&
                    (findings.front().missingNarrativePresence ||
                     findings.front().missingCodaResolution)) {
                    // The first recovery can turn a silent cell into real MIDI while
                    // still leaving a sparse narrative or absent terminal answer.
                    // Preserve every accepted cell and ask only for missing phrases.
                    const auto requestAddition = [&](const juce::String& prompt,
                                                     int outputTokens, int seconds,
                                                     bool codaOnly) {
                        const auto before = SelectiveRepair::performanceDeficits(
                            result, assembledScore, indices);
                        if (before.size() != 1) return false;
                        const auto targetWindows = codaOnly
                            ? std::vector<int>{}
                            : focusedProtagonistWindowTargets(before.front());
                        if (!codaOnly && targetWindows.empty()) return false;
                        std::vector<double> sectionLengths;
                        sectionLengths.reserve(result.sections.size());
                        for (const auto& section : result.sections)
                            sectionLengths.push_back(section.bars * result.beatsPerBar);
                        const auto& ownerId = result.instruments[indices.front()].id;
                        const auto beforeRealization = PerformanceScoreEngine::auditRealization(
                            assembledScore, ownerId, result.instruments, sectionLengths);
                        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                            overallDeadline - std::chrono::steady_clock::now());
                        if (remaining < std::chrono::seconds(25)) return false;
                        const auto body = juce::String("{\"model\":\"") + model +
                            "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                            realizationReasoningEffort +
                            "\"},\"max_output_tokens\":" + juce::String(outputTokens) +
                            ",\"input\":" + juce::JSON::toString(juce::var(prompt)) +
                            ",\"text\":{\"format\":{\"type\":\"json_schema\","
                            "\"name\":\"pulso_performance_block\",\"strict\":true,\"schema\":" +
                            performanceSchemaFor(result, indices) + "}}}";
                        const auto response = performRequest(body, apiKey, token,
                            std::min(remaining,
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::seconds(seconds))));
                        PerformanceScore addition;
                        juce::String parseError;
                        const auto ownerIds = instrumentIdsFor(result, indices);
                        const auto responseText = extractOutputText(
                            juce::JSON::parse(response.body));
                        if (!response.connected || response.status < 200 ||
                            response.status >= 300 || response.cancelled ||
                            response.timedOut || incompleteStructuredResponse(response.body) ||
                            !parsePerformanceBlock(responseText, result, ownerIds,
                                                   addition, parseError)) {
                            lastBlockError = "Focused protagonist completion unavailable: " +
                                (parseError.isNotEmpty() ? parseError :
                                    apiErrorMessage(response));
                            OperationalJournal::write("WARN", "RECOVERY", lastBlockError);
                            return false;
                        }
                        retainOnlyInstrumentMaterial(addition, ownerIds);
                        if (!SelectiveRepair::focusedProtagonistAdditionInScope(
                                result, addition, indices.front(),
                                targetWindows, codaOnly)) {
                            lastBlockError = "Focused protagonist reply contained unplaced, clipped or out-of-window MIDI: " +
                                realizationBrief(result, addition, indices);
                            OperationalJournal::write("WARN", "RECOVERY", lastBlockError);
                            return false;
                        }
                        auto candidate = assembledScore;
                        mergePerformanceBlock(candidate, std::move(addition), requestSerial++);
                        if (!normalizeAssembledScore(candidate,
                                "focused AI protagonist completion")) return false;
                        const auto after = SelectiveRepair::performanceDeficits(
                            result, candidate, indices);
                        if (after.size() > 1) return false;
                        const auto complete = after.empty();
                        const auto afterRealization = PerformanceScoreEngine::auditRealization(
                            candidate, ownerId, result.instruments, sectionLengths);
                        if (!SelectiveRepair::acceptsFocusedProtagonistCompletion(
                                before.front(), complete ? nullptr : &after.front(),
                                targetWindows, codaOnly,
                                beforeRealization.realizableNotes,
                                afterRealization.realizableNotes)) {
                            saveRejectedCandidate(candidate);
                            lastBlockError = "Focused protagonist completion did not render a requested phrase or coda: " +
                                realizationBrief(result, candidate, indices);
                            OperationalJournal::write("WARN", "RECOVERY", lastBlockError);
                            return false;
                        }
                        assembledScore = std::move(candidate);
                        saveAcceptedCheckpoint(false);
                        OperationalJournal::write("OK", "CHECKPOINT",
                            "focused AI protagonist addition preserved accepted notes; "
                            "narrative_windows=" +
                            juce::String(static_cast<int>(complete
                                ? before.front().minimumNarrativePhraseWindows
                                : after.front().narrativePhraseWindows)) +
                            "/" + juce::String(static_cast<int>(
                                before.front().minimumNarrativePhraseWindows)) +
                            " coda_missing=" + juce::String(static_cast<int>(
                                !complete && after.front().missingCodaResolution)) +
                            " | " + realizationBrief(result, assembledScore, indices));
                        if (progress) progress({AiSongStage::Validation, completedBlocks,
                            blocks.size(), 0, "protagonist phrase-to-MIDI audit: " +
                                realizationBrief(result, assembledScore, indices)});
                        return true;
                    };
                    if (progress) progress({AiSongStage::Recovery, completedBlocks,
                        blocks.size(), 3,
                        "AI completing accepted protagonist phrases and resolution"});
                    const auto narrativeShortfall = findings.front().minimumNarrativePhraseWindows >
                            findings.front().narrativePhraseWindows
                        ? findings.front().minimumNarrativePhraseWindows -
                            findings.front().narrativePhraseWindows : std::size_t{};
                    const auto focusedAttemptBudget = std::min<std::size_t>(4,
                        (narrativeShortfall + 2) / 3);
                    for (auto narrativeAttempt = std::size_t{};
                         narrativeAttempt < focusedAttemptBudget; ++narrativeAttempt) {
                        findings = SelectiveRepair::performanceDeficits(
                            result, assembledScore, indices);
                        if (findings.empty() || !findings.front().missingNarrativePresence)
                            break;
                        if (!requestAddition(focusedProtagonistNarrativePrompt(direction, result,
                                assembledScore, findings.front()), 5000, 75, false))
                            break;
                    }
                    findings = SelectiveRepair::performanceDeficits(
                        result, assembledScore, indices);
                    if (findings.size() == 1 && findings.front().notes > 0 &&
                        findings.front().missingCodaResolution)
                        requestAddition(sovereignCodaPrompt(result, assembledScore,
                            indices.front()), 4500, 60, true);
                    unresolved = uncoveredInstruments(result, assembledScore,
                        indices, localEditorial);
                }
            }
            if (unresolved.empty()) return true;
            if (isProtagonistOnly(indices)) {
                lastBlockError = "OpenAI protagonist remains below the measured contract: " +
                    performanceDeficitBrief(result, assembledScore, unresolved) +
                    " | MIDI realization: " + realizationBrief(result, assembledScore, unresolved) +
                    " | last recovery: " + lastBlockError;
                if (!rejectedCandidateCaptured)
                    saveRejectedCandidate(assembledScore);
                return false;
            }
            const auto unresolvedConstraints = SelectiveRepair::performanceConstraints(
                result, assembledScore, unresolved, requestedCastCount > 0);
            std::vector<std::size_t> preservedObjectives;
            std::vector<std::size_t> emptyNominalParts;
            for (const auto& constraint : unresolvedConstraints) {
                if (constraint.blocksPublication || constraint.evidence.notes > 0)
                    preservedObjectives.push_back(constraint.evidence.instrumentIndex);
                else
                    emptyNominalParts.push_back(constraint.evidence.instrumentIndex);
            }
            if (!preservedObjectives.empty()) {
                lastBlockError = "bounded recovery left musical objectives for final global evaluation";
                deferConstraints(preservedObjectives, lastBlockError, displayBlock);
                if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), attempt,
                    "deferring " + juce::String(static_cast<int>(preservedObjectives.size())) +
                    " musical objective(s); continuing the complete score"});
            }
            // A numerical cast target does not make GPT's invented labels explicit
            // user commitments. After bounded recovery, retire only empty optional
            // identities; concrete user-named parts are already blocking above.
            for (const auto index : emptyNominalParts)
                if (index < result.instruments.size())
                    retiredInstrumentIds.insert(result.instruments[index].id);
            if (!emptyNominalParts.empty() && progress)
                progress({AiSongStage::Recovery, completedBlocks, blocks.size(), attempt,
                    "retiring " + juce::String(static_cast<int>(emptyNominalParts.size())) +
                    " empty unrequested nominal part(s); preserving authored music"});
            return true;
        }
        return true;
    };

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto scoreBeforeBlock = localEditorial ? assembledScore : PerformanceScore{};
        if (!authorBlock(blocks[index], 1, index)) {
            error = "Incremental GPT performance failed after bounded recovery: " + lastBlockError;
            return {};
        }
        if (localEditorial) {
            const auto audit = auditLocalScore(assembledScore);
            const auto tonalEvents = SelectiveRepair::measuredTonalDebt(audit);
            const auto lowSeconds = SelectiveRepair::sustainedLowChordBedSeconds(
                result, audit.finalTonalPass.after);
            const auto bed = SelectiveRepair::centralChordBedOwner(result);
            const auto bedWritten = bed &&
                std::find(blocks[index].begin(), blocks[index].end(), *bed) !=
                    blocks[index].end();
            const auto bedCoverage = bedWritten
                ? SelectiveRepair::chordBedFormCoverage(result, assembledScore, *bed)
                : ChordBedFormCoverage{true, true, true};
            if (tonalEvents > 0 || !lowSeconds.empty() || !bedCoverage.ready()) {
                lastBlockError = tonalEvents > 0 || !lowSeconds.empty()
                    ? "Complete MIDI still has " + juce::String(tonalEvents) +
                        " unresolved tonal events after block " +
                        juce::String(static_cast<int>(index + 1))
                    : "Primary chord bed does not yet span opening, development and closing "
                        "after bounded AI recovery";
                if (localCheckpointAudit && localCheckpointAudit(assembledScore)) {
                    OperationalJournal::write("WARN", "EDITORIAL",
                        lastBlockError + "; authored MIDI retained for full-score review");
                    if (progress) progress({AiSongStage::Validation, completedBlocks,
                        blocks.size(), 0, lastBlockError +
                        "; continuing with all technically valid authored notes"});
                    saveAcceptedCheckpoint(true);
                    ++completedBlocks;
                    continue;
                }
                if (progress) progress({AiSongStage::Validation, completedBlocks,
                    blocks.size(), 0, lastBlockError +
                        " | bed_form=" +
                        juce::String(static_cast<int>(bedCoverage.opening)) + "/" +
                        juce::String(static_cast<int>(bedCoverage.development)) + "/" +
                        juce::String(static_cast<int>(bedCoverage.closing)) +
                        localTonalConflictBrief(result, audit,
                            instrumentIdsFor(result, blocks[index])).substring(0, 1800)});
                saveRejectedCandidate(assembledScore);
                assembledScore = scoreBeforeBlock;
                saveAcceptedCheckpoint(true);
                error = "Incremental GPT performance failed after bounded recovery: " +
                    lastBlockError;
                return {};
            }
        }
        ++completedBlocks;
    }
    if (!deferredConstraintInstrumentIds.empty())
        OperationalJournal::write("WARN", "CHECKPOINT",
            "all performance blocks completed with " +
            juce::String(static_cast<int>(deferredConstraintInstrumentIds.size())) +
            " deferred constraint identity(s); final global classification will decide publication");

    if (!retiredInstrumentIds.empty()) {
        result.instruments.erase(std::remove_if(result.instruments.begin(), result.instruments.end(),
            [&](const auto& instrument) { return retiredInstrumentIds.contains(instrument.id); }),
            result.instruments.end());
        result.soundscape.layers.erase(std::remove_if(result.soundscape.layers.begin(),
            result.soundscape.layers.end(), [&](const auto& layer) {
                return retiredInstrumentIds.contains(layer.instrumentId);
            }), result.soundscape.layers.end());
        for (auto& cell : assembledScore.cells) {
            cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                return retiredInstrumentIds.contains(note.instrumentId);
            }), cell.notes.end());
            cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
                return retiredInstrumentIds.contains(control.instrumentId);
            }), cell.controls.end());
        }
        std::set<std::string> emptyCellIds;
        for (const auto& cell : assembledScore.cells)
            if (cell.notes.empty()) emptyCellIds.insert(cell.id);
        assembledScore.cells.erase(std::remove_if(assembledScore.cells.begin(), assembledScore.cells.end(),
            [&](const auto& cell) { return emptyCellIds.contains(cell.id); }), assembledScore.cells.end());
        assembledScore.placements.erase(std::remove_if(assembledScore.placements.begin(),
            assembledScore.placements.end(), [&](const auto& placement) {
                return emptyCellIds.contains(placement.cellId);
            }), assembledScore.placements.end());
    }
    result.performanceScore = std::move(assembledScore);
    if (progress) progress({AiSongStage::Validation, completedBlocks, blocks.size(), 1,
                            "assembling and validating all authored blocks"});
    applyExplicitRhythmRequest(result, direction);
    result.harmonicLanguage.tonalPolicy = tonalPolicy;
    const auto finalCellsBefore = result.performanceScore.cells.size();
    const auto finalPlacementsBefore = result.performanceScore.placements.size();
    SongComposer::normalizePlan(result);
    const auto finalScoreShrank = result.performanceScore.cells.size() < finalCellsBefore ||
        result.performanceScore.placements.size() < finalPlacementsBefore;
    OperationalJournal::write(finalScoreShrank ? "ERROR" : "INFO", "NORMALIZATION",
        "final plan normalization | cells " +
        juce::String(static_cast<int>(finalCellsBefore)) + " -> " +
        juce::String(static_cast<int>(result.performanceScore.cells.size())) +
        " | placements " + juce::String(static_cast<int>(finalPlacementsBefore)) + " -> " +
        juce::String(static_cast<int>(result.performanceScore.placements.size())));

    const auto motionReconciliation =
        ElectronicRoleContract::reconcileAuthoredPrimaryMotionOwner(result);
    if (motionReconciliation.changed) {
        OperationalJournal::write("OK", "RECOVERY",
            "transferred primary motion ownership from empty AI lane '" +
            juce::String(motionReconciliation.previousOwnerId) + "' to populated authored lane '" +
            juce::String(motionReconciliation.electedOwnerId) + "' | authored note events=" +
            juce::String(static_cast<int>(motionReconciliation.electedAuthoredNotes)) +
            " | no MIDI copied or generated");
    }

    if (!aiSovereign && SelectiveRepair::ensurePrimaryChordBedClosure(result)) {
        SongComposer::normalizePlan(result);
        OperationalJournal::write("OK", "RECOVERY",
            "repaired only the primary chord bed's final four bars from the AI-authored tonic palette; "
            "all earlier placements and unrelated instruments preserved");
    }

    // Coda closure is an invariant of the assembled score, not merely a recovery
    // side effect of another missing-coverage finding. A populated protagonist could
    // previously bypass the conditional recovery branch and disappear many bars
    // before the actual ending. Reuse its own authored phrase transactionally before
    // classifying any remaining constraints; no note content is invented here.
    if (!aiSovereign && SelectiveRepair::ensureAuthoredProtagonistCoda(
            result, result.performanceScore)) {
        SongComposer::normalizePlan(result);
        OperationalJournal::write("OK", "RECOVERY",
            "verified authored protagonist recap placed at the audible final boundary; notes unchanged");
    }

    std::vector<std::size_t> allInstruments;
    allInstruments.reserve(result.instruments.size());
    for (std::size_t index = 0; index < result.instruments.size(); ++index)
        allInstruments.push_back(index);
    const auto writeEarlyRejectedAudit = [&](const juce::String& reason,
                                              const juce::String& stage) {
        GenerationContext foundation;
        foundation.role = Role::Ensemble;
        foundation.rootPitchClass = result.rootPitchClass;
        foundation.scale = result.scale;
        foundation.beatsPerBar = result.beatsPerBar;
        foundation.seed = result.seed;
        foundation.humanize = 0.0;
        CompositionRenderReport report;
        [[maybe_unused]] const auto rendered = SongComposer{}.render(
            result, foundation, {}, &report);
        const auto file = OperationalJournal::writeRejectedAudit(
            result, report, reason, 0, stage);
        OperationalJournal::write("ERROR", "AUDIT",
            "rejected composition preserved at " + file.getFullPathName());
    };
    reportMarginalBarAcceptances(
        result.performanceScore, allInstruments, "final assembled score");
    auto stillMissing = uncoveredInstruments(result, result.performanceScore,
                                            allInstruments, localEditorial);
    if (!stillMissing.empty()) {
        const auto finalDeficits = performanceDeficitBrief(
            result, result.performanceScore, stillMissing);
        auto finalConstraints = SelectiveRepair::performanceConstraints(
            result, result.performanceScore, stillMissing, requestedCastCount > 0);
        auto blockingTargets = SelectiveRepair::blockingTargets(finalConstraints);
        const auto unresolvedPopulatedCoda = std::find_if(
            finalConstraints.begin(), finalConstraints.end(), [&](const auto& constraint) {
                return constraint.evidence.instrumentId ==
                           result.narrativeSpine.protagonistInstrumentId &&
                    constraint.evidence.notes >= 3 &&
                    constraint.evidence.missingCodaResolution;
            });
        if (unresolvedPopulatedCoda != finalConstraints.end()) {
            if (!aiSovereign && SelectiveRepair::ensureAuthoredProtagonistCoda(
                    result, result.performanceScore)) {
                SongComposer::normalizePlan(result);
                stillMissing = uncoveredInstruments(
                    result, result.performanceScore, allInstruments, localEditorial);
                finalConstraints = SelectiveRepair::performanceConstraints(
                    result, result.performanceScore, stillMissing,
                    requestedCastCount > 0);
                blockingTargets = SelectiveRepair::blockingTargets(finalConstraints);
                OperationalJournal::write("OK", "RECOVERY",
                    "populated protagonist received a transactionally verified authored coda; "
                    "complete score re-audited");
            } else {
                OperationalJournal::write("WARN", "RECOVERY",
                    "populated protagonist coda recovery did not pass the independent audit; "
                    "original score preserved");
            }
        }
        auto redundantCloneTargets = aiSovereign ? std::vector<std::size_t>{} :
            SelectiveRepair::consolidatableDuplicateTargets(
                result, finalConstraints, requestedCastCount > 0);
        if (!redundantCloneTargets.empty()) {
            blockingTargets.erase(std::remove_if(blockingTargets.begin(), blockingTargets.end(),
                [&](const auto index) {
                    return std::find(redundantCloneTargets.begin(), redundantCloneTargets.end(), index) !=
                        redundantCloneTargets.end();
                }), blockingTargets.end());
            OperationalJournal::write("WARN", "ORCHESTRATION",
                "consolidating " + juce::String(static_cast<int>(redundantCloneTargets.size())) +
                " redundant unrequested clone lane(s); counterpart performances and all unique MIDI are preserved");
        }
        const auto editorialTargets = SelectiveRepair::editorialTargets(finalConstraints);
        if (!editorialTargets.empty())
            OperationalJournal::write("WARN", "EDITORIAL",
                "complete score retains non-blocking musical objectives | " +
                performanceConstraintBrief(result, result.performanceScore,
                    editorialTargets, requestedCastCount > 0));
        // A long-form cast can occasionally return a valid melodic cell while the
        // explicitly declared protagonist arrives empty.  Do not invent a new line
        // or relax the identity invariant: promote the best authored melodic cell to
        // that destination, preserving its phrases, placements and transformations.
        // This is an ownership repair only, so the musical material remains GPT-authored.
        auto promoteAuthoredProtagonist = [&]() {
            const auto& protagonistId = result.narrativeSpine.protagonistInstrumentId;
            if (protagonistId.empty()) return false;
            const auto protagonistPresent = std::any_of(
                result.performanceScore.cells.begin(), result.performanceScore.cells.end(),
                [&](const auto& cell) {
                    return std::any_of(cell.notes.begin(), cell.notes.end(),
                        [&](const auto& note) { return note.instrumentId == protagonistId; });
                });
            if (protagonistPresent) return false;

            const auto voiceRank = [](VoiceId voice) {
                if (voice == VoiceId::Lead) return 40;
                if (voice == VoiceId::Countermelody) return 30;
                if (voice == VoiceId::HarmonicPulse) return 20;
                if (voice == VoiceId::HarmonicUpper) return 10;
                return 0;
            };
            const PerformanceCell* source = nullptr;
            std::string sourceInstrument;
            int bestScore = -1;
            for (const auto& cell : result.performanceScore.cells) {
                if (cell.notes.empty()) continue;
                std::map<std::string, std::size_t> notesByInstrument;
                for (const auto& note : cell.notes) {
                    if (note.instrumentId.empty() || note.instrumentId == protagonistId) continue;
                    ++notesByInstrument[note.instrumentId];
                }
                for (const auto& [instrumentId, count] : notesByInstrument) {
                    std::vector<double> attacks;
                    for (const auto& note : cell.notes)
                        if (note.instrumentId == instrumentId)
                            attacks.push_back(note.beat);
                    std::sort(attacks.begin(), attacks.end());
                    attacks.erase(std::unique(attacks.begin(), attacks.end(),
                        [](double left, double right) {
                            return std::abs(left - right) < .01;
                        }), attacks.end());
                    auto connected = attacks.empty() ? std::size_t{} : std::size_t{1};
                    auto longestConnected = connected;
                    for (std::size_t attack = 1; attack < attacks.size(); ++attack) {
                        connected = attacks[attack] - attacks[attack - 1] <=
                            result.beatsPerBar * .75 + .001 ? connected + 1 : 1;
                        longestConnected = std::max(longestConnected, connected);
                    }
                    // Ownership recovery may only transfer an actual melodic
                    // sentence. Isolated cue/marker events are never a protagonist.
                    if (longestConnected < 4) continue;
                    auto assignment = std::find_if(result.instruments.begin(), result.instruments.end(),
                        [&](const auto& part) { return part.id == instrumentId; });
                    if (assignment == result.instruments.end() ||
                        assignment->explicitPromptIdentity ||
                        ElectronicRoleContract::motionOwner(*assignment)) continue;
                    const auto voice = assignment->sourceVoice;
                    if (isVoiceInFamily(voice, VoiceFamily::Rhythm) ||
                        voice == VoiceId::Transitions) continue;
                    const auto score = static_cast<int>(count) + voiceRank(voice) * 100;
                    if (score > bestScore) {
                        bestScore = score;
                        source = &cell;
                        sourceInstrument = instrumentId;
                    }
                }
            }
            if (source == nullptr) return false;

            PerformanceCell promoted = *source;
            const auto sourceId = source->id;
            promoted.id = "fallback_protagonist_" + source->id;
            promoted.ownedVoices.clear();
            VoiceId protagonistVoice = VoiceId::Lead;
            for (const auto& part : result.instruments)
                if (part.id == protagonistId) { protagonistVoice = part.sourceVoice; break; }
            promoted.ownedVoices.push_back(protagonistVoice);
            promoted.narrativeFunction = "protagonist_statement";
            promoted.notes.erase(std::remove_if(promoted.notes.begin(), promoted.notes.end(),
                [&](const auto& note) { return note.instrumentId != sourceInstrument; }),
                promoted.notes.end());
            for (auto& note : promoted.notes) {
                note.instrumentId = protagonistId;
                note.voice = protagonistVoice;
            }
            promoted.controls.erase(std::remove_if(promoted.controls.begin(), promoted.controls.end(),
                [&](const auto& control) {
                    return !control.instrumentId.empty() && control.instrumentId != sourceInstrument;
                }), promoted.controls.end());
            for (auto& control : promoted.controls) {
                control.instrumentId = protagonistId;
                control.voice = protagonistVoice;
            }
            if (promoted.notes.empty()) return false;
            const auto promotedId = promoted.id;
            std::vector<PerformancePlacement> promotedPlacements;
            for (const auto& placement : result.performanceScore.placements) {
                if (placement.cellId != sourceId) continue;
                auto copy = placement;
                copy.cellId = promotedId;
                promotedPlacements.push_back(std::move(copy));
            }
            if (promotedPlacements.empty()) return false;

            // This is a transfer of authored responsibility, not a doubling. Remove
            // the promoted instrument's events from the source cell before publishing
            // the new protagonist cell, otherwise the recovery itself creates a clone
            // that the independence gate correctly rejects.
            auto mutableSource = std::find_if(result.performanceScore.cells.begin(),
                result.performanceScore.cells.end(),
                [&](const auto& cell) { return cell.id == sourceId; });
            if (mutableSource == result.performanceScore.cells.end()) return false;
            mutableSource->notes.erase(std::remove_if(
                mutableSource->notes.begin(), mutableSource->notes.end(),
                [&](const auto& note) { return note.instrumentId == sourceInstrument; }),
                mutableSource->notes.end());
            if (mutableSource->notes.empty()) {
                result.performanceScore.placements.erase(std::remove_if(
                    result.performanceScore.placements.begin(),
                    result.performanceScore.placements.end(),
                    [&](const auto& placement) { return placement.cellId == sourceId; }),
                    result.performanceScore.placements.end());
                result.performanceScore.cells.erase(mutableSource);
            } else {
                mutableSource->controls.erase(std::remove_if(
                    mutableSource->controls.begin(), mutableSource->controls.end(),
                    [&](const auto& control) {
                        return control.instrumentId == sourceInstrument;
                    }), mutableSource->controls.end());
            }
            result.performanceScore.cells.push_back(std::move(promoted));
            result.performanceScore.placements.insert(
                result.performanceScore.placements.end(),
                promotedPlacements.begin(), promotedPlacements.end());
            OperationalJournal::write("WARN", "RECOVERY",
                "protagonist identity was empty; transferred authored melodic cell '" +
                juce::String(sourceId) + "' to '" + juce::String(protagonistId) +
                "' without rewriting or duplicating notes");
            return true;
        };

        if (!aiSovereign && !blockingTargets.empty() && promoteAuthoredProtagonist()) {
            const auto promotedCell = result.performanceScore.cells.empty()
                ? std::string{} : result.performanceScore.cells.back().id;
            if (!aiSovereign && SelectiveRepair::ensureAuthoredProtagonistCoda(
                    result, result.performanceScore, promotedCell))
                OperationalJournal::write("WARN", "RECOVERY",
                    "promoted protagonist received an authored transformed coda placement; notes unchanged");
            SongComposer::normalizePlan(result);
            stillMissing = uncoveredInstruments(result, result.performanceScore,
                                                allInstruments, localEditorial);
            const auto repairedConstraints = SelectiveRepair::performanceConstraints(
                result, result.performanceScore, stillMissing, requestedCastCount > 0);
            auto repairedBlocking = SelectiveRepair::blockingTargets(repairedConstraints);
            const auto repairedRedundant =
                SelectiveRepair::consolidatableDuplicateTargets(
                    result, repairedConstraints, requestedCastCount > 0);
            redundantCloneTargets.insert(redundantCloneTargets.end(),
                repairedRedundant.begin(), repairedRedundant.end());
            std::sort(redundantCloneTargets.begin(), redundantCloneTargets.end());
            redundantCloneTargets.erase(std::unique(redundantCloneTargets.begin(),
                redundantCloneTargets.end()), redundantCloneTargets.end());
            repairedBlocking.erase(std::remove_if(repairedBlocking.begin(), repairedBlocking.end(),
                [&](const auto index) {
                    return std::find(redundantCloneTargets.begin(), redundantCloneTargets.end(), index) !=
                        redundantCloneTargets.end();
                }), repairedBlocking.end());
            if (repairedBlocking.empty()) {
                OperationalJournal::write("OK", "CONSTRAINT",
                    "blocking protagonist identity repaired by authored ownership promotion");
            } else {
                error = "Incremental GPT score violated a blocking identity commitment: " +
                    performanceConstraintBrief(result, result.performanceScore,
                        repairedBlocking, requestedCastCount > 0);
                OperationalJournal::write("ERROR", "CONSTRAINT", error);
                writeEarlyRejectedAudit(error, "final_constraint_after_protagonist_recovery");
                return {};
            }
        } else if (!blockingTargets.empty()) {
            error = "Incremental GPT score violated a blocking identity commitment: " +
                performanceConstraintBrief(result, result.performanceScore,
                    blockingTargets, requestedCastCount > 0);
            OperationalJournal::write("ERROR", "CONSTRAINT", error);
            writeEarlyRejectedAudit(error, "final_constraint");
            return {};
        }
        // Only a non-essential, non-user-named identity with zero concrete MIDI is
        // retired. Quantitative and narrative findings preserve every authored track.
        stillMissing.clear();
        for (const auto& constraint : finalConstraints)
            if (!constraint.blocksPublication && constraint.evidence.notes == 0)
                stillMissing.push_back(constraint.evidence.instrumentIndex);
        stillMissing.insert(stillMissing.end(), redundantCloneTargets.begin(),
                            redundantCloneTargets.end());
        std::sort(stillMissing.begin(), stillMissing.end());
        stillMissing.erase(std::unique(stillMissing.begin(), stillMissing.end()), stillMissing.end());
        if (stillMissing.empty()) {
            OperationalJournal::write("OK", "CONSTRAINT",
                "all hard invariants and explicit commitments satisfied; editorial objectives preserved");
        }
    }
    if (!stillMissing.empty()) {
        std::set<std::string> finalRetiredIds;
        for (const auto index : stillMissing)
            if (index < result.instruments.size())
                finalRetiredIds.insert(result.instruments[index].id);
        if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), 3,
            "retiring " + juce::String(static_cast<int>(finalRetiredIds.size())) +
            " empty or redundant unrequested part(s); preserving authored musical objectives"});
        result.instruments.erase(std::remove_if(result.instruments.begin(), result.instruments.end(),
            [&](const auto& instrument) { return finalRetiredIds.contains(instrument.id); }),
            result.instruments.end());
        result.soundscape.layers.erase(std::remove_if(result.soundscape.layers.begin(),
            result.soundscape.layers.end(), [&](const auto& layer) {
                return finalRetiredIds.contains(layer.instrumentId);
            }), result.soundscape.layers.end());
        for (auto& cell : result.performanceScore.cells) {
            cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                return finalRetiredIds.contains(note.instrumentId);
            }), cell.notes.end());
            cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
                return finalRetiredIds.contains(control.instrumentId);
            }), cell.controls.end());
        }
        std::set<std::string> finalEmptyCellIds;
        for (const auto& cell : result.performanceScore.cells)
            if (cell.notes.empty()) finalEmptyCellIds.insert(cell.id);
        result.performanceScore.cells.erase(std::remove_if(result.performanceScore.cells.begin(),
            result.performanceScore.cells.end(), [&](const auto& cell) {
                return finalEmptyCellIds.contains(cell.id);
            }), result.performanceScore.cells.end());
        result.performanceScore.placements.erase(std::remove_if(result.performanceScore.placements.begin(),
            result.performanceScore.placements.end(), [&](const auto& placement) {
                return finalEmptyCellIds.contains(placement.cellId);
            }), result.performanceScore.placements.end());
        SongComposer::normalizePlan(result);
        allInstruments.clear();
        for (std::size_t index = 0; index < result.instruments.size(); ++index)
            allInstruments.push_back(index);
        auto convergedConstraints = SelectiveRepair::performanceConstraints(
            result, result.performanceScore, allInstruments, requestedCastCount > 0);
        const auto convergedBlocking = SelectiveRepair::blockingTargets(convergedConstraints);
        if (!convergedBlocking.empty()) {
            error = "Coverage convergence violated a blocking identity commitment: " +
                performanceConstraintBrief(result, result.performanceScore,
                    convergedBlocking, requestedCastCount > 0);
            OperationalJournal::write("ERROR", "CONSTRAINT", error);
            writeEarlyRejectedAudit(error, "coverage_convergence");
            return {};
        }
        stillMissing.clear();
        for (const auto& constraint : convergedConstraints)
            if (constraint.evidence.notes == 0)
                stillMissing.push_back(constraint.evidence.instrumentIndex);
        for (int convergencePass = 0; !stillMissing.empty() && convergencePass < 3;
             ++convergencePass) {
            std::set<std::string> cascadeIds;
            for (const auto index : stillMissing)
                if (index < result.instruments.size())
                    cascadeIds.insert(result.instruments[index].id);
            if (cascadeIds.empty()) break;
            if (progress) progress({AiSongStage::Recovery, completedBlocks, blocks.size(), 3,
                "coverage convergence: retiring " +
                juce::String(static_cast<int>(cascadeIds.size())) + " dependent part(s)"});
            result.instruments.erase(std::remove_if(result.instruments.begin(), result.instruments.end(),
                [&](const auto& instrument) { return cascadeIds.contains(instrument.id); }),
                result.instruments.end());
            result.soundscape.layers.erase(std::remove_if(result.soundscape.layers.begin(),
                result.soundscape.layers.end(), [&](const auto& layer) {
                    return cascadeIds.contains(layer.instrumentId);
                }), result.soundscape.layers.end());
            for (auto& cell : result.performanceScore.cells) {
                cell.notes.erase(std::remove_if(cell.notes.begin(), cell.notes.end(), [&](const auto& note) {
                    return cascadeIds.contains(note.instrumentId);
                }), cell.notes.end());
                cell.controls.erase(std::remove_if(cell.controls.begin(), cell.controls.end(), [&](const auto& control) {
                    return cascadeIds.contains(control.instrumentId);
                }), cell.controls.end());
            }
            std::set<std::string> cascadeEmptyCells;
            for (const auto& cell : result.performanceScore.cells)
                if (cell.notes.empty()) cascadeEmptyCells.insert(cell.id);
            result.performanceScore.cells.erase(std::remove_if(result.performanceScore.cells.begin(),
                result.performanceScore.cells.end(), [&](const auto& cell) {
                    return cascadeEmptyCells.contains(cell.id);
                }), result.performanceScore.cells.end());
            result.performanceScore.placements.erase(std::remove_if(result.performanceScore.placements.begin(),
                result.performanceScore.placements.end(), [&](const auto& placement) {
                    return cascadeEmptyCells.contains(placement.cellId);
                }), result.performanceScore.placements.end());
            allInstruments.clear();
            for (std::size_t index = 0; index < result.instruments.size(); ++index)
                allInstruments.push_back(index);
            convergedConstraints = SelectiveRepair::performanceConstraints(
                result, result.performanceScore, allInstruments, requestedCastCount > 0);
            const auto hardAfterConvergence =
                SelectiveRepair::blockingTargets(convergedConstraints);
            if (!hardAfterConvergence.empty()) {
                error = "Coverage convergence violated a blocking identity commitment: " +
                    performanceConstraintBrief(result, result.performanceScore,
                        hardAfterConvergence, requestedCastCount > 0);
                OperationalJournal::write("ERROR", "CONSTRAINT", error);
                writeEarlyRejectedAudit(error, "coverage_cascade");
                return {};
            }
            stillMissing.clear();
            for (const auto& constraint : convergedConstraints)
                if (constraint.evidence.notes == 0)
                    stillMissing.push_back(constraint.evidence.instrumentIndex);
        }
        if (!stillMissing.empty() || result.performanceScore.empty() || result.instruments.empty()) {
            error = "Incremental GPT score retained an empty identity after bounded convergence";
            OperationalJournal::write("ERROR", "CONSTRAINT", error);
            writeEarlyRejectedAudit(error, "empty_identity_after_convergence");
            return {};
        }
    }
    const auto protagonistPreserved = result.narrativeSpine.protagonistInstrumentId.empty() ||
        std::any_of(result.instruments.begin(), result.instruments.end(), [&](const auto& instrument) {
            return instrument.id == result.narrativeSpine.protagonistInstrumentId;
        });
    if (!protagonistPreserved) {
        error = "Incremental GPT score lost its declared protagonist";
        OperationalJournal::write("ERROR", "CONSTRAINT", error);
        writeEarlyRejectedAudit(error, "protagonist_preservation");
        return {};
    }
    if (!repairMotionOwnerContract(result)) {
        error = "Incremental GPT score must preserve exactly one elected primary electronic motion owner";
        OperationalJournal::write("ERROR", "CONSTRAINT", error);
        writeEarlyRejectedAudit(error, "motion_owner_contract");
        return {};
    }
    repairCentralChordBedContract(result);
    const auto continuity = SelectiveRepair::ensembleContinuity(
        result, result.performanceScore);
    OperationalJournal::write(continuity.ready ? "OK" : "ERROR", "CONTINUITY",
        "authored ensemble | windows=" +
        juce::String(static_cast<int>(continuity.evaluatedWindows)) +
        " | silent=" + juce::String(static_cast<int>(continuity.silentWindows)) +
        " | consecutive_silent=" + juce::String(static_cast<int>(
            continuity.maximumConsecutiveSilentWindows)) +
        " | underfilled=" + juce::String(static_cast<int>(continuity.underfilledWindows)) +
        " | ensemble=" + juce::String(continuity.audibleCoverage * 100.0, 0) + "%" +
        " | two-layer floor=" + juce::String(continuity.harmonicFloorCoverage * 100.0, 0) + "%" +
        " | longest silence beats=" + juce::String(continuity.longestGlobalSilenceBeats, 2));
    if (!continuity.ready) {
        OperationalJournal::write("WARN", "CONTINUITY",
            "authored ensemble needs selective continuity repair; complete score checkpoint preserved");
    }
    if (requestedCastCount > 0 && result.instruments.size() != requestedCastCount)
        OperationalJournal::write("WARN", "CAST",
            "published " + juce::String(static_cast<int>(result.instruments.size())) + "/" +
            juce::String(static_cast<int>(requestedCastCount)) +
            " meaningful tracks after retiring empty AI-invented identities; no filler or duplicate MIDI added");
    // The score has passed every hard identity and MIDI-structure contract. Run one
    // bounded transactional editorial audition before publication: only diagnosed
    // instruments may change, and a failed or inferior edit always preserves this
    // accepted checkpoint instead of rejecting the complete song.
    OperationalJournal::write("OK", "CHECKPOINT",
        "complete AI score checkpointed; starting bounded transactional musical audition");
    GenerationContext auditFoundation;
    auditFoundation.role = Role::Ensemble;
    auditFoundation.rootPitchClass = result.rootPitchClass;
    auditFoundation.scale = result.scale;
    auditFoundation.beatsPerBar = result.beatsPerBar;
    auditFoundation.seed = result.seed;
    auditFoundation.humanize = 0.0;
    CompositionRenderReport initialReport;
    [[maybe_unused]] const auto initialPattern = SongComposer{}.render(
        result, auditFoundation, {}, &initialReport);
    OperationalJournal::write("INFO", "AUDITION", audibleAuditSummary(initialReport));
    if (localEditorial && initialReport.production.unintendedHarshOverlaps > 0) {
        const auto groups = SelectiveRepair::tonalConflictGroups(
            result, initialReport.finalTonalPass.after);
        juce::String summary;
        for (std::size_t index = 0; index < std::min<std::size_t>(4, groups.size()); ++index) {
            const auto& group = groups[index];
            if (summary.isNotEmpty()) summary << "; ";
            summary << juce::String::fromUTF8(
                result.instruments[group.firstInstrument].id.c_str()) << "/"
                << juce::String::fromUTF8(
                    result.instruments[group.secondInstrument].id.c_str())
                << " bar=" << group.bar + 1
                << " events=" << static_cast<int>(group.events)
                << " overlap_beats=" << juce::String(group.overlapBeats, 2);
        }
        OperationalJournal::write("INFO", "TONAL_MAP",
            "measured overlap pairs=" + juce::String(
                initialReport.production.unintendedHarshOverlaps) +
            " | grouped_windows=" + juce::String(static_cast<int>(groups.size())) +
            " | top=" + summary);
    }
    const auto initialDiagnosis = SelectiveRepair::diagnose(
        result, initialPattern, initialReport, 4);
    if (SelectiveRepair::publicationReady(initialReport) && !initialDiagnosis.needed) {
        OperationalJournal::write("OK", "AUDITION", "score passed without editorial repair");
        error.clear();
        return result;
    }

    // A failed or merely partial repair must never turn an otherwise complete AI
    // composition into an empty result.
    auto bestPlan = result;
    auto bestPattern = initialPattern;
    auto bestReport = initialReport;
    std::set<std::string> repairedInstrumentIds;
    std::set<std::string> attemptedInstrumentIds;
    auto completedRepairs = std::size_t{};
    juce::String terminalReason = "audible quality remained below the publication threshold";
    const auto performanceDebt = [](const SongPlan& plan, const PerformanceScore& score,
                                    const std::vector<std::size_t>& targets) {
        auto debt = 0.0;
        for (const auto& finding : SelectiveRepair::performanceDeficits(plan, score, targets)) {
            const auto ratioDebt = [](std::size_t actual, std::size_t required) {
                return required == 0 ? 0.0 : std::max(0.0,
                    1.0 - static_cast<double>(actual) / static_cast<double>(required));
            };
            debt += ratioDebt(finding.notes, finding.minimumNotes) +
                ratioDebt(finding.authoredNotes, finding.minimumAuthoredNotes) +
                ratioDebt(finding.activeBars, finding.minimumActiveBars) +
                ratioDebt(finding.phrases, finding.minimumPhrases) +
                ratioDebt(finding.sections, finding.minimumSections);
            debt += finding.missingCodaResolution ? 3.0 : 0.0;
            debt += finding.duplicatedIndependentLine ? 3.0 : 0.0;
            debt += finding.missingNarrativePresence ? 2.0 : 0.0;
            debt += finding.missingThematicDevelopment ? 2.0 : 0.0;
            debt += finding.missingAuthoredDevelopment ? 3.0 : 0.0;
            debt += finding.missingMelodicSpeech ? 2.0 : 0.0;
            debt += finding.missingSectionalEvolution ? 1.5 : 0.0;
            debt += finding.missingCentralChordBed ? 3.0 : 0.0;
            debt += finding.missingChordBedBreath ? 1.5 : 0.0;
            debt += finding.missingChordBedNarrativeArc ? 2.5 : 0.0;
        }
        return debt;
    };
    const auto densityContrast = [](const Pattern& pattern, double beatsPerBar) {
        if (pattern.lengthBeats <= 0.0 || beatsPerBar <= 0.0) return 0.0;
        std::vector<double> windows;
        const auto window = beatsPerBar * 4.0;
        for (auto start = 0.0; start < pattern.lengthBeats; start += window) {
            auto total = 0.0;
            auto samples = 0;
            for (auto beat = start + beatsPerBar * .5;
                 beat < std::min(pattern.lengthBeats, start + window);
                 beat += beatsPerBar) {
                std::set<std::uint16_t> owners;
                for (const auto& note : pattern.notes)
                    if (note.partId != 0 && note.startBeat <= beat && note.endBeat() > beat)
                        owners.insert(note.partId);
                total += static_cast<double>(owners.size());
                ++samples;
            }
            if (samples > 0) windows.push_back(total / samples);
        }
        if (windows.empty()) return 0.0;
        return *std::max_element(windows.begin(), windows.end()) -
            *std::min_element(windows.begin(), windows.end());
    };
    const auto repairPassLimit = localEditorial ? 4 : 2;
    const auto repairDeadline = std::min(overallDeadline,
        std::chrono::steady_clock::now() + std::chrono::minutes(4));

    for (auto repairPass = 1; repairPass <= repairPassLimit; ++repairPass) {
        if (token.stop_requested()) {
            error = "Generation cancelled";
            return {};
        }
        auto diagnosis = SelectiveRepair::diagnose(bestPlan, bestPattern, bestReport, 4);
        if (!diagnosis.needed) break;
        // The local AI-only editor auditions one musical owner at a time. The
        // former four-owner transaction could contain three excellent rewrites
        // and one invalid sustain; rejecting the combined candidate then threw
        // all four away. Re-diagnosing after every accepted lane also prevents
        // stale repair objectives and unnecessary calls.
        if (localEditorial) {
            std::erase_if(diagnosis.instrumentIndices, [&](const auto index) {
                return index >= bestPlan.instruments.size() ||
                    attemptedInstrumentIds.contains(bestPlan.instruments[index].id);
            });
            if (diagnosis.instrumentIndices.size() > 1)
                diagnosis.instrumentIndices.resize(1);
        } else if (repairPass > 1 && diagnosis.instrumentIndices.size() > 1) {
            std::vector<std::size_t> freshTargets;
            for (const auto index : diagnosis.instrumentIndices)
                if (index < bestPlan.instruments.size() &&
                    !repairedInstrumentIds.contains(bestPlan.instruments[index].id))
                    freshTargets.push_back(index);
            // Prefer a complementary second shard. If the same severe culprit is the
            // only remaining one, allow one final focused refinement instead of stalling.
            if (!freshTargets.empty()) diagnosis.instrumentIndices = std::move(freshTargets);
        }
        if (diagnosis.instrumentIndices.empty()) {
            terminalReason = "creative audition found no bounded instrument owner for the remaining issue";
            break;
        }
        const auto repairRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            repairDeadline - std::chrono::steady_clock::now());
        if (repairRemaining < std::chrono::seconds(30)) {
            terminalReason = "selective repair exhausted its three-minute global budget";
            break;
        }
        const auto targetIds = instrumentIdsFor(bestPlan, diagnosis.instrumentIndices);
        if (localEditorial)
            attemptedInstrumentIds.insert(targetIds.begin(), targetIds.end());
        juce::String targetList;
        for (const auto& id : targetIds) targetList += juce::String::fromUTF8(id.c_str()) + ",";
        OperationalJournal::write("INFO", "REPAIR", "pass " + juce::String(repairPass) +
            "/" + juce::String(repairPassLimit) + " | targets=" + targetList +
            " | before " + audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, completedBlocks + repairPass - 1,
            blocks.size() + static_cast<std::size_t>(repairPassLimit), repairPass,
            "selective musical repair " +
            juce::String(repairPass) + "/" + juce::String(repairPassLimit) + " - " +
            juce::String(static_cast<int>(diagnosis.instrumentIndices.size())) + " part(s)"});

        std::vector<std::vector<std::size_t>> repairShards;
        for (std::size_t begin = 0; begin < diagnosis.instrumentIndices.size();
             begin += instrumentsPerEditorialRepairShard) {
            repairShards.emplace_back(
                diagnosis.instrumentIndices.begin() + static_cast<std::ptrdiff_t>(begin),
                diagnosis.instrumentIndices.begin() + static_cast<std::ptrdiff_t>(
                    std::min(diagnosis.instrumentIndices.size(), begin + instrumentsPerEditorialRepairShard)));
        }
        PerformanceScore repairedMaterial;
        std::set<std::string> completedTargetIds;
        auto repairSerial = std::size_t{};
        auto makeRepairBody = [&](const std::vector<std::size_t>& indices,
                                  int attempt, std::size_t shardIndex) {
            auto shardDiagnosis = diagnosis;
            shardDiagnosis.instrumentIndices = indices;
            auto repairPrompt = selectiveRepairPrompt(direction, bestPlan, shardDiagnosis);
            if (localEditorial)
                repairPrompt += localTonalConflictBrief(bestPlan, bestReport,
                    instrumentIdsFor(bestPlan, indices)) +
                    "LOCAL EDITORIAL AUTHORITY: the measured events above are the repair objective. "
                    "Do not add notes merely to reach numeric coverage targets. Preserve the full "
                    "identity of the target performance and its unaffected passages. Revoice, shorten, "
                    "rest or move only the phrases causing the measured pair conflicts; coordinate "
                    "against the immutable counterpart MIDI. You alone decide every replacement note "
                    "and rest. Keep all untargeted instruments unchanged.\n";
            const auto repairCacheKey = "pulso-repair-" + juce::String::toHexString(
                static_cast<juce::int64>(seed)) + "-p" + juce::String(repairPass) +
                "-s" + juce::String(static_cast<int>(shardIndex + 1)) +
                "-a" + juce::String(attempt);
            return juce::String("{\"model\":\"") + model +
                "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                realizationReasoningEffort +
                "\"},\"max_output_tokens\":10000,\"prompt_cache_key\":" +
                juce::JSON::toString(juce::var(repairCacheKey)) + ",\"input\":" +
                juce::JSON::toString(juce::var(repairPrompt)) +
                ",\"text\":{\"format\":{\"type\":\"json_schema\","
                "\"name\":\"pulso_selective_repair_shard\",\"strict\":true,\"schema\":" +
                performanceSchemaFor(bestPlan, indices) + "}}}";
        };
        auto executeRepairRound = [&](const std::vector<std::vector<std::size_t>>& shards,
                                      int attempt, std::chrono::milliseconds requestBudget) {
            std::vector<std::vector<std::size_t>> unresolved;
            for (std::size_t batchBegin = 0; batchBegin < shards.size();
                 batchBegin += maximumConcurrentRepairShards) {
                const auto batchEnd = std::min(shards.size(),
                    batchBegin + maximumConcurrentRepairShards);
                std::vector<std::future<HttpResponse>> requests;
                for (auto shardIndex = batchBegin; shardIndex < batchEnd; ++shardIndex) {
                    requests.push_back(std::async(std::launch::async,
                        [&, shardIndex] {
                            return performRequest(makeRepairBody(shards[shardIndex], attempt,
                                shardIndex), apiKey, token, requestBudget);
                        }));
                }
                for (auto shardIndex = batchBegin; shardIndex < batchEnd; ++shardIndex) {
                    auto response = requests[shardIndex - batchBegin].get();
                    const auto shardIds = instrumentIdsFor(bestPlan, shards[shardIndex]);
                    const auto repairText = extractOutputText(juce::JSON::parse(response.body));
                    PerformanceScore shardMaterial;
                    juce::String repairError;
                    PerformanceRoutingReport routing;
                    const auto transportOk = response.connected && response.status >= 200 &&
                        response.status < 300 && !response.cancelled && !response.timedOut;
                    const auto parsed = transportOk && repairText.isNotEmpty() &&
                        parsePerformanceBlock(repairText, bestPlan, shardIds,
                                              shardMaterial, repairError, &routing);
                    OperationalJournal::write(parsed ? "INFO" : "WARN", "ROUTING",
                        "editorial repair routing: " + routing.summary(shardIds));
                    if (!parsed) {
                        const auto reason = !transportOk
                            ? apiErrorMessage(response)
                            : structuredResponseError(response.body,
                                repairText.isEmpty() ? "OpenAI returned no repair shard" :
                                "repair shard JSON was invalid: " + repairError);
                        OperationalJournal::write(attempt == 1 ? "WARN" : "ERROR", "REPAIR",
                            "pass " + juce::String(repairPass) + " shard " +
                            juce::String(static_cast<int>(shardIndex + 1)) + "/" +
                            juce::String(static_cast<int>(shards.size())) + " attempt " +
                            juce::String(attempt) + " failed: " + reason);
                        unresolved.push_back(shards[shardIndex]);
                        terminalReason = reason;
                        continue;
                    }
                    retainOnlyInstrumentMaterial(shardMaterial, shardIds);
                    const auto missing = uncoveredInstruments(bestPlan, shardMaterial,
                                                               shards[shardIndex], localEditorial);
                    auto acceptedIds = shardIds;
                    for (const auto index : missing)
                        if (index < bestPlan.instruments.size())
                            acceptedIds.erase(bestPlan.instruments[index].id);
                    if (!acceptedIds.empty()) {
                        retainOnlyInstrumentMaterial(shardMaterial, acceptedIds);
                        mergePerformanceBlock(repairedMaterial, std::move(shardMaterial),
                                              requestSerial + repairSerial++);
                        completedTargetIds.insert(acceptedIds.begin(), acceptedIds.end());
                        OperationalJournal::write("OK", "CHECKPOINT",
                            "repair pass " + juce::String(repairPass) + " shard " +
                            juce::String(static_cast<int>(shardIndex + 1)) + " accepted " +
                            juce::String(static_cast<int>(acceptedIds.size())) + " instrument(s)");
                    }
                    if (!missing.empty()) {
                        for (std::size_t begin = 0; begin < missing.size();
                             begin += instrumentsPerEditorialRepairShard) {
                            unresolved.emplace_back(missing.begin() + static_cast<std::ptrdiff_t>(begin),
                                missing.begin() + static_cast<std::ptrdiff_t>(
                                    std::min(missing.size(), begin + instrumentsPerEditorialRepairShard)));
                        }
                        terminalReason = "repair shard omitted " +
                            juce::String(static_cast<int>(missing.size())) + " instrument part(s)";
                    }
                }
            }
            return unresolved;
        };

        auto unresolvedRepairs = executeRepairRound(repairShards, 1,
            std::min(repairRemaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::seconds(90))));
        if (!unresolvedRepairs.empty() && !token.stop_requested()) {
            const auto retryRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                repairDeadline - std::chrono::steady_clock::now());
            if (retryRemaining >= std::chrono::seconds(25)) {
                OperationalJournal::write("WARN", "RECOVERY", "repair pass " +
                    juce::String(repairPass) + " retrying only " +
                    juce::String(static_cast<int>(unresolvedRepairs.size())) + " unresolved shard(s)");
                unresolvedRepairs = executeRepairRound(unresolvedRepairs, 2,
                    std::min(retryRemaining,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::seconds(75))));
            }
        }
        if (!unresolvedRepairs.empty()) {
            terminalReason = "selective repair retained " +
                juce::String(static_cast<int>(completedTargetIds.size())) + "/" +
                juce::String(static_cast<int>(targetIds.size())) +
                " instruments; unresolved shards remain after bounded recovery";
            OperationalJournal::write("WARN", "REPAIR", terminalReason);
        }
        if (completedTargetIds.empty() || repairedMaterial.empty()) {
            OperationalJournal::write("WARN", "REPAIR",
                "no valid repair shard was available; preserving the prior score");
            if (localEditorial) continue;
            break;
        }

        auto candidatePlan = bestPlan;
        eraseInstrumentMaterial(candidatePlan.performanceScore, completedTargetIds);
        mergePerformanceBlock(candidatePlan.performanceScore, std::move(repairedMaterial), requestSerial++);
        applyExplicitRhythmRequest(candidatePlan, direction);
        candidatePlan.harmonicLanguage.tonalPolicy = tonalPolicy;
        SongComposer::normalizePlan(candidatePlan);
        CompositionRenderReport candidateReport;
        const auto candidatePattern = SongComposer{}.render(
            candidatePlan, auditFoundation, {}, &candidateReport);
        if (localEditorial && !localUntouchedPassagesPreserved(
                bestPlan, bestPattern, candidatePattern,
                bestReport.finalTonalPass.after, completedTargetIds))
            OperationalJournal::write("WARN", "REPAIR",
                "AI repair changed passages outside the measured conflict bars within its target lanes; "
                "all untargeted MIDI remains preserved and the complete audible gate decides acceptance");
        OperationalJournal::write("INFO", "REPAIR", "pass " + juce::String(repairPass) +
            "/" + juce::String(repairPassLimit) + " result | " +
            audibleAuditSummary(candidateReport));
        const auto priorPerformanceDebt = performanceDebt(
            bestPlan, bestPlan.performanceScore, diagnosis.instrumentIndices);
        const auto candidatePerformanceDebt = performanceDebt(
            candidatePlan, candidatePlan.performanceScore, diagnosis.instrumentIndices);
        const auto priorDensityContrast = densityContrast(bestPattern, bestPlan.beatsPerBar);
        const auto candidateDensityContrast = densityContrast(
            candidatePattern, candidatePlan.beatsPerBar);
        const auto strictSafer = candidateReport.production.ready &&
            !SelectiveRepair::criticalFailure(candidateReport) &&
            (SelectiveRepair::deficit(candidateReport) + 0.01 <
                 SelectiveRepair::deficit(bestReport) ||
             candidatePerformanceDebt + 0.25 < priorPerformanceDebt ||
             candidateDensityContrast > priorDensityContrast + 0.5);
        const auto incrementalSafer = localEditorial &&
            SelectiveRepair::improvedTonalCheckpoint(bestReport, candidateReport,
                                                     candidatePattern.aiAuthoredNoteRatio);
        const auto creativeSafer = localEditorial &&
            EditorialSafety::technicallySafeAiScore(candidatePattern, candidateReport) &&
            SelectiveRepair::improvedCreativeCheckpoint(
                bestReport, candidateReport, candidatePattern.aiAuthoredNoteRatio);
        const auto safer = (strictSafer || incrementalSafer || creativeSafer) &&
            (!localEditorial ||
             SelectiveRepair::preservesNarrative(bestReport, candidateReport));
        if (!safer) {
            terminalReason = "selective repair did not produce a safer audible candidate";
            OperationalJournal::write("WARN", "REPAIR", terminalReason);
            if (localEditorial) continue;
            break;
        }
        bestPlan = std::move(candidatePlan);
        bestPattern = candidatePattern;
        bestReport = candidateReport;
        repairedInstrumentIds.insert(completedTargetIds.begin(), completedTargetIds.end());
        ++completedRepairs;
        if (localEditorial && checkpoint)
            checkpoint(bestPlan, blocks.size() + completedRepairs + 1, true, false);
        const auto postRepairDiagnosis = SelectiveRepair::diagnose(
            bestPlan, bestPattern, bestReport, 4);
        if (SelectiveRepair::publicationReady(bestReport) && !postRepairDiagnosis.needed) {
            OperationalJournal::write("OK", "GATE", "strict audible contract passed after " +
                juce::String(static_cast<int>(completedRepairs)) + " repair pass(es)");
            if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
                blocks.size() + static_cast<std::size_t>(repairPassLimit), repairPass,
                "selective repair passed the strict audible gate"});
            error.clear();
            return bestPlan;
        }
    }
    if (SelectiveRepair::editoriallyAcceptable(initialReport, bestReport, completedRepairs)) {
        OperationalJournal::write("OK", "GATE", "editorially accepted after measurable improvement | " +
            audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
            blocks.size() + static_cast<std::size_t>(repairPassLimit),
            static_cast<int>(completedRepairs),
            "musically improved candidate accepted with non-critical editorial observations"});
        error.clear();
        return bestPlan;
    }

    if (SelectiveRepair::safeWithEditorialObservations(bestReport)) {
        OperationalJournal::write("OK", "GATE",
            "safe score published with non-critical editorial observations; optional repair was incomplete | " +
            audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
            blocks.size() + 2, static_cast<int>(completedRepairs),
            "safe composition accepted; remaining findings are editorial only"});
        error.clear();
        return bestPlan;
    }

    if (localEditorial &&
        EditorialSafety::technicallySafeAiScore(bestPattern, bestReport)) {
        OperationalJournal::write("WARN", "GATE",
            "local editorial preview published with technically safe AI-authored MIDI; "
            "remaining narrative, soundscape and density findings require human audition | " +
            audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
            blocks.size() + 2, static_cast<int>(completedRepairs),
            "technically safe AI composition ready for human editorial audition"});
        error.clear();
        return bestPlan;
    }

    if (SelectiveRepair::criticalFailure(bestReport)) {
        error = "AI composition remained critically incomplete after bounded repair: " +
            terminalReason + " | " + audibleAuditSummary(bestReport);
        const auto auditFile = OperationalJournal::writeRejectedAudit(
            bestPlan, bestReport, error, completedRepairs, "terminal_audible_gate");
        OperationalJournal::write("ERROR", "GATE",
            "critical audible score rejected; current composition preserved | audit=" +
            auditFile.getFullPathName());
        return {};
    }

    OperationalJournal::write("WARN", "GATE",
        "bounded editorial repair remained incomplete; publishing the safest checkpoint | " +
        terminalReason + " | " + audibleAuditSummary(bestReport));
    if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
        blocks.size() + 2, static_cast<int>(completedRepairs),
        "complete composition preserved; remaining findings are editorial"});
    error.clear();
    return bestPlan;
}

#if 0
    // Legacy monolithic critic retained below for source-level comparison only. The
    // incremental path above is now the sole production path and returns before it.
    GenerationContext auditFoundation;
    auditFoundation.role = Role::Ensemble;
    auditFoundation.rootPitchClass = result.rootPitchClass;
    auditFoundation.scale = result.scale;
    auditFoundation.beatsPerBar = result.beatsPerBar;
    auditFoundation.seed = result.seed;
    auditFoundation.humanize = 0.0;
    CompositionRenderReport draftReport;
    [[maybe_unused]] const auto auditedDraft = SongComposer{}.render(
        result, auditFoundation, {}, &draftReport);
    juce::String auditSummary;
    auto authoredNotes = std::size_t{};
    std::set<std::uint64_t> performanceFingerprints;
    auto duplicateCells = std::size_t{};
    auto verbatimRepeatIterations = std::size_t{};
    auto longestVerbatimRun = 0;
    auto mappedDialoguePlacements = std::size_t{};
    auto transformedPlacements = std::size_t{};
    std::set<VoiceId> dialogueVoices;
    for (const auto& cell : result.performanceScore.cells) {
        authoredNotes += cell.notes.size();
        if (!performanceFingerprints.insert(PerformanceScoreEngine::fingerprint(cell)).second)
            ++duplicateCells;
    }
    for (const auto& placement : result.performanceScore.placements) {
        verbatimRepeatIterations += static_cast<std::size_t>(std::max(0, placement.repeats - 1));
        longestVerbatimRun = std::max(longestVerbatimRun, placement.repeats);
        if (!placement.voiceMap.empty()) {
            ++mappedDialoguePlacements;
            for (const auto& mapping : placement.voiceMap) {
                dialogueVoices.insert(mapping.from);
                dialogueVoices.insert(mapping.to);
            }
        }
        if (placement.retrograde || placement.invertContour ||
            std::abs(placement.timeScale - 1.0) > 0.001 ||
            placement.fragmentStart > 0.001)
            ++transformedPlacements;
    }
    auditSummary << "\nDeterministic MIDI render audit (the critic must reduce these causes, not merely rename them):\n"
                 << "harmonic_windows=" << static_cast<int>(draftReport.harmonicWindows)
                 << ", pitched_notes=" << draftReport.finalTonalPass.before.pitchedNotes
                 << ", unsupported_chromatic=" << draftReport.finalTonalPass.before.unsupportedChromaticNotes
                 << ", strong_non_chord=" << draftReport.finalTonalPass.before.strongNonChordNotes
                 << ", invalid_sustains=" << draftReport.finalTonalPass.before.invalidSustains
                 << ", unintended_harsh_overlaps=" << draftReport.finalTonalPass.before.unintendedHarshOverlaps
                 << ", intentional_colours=" << draftReport.finalTonalPass.before.intentionalClusters
                 << ", performance_boundary_trims=" << draftReport.finalTonalPass.exactBoundaryTrims
                 << ", voicing_retunes=" << draftReport.finalTonalPass.notesRetunedForVoicing
                 << ", removed_notes=" << draftReport.finalTonalPass.notesRemoved
                 << ", post_repair_unresolved="
                 << draftReport.finalTonalPass.after.unintendedHarshOverlaps
                 << ", orchestral_parts=" << static_cast<int>(draftReport.orchestration.parts)
                 << ", rhythm_parts=" << static_cast<int>(draftReport.orchestration.rhythmParts)
                 << ", harmony_parts=" << static_cast<int>(draftReport.orchestration.harmonyParts)
                 << ", melody_parts=" << static_cast<int>(draftReport.orchestration.melodyParts)
                 << ", foreground_changes=" << static_cast<int>(draftReport.orchestration.foregroundChanges)
                 << ", restrained_doublings=" << static_cast<int>(draftReport.orchestration.notesDoubled)
                 << ", chamber_sections=" << static_cast<int>(draftReport.orchestration.chamberSections)
                 << ", tutti_sections=" << static_cast<int>(draftReport.orchestration.tuttiSections)
                 << ", musical_quality=" << juce::String(draftReport.musical.overall, 3)
                 << ", narrative_score=" << juce::String(draftReport.narrative.score, 3)
                 << ", ai_authored_note_ratio="
                 << juce::String(draftReport.narrative.aiAuthoredNoteRatio, 3)
                 << ", primary_voice_authorship_coverage="
                 << juce::String(draftReport.narrative.primaryVoiceCoverage, 3)
                 << ", foreground_ai_authorship_ratio="
                 << juce::String(draftReport.narrative.foregroundAiAuthorshipRatio, 3)
                 << ", movement_bass_ai_authorship_ratio="
                 << juce::String(draftReport.narrative.movementBassAiAuthorshipRatio, 3)
                 << ", groove_authorship_coverage="
                 << juce::String(draftReport.narrative.grooveAuthorshipCoverage, 3)
                 << ", narrative_thematic_recall="
                 << juce::String(draftReport.narrative.thematicRecallRatio, 3)
                 << ", audible_thematic_similarity="
                 << juce::String(draftReport.narrative.audibleThematicSimilarity, 3)
                 << ", bass_phrase_continuity="
                 << juce::String(draftReport.narrative.bassPhraseContinuity, 3)
                 << ", melodic_stepwise_ratio="
                 << juce::String(draftReport.narrative.melodicStepwiseRatio, 3)
                 << ", maximum_melodic_step_run="
                 << static_cast<int>(draftReport.narrative.maximumMelodicStepRun)
                 << ", maximum_club_drum_gap_bars="
                 << static_cast<int>(draftReport.narrative.maximumClubDrumGapBars)
                 << ", maximum_club_low_end_gap_bars="
                 << static_cast<int>(draftReport.narrative.maximumClubLowEndGapBars)
                 << ", density_control="
                 << juce::String(draftReport.narrative.densityControl, 3)
                 << ", peak_active_voices="
                 << static_cast<int>(draftReport.narrative.peakActiveVoices)
                 << ", overcrowded_bars="
                 << static_cast<int>(draftReport.narrative.overcrowdedBars)
                 << ", harmonic_direction="
                 << juce::String(draftReport.narrative.harmonicDirection, 3)
                 << ", rhythmic_development="
                 << juce::String(draftReport.narrative.rhythmicDevelopment, 3)
                 << ", causal_narrative="
                 << juce::String(draftReport.narrative.causalNarrative, 3)
                 << ", resolution_score="
                 << juce::String(draftReport.narrative.resolutionScore, 3)
                 << ", narrative_spine_ready="
                 << (draftReport.narrative.narrativeSpineReady ? "true" : "false")
                 << ", repeated_rendered_bars=" << static_cast<int>(draftReport.musical.repeatedBars)
                 << ", literal_rhythm_bars_varied="
                 << static_cast<int>(draftReport.musical.literalRhythmBarsVaried)
                 << ", longest_global_silence_beats_before="
                 << juce::String(draftReport.longestGlobalSilenceBefore, 2)
                 << ", longest_global_silence_beats_after="
                 << juce::String(draftReport.longestGlobalSilenceAfter, 2)
                 << ", accidental_silence_windows_repaired="
                 << static_cast<int>(draftReport.unintendedSilenceWindowsRepaired)
                 << ", authored_cells=" << static_cast<int>(result.performanceScore.cells.size())
                 << ", authored_notes=" << static_cast<int>(authoredNotes)
                 << ", duplicate_authored_cells=" << static_cast<int>(duplicateCells)
                 << ", verbatim_repeat_iterations=" << static_cast<int>(verbatimRepeatIterations)
                 << ", longest_verbatim_run=" << longestVerbatimRun << ".\n";
    auditSummary << "arrangement_target_parts=" << static_cast<int>(draftReport.arrangementDensity.targets.proposedParts)
                 << ", arrangement_populated_parts=" << static_cast<int>(draftReport.arrangementDensity.populatedParts)
                 << ", arrangement_harmony_parts=" << static_cast<int>(draftReport.arrangementDensity.harmonyParts)
                 << ", arrangement_melody_parts=" << static_cast<int>(draftReport.arrangementDensity.melodyParts)
                 << ", arrangement_texture_parts=" << static_cast<int>(draftReport.arrangementDensity.textureParts)
                 << ", arrangement_peak_parts=" << static_cast<int>(draftReport.arrangementDensity.peakSimultaneousParts)
                 << ", arrangement_underfilled_sections=" << static_cast<int>(draftReport.arrangementDensity.underfilledSections)
                 << ", arrangement_section_coverage=" << juce::String(draftReport.arrangementDensity.sectionCoverage, 3)
                 << ", arrangement_role_coverage=" << juce::String(draftReport.arrangementDensity.roleCoverage, 3)
                 << ", arrangement_independence=" << juce::String(draftReport.arrangementDensity.independenceScore, 3)
                  << ", arrangement_ready=" << (draftReport.arrangementDensity.ready ? "true" : "false") << ".\n";
    auditSummary << "perceptual_load="
                 << juce::String(draftReport.attention.averagePerceptualLoadBefore, 2) << "->"
                 << juce::String(draftReport.attention.averagePerceptualLoadAfter, 2)
                 << ", perceptual_peak=" << juce::String(draftReport.attention.peakPerceptualLoadAfter, 2)
                 << ", underfilled_bars=" << static_cast<int>(draftReport.attention.underfilledBarsBefore)
                 << "->" << static_cast<int>(draftReport.attention.underfilledBarsAfter)
                 << ", overloaded_bars=" << static_cast<int>(draftReport.attention.overloadedBarsBefore)
                 << "->" << static_cast<int>(draftReport.attention.overloadedBarsAfter)
                 << ", density_notes_removed=" << static_cast<int>(draftReport.attention.densityNotesRemoved)
                 << ", semantic_notes_removed=" << static_cast<int>(draftReport.attention.semanticNotesRemoved)
                 << ", authored_notes_preserved=" << static_cast<int>(draftReport.attention.authoredNotesPreserved)
                 << ".\n";
    auditSummary << "soundscape_active=" << (draftReport.soundscape.active ? "true" : "false")
                 << ", soundscape_percussion_free=" << (draftReport.soundscape.percussionFree ? "true" : "false")
                 << ", soundscape_declared_layers=" << static_cast<int>(draftReport.soundscape.declaredLayers)
                 << ", soundscape_materialized_layers=" << static_cast<int>(draftReport.soundscape.materializedLayers)
                 << ", soundscape_meaningful_layers=" << static_cast<int>(draftReport.soundscape.meaningfulLayers)
                 << ", soundscape_underdeveloped_voices=" << static_cast<int>(draftReport.soundscape.underdevelopedVoices)
                 << ", soundscape_underdeveloped_environments=" << static_cast<int>(draftReport.soundscape.underdevelopedEnvironments)
                 << ", soundscape_missing_transitions=" << static_cast<int>(draftReport.soundscape.missingTransitionEvents)
                 << ", soundscape_static_runs=" << static_cast<int>(draftReport.soundscape.staticLayerRuns)
                 << ", soundscape_median_active_layers=" << juce::String(draftReport.soundscape.medianActiveLayers, 2)
                 << ", soundscape_score=" << juce::String(draftReport.soundscape.score, 3)
                 << ", soundscape_ready=" << (draftReport.soundscape.ready ? "true" : "false") << ".\n";
    auditSummary << "track_viability_declared=" << static_cast<int>(draftReport.trackViability.declaredTracks)
                 << ", track_viability_retained=" << static_cast<int>(draftReport.trackViability.retainedTracks)
                 << ", track_viability_viable=" << static_cast<int>(draftReport.trackViability.viableTracks)
                 << ", track_viability_tokens=" << static_cast<int>(draftReport.trackViability.tokenTracks)
                 << ", track_viability_developed=" << static_cast<int>(draftReport.trackViability.developedTracks)
                 << ", track_viability_merged=" << static_cast<int>(draftReport.trackViability.mergedTracks)
                 << ", track_viability_pruned=" << static_cast<int>(draftReport.trackViability.prunedTracks)
                 << ", track_viability_score=" << juce::String(draftReport.trackViability.score, 3)
                 << ", track_viability_ready=" << (draftReport.trackViability.ready ? "true" : "false") << ".\n";
    auditSummary << "content_lanes=" << static_cast<int>(draftReport.timbralHandoffs.contentLanes)
                 << ", timbral_destinations="
                 << static_cast<int>(draftReport.timbralHandoffs.timbralDestinations)
                 << ", timbral_handoff_windows="
                 << static_cast<int>(draftReport.timbralHandoffs.phraseWindowsReassigned)
                 << ", timbral_handoff_notes="
                 << static_cast<int>(draftReport.timbralHandoffs.notesReassigned)
                 << ", exact_cast_published="
                 << (draftReport.timbralHandoffs.exactCast ? "true" : "false") << ".\n";
    auditSummary << "thematic_voice_mappings=" << static_cast<int>(mappedDialoguePlacements)
                 << ", transformed_placements=" << static_cast<int>(transformedPlacements)
                 << ", dialogue_voices=" << static_cast<int>(dialogueVoices.size())
                 << ", audible_dialogue_lines="
                 << static_cast<int>(draftReport.electronicFabric.dialogueLines)
                 << ", production_ready=" << (draftReport.production.ready ? "true" : "false")
                 << ", production_score=" << juce::String(draftReport.production.score, 3)
                 << ", metric_violations=" << static_cast<int>(draftReport.production.metricViolations)
                 << ", expression_events_per_note="
                 << juce::String(draftReport.production.expressionEventsPerNote, 2)
                 << ", literal_rhythm_bars=" << static_cast<int>(draftReport.production.literalRhythmBars)
                 << ", maximum_rhythm_run=" << static_cast<int>(draftReport.production.maximumRhythmRun)
                 << ", thematic_windows="
                 << static_cast<int>(draftReport.electronicProduction.thematicWindows)
                 << ", recurring_thematic_windows="
                 << static_cast<int>(draftReport.electronicProduction.recurringThematicWindows)
                 << ", thematic_recurrence_ratio="
                 << juce::String(draftReport.electronicProduction.thematicRecurrenceRatio, 3)
                 << ", sparse_structural_windows_repaired="
                 << static_cast<int>(draftReport.sparseStructuralWindowsRepaired)
                 << ", structural_continuity_notes_created="
                 << static_cast<int>(draftReport.structuralContinuityNotesCreated)
                 << ", extended_foreground_windows_repaired="
                 << static_cast<int>(draftReport.extendedForegroundWindowsRepaired)
                 << ", foreground_continuity_notes_created="
                 << static_cast<int>(draftReport.foregroundContinuityNotesCreated)
                 << ", early_rhythm_notes_created="
                 << static_cast<int>(draftReport.earlyRhythmNotesCreated)
                 << ", audible_duration_repairs="
                 << static_cast<int>(draftReport.audibleDurationRepairs)
                 << ", inaudible_notes_removed="
                 << static_cast<int>(draftReport.inaudibleNotesRemoved)
                 << ", groove_phrase_pairs="
                 << static_cast<int>(draftReport.musicalIdentity.groovePhrasePairs)
                 << ", groove_recall_ratio="
                 << juce::String(draftReport.musicalIdentity.grooveRecallRatio, 3)
                 << ", groove_phrase_developments="
                 << static_cast<int>(draftReport.musicalIdentity.groovePhraseDevelopments)
                 << ", groove_development_notes="
                 << static_cast<int>(draftReport.musicalIdentity.grooveDevelopmentNotes)
                 << ", response_phrases="
                 << static_cast<int>(draftReport.musicalIdentity.responsePhrases)
                 << ", response_parts="
                 << static_cast<int>(draftReport.musicalIdentity.responseParts)
                 << ", response_lineage_ratio="
                 << juce::String(draftReport.musicalIdentity.responseLineageRatio, 3)
                 << ", transition_notes_before="
                 << static_cast<int>(draftReport.musicalIdentity.transitionNotesBefore)
                 << ", transition_notes_after="
                 << static_cast<int>(draftReport.musicalIdentity.transitionNotesAfter)
                 << ", percussion_durations_authored="
                 << static_cast<int>(draftReport.musicalIdentity.percussionDurationsAuthored)
                 << ", expression_events_before="
                 << static_cast<int>(draftReport.expression.controlsBefore +
                                     draftReport.expression.expressionsBefore)
                 << ", expression_events_after="
                 << static_cast<int>(draftReport.expression.controlsAfter +
                                     draftReport.expression.expressionsAfter)
                 << ", low_vertical_collisions_before="
                 << static_cast<int>(draftReport.verticalHarmony.collisionsBefore)
                 << ", low_vertical_collisions_after="
                 << static_cast<int>(draftReport.verticalHarmony.collisionsAfter)
                 << ", support_notes_ducked="
                 << static_cast<int>(draftReport.verticalHarmony.supportNotesDucked)
                 << ", implicit_voices_pruned="
                 << static_cast<int>(draftReport.orchestration.implicitVoicesPruned)
                 << ", implicit_performance_notes_pruned="
                 << static_cast<int>(draftReport.orchestration.implicitPerformanceNotesPruned)
                 << ", production_low_vertical_clashes="
                 << static_cast<int>(draftReport.production.lowRegisterVerticalClashes)
                 << ", production_implicit_cast_parts="
                 << static_cast<int>(draftReport.production.implicitCastParts)
                 << ", production_longest_global_silence_beats="
                 << juce::String(draftReport.production.longestGlobalSilenceBeats, 2)
                 << ", maximum_kickless_bars_before="
                 << static_cast<int>(draftReport.electronicProduction.maximumKicklessBarsBefore)
                 << ", maximum_kickless_bars_after="
                 << static_cast<int>(draftReport.electronicProduction.maximumKicklessBarsAfter)
                 << ", maximum_low_end_gap_bars_after="
                 << static_cast<int>(draftReport.electronicProduction.maximumLowEndGapBarsAfter)
                 << ", publication_kick_bars_repaired="
                 << static_cast<int>(draftReport.electronicProduction.publicationKickBarsRepaired)
                 << ", low_end_continuity_bars_repaired="
                 << static_cast<int>(draftReport.electronicProduction.lowEndContinuityBarsRepaired)
                 << ", procedural_scalar_notes_removed="
                 << static_cast<int>(draftReport.electronicProduction.proceduralScalarNotesRemoved)
                 << ", creative_authority_foreground_removed="
                 << static_cast<int>(draftReport.creativeAuthority.foregroundFallbackNotesRemoved)
                 << ", creative_authority_bass_removed="
                 << static_cast<int>(draftReport.creativeAuthority.movementBassFallbackNotesRemoved)
                 << ", creative_authority_harmony_removed="
                 << static_cast<int>(draftReport.creativeAuthority.ownedHarmonyProceduralNotesRemoved)
                 << ", creative_authority_groove_removed="
                 << static_cast<int>(draftReport.creativeAuthority.ownedGrooveProceduralNotesRemoved)
                 << ", macro_kick_anchor_bars_created="
                 << static_cast<int>(draftReport.electronicProduction.macroKickAnchorBarsCreated)
                 << ", bass_phrase_developments_created="
                 << static_cast<int>(draftReport.electronicProduction.bassPhraseDevelopmentsCreated)
                 << ", bass_notes_developed="
                 << static_cast<int>(draftReport.electronicProduction.bassNotesDeveloped)
                 << ", late_percussion_articulation_repairs="
                 << static_cast<int>(draftReport.electronicProduction.latePercussionArticulationRepairs) << ".\n";
    if (!draftReport.finalTonalPass.before.issues.empty()) {
        auditSummary << "Representative exact-timeline issues:\n";
        for (const auto& issue : draftReport.finalTonalPass.before.issues) {
            auditSummary << "- beat " << juce::String(issue.beat, 3) << ", "
                         << juce::String(issue.kind) << ", voice="
                         << juce::String(voiceDefinition(issue.voice).key.data())
                         << ", pitch=" << issue.pitch;
            if (issue.otherVoice != VoiceId::Unspecified)
                auditSummary << ", against="
                             << juce::String(voiceDefinition(issue.otherVoice).key.data())
                             << ":" << issue.otherPitch;
            auditSummary << "\n";
        }
    }

    const auto elapsed = std::chrono::steady_clock::now() - aiStarted;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(totalAiBudget - elapsed);
    if (remaining < std::chrono::seconds(3)) {
        applyExplicitRhythmRequest(result, direction);
        SongComposer::normalizePlan(result);
        return result;
    }
    if (progress) progress({AiSongStage::Validation, 0, 1, 1, "legacy critic"});
    const auto criticPrompt = juce::String(
        "Act as PULSO's independent composer-critic. Return a complete revised plan using the same schema. "
        "Preserve the exact requested bar count, tempo, meter, tonal centre and all explicit user constraints. "
        "Before selecting the revision, silently compare at least three plausible rhythmic and harmonic developments. "
        "Audit tonal narrative, palette identity, inversions, bass motion, structural chromatic logic, harmonic rhythm, "
        "voice-leading continuity, section-level centres, cadence consequence, dance-floor foundation, motif lineage, "
        "kick-bass interlock, meaningful silence, phrase-level cause and effect, orchestral breathing, contrast and "
        "climax. Audit the orchestration as a real score: foreground rotation, playable ranges, family contrast, "
        "chamber-to-tutti development, independent inner voices, restrained doubling and meaningful instrumental rests. "
        "Repair soloist monopoly, fake symphonic density, four-chord cycling, decorative complexity, generic repetition or arbitrary novelty. "
        "Audit electronic_soundscape against rendered MIDI, not track names. Every declared voice must contain developed "
        "phrases; every environment needs sectional evolution; transition layers need their declared boundary events; "
        "one_shot is reserved for genuinely singular FX. Remove token tracks or write their actual trajectory. Match the "
        "declared median fabric through rotating foreground, midground and background responsibilities, without constant tutti. "
        "Treat track_viability_ready=false as a score-level defect. For each weak non-event instrument, author complete "
        "performance_score phrases until its declared active-bar and phrase contract is audible. Preserve intentional rare "
        "transitions and one-shots. If there is no independent musical idea to write, remove the instrument or convert it "
        "to an explicit relay/timbral_handoff instead of leaving one-to-six placeholder notes. Do not split one phrase "
        "across extra tracks to improve the count. "
        "Audit performance_score at note level: cells must differ materially in rhythm, contour, register, duration, "
        "dynamics and ownership; placements must cover the form while preserving real rests. Treat repaired accidental "
        "silence windows, long verbatim runs and repeated rendered bars as concrete defects: replace them with related "
        "question/answer or developmental cells, not cosmetic velocity changes. If thematic_voice_mappings is zero or "
        "dialogue_voices is below three, create meaningful cross-instrument mappings and transformations across separate "
        "sections; do not merely double the same notes. Rewrite only weak cells "
        "and their placements while keeping strong material intact. Every related statement, answer and development "
        "must carry the same theme_id, while narrative_function must express its actual role. Raise "
        "primary_voice_authorship_coverage to at least 0.65, foreground_ai_authorship_ratio to at least 0.85, "
        "movement_bass_ai_authorship_ratio to at least 0.75, groove_authorship_coverage to at least 0.45, "
        "narrative_thematic_recall to at least 0.40, "
        "audible_thematic_similarity into a recognisable but developed 0.66-0.96 band, "
        "literal_thematic_return_ratio below 0.70, thematic_development above 0.55, "
        "density_control to at least 0.82, causal_narrative to at least 0.62 and resolution_score to at least 0.58. "
        "For electronic harmonic architecture, harmonic_floor_coverage must reach 0.85 with at least two interlocking "
        "floor layers. The foreground must recur as phrases, not continuous filler, and the resolution section's last "
        "foreground event must land on tonic with lower register and fewer simultaneous parts than the climax. "
        "When either narrative metric fails, rewrite narrative_spine and the exact performance_score cells that enact its "
        "question, transformation, climax and resolution; changing prose or labels alone is never a repair. "
        "bass_phrase_continuity to at least 0.60, maximum_melodic_step_run to five or less, and harmonic_direction "
        "to at least 0.70. Repair those metrics by "
        "writing and placing musical cells, never by changing theme_id, descriptions or adding arbitrary notes. The "
        "audible audit is transposition-invariant and compares actual onset rhythm and interval contour, so labels cannot "
        "fake lineage. Keep at most nine sounding execution voices in a club bar and no more than two foreground voices. "
        "Use rhythm masks as supporting cells and sparse "
        "mutations as development; do not merely add density. production_ready must become true: strict-grid material "
        "must also leave zero sparse_structural_windows_repaired and establish a thematic_recurrence_ratio of at least "
        "0.20 whenever six or more thematic windows exist; repair those causes in the score rather than changing labels. "
        "It must also leave zero extended_foreground_windows_repaired, zero audible_duration_repairs and zero "
        "inaudible_notes_removed: distribute foreground ownership in the authored score and write playable gates directly. "
        "A transition role must contain only boundary arrivals, reverses or impacts, never a continuous percussion pattern. "
        "Every performed voice must have an explicit compatible instrument owner: leave implicit_voices_pruned, "
        "implicit_performance_notes_pruned and production_implicit_cast_parts at zero. Repair the authored cast instead "
        "of accepting an invented generic orchestral lane. Leave low_vertical_collisions_after and "
        "production_low_vertical_clashes at zero; prefer correct voicing and intentional support rests over relying on "
        "the deterministic safety duck. Track names and live_preset_intent descriptors must be mutually consistent. "
        "For club_electronic leave maximum_kickless_bars_after and maximum_low_end_gap_bars_after at twelve or less "
        "only when percussion_free is false. A percussion-free plan must instead pass the soundscape contract and must "
        "never reintroduce drums, percussion or a kick merely to satisfy club metrics. "
        "unless the form explicitly declares "
        "full silence, and leave late_percussion_articulation_repairs at zero by writing concrete GM identities directly. "
        "A movement-bass phrase may repeat exactly to establish identity, but the third occurrence must develop its last "
        "two bars through a pickup, subtraction, gate change or answer that preserves kick interlock and tonal function. "
        "Preserve one onset skeleton for two adjacent groove phrases before changing it; reserve fills and mutations for "
        "the final two bars. Every response phrase must audibly derive its interval contour from the primary hook. "
        "Strict-grid material must be exact, every metric exception must be declared, consolidated tonality must contain zero external "
        "notes, and expressive controls must be phrase-local rather than continuous through silence. The final "
        "JSON must be self-contained. Original creative direction: ") + direction +
        auditSummary + "\nCandidate plan to critique and revise:\n" + outputText;
    if (const auto revisedText = requestRevisedSongPlan(criticPrompt, apiKey, token,
            std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(7))));
        revisedText.isNotEmpty()) {
        SongPlan revised;
        juce::String criticError;
        if (parseSongPlanJson(revisedText, targetSeconds, totalBars, bpm, beatsPerBar,
                              seed, revised, criticError,
                              tonalPolicyForDirection(direction.toStdString()))) {
            revised.compositionBehavior = behavior;
            revised.aiSovereign = aiSovereign;
            applyExplicitRhythmRequest(revised, direction);
            SongComposer::normalizePlan(revised);
            GenerationContext revisedFoundation = auditFoundation;
            revisedFoundation.rootPitchClass = revised.rootPitchClass;
            revisedFoundation.scale = revised.scale;
            CompositionRenderReport revisedReport;
            [[maybe_unused]] const auto auditedRevision = SongComposer{}.render(
                revised, revisedFoundation, {}, &revisedReport);
            const auto longFormDialogueTarget = totalBars >= 96 && result.instruments.size() >= 12
                ? std::size_t{3} : std::size_t{1};
            const auto meetsNarrativeTarget = [longFormDialogueTarget](const CompositionRenderReport& report) {
                const auto memoryReady = report.narrative.audibleThematicWindows < 3 ||
                    (report.narrative.thematicRecallRatio >= 0.40 &&
                     report.narrative.audibleThematicSimilarity >= 0.66);
                const auto bassReady = report.narrative.bassPhrases < 4 ||
                    report.narrative.bassPhraseContinuity >= 0.60;
                const auto developmentReady = report.narrative.comparableThematicReturns < 4 ||
                    (report.narrative.literalThematicReturnRatio <= 0.70 &&
                     report.narrative.thematicDevelopment >= 0.55);
                const auto electronicReady = !report.electronicProduction.active ||
                    (report.electronicProduction.score >= 0.68 &&
                     report.electronicProduction.maximumRhythmRun <= 6 &&
                     (report.electronicProduction.percussionFree ||
                      (report.electronicProduction.maximumKicklessBarsAfter <= 12 &&
                       report.electronicProduction.maximumLowEndGapBarsAfter <= 12)));
                const auto audibleAuthorship =
                    (!report.narrative.foregroundExpected ||
                     (report.narrative.foregroundNotes >= 8 &&
                      report.narrative.foregroundAiAuthorshipRatio >= 0.85)) &&
                    (!report.narrative.movementBassExpected ||
                     (report.narrative.movementBassNotes >= 8 &&
                      report.narrative.movementBassAiAuthorshipRatio >= 0.75)) &&
                    (!report.electronicProduction.active ||
                     report.narrative.grooveAuthorshipCoverage >= 0.45);
                const auto melodicSpeech = report.narrative.melodicIntervals < 8 ||
                    (report.narrative.melodicStepwiseRatio >= 0.25 &&
                     report.narrative.melodicStepwiseRatio <= 0.68 &&
                     report.narrative.maximumMelodicStepRun <= 5);
                return report.production.ready && report.narrative.creativeReady &&
                    (!report.soundscape.active || report.soundscape.ready) &&
                    report.trackViability.ready &&
                    report.narrative.primaryVoiceCoverage >= 0.65 && audibleAuthorship &&
                    memoryReady && bassReady && developmentReady && electronicReady && melodicSpeech &&
                    report.narrative.resolutionScore >= 0.58 &&
                    report.electronicFabric.dialogueLines >= longFormDialogueTarget &&
                    report.narrative.densityControl >= 0.82 &&
                    report.narrative.maximumMelodicStepRun <= 5;
            };
            const auto targetDeficit = [longFormDialogueTarget](const CompositionRenderReport& report) {
                auto deficit = std::max(0.0, 0.65 - report.narrative.primaryVoiceCoverage) * 1.8;
                if (report.narrative.foregroundNotes >= 8)
                    deficit += std::max(0.0, 0.85 - report.narrative.foregroundAiAuthorshipRatio) * 1.8;
                else if (report.narrative.foregroundExpected)
                    deficit += 1.4;
                if (report.narrative.movementBassNotes >= 8)
                    deficit += std::max(0.0, 0.75 - report.narrative.movementBassAiAuthorshipRatio) * 1.7;
                else if (report.narrative.movementBassExpected)
                    deficit += 1.2;
                if (report.electronicProduction.active)
                    deficit += std::max(0.0, 0.45 - report.narrative.grooveAuthorshipCoverage) * 1.3;
                if (report.narrative.bassPhrases >= 4)
                    deficit += std::max(0.0, 0.60 - report.narrative.bassPhraseContinuity) * 1.25;
                if (report.narrative.audibleThematicWindows >= 3) {
                    deficit += std::max(0.0, 0.40 - report.narrative.thematicRecallRatio);
                    deficit += std::max(0.0, 0.66 - report.narrative.audibleThematicSimilarity);
                }
                if (report.narrative.comparableThematicReturns >= 4) {
                    deficit += std::max(0.0,
                        report.narrative.literalThematicReturnRatio - 0.70);
                    deficit += std::max(0.0, 0.55 - report.narrative.thematicDevelopment);
                }
                deficit += std::max(0.0, 0.82 - report.narrative.densityControl);
                deficit += std::max(0.0, 0.62 - report.narrative.causalNarrative) * 1.4;
                deficit += std::max(0.0, 0.58 - report.narrative.resolutionScore) * 1.4;
                if (report.narrative.melodicIntervals >= 8) {
                    deficit += std::max(0.0,
                        0.25 - report.narrative.melodicStepwiseRatio) * 1.6;
                    deficit += std::max(0.0,
                        report.narrative.melodicStepwiseRatio - 0.68) * 1.2;
                }
                deficit += static_cast<double>(longFormDialogueTarget - std::min(
                    longFormDialogueTarget, report.electronicFabric.dialogueLines)) * 0.35;
                deficit += std::max(0.0, 0.90 - report.trackViability.score) * 1.8;
                if (!report.trackViability.ready) deficit += .8;
                deficit += std::max(0.0,
                    static_cast<double>(report.narrative.maximumMelodicStepRun) - 5.0) * 0.08;
                if (report.electronicProduction.active) {
                    deficit += std::max(0.0, 0.68 - report.electronicProduction.score);
                    deficit += std::max(0.0,
                        static_cast<double>(report.electronicProduction.maximumRhythmRun) - 6.0) * 0.03;
                    if (!report.electronicProduction.percussionFree) {
                        deficit += std::max(0.0,
                            static_cast<double>(report.electronicProduction.maximumKicklessBarsAfter) - 12.0) * 0.04;
                        deficit += std::max(0.0,
                            static_cast<double>(report.electronicProduction.maximumLowEndGapBarsAfter) - 12.0) * 0.035;
                    }
                }
                if (report.soundscape.active)
                    deficit += std::max(0.0, 0.78 - report.soundscape.meaningfulCoverage) * 1.6 +
                        std::max(0.0, 0.75 - report.soundscape.score);
                return deficit;
            };
            const auto candidateQuality = [](const CompositionRenderReport& report) {
                const auto electronic = report.electronicProduction.active
                    ? report.electronicProduction.score : report.production.score;
                return report.narrative.score * 0.38 + electronic * 0.20 +
                    report.musical.overall * 0.15 + report.production.score * 0.09 +
                    report.trackViability.score * 0.18;
            };

            auto bestRevision = std::move(revised);
            auto bestReport = revisedReport;
            auto bestRevisionText = revisedText;
            for (auto repairPass = 0; repairPass < 2 && !meetsNarrativeTarget(bestReport); ++repairPass) {
                const auto elapsedForRepair = std::chrono::steady_clock::now() - aiStarted;
                const auto remainingForRepair = std::chrono::duration_cast<std::chrono::milliseconds>(
                    totalAiBudget - elapsedForRepair);
                if (remainingForRepair <= std::chrono::seconds(30)) break;
                const auto focusedPrompt = juce::String(
                    "Return a complete corrected PULSO song plan using the required schema. This is a focused "
                    "note-level repair, not a fresh stylistic rewrite. ") + behaviorBrief +
                    "Preserve the candidate's strong form, harmony, "
                    "cast and successful cells. Rewrite and replace only placements or cells responsible for these "
                    "measured failures. Principal voice coverage must be at least " +
                    juce::String(.65, 2) +
                    ". A recurring theme must preserve "
                    "audible onset rhythm and interval contour, not merely theme_id. Movement bass must form coherent "
                    "four-to-eight-bar pocket phrases with breaths and a developed return. Club bars must contain no "
                    "more than nine sounding execution voices and no more than two foreground owners. Do not add notes "
                    "to raise a score; use ownership, subtraction, recurrence and transformation. Allow a literal theme "
                    "anchor, but develop later returns through fragmentation, displacement, changed cadence or a real "
                    "answer; do not copy the same MIDI cell throughout the arrangement. Replace procedural ownership "
                    "of lead, response, movement bass and harmonic identity with explicit performance cells and "
                    "placements; do not merely add labels or duplicate notes. The rendered foreground AI ratio must "
                    "reach 0.85, movement-bass AI ratio 0.75, groove coverage 0.45, and scalar runs must stop after "
                    "five steps. Club drum and low-end gaps must remain at twelve bars or less only when percussion_free "
                    "is false. Repair every underdeveloped soundscape voice/environment with real phrases and evolution, "
                    "or remove it from both cast and soundscape; never satisfy depth with token tracks. Repair pass ") +
                    juce::String(repairPass + 1) + ". Exact failed metrics: " +
                    "coverage=" + juce::String(bestReport.narrative.primaryVoiceCoverage, 3) +
                    ", recall=" + juce::String(bestReport.narrative.thematicRecallRatio, 3) +
                    ", audible_similarity=" + juce::String(bestReport.narrative.audibleThematicSimilarity, 3) +
                    ", literal_theme_returns=" + juce::String(
                        bestReport.narrative.literalThematicReturnRatio, 3) +
                    ", thematic_development=" + juce::String(
                        bestReport.narrative.thematicDevelopment, 3) +
                    ", bass_continuity=" + juce::String(bestReport.narrative.bassPhraseContinuity, 3) +
                    ", foreground_ai=" + juce::String(bestReport.narrative.foregroundAiAuthorshipRatio, 3) +
                    ", movement_bass_ai=" + juce::String(bestReport.narrative.movementBassAiAuthorshipRatio, 3) +
                    ", groove_coverage=" + juce::String(bestReport.narrative.grooveAuthorshipCoverage, 3) +
                    ", max_scalar_run=" + juce::String(
                        static_cast<int>(bestReport.narrative.maximumMelodicStepRun)) +
                    ", melodic_stepwise_ratio=" +
                        juce::String(bestReport.narrative.melodicStepwiseRatio, 3) +
                    ", resolution_score=" +
                        juce::String(bestReport.narrative.resolutionScore, 3) +
                    ", audible_dialogue_lines=" +
                        juce::String(static_cast<int>(bestReport.electronicFabric.dialogueLines)) +
                    ", max_drum_gap=" + juce::String(
                        static_cast<int>(bestReport.narrative.maximumClubDrumGapBars)) +
                    ", max_low_end_gap=" + juce::String(
                        static_cast<int>(bestReport.narrative.maximumClubLowEndGapBars)) +
                    ", density_control=" + juce::String(bestReport.narrative.densityControl, 3) +
                    ", peak_voices=" + juce::String(static_cast<int>(bestReport.narrative.peakActiveVoices)) +
                    ", soundscape_coverage=" + juce::String(bestReport.soundscape.meaningfulCoverage, 3) +
                    ", soundscape_median_layers=" + juce::String(bestReport.soundscape.medianActiveLayers, 2) +
                    ". Original direction: " + direction + "\nCandidate to repair:\n" + bestRevisionText;
                if (const auto focusedText = requestRevisedSongPlan(focusedPrompt, apiKey, token,
                        std::min(remainingForRepair,
                            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(5))));
                    focusedText.isNotEmpty()) {
                    SongPlan focused;
                    juce::String focusedError;
                    if (parseSongPlanJson(focusedText, targetSeconds, totalBars, bpm, beatsPerBar,
                                          seed, focused, focusedError,
                                          tonalPolicyForDirection(direction.toStdString()))) {
                        focused.compositionBehavior = behavior;
                        focused.aiSovereign = aiSovereign;
                        applyExplicitRhythmRequest(focused, direction);
                        SongComposer::normalizePlan(focused);
                        auto focusedFoundation = auditFoundation;
                        focusedFoundation.rootPitchClass = focused.rootPitchClass;
                        focusedFoundation.scale = focused.scale;
                        CompositionRenderReport focusedReport;
                        [[maybe_unused]] const auto auditedFocused = SongComposer{}.render(
                            focused, focusedFoundation, {}, &focusedReport);
                        const auto bestQuality = candidateQuality(bestReport);
                        const auto focusedQuality = candidateQuality(focusedReport);
                        const auto bestDeficit = targetDeficit(bestReport);
                        const auto focusedDeficit = targetDeficit(focusedReport);
                        if (focusedReport.production.ready &&
                            ((meetsNarrativeTarget(focusedReport) && !meetsNarrativeTarget(bestReport)) ||
                             focusedDeficit + 0.015 < bestDeficit ||
                             (std::abs(focusedDeficit - bestDeficit) <= 0.015 &&
                              focusedQuality > bestQuality + 0.01))) {
                            bestRevision = std::move(focused);
                            bestReport = focusedReport;
                            bestRevisionText = focusedText;
                        }
                    }
                }
            }
            const auto draftNarrative = candidateQuality(draftReport);
            const auto revisedNarrative = candidateQuality(bestReport);
            const auto draftDeficit = targetDeficit(draftReport);
            const auto revisedDeficit = targetDeficit(bestReport);
            const auto safer = bestReport.production.ready || !draftReport.production.ready;
            if (safer && (revisedDeficit + 0.015 < draftDeficit ||
                          (std::abs(revisedDeficit - draftDeficit) <= 0.015 &&
                           revisedNarrative + 0.015 >= draftNarrative)))
                result = std::move(bestRevision);
        }
    }
    result.compositionBehavior = behavior;
    applyExplicitRhythmRequest(result, direction);
    result.harmonicLanguage.tonalPolicy = tonalPolicyForDirection(direction.toStdString());
    SongComposer::normalizePlan(result);
    // The optional whole-plan critic can replace the previously converged score.
    // Reassert closure at the real return boundary so no accepted revision can
    // silently remove the protagonist recap or terminal harmonic arrival.
    if (!aiSovereign && SelectiveRepair::ensurePrimaryChordBedClosure(result))
        SongComposer::normalizePlan(result);
    if (!aiSovereign && SelectiveRepair::ensureAuthoredProtagonistCoda(
            result, result.performanceScore)) {
        SongComposer::normalizePlan(result);
        OperationalJournal::write("OK", "CHECKPOINT",
            "final revised score retained a verified authored protagonist recap at publication");
    }
    return result;
}
#endif

bool AiComposer::parseSongPlanJson(const juce::String& text, int targetSeconds,
                                   int requestedBars, double bpm, double beatsPerBar,
                                   std::uint64_t seed, SongPlan& result,
                                   juce::String& error, TonalPolicy tonalPolicy) {
    const auto parsed = juce::JSON::parse(text);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr || requestedBars < 8 || requestedBars > 512) {
        error = "Song-plan JSON is invalid";
        return false;
    }
    result = {};
    result.productionModeSource = "gpt_plan";
    result.harmonicLanguage.tonalPolicy = tonalPolicy;
    result.title = object->getProperty("title").toString().trim().toStdString();
    result.key = object->getProperty("key").toString().trim().toStdString();
    result.summary = object->getProperty("summary").toString().trim().toStdString();
    const auto savedBehavior = object->getProperty("composition_behavior").toString();
    result.compositionBehavior = savedBehavior == "hypnotic" ? CompositionBehavior::Hypnotic :
        savedBehavior == "narrative" ? CompositionBehavior::Narrative :
        CompositionBehavior::Adaptive;
    result.targetSeconds = targetSeconds;
    result.totalBars = requestedBars;
    result.bpm = bpm;
    result.beatsPerBar = beatsPerBar;
    result.seed = seed;
    result.rootPitchClass = static_cast<int>(object->getProperty("root_pitch_class"));
    const auto mode = object->getProperty("mode").toString();
    result.scale = mode == "major" ? ScaleKind::Major : mode == "dorian" ? ScaleKind::Dorian
                 : mode == "mixolydian" ? ScaleKind::Mixolydian : ScaleKind::Minor;
    if (const auto* spine = object->getProperty("narrative_spine").getDynamicObject()) {
        result.narrativeSpine.authored = true;
        result.narrativeSpine.premise = spine->getProperty("premise").toString().trim().toStdString();
        result.narrativeSpine.question = spine->getProperty("question").toString().trim().toStdString();
        result.narrativeSpine.harmonicDebt = spine->getProperty("harmonic_debt").toString().trim().toStdString();
        result.narrativeSpine.protagonistInstrumentId = spine->getProperty("protagonist_instrument_id").toString().trim().toStdString();
        result.narrativeSpine.motifIdentity = spine->getProperty("motif_identity").toString().trim().toStdString();
        result.narrativeSpine.climaxConsequence = spine->getProperty("climax_consequence").toString().trim().toStdString();
        result.narrativeSpine.resolution = spine->getProperty("resolution").toString().trim().toStdString();
        if (const auto* acts = spine->getProperty("acts").getArray()) {
            for (const auto& item : *acts) {
                const auto* act = item.getDynamicObject();
                if (act == nullptr) continue;
                NarrativeAct parsedAct;
                parsedAct.sectionName = act->getProperty("section_name").toString().trim().toStdString();
                const auto stage = act->getProperty("stage").toString();
                parsedAct.stage = stage == "premise" ? NarrativeStage::Premise :
                    stage == "question" ? NarrativeStage::Question :
                    stage == "departure" ? NarrativeStage::Departure :
                    stage == "climax" ? NarrativeStage::Climax :
                    stage == "resolution" ? NarrativeStage::Resolution :
                    stage == "aftermath" ? NarrativeStage::Aftermath : NarrativeStage::Transformation;
                parsedAct.cause = act->getProperty("cause").toString().trim().toStdString();
                parsedAct.consequence = act->getProperty("consequence").toString().trim().toStdString();
                parsedAct.unresolvedElement = act->getProperty("unresolved_element").toString().trim().toStdString();
                parsedAct.resolutionTarget = act->getProperty("resolution_target").toString().trim().toStdString();
                parsedAct.tensionTarget = static_cast<double>(act->getProperty("tension_target"));
                parsedAct.resolutionStrength = static_cast<double>(act->getProperty("resolution_strength"));
                result.narrativeSpine.acts.push_back(std::move(parsedAct));
            }
        }
    }
    if (const auto* language = object->getProperty("production_language").getDynamicObject()) {
        const auto source = language->getProperty("source").toString().trim();
        if (source.isNotEmpty()) result.productionModeSource = source.substring(0, 48).toStdString();
        const auto domain = language->getProperty("domain").toString();
        result.productionLanguage.domain = domain == "club_electronic" ? ProductionDomain::ClubElectronic
            : domain == "hybrid" ? ProductionDomain::Hybrid
            : domain == "orchestral" ? ProductionDomain::Orchestral
            : ProductionDomain::Adaptive;
        result.productionLanguage.description = language->getProperty("description").toString().trim().toStdString();
        result.productionLanguage.electronicIntent = static_cast<double>(language->getProperty("electronic_intent"));
        result.productionLanguage.clubFocus = static_cast<double>(language->getProperty("club_focus"));
        result.productionLanguage.lowEndInterlock = static_cast<double>(language->getProperty("low_end_interlock"));
        result.productionLanguage.grooveEvolution = static_cast<double>(language->getProperty("groove_evolution"));
        result.productionLanguage.hookEconomy = static_cast<double>(language->getProperty("hook_economy"));
        result.productionLanguage.automationMotion = static_cast<double>(language->getProperty("automation_motion"));
        result.productionLanguage.djUtility = static_cast<double>(language->getProperty("dj_utility"));
        result.productionLanguage.spectralRestraint = static_cast<double>(language->getProperty("spectral_restraint"));
        result.productionLanguage.orchestralAllowance = static_cast<double>(language->getProperty("orchestral_allowance"));
    }
    if (const auto* language = object->getProperty("rhythm_language").getDynamicObject()) {
        result.rhythmLanguage.description = language->getProperty("description").toString().trim().toStdString();
        result.rhythmLanguage.pulseStability = static_cast<double>(language->getProperty("pulse_stability"));
        result.rhythmLanguage.backbeatGravity = static_cast<double>(language->getProperty("backbeat_gravity"));
        result.rhythmLanguage.syncopation = static_cast<double>(language->getProperty("syncopation"));
        result.rhythmLanguage.ghostDensity = static_cast<double>(language->getProperty("ghost_density"));
        result.rhythmLanguage.velocityContrast = static_cast<double>(language->getProperty("velocity_contrast"));
        result.rhythmLanguage.timingFreedom = static_cast<double>(language->getProperty("timing_freedom"));
        result.rhythmLanguage.orchestrationMotion = static_cast<double>(language->getProperty("orchestration_motion"));
        result.rhythmLanguage.silenceBias = static_cast<double>(language->getProperty("silence_bias"));
        result.rhythmLanguage.callResponse = static_cast<double>(language->getProperty("call_response"));
    }
    if (const auto* language = object->getProperty("harmonic_language").getDynamicObject()) {
        result.harmonicLanguage.description = language->getProperty("description").toString().trim().toStdString();
        result.harmonicLanguage.tonalGravity = static_cast<double>(language->getProperty("tonal_gravity"));
        result.harmonicLanguage.modalFluidity = static_cast<double>(language->getProperty("modal_fluidity"));
        result.harmonicLanguage.chromaticism = static_cast<double>(language->getProperty("chromaticism"));
        result.harmonicLanguage.extensionRichness = static_cast<double>(language->getProperty("extension_richness"));
        result.harmonicLanguage.inversionMotion = static_cast<double>(language->getProperty("inversion_motion"));
        result.harmonicLanguage.voiceLeadingSmoothness = static_cast<double>(language->getProperty("voice_leading_smoothness"));
        result.harmonicLanguage.harmonicRhythmActivity = static_cast<double>(language->getProperty("harmonic_rhythm_activity"));
        result.harmonicLanguage.pedalToneAffinity = static_cast<double>(language->getProperty("pedal_tone_affinity"));
        result.harmonicLanguage.ambiguity = static_cast<double>(language->getProperty("ambiguity"));
        result.harmonicLanguage.cadenceStrength = static_cast<double>(language->getProperty("cadence_strength"));
    }
    if (const auto* language = object->getProperty("orchestration_language").getDynamicObject()) {
        result.orchestrationLanguage.description = language->getProperty("description").toString().trim().toStdString();
        result.orchestrationLanguage.ensembleScale = static_cast<double>(language->getProperty("ensemble_scale"));
        result.orchestrationLanguage.timbralMotion = static_cast<double>(language->getProperty("timbral_motion"));
        result.orchestrationLanguage.foregroundRotation = static_cast<double>(language->getProperty("foreground_rotation"));
        result.orchestrationLanguage.doublingRestraint = static_cast<double>(language->getProperty("doubling_restraint"));
        result.orchestrationLanguage.registerSeparation = static_cast<double>(language->getProperty("register_separation"));
        result.orchestrationLanguage.chamberContrast = static_cast<double>(language->getProperty("chamber_contrast"));
        result.orchestrationLanguage.tuttiRarity = static_cast<double>(language->getProperty("tutti_rarity"));
        result.orchestrationLanguage.harmonicDepth = static_cast<double>(language->getProperty("harmonic_depth"));
        result.orchestrationLanguage.counterpointActivity = static_cast<double>(language->getProperty("counterpoint_activity"));
        result.orchestrationLanguage.divisiDepth = static_cast<double>(language->getProperty("divisi_depth"));
        result.orchestrationLanguage.articulationContrast = static_cast<double>(language->getProperty("articulation_contrast"));
        result.orchestrationLanguage.familyDialogue = static_cast<double>(language->getProperty("family_dialogue"));
        result.orchestrationLanguage.hybridProduction = static_cast<double>(language->getProperty("hybrid_production"));
    }
    if (const auto* palette = object->getProperty("timbre_palette").getDynamicObject()) {
        result.timbrePalette.description = palette->getProperty("description").toString().trim().toStdString();
        result.timbrePalette.material = palette->getProperty("material").toString().trim().toStdString();
        result.timbrePalette.space = palette->getProperty("space").toString().trim().toStdString();
        result.timbrePalette.warmth = static_cast<double>(palette->getProperty("warmth"));
        result.timbrePalette.brightness = static_cast<double>(palette->getProperty("brightness"));
        result.timbrePalette.transientDefinition = static_cast<double>(palette->getProperty("transient_definition"));
        result.timbrePalette.acousticElectronicBalance = static_cast<double>(palette->getProperty("acoustic_electronic_balance"));
        result.timbrePalette.cohesion = static_cast<double>(palette->getProperty("cohesion"));
        result.timbrePalette.contrast = static_cast<double>(palette->getProperty("contrast"));
    }
    if (const auto* soundscape = object->getProperty("electronic_soundscape").getDynamicObject()) {
        result.soundscape.authored = true;
        result.soundscape.active = static_cast<bool>(soundscape->getProperty("active"));
        result.soundscape.percussionFree = static_cast<bool>(soundscape->getProperty("percussion_free"));
        result.percussionFreeIntent = result.soundscape.percussionFree;
        result.soundscape.scene = soundscape->getProperty("scene").toString().trim().toStdString();
        result.soundscape.spatialNarrative = soundscape->getProperty("spatial_narrative").toString().trim().toStdString();
        result.soundscape.targetMedianActiveLayers = static_cast<double>(
            soundscape->getProperty("target_median_active_layers"));
        if (const auto* layers = soundscape->getProperty("layers").getArray()) {
            for (const auto& item : *layers) {
                const auto* layer = item.getDynamicObject();
                if (layer == nullptr) continue;
                SoundscapeLayerPlan parsedLayer;
                parsedLayer.instrumentId = layer->getProperty("instrument_id").toString().trim().toStdString();
                parsedLayer.kind = soundscapeLayerKindFromKey(
                    layer->getProperty("kind").toString().toStdString());
                parsedLayer.timeScale = soundscapeTimeScaleFromKey(
                    layer->getProperty("time_scale").toString().toStdString());
                parsedLayer.narrativeRole = layer->getProperty("narrative_role").toString().trim().toStdString();
                parsedLayer.relationship = layer->getProperty("relationship").toString().trim().toStdString();
                parsedLayer.evolution = layer->getProperty("evolution").toString().trim().toStdString();
                parsedLayer.minimumActiveBars = static_cast<int>(layer->getProperty("minimum_active_bars"));
                parsedLayer.minimumPhrases = static_cast<int>(layer->getProperty("minimum_phrases"));
                parsedLayer.maximumStaticBars = static_cast<int>(layer->getProperty("maximum_static_bars"));
                parsedLayer.foregroundDepth = static_cast<double>(layer->getProperty("foreground_depth"));
                result.soundscape.layers.push_back(std::move(parsedLayer));
            }
        }
    }
    if (const auto* palette = object->getProperty("chord_palette").getArray()) {
        for (const auto& item : *palette) {
            const auto* chord = item.getDynamicObject();
            if (chord == nullptr) continue;
            HarmonicChord parsedChord;
            parsedChord.id = chord->getProperty("id").toString().trim().toStdString();
            parsedChord.label = chord->getProperty("label").toString().trim().toStdString();
            parsedChord.rootPitchClass = static_cast<int>(chord->getProperty("root_pitch_class"));
            parsedChord.bassPitchClass = static_cast<int>(chord->getProperty("bass_pitch_class"));
            if (const auto* pitchClasses = chord->getProperty("pitch_classes").getArray())
                for (const auto& pitchClass : *pitchClasses)
                    parsedChord.pitchClasses.push_back(static_cast<int>(pitchClass));
            if (const auto function = harmonicFunctionFromKey(
                    chord->getProperty("function").toString().toStdString()))
                parsedChord.function = *function;
            if (const auto voicing = voicingStrategyFromKey(
                    chord->getProperty("voicing").toString().toStdString()))
                parsedChord.voicing = *voicing;
            parsedChord.tension = static_cast<double>(chord->getProperty("tension"));
            result.chordPalette.push_back(std::move(parsedChord));
        }
    }
    if (const auto* motif = object->getProperty("motif_intervals").getArray())
        for (const auto& value : *motif) result.motifIntervals.push_back(static_cast<int>(value));
    if (const auto* instruments = object->getProperty("instruments").getArray()) {
        for (const auto& item : *instruments) {
            const auto* instrument = item.getDynamicObject();
            if (instrument == nullptr) continue;
            const auto sourceVoice = voiceIdFromKey(
                instrument->getProperty("source_voice").toString().toStdString());
            if (!sourceVoice) continue;
            InstrumentAssignment assignment;
            assignment.id = instrument->getProperty("id").toString().trim().toStdString();
            assignment.instrumentId = instrument->getProperty("instrument").toString().trim().toStdString();
            assignment.name = instrument->getProperty("name").toString().trim().toStdString();
            assignment.sourceVoice = *sourceVoice;
            assignment.role = instrument->getProperty("role").toString().trim().toStdString();
            assignment.contentLaneId = instrument->getProperty("content_lane_id").toString().trim().toStdString();
            assignment.lineRelationship = instrument->getProperty("line_relationship").toString().trim().toStdString();
            assignment.minimumPitch = static_cast<int>(instrument->getProperty("minimum_pitch"));
            assignment.maximumPitch = static_cast<int>(instrument->getProperty("maximum_pitch"));
            assignment.octaveShift = static_cast<int>(instrument->getProperty("octave_shift"));
            assignment.activity = static_cast<double>(instrument->getProperty("activity"));
            assignment.prominence = static_cast<double>(instrument->getProperty("prominence"));
            assignment.doubling = static_cast<double>(instrument->getProperty("doubling"));
            assignment.orchestralFunction = instrument->getProperty("orchestral_function").toString().trim().toStdString();
            assignment.articulation = instrument->getProperty("articulation_intent").toString().trim().toStdString();
            assignment.divisiVoices = static_cast<int>(instrument->getProperty("divisi_voices"));
            assignment.liveDevice = instrument->getProperty("live_device").toString().trim().toStdString();
            assignment.livePresetIntent = instrument->getProperty("live_preset_intent").toString().trim().toStdString();
            if (const auto* timbre = instrument->getProperty("timbre_signature").getDynamicObject()) {
                assignment.timbre.source = timbre->getProperty("source").toString().trim().toStdString();
                assignment.timbre.envelope = timbre->getProperty("envelope").toString().trim().toStdString();
                assignment.timbre.spectrum = timbre->getProperty("spectrum").toString().trim().toStdString();
                assignment.timbre.motion = timbre->getProperty("motion").toString().trim().toStdString();
                assignment.timbre.space = timbre->getProperty("space").toString().trim().toStdString();
                assignment.timbre.texture = timbre->getProperty("texture").toString().trim().toStdString();
                assignment.timbre.uniqueness = std::clamp(
                    static_cast<double>(timbre->getProperty("uniqueness")), 0.0, 1.0);
            }
            if (const auto* sections = instrument->getProperty("active_sections").getArray())
                for (const auto& section : *sections)
                    assignment.activeSections.push_back(section.toString().trim().toStdString());
            result.instruments.push_back(std::move(assignment));
        }
    }
    result.instrumentCastAuthored = object->hasProperty("instrument_cast_authored")
        ? static_cast<bool>(object->getProperty("instrument_cast_authored"))
        : !result.instruments.empty();
    if (const auto* motifs = object->getProperty("rhythm_motifs").getArray()) {
        for (const auto& item : *motifs) {
            const auto* motif = item.getDynamicObject();
            if (motif == nullptr) continue;
            RhythmMotif parsedMotif{
                motif->getProperty("id").toString().trim().toStdString(),
                static_cast<int>(motif->getProperty("bars")),
                static_cast<int>(motif->getProperty("steps_per_bar")),
                motif->getProperty("kick").toString().toStdString(),
                motif->getProperty("snare_clap").toString().toStdString(),
                motif->getProperty("closed_hats").toString().toStdString(),
                motif->getProperty("open_hats_shaker").toString().toStdString(),
                motif->getProperty("low_percussion").toString().toStdString(),
                motif->getProperty("high_percussion").toString().toStdString()};
            if (const auto* ornaments = motif->getProperty("ornaments").getArray())
                for (const auto& ornamentItem : *ornaments) {
                    const auto* ornament = ornamentItem.getDynamicObject();
                    if (ornament == nullptr) continue;
                    if (const auto instrument = rhythmInstrumentFromKey(
                            ornament->getProperty("instrument").toString().toStdString()))
                        parsedMotif.ornaments.push_back({
                            static_cast<int>(ornament->getProperty("step")), *instrument,
                            static_cast<int>(ornament->getProperty("velocity")),
                            static_cast<double>(ornament->getProperty("duration_steps"))});
                }
            result.rhythmMotifs.push_back(std::move(parsedMotif));
        }
    }
    if (const auto* voices = object->getProperty("voices").getArray()) {
        for (const auto& item : *voices) {
            const auto* voice = item.getDynamicObject();
            if (voice == nullptr) continue;
            const auto id = voiceIdFromKey(voice->getProperty("id").toString().toStdString());
            if (!id) continue;
            PlannedVoice parsedVoice{*id,
                voice->getProperty("function").toString().trim().toStdString(),
                voice->getProperty("interaction").toString().trim().toStdString(),
                static_cast<double>(voice->getProperty("activity")),
                static_cast<double>(voice->getProperty("syncopation")),
                static_cast<int>(voice->getProperty("minimum_pitch")),
                static_cast<int>(voice->getProperty("maximum_pitch"))};
            parsedVoice.performance = defaultPerformanceProfile(*id);
            if (const auto value = articulationStyleFromKey(
                    voice->getProperty("articulation").toString().toStdString()))
                parsedVoice.performance.articulation = *value;
            if (const auto value = dynamicContourFromKey(
                    voice->getProperty("dynamic_contour").toString().toStdString()))
                parsedVoice.performance.dynamics = *value;
            if (const auto value = vibratoStyleFromKey(
                    voice->getProperty("vibrato").toString().toStdString()))
                parsedVoice.performance.vibrato = *value;
            if (const auto value = pitchGestureFromKey(
                    voice->getProperty("pitch_gesture").toString().toStdString()))
                parsedVoice.performance.pitchGesture = *value;
            if (voice->hasProperty("expression_depth"))
                parsedVoice.performance.expressionDepth = static_cast<double>(voice->getProperty("expression_depth"));
            if (voice->hasProperty("brightness"))
                parsedVoice.performance.brightness = static_cast<double>(voice->getProperty("brightness"));
            if (voice->hasProperty("humanization"))
                parsedVoice.performance.humanization = static_cast<double>(voice->getProperty("humanization"));
            if (voice->hasProperty("sustain_pedal"))
                parsedVoice.performance.sustainPedal = static_cast<bool>(voice->getProperty("sustain_pedal"));
            parsedVoice.performance.intent = voice->getProperty("performance_intent").toString().trim().toStdString();
            parsedVoice.performance.authored = voice->hasProperty("articulation");
            result.voices.push_back(std::move(parsedVoice));
        }
    }

    const auto* sections = object->getProperty("sections").getArray();
    if (sections == nullptr || sections->size() < 3 || sections->size() > 20) {
        error = "Song plan needs between 3 and 20 sections";
        return false;
    }
    auto reportedBars = 0;
    for (const auto& item : *sections) {
        const auto* section = item.getDynamicObject();
        if (section == nullptr) continue;
        SongSection parsedSection;
        parsedSection.name = section->getProperty("name").toString().trim().toStdString();
        parsedSection.function = section->getProperty("function").toString().trim().toStdString();
        parsedSection.harmonicDirection = section->getProperty("harmonic_direction").toString().trim().toStdString();
        parsedSection.motifTreatment = section->getProperty("motif_treatment").toString().trim().toStdString();
        parsedSection.bars = std::max(1, static_cast<int>(section->getProperty("bars")));
        parsedSection.energy = static_cast<double>(section->getProperty("energy"));
        parsedSection.tension = static_cast<double>(section->getProperty("tension"));
        parsedSection.density = static_cast<double>(section->getProperty("density"));
        parsedSection.motifVariant = static_cast<int>(section->getProperty("motif_variant"));
        parsedSection.tonalCenterPitchClass = static_cast<int>(
            section->getProperty("tonal_center_pitch_class"));
        parsedSection.modeHint = section->getProperty("mode_hint").toString().trim().toStdString();
        if (const auto* events = section->getProperty("harmonic_events").getArray())
            for (const auto& eventItem : *events) {
                const auto* event = eventItem.getDynamicObject();
                if (event == nullptr) continue;
                parsedSection.harmonicEvents.push_back({
                    static_cast<int>(event->getProperty("bar_offset")),
                    static_cast<double>(event->getProperty("beat_offset")),
                    event->getProperty("chord_id").toString().trim().toStdString(),
                    static_cast<double>(event->getProperty("emphasis")),
                    event->getProperty("purpose").toString().trim().toStdString()});
            }
        if (const auto state = kickStateFromKey(
                section->getProperty("kick_state").toString().toStdString()))
            parsedSection.rhythm.kickState = *state;
        if (const auto continuity = kickContinuityFromKey(
                section->getProperty("kick_continuity").toString().toStdString()))
            parsedSection.rhythm.continuity = *continuity;
        parsedSection.rhythm.percussionDensity = static_cast<double>(
            section->getProperty("percussion_density"));
        parsedSection.rhythm.syncopation = static_cast<double>(
            section->getProperty("rhythmic_syncopation"));
        parsedSection.rhythm.swing = static_cast<double>(section->getProperty("swing"));
        parsedSection.rhythm.motifId = section->getProperty("rhythm_motif_id").toString().trim().toStdString();
        parsedSection.rhythm.authored = section->hasProperty("kick_state");
        if (const auto* mutations = section->getProperty("rhythm_mutations").getArray())
            for (const auto& mutationItem : *mutations) {
                const auto* mutation = mutationItem.getDynamicObject();
                if (mutation == nullptr) continue;
                const auto lane = rhythmLaneFromKey(mutation->getProperty("lane").toString().toStdString());
                const auto kind = rhythmMutationFromKey(mutation->getProperty("operation").toString().toStdString());
                if (!lane || !kind) continue;
                parsedSection.rhythm.mutations.push_back({
                    static_cast<int>(mutation->getProperty("bar_offset")), *lane, *kind,
                    static_cast<int>(mutation->getProperty("step")),
                    static_cast<int>(mutation->getProperty("amount")),
                    static_cast<int>(mutation->getProperty("velocity")),
                    mutation->getProperty("purpose").toString().trim().toStdString()});
            }
        if (const auto* gestures = section->getProperty("rhythm_gestures").getArray())
            for (const auto& gestureItem : *gestures) {
                const auto* gesture = gestureItem.getDynamicObject();
                if (gesture == nullptr) continue;
                if (const auto kind = rhythmGestureFromKey(
                        gesture->getProperty("type").toString().toStdString()))
                    parsedSection.rhythm.gestures.push_back({
                        static_cast<int>(gesture->getProperty("bar_offset")), *kind,
                        static_cast<double>(gesture->getProperty("beat")),
                        static_cast<double>(gesture->getProperty("intensity"))});
            }
        if (const auto* activeVoices = section->getProperty("active_voices").getArray())
            for (const auto& voice : *activeVoices)
                if (const auto id = voiceIdFromKey(voice.toString().toStdString()))
                    parsedSection.activeVoices.push_back(*id);
        reportedBars += parsedSection.bars;
        result.sections.push_back(std::move(parsedSection));
    }
    if (result.sections.size() < 3 || reportedBars <= 0) {
        error = "Song plan contains no usable form";
        return false;
    }
    if (const auto* performance = object->getProperty("performance_score").getDynamicObject()) {
        if (const auto* cells = performance->getProperty("cells").getArray()) {
            for (const auto& cellItem : *cells) {
                const auto* cell = cellItem.getDynamicObject();
                if (cell == nullptr) continue;
                PerformanceCell parsedCell;
                parsedCell.id = cell->getProperty("id").toString().trim().toStdString();
                parsedCell.themeId = cell->getProperty("theme_id").toString().trim().toStdString();
                parsedCell.narrativeFunction = cell->getProperty("narrative_function").toString().trim().toStdString();
                parsedCell.lengthBeats = static_cast<double>(cell->getProperty("length_beats"));
                if (const auto* owners = cell->getProperty("owned_voices").getArray())
                    for (const auto& owner : *owners)
                        if (const auto voice = voiceIdFromKey(owner.toString().toStdString()))
                            parsedCell.ownedVoices.push_back(*voice);
                if (const auto* notes = cell->getProperty("notes").getArray())
                    for (const auto& noteItem : *notes) {
                        const auto* note = noteItem.getDynamicObject();
                        if (note == nullptr) continue;
                        if (const auto voice = voiceIdFromKey(note->getProperty("voice").toString().toStdString())) {
                            auto instrumentId = note->getProperty("instrument_id").toString().trim().toStdString();
                            const auto validOwner = std::find_if(result.instruments.begin(), result.instruments.end(),
                                [&](const auto& assignment) {
                                    return assignment.id == instrumentId && assignment.sourceVoice == *voice;
                                });
                            if (!instrumentId.empty() && validOwner == result.instruments.end()) instrumentId.clear();
                            parsedCell.notes.push_back({
                                static_cast<double>(note->getProperty("beat")),
                                static_cast<double>(note->getProperty("duration")),
                                static_cast<int>(note->getProperty("pitch")),
                                static_cast<int>(note->getProperty("velocity")), *voice,
                                metricIntentFromKey(note->getProperty("metric_intent").toString().toStdString()),
                                std::move(instrumentId)});
                        }
                    }
                if (const auto* controls = cell->getProperty("controls").getArray())
                    for (const auto& controlItem : *controls) {
                        const auto* control = controlItem.getDynamicObject();
                        if (control == nullptr) continue;
                        if (const auto voice = voiceIdFromKey(control->getProperty("voice").toString().toStdString())) {
                            auto instrumentId = control->getProperty("instrument_id").toString().trim().toStdString();
                            const auto validOwner = std::find_if(result.instruments.begin(), result.instruments.end(),
                                [&](const auto& assignment) {
                                    return assignment.id == instrumentId && assignment.sourceVoice == *voice;
                                });
                            if (!instrumentId.empty() && validOwner == result.instruments.end()) instrumentId.clear();
                            parsedCell.controls.push_back({
                                static_cast<double>(control->getProperty("beat")),
                                static_cast<int>(control->getProperty("controller")),
                                static_cast<int>(control->getProperty("value")), *voice,
                                std::move(instrumentId)});
                        }
                    }
                result.performanceScore.cells.push_back(std::move(parsedCell));
            }
        }
        if (const auto* placements = performance->getProperty("placements").getArray())
            for (const auto& placementItem : *placements) {
                const auto* placement = placementItem.getDynamicObject();
                if (placement == nullptr) continue;
                PerformancePlacement parsedPlacement;
                parsedPlacement.cellId = placement->getProperty("cell_id").toString().trim().toStdString();
                parsedPlacement.sectionIndex = static_cast<int>(placement->getProperty("section_index"));
                parsedPlacement.startBeat = static_cast<double>(placement->getProperty("start_beat"));
                parsedPlacement.repeats = static_cast<int>(placement->getProperty("repeats"));
                parsedPlacement.transpose = static_cast<int>(placement->getProperty("transpose"));
                parsedPlacement.velocityScale = static_cast<double>(placement->getProperty("velocity_scale"));
                parsedPlacement.timeScale = static_cast<double>(placement->getProperty("time_scale"));
                parsedPlacement.purpose = placement->getProperty("purpose").toString().trim().toStdString();
                if (const auto* mappings = placement->getProperty("voice_map").getArray())
                    for (const auto& mappingItem : *mappings) {
                        const auto* mapping = mappingItem.getDynamicObject();
                        if (mapping == nullptr) continue;
                        const auto from = voiceIdFromKey(mapping->getProperty("from").toString().toStdString());
                        const auto to = voiceIdFromKey(mapping->getProperty("to").toString().toStdString());
                        if (from && to) parsedPlacement.voiceMap.push_back({*from, *to});
                    }
                parsedPlacement.retrograde = static_cast<bool>(placement->getProperty("retrograde"));
                parsedPlacement.invertContour = static_cast<bool>(placement->getProperty("invert_contour"));
                parsedPlacement.inversionAxis = static_cast<int>(placement->getProperty("inversion_axis"));
                parsedPlacement.fragmentStart = static_cast<double>(placement->getProperty("fragment_start"));
                parsedPlacement.fragmentEnd = static_cast<double>(placement->getProperty("fragment_end"));
                parsedPlacement.metricIntent = metricIntentFromKey(
                    placement->getProperty("metric_intent").toString().toStdString());
                result.performanceScore.placements.push_back(std::move(parsedPlacement));
            }
    }
    SongComposer::normalizePlan(result);
    if (result.title.empty()) result.title = "Untitled Song";
    if (result.key.empty()) result.key = "Unspecified key";
    return true;
}

} // namespace pulso::plugin
