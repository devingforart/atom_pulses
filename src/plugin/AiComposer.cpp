#include "AiComposer.h"

#include "core/Scale.h"
#include "core/ElectronicRoleContract.h"
#include "core/OrchestrationScore.h"
#include "core/SelectiveRepair.h"
#include "core/TonalContract.h"
#include "core/TrackViability.h"
#include "AiModelConfig.h"
#include "OperationalJournal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <future>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <thread>

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
constexpr std::size_t maximumConcurrentRepairShards = 3;
constexpr std::array layerNames{"harmony", "melody", "bass", "drums"};
constexpr std::array layerChannels{3, 2, 1, 10};
constexpr std::array layerVoices{VoiceId::HarmonicFoundation, VoiceId::Lead,
                                 VoiceId::SubBass, VoiceId::CoreDrums};

std::size_t requestedInstrumentCountFromDirection(const juce::String& direction) noexcept {
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
    const auto houseFoundation = !explicitlyBroken && containsAny({"progressive house", "deep house",
        "organic house", "four on the floor", "four-on-the-floor", "4x4", "guy j", "bombo en negras"});
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
        } else if (section.energy >= 0.40 && section.rhythm.kickState != KickState::Muted) {
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
    const auto required = electronic && static_cast<bool>(soundscape->getProperty("percussion_free")) &&
        !explicitlyStatic;
    if (!required) return true;

    auto owners = std::size_t{};
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
        if (ElectronicRoleContract::motionOwner(instrument)) ++owners;
    }
    if (owners == 1) return true;
    error = "Percussion-free electronic cast requires exactly one non-transition motion owner; received " +
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
    unsigned long nativeError{};
};

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

    std::atomic<HINTERNET> activeRequest{request};
    std::atomic<bool> finished{};
    std::atomic<bool> deadlineReached{};
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::jthread watchdog([&](std::stop_token watchdogToken) {
        while (!watchdogToken.stop_requested() && !finished.load(std::memory_order_acquire)) {
            if (token.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                deadlineReached.store(!token.stop_requested(), std::memory_order_release);
                if (const auto handle = activeRequest.exchange(nullptr, std::memory_order_acq_rel))
                    WinHttpCloseHandle(handle);
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
    if (const auto handle = activeRequest.exchange(nullptr, std::memory_order_acq_rel))
        WinHttpCloseHandle(handle);
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
    if (juce::String(method) == "POST")
        url = url.withPOSTData(body.isEmpty() ? "{}" : body);
    juce::WebInputStream stream(url, true);
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
    const auto deadline = started + budget;
    const auto initialBudget = std::min(budget, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::seconds(30)));
    auto response = singleRequest(true, "/v1/responses", body, apiKey, token, initialBudget);
    const auto background = body.contains("\"background\":true");
    if (!background || !response.connected || response.status < 200 || response.status >= 300 ||
        response.cancelled || response.timedOut)
        return response;

    juce::String responseId;
    juce::String state;
    if (!responseIdentity(response.body, responseId, state)) return response;

    int transientFailures{};
    while (state == "queued" || state == "in_progress") {
        for (int elapsed = 0; elapsed < 2000; elapsed += 25) {
            if (token.stop_requested()) {
                cancelBackgroundResponse(responseId, apiKey);
                response.cancelled = true;
                return response;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                cancelBackgroundResponse(responseId, apiKey);
                response.timedOut = true;
                return response;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            cancelBackgroundResponse(responseId, apiKey);
            response.timedOut = true;
            return response;
        }
        const auto pollBudget = std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::seconds(30)));
        auto polled = singleRequest(false, "/v1/responses/" + responseId, {}, apiKey, token, pollBudget);
        if (polled.cancelled) {
            cancelBackgroundResponse(responseId, apiKey);
            return polled;
        }
        if (!polled.connected || polled.timedOut) {
            if (++transientFailures < 3) continue;
            return polled;
        }
        transientFailures = 0;
        response = std::move(polled);
        if (response.status < 200 || response.status >= 300 ||
            !responseIdentity(response.body, responseId, state))
            return response;
    }
    return response;
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
    return juce::SystemStats::getEnvironmentVariable("OPENAI_API_KEY", {}).trim().isNotEmpty();
}

juce::String instrumentBlockBrief(const SongPlan& plan,
                                  const std::vector<std::size_t>& indices) {
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
              << "; relationship=" << juce::String::fromUTF8(instrument.lineRelationship.c_str())
              << "; publication_minimums=" << static_cast<int>(contract.minimumNotes)
              << " rendered notes, " << static_cast<int>(contract.minimumActiveBars)
              << " active bars, " << static_cast<int>(contract.minimumPhrases) << " phrases"
              << "; active_sections=";
        for (const auto& section : instrument.activeSections)
            brief << juce::String::fromUTF8(section.c_str()) << ",";
        brief << "\n";
    }
    return brief;
}

juce::String performanceBlockPrompt(const juce::String& direction,
                                    const juce::String& blueprintJson,
                                    const SongPlan& plan,
                                    const std::vector<std::size_t>& indices,
                                    std::size_t blockIndex, int attempt) {
    auto prompt = juce::String(
        "You are a PULSO performance orchestrator. The immutable JSON below is the shared song blueprint. "
        "Write MIDI performance cells only for the exact instrument ids listed in THIS BLOCK. Do not redesign the "
        "form, harmony, cast, key or narrative. Every pitch must agree with the blueprint's exact section chord at its "
        "placement. Use strict quarter-note beat coordinates relative to each reusable cell and section-relative "
        "placements. Write complete playable phrases, not token notes: beds and pulses need repeated but evolving "
        "coverage; leads need statement, answer, rests and transformed return; transitions may be rare. One cell may "
        "serve related instruments only when its notes retain each exact instrument_id. Use at most one principal cell "
        "per independent content lane and develop it through placements rather than inventing unrelated fragments. "
        "The declared protagonist owns the complete leitmotif lineage. A call_response instrument may quote only a "
        "short clue and must answer in negative space with a different rhythmic sentence; harmonic, color, pedal, body "
        "and transition instruments must follow their own chordal or textural trajectories and must not receive copies "
        "of the protagonist cell. Relay and timbral_handoff members share one content lane and never sound the complete "
        "line simultaneously. Give protagonist, answerer and genuinely thematic counterpoint the same non-empty theme_id, "
        "but differentiate their onset grammar and interval contour; related must never mean cloned. The protagonist must "
        "have an authored transformed-return phrase in the resolution section's final eight bars, containing at least "
        "three notes; its last attack must occur inside the final two bars and land on a stable pitch of the terminal "
        "harmony. Use the home tonic only when the blueprint explicitly requires tonic closure; suspended, modal and "
        "open endings remain valid creative decisions. A declared "
        "non-percussive motion owner "
        "(arp, sequence, pulse, orbit or ostinato) must receive its own evolving GPT-authored cell and placements; the local "
        "renderer will not write principal, response or motion material for you. "
        "Every independent instrument must meet its publication_minimums after placement repetitions are rendered; later "
        "stages will neither develop nor merge an incomplete independent AI line. Regular instruments must appear in at "
        "least two structurally different sections; one_shot/transition material may be rare. Rhythm motifs in the shared "
        "blueprint are context, not a substitute for this block: every assigned drum or percussion identity must still own "
        "at least one explicit note event with its exact instrument_id and a valid placement. "
        "A transition is boundary "
        "punctuation, never the recurring motor: keep each transition within one-to-three-bar windows around structural "
        "changes and below one eighth of the complete form. Keep each note inside the declared register and use only the declared source_voice for that "
        "instrument. Return no instrument outside this block. Cell ids must start with b") +
        juce::String(static_cast<int>(blockIndex + 1)) + "_. Attempt " + juce::String(attempt) +
        ". Original direction: " + direction +
        "\nINSTRUMENTS IN THIS BLOCK:\n" + instrumentBlockBrief(plan, indices) +
        "\nIMMUTABLE SHARED BLUEPRINT:\n" + blueprintJson;
    return prompt;
}

struct PerformanceRoutingReport {
    std::map<std::string, std::size_t> received;
    std::map<std::string, std::size_t> accepted;
    std::map<std::string, std::size_t> reassigned;
    std::map<std::string, std::size_t> discarded;
    std::size_t foreignNotes{};
    std::size_t unknownInstrumentNotes{};

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
            score.placements.push_back(std::move(parsedPlacement));
        }
    }
    std::vector<double> sectionLengths;
    sectionLengths.reserve(blueprint.sections.size());
    for (const auto& section : blueprint.sections)
        sectionLengths.push_back(section.bars * blueprint.beatsPerBar);
    const auto report = PerformanceScoreEngine::normalize(
        score, blueprint.sections.size(), sectionLengths);
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
                                              const std::vector<std::size_t>& candidates) {
    return SelectiveRepair::incompleteTargets(plan, score, candidates);
}

juce::String performanceDeficitBrief(const SongPlan& plan,
                                     const PerformanceScore& score,
                                     const std::vector<std::size_t>& candidates) {
    juce::String result;
    for (const auto& deficit : SelectiveRepair::performanceDeficits(plan, score, candidates)) {
        if (result.isNotEmpty()) result << "; ";
        result << juce::String::fromUTF8(deficit.instrumentId.c_str())
               << " notes=" << static_cast<int>(deficit.notes) << "/"
               << static_cast<int>(deficit.minimumNotes)
               << " bars=" << static_cast<int>(deficit.activeBars) << "/"
               << static_cast<int>(deficit.minimumActiveBars)
               << " phrases=" << static_cast<int>(deficit.phrases) << "/"
               << static_cast<int>(deficit.minimumPhrases);
        if (deficit.minimumSections > 0)
            result << " sections=" << static_cast<int>(deficit.sections) << "/"
                   << static_cast<int>(deficit.minimumSections);
        if (deficit.missingCodaResolution) result << " coda=missing";
        if (deficit.missingThematicRelationship) result << " theme_relation=missing";
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
               << static_cast<int>(evidence.minimumPhrases)
               << "; operations=";
        for (const auto operation : constraint.operations) {
            const auto key = performanceRepairOperationKey(operation);
            result << juce::String::fromUTF8(key.data(), static_cast<int>(key.size())) << ",";
        }
        if (evidence.missingCodaResolution) result << "; resolution_evidence=missing";
        if (evidence.missingThematicRelationship) result << "; thematic_relation=missing";
    }
    return result.isEmpty() ? juce::String("none") : result;
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

juce::String selectiveRepairPrompt(const juce::String& direction,
                                   const juce::String& blueprintJson,
                                   const SongPlan& plan,
                                   const SelectiveRepairPlan& diagnosis) {
    juce::String issues;
    for (const auto& issue : diagnosis.issues)
        issues << "- " << juce::String::fromUTF8(issue.c_str()) << "\n";
    const auto targetIds = instrumentIdsFor(plan, diagnosis.instrumentIndices);
    return juce::String(
        "You are PULSO's final score editor. The macro form, section harmony, tonal policy, cast and every instrument "
        "not listed below are immutable and already accepted. Return replacement performance_score cells and placements "
        "ONLY for the listed instrument ids. Solve every audible critic finding as one coherent edit. Do not add tracks, "
        "change chords, rewrite unrelated ideas or increase global density. Preserve the song's recognisable motifs while "
        "giving underwritten lines complete phrases, contrasting returns and meaningful rests. If a pulse or arpeggio is "
        "dominant, create subtraction, mutations and hand-offs instead of a continuous note stream. If closure is weak, "
        "make the final active phrases answer earlier material and resolve harmonic debt. All notes remain strict-grid, "
        "inside the declared registers and compatible with the exact section chords. Use compact reusable cells; this is "
        "a bounded repair, not a new composition.\nORIGINAL DIRECTION:\n") + direction +
        "\nAUDIBLE CRITIC FINDINGS:\n" + issues +
        "TARGET INSTRUMENTS (replace these only):\n" +
        instrumentBlockBrief(plan, diagnosis.instrumentIndices) +
        "\nCURRENT TARGET MATERIAL (retain its identity while improving it):\n" +
        existingTargetMaterial(plan, targetIds) +
        "\nIMMUTABLE SHARED BLUEPRINT:\n" + blueprintJson;
}

juce::String audibleAuditSummary(const CompositionRenderReport& report) {
    return "production=" + juce::String(report.production.score, 3) +
        " ready=" + juce::String(static_cast<int>(report.production.ready)) +
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

juce::String AiComposer::defaultModel() { return model; }

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
    return instruments == 0 ? 0 : (instruments + instrumentsPerRepairShard - 1) /
        instrumentsPerRepairShard;
}

std::size_t AiComposer::performanceBlockCount(std::size_t instruments) noexcept {
    instruments = std::min(instruments, maximumInstruments);
    return instruments == 0 ? 0 : (instruments + instrumentsPerPerformanceBlock - 1) /
        instrumentsPerPerformanceBlock;
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

bool AiComposer::bindCastProtagonist(const juce::String& macroBlueprint,
                                    const juce::String& castManifest,
                                    juce::String& mergedBlueprint,
                                    juce::String& error) {
    mergedBlueprint.clear();
    if (!validateProtagonistManifest(macroBlueprint, castManifest, error)) return false;
    mergedBlueprint = mergeBlueprintPhases(macroBlueprint, castManifest);
    if (mergedBlueprint.isEmpty()) {
        error = "Could not bind the authoritative cast protagonist into the macro blueprint";
        return false;
    }
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

std::size_t AiComposer::requestedInstrumentCount(const juce::String& direction) noexcept {
    return requestedInstrumentCountFromDirection(direction);
}

AiComposition AiComposer::compose(const juce::String& creativeDirection, int bars, double bpm,
                                  const Pattern* reference, std::uint8_t lockedLayers,
                                  std::stop_token token, juce::String& error) {
    AiComposition result;
    const auto apiKey = juce::SystemStats::getEnvironmentVariable("OPENAI_API_KEY", {}).trim();
    if (apiKey.isEmpty()) {
        error = "OPENAI_API_KEY is not configured";
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
                              std::uint64_t seed, std::stop_token token,
                              juce::String& error, const AiSongProgress& progress) {
    SongPlan result;
    result.sections.clear();
    const auto apiKey = juce::SystemStats::getEnvironmentVariable("OPENAI_API_KEY", {}).trim();
    if (apiKey.isEmpty()) {
        error = "OPENAI_API_KEY is not configured";
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
    OperationalJournal::write("INFO", "GENERATION", "request started | seed=" +
        juce::String(static_cast<juce::int64>(seed)) + " | target_seconds=" +
        juce::String(targetSeconds) + " | bars=" + juce::String(totalBars) +
        " | requested_cast=" + juce::String(static_cast<int>(requestedCastCount)) +
        " | direction=" + direction.substring(0, 240));
    const auto directionLower = direction.toLowerCase();
    const auto hypnoticProgressiveReference = directionLower.contains("guy j");
    const auto referenceBrief = hypnoticProgressiveReference
        ? juce::String(
            "The named artist is a high-level reference, never a request to reproduce a recording. Translate it into "
            "deep hypnotic progressive-house traits: an unwavering but breathing four-on-floor foundation, an "
            "eight-to-sixteen-bar kick/bass pocket, one compact emotional hook, patient subtractive development, "
            "long tension plateaus, delayed returns and a decisive transformed recall. Complexity must emerge from "
            "micro-development and automation, not melodic busyness or orchestral accumulation. ")
        : juce::String();
    const auto prompt = juce::String(
        "You are the long-form composition architect for PULSO. Design one complete song, not a loop. "
        "Create a narratively inevitable form with introduction, thematic statements, contrast, development, "
        "a true climax and a conclusive ending. ") + referenceBrief + juce::String(
        "Author narrative_spine before writing notes. It is a causal contract, not a synopsis: premise introduces one "
        "recognisable identity; question creates a specific unfinished melodic or harmonic obligation; every act names "
        "what caused it and the audible consequence; transformation changes that identity because of the obligation; "
        "climax makes the accumulated debt unavoidable; resolution audibly repays it through motif closure, tonal arrival, "
        "register relaxation and reduced density. protagonist_instrument_id must exactly match one declared Lead instrument "
        "and that protagonist or its explicit handoff must speak in at least 65 percent of eligible eight-bar phrase "
        "windows, using rests inside phrases rather than continuous note streams. In electronic harmonic works, two "
        "interlocking harmonic-floor instruments must cover at least 85 percent of bars while preserving deliberate "
        "phrase-end breaths. The resolution act must end its foreground contour on tonic, lower register and reduced "
        "simultaneous density so the declared harmonic debt is audibly repaid. "
        "id. Map every act to an exact section_name and include premise, question, transformation, climax and resolution. "
        "Do not claim a consequence or closure that the performance_score MIDI notes do not enact. "
        "Recurring sections must share a recognisable motif while changing "
        "orchestration, register, harmony or rhythm. Design a variable ensemble rather than four fixed layers. "
        "Choose 7-15 execution voices from the supplied IDs; give every voice an independent function, interaction rule, "
        "activity and register. Use core_drums plus low_percussion and high_percussion as distinct rhythmic strata, "
        "multiple complementary harmonic voices, independent bass functions, melodic dialogue, atmosphere and "
        "transitions when musically justified. Do not activate every voice in every section. "
        "First classify the requested production in production_language. club_electronic means the musical logic is "
        "that of a producer and DJ: kick-bass interlock, groove, hook economy, automation, spectral space and energy "
        "over 4/8/16/32-bar horizons. It is not an instruction to imitate one fixed genre template. hybrid preserves "
        "electronic production logic while allowing requested acoustic families; orchestral is reserved for genuinely "
        "orchestral requests. All production dimensions are 0 to 1. For club_electronic, complexity comes from timbral "
        "evolution, rhythmic conversation, subtraction and structural returns rather than many simultaneous pitches. "
        "Use functional production roles such as kick, backbeat, tops, low/high percussion, sub, bass groove, chord body, "
        "stab, hook, response, atmosphere and transitions. One foreground hook owns attention at a time. "
        "In club_electronic with low orchestral_allowance, do not cast marimba, vibraphone, celesta, tubular bells or "
        "generic pitched mallets unless the user explicitly requested that physical identity. Use a filtered synth pulse, "
        "restrained analog hook or evolving spectral bed instead; avoid toy, chiptune, game and novelty preset character. "
        "Above those execution voices, design a production cast of 8-64 instrument instances from the supplied catalog. "
        "Use the smallest cast that can realize the requested depth. It has three coordinated departments: rhythm and "
        "percussion, harmonic fabric, and hooks or melodic speakers. source_voice is the playable archetype feeding an instrument, not its identity, and MUST reference "
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
        "among many tracks merely to increase track count. Every instrument has content_lane_id: use a unique id for "
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
        "bars it may sustain. target_median_active_layers describes the complete rendered fabric, normally 3-4 for a "
        "percussion-free electronic journey and 3-5 with rhythm, while peaks may be richer. These are rotating layers, "
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
        "call-and-response and recognizable lineage across the full song. Give every voice a distinct performance "
        "identity: articulation, dynamic contour, vibrato, pitch gesture, brightness, expression depth and "
        "humanization must serve its instrumental role. Pitch gestures belong only to monophonic bass or melodic "
        "voices; polyphonic harmony and drums remain pitch-stable. Sustain pedal is only for foundation, upper "
        "harmony or atmosphere when connected phrasing is intentional. Expression must breathe with the form and "
        "must never remain maximal or mechanically identical. The macro kick contract still wins when "
        "the user explicitly requests constant quarter-note kick or when a section deliberately mutes it. "
        "The section bars MUST sum exactly to ") + juce::String(totalBars) + ". Use between 5 and 14 sections. Energy, tension "
        "and density are values from 0 to 1. Motif intervals are semitones relative to the "
        "tonic and form the immutable thematic DNA. Establish one compact primary statement, then make at least "
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
        "14-28 populated instrument parts "
        "across the complete arrangement while normally limiting simultaneous parts to five-to-eight. "
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
        "trajectory for every member or declare fewer instruments—never split one gesture into nominal lanes. "
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

    const auto macroPrompt = juce::String(
        "You are PULSO's long-form musical architect. Design the immutable macro composition, not its instruments or "
        "MIDI performances. Return a coherent dramatic journey whose sections cover exactly ") +
        juce::String(totalBars) + " bars (the sum of section bars must equal that number), lasting " +
        juce::String(targetSeconds) + " seconds at " + juce::String(bpm, 1) + " BPM in " +
        juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. Establish, develop, contrast, transform and resolve a recognisable musical identity; "
        "avoid interchangeable sections and arbitrary novelty. Make every harmonic event reference a declared chord ID, "
        "and keep tonal centers, modes, borrowed harmony and tension releases intentional under the declared tonal policy. "
        "Consolidated tonality may still declare a small number of functional chromatic or modal-color chords: mark their "
        "function honestly, keep every foreign pitch inside that exact chord window, and follow it with a chord whose "
        "scale tone resolves the foreign pitch by semitone or whole step. Never promise Neapolitan, Dorian, altered or "
        "chromatic color in prose unless chord_palette and harmonic_events actually materialize it. "
        "Honor exclusions literally (especially requests for no drums or percussion) and describe genre through musical "
        "behavior rather than artist imitation. Do not choose instruments, soundscape layers, rhythm motifs, voices, cells, "
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
    constexpr auto architectureBudget = std::chrono::seconds(30);
    const auto aiStarted = std::chrono::steady_clock::now();
    if (progress) progress({AiSongStage::Blueprint, 0, 2, 1, "macro form, harmony and narrative"});
    auto http = performRequest(body, apiKey, token, architectureBudget);
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
        if (remaining >= std::chrono::seconds(45))
            http = performRequest(macroRecoveryBody, apiKey, token,
                std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::minutes(3))));
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
    const auto manifestPrompt = castCountContract + juce::String(
        "You are PULSO's global orchestration director. Decide the complete ensemble once, before any detail is written. "
        "Return the compact cast manifest only. Every member needs a unique stable id, a distinct musical responsibility "
        "or an explicit complementary relationship, and an intentional section trajectory. The IDs, instrument types, "
        "source voices, roles, lane identities, relationships, orchestral functions and active sections are immutable in "
        "later phases. Satisfy any explicit requested instrument count up to 64 exactly; otherwise choose only the number "
        "that the story can support with independent material. Preserve exclusions literally, especially percussion-free "
        "requests. Avoid constant tutti and distribute foreground, harmonic floor, dialogue, movement and atmosphere over "
        "the whole arc. Declare exactly one protagonist and at most one motif-derived answerer. Transformations such as "
        "original, inversion, fragmentation, recovery and return belong to that protagonist's later performance cells, "
        "not to separate instrument members. Use additional tracks for true orchestration: independent inner voices, "
        "pedals, chord bodies, contrary counterpoint, spectral color and transition functions. Permit an arpeggiator only "
        "when it is an intentional part of this particular production argument. In percussion-free electronic music, "
        "declare exactly one non-percussive recurrence owner (arp, sequence, pulse, orbit or ostinato) unless the direction "
        "explicitly requests a static/drone-only work; it supplies evolving hypnotic motion, not generic sixteenth-note filler. "
        "At the manifest root, protagonist_instrument_id must equal the exact stable id of that one protagonist. It must "
        "use source_voice=lead and include the macro's resolution section in active_sections so its authored transformed "
        "return can occur in the last sixteen bars. This manifest ID is authoritative and replaces any earlier narrative "
        "placeholder. Do not write registers, presets, timbre "
        "signatures, soundscape layers, notes or performances yet. "
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
    auto castIds = manifestInstrumentIds(manifestText, manifestError);
    auto manifestContractsReady = !castIds.empty() &&
        validateProtagonistManifest(macroText, manifestText, manifestError) &&
        validateMotionManifest(macroText, manifestText, direction, manifestError);
    if (!token.stop_requested() &&
        (!manifestHttp.connected || manifestHttp.status < 200 || manifestHttp.status >= 300 ||
         manifestHttp.cancelled || manifestHttp.timedOut || !manifestContractsReady)) {
        if (progress) progress({AiSongStage::Recovery, 1, 2, 2,
                                "global cast manifest recovery"});
        manifestHttp = performRequest(manifestBody, apiKey, token, std::chrono::seconds(90));
        manifestText = extractOutputText(juce::JSON::parse(manifestHttp.body));
        manifestError.clear();
        castIds = manifestInstrumentIds(manifestText, manifestError);
        manifestContractsReady = !castIds.empty() &&
            validateProtagonistManifest(macroText, manifestText, manifestError) &&
            validateMotionManifest(macroText, manifestText, direction, manifestError);
    }
    if (!manifestHttp.connected || manifestHttp.status < 200 || manifestHttp.status >= 300 ||
        manifestHttp.cancelled || manifestHttp.timedOut || !manifestContractsReady) {
        error = "OpenAI global cast manifest failed: " +
            (manifestError.isNotEmpty() ? manifestError : apiErrorMessage(manifestHttp));
        return result;
    }

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
                                        reconciledText, reconciliationError) &&
            validateProtagonistManifest(macroText, reconciledText, reconciliationError) &&
            validateMotionManifest(macroText, reconciledText, direction, reconciliationError);
        if (reconciledByAi) {
            manifestText = std::move(reconciledText);
            OperationalJournal::write("OK", "CHECKPOINT",
                "AI cast reconciliation accepted; all " +
                juce::String(static_cast<int>(requestedCastCount)) + " identities preserved");
        } else {
            if (reconciliationError.isEmpty()) reconciliationError = apiErrorMessage(supplementHttp);
            OperationalJournal::write("WARN", "RECOVERY",
                "bounded cast supplement unavailable (" + reconciliationError +
                "); completing identity metadata from the catalog without generating notes");
            if (!completeCastManifestFromCatalog(manifestText, macroText, requestedCastCount,
                                                 reconciledText, reconciliationError) ||
                !validateProtagonistManifest(macroText, reconciledText, reconciliationError) ||
                !validateMotionManifest(macroText, reconciledText, direction, reconciliationError)) {
                error = "OpenAI global cast reconciliation failed: " + reconciliationError;
                return result;
            }
            manifestText = std::move(reconciledText);
            OperationalJournal::write("OK", "CHECKPOINT",
                "catalog completed " + juce::String(static_cast<int>(missing)) +
                " cast identities only; no local notes were generated and performance remains assigned to AI writing");
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
    manifestError.clear();
    if (!validateProtagonistManifest(macroText, manifestText, manifestError)) {
        error = "OpenAI global cast manifest failed: " + manifestError;
        return result;
    }

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

    std::vector<std::size_t> unresolved;
    for (std::size_t batchBegin = 0; batchBegin < castShards.size();
         batchBegin += maximumConcurrentCastShards) {
        const auto batchEnd = std::min(castShards.size(), batchBegin + maximumConcurrentCastShards);
        std::vector<std::future<HttpResponse>> requests;
        for (auto index = batchBegin; index < batchEnd; ++index) {
            requests.push_back(std::async(std::launch::async, requestCastShard, index,
                                          std::chrono::milliseconds(std::chrono::seconds(90))));
        }
        for (auto index = batchBegin; index < batchEnd; ++index) {
            auto response = requests[index - batchBegin].get();
            auto detailText = extractOutputText(juce::JSON::parse(response.body));
            juce::String detailError;
            if (response.connected && response.status >= 200 && response.status < 300 &&
                !response.cancelled && !response.timedOut &&
                validateCastDetailShard(manifestText, castShards[index], detailText, detailError)) {
                castDetailTexts[index] = std::move(detailText);
                if (progress) progress({AiSongStage::Blueprint, index + 1, castShards.size(), 1,
                                        "validated cast detail shard"});
            } else {
                unresolved.push_back(index);
            }
        }
    }
    if (!unresolved.empty() && !token.stop_requested()) {
        if (progress) progress({AiSongStage::Recovery, castShards.size() - unresolved.size(),
                                castShards.size(), 2,
                                "recovering only unresolved cast detail shards"});
        std::vector<std::size_t> stillMissing;
        for (std::size_t batchBegin = 0; batchBegin < unresolved.size();
             batchBegin += maximumConcurrentCastShards) {
            const auto batchEnd = std::min(unresolved.size(), batchBegin + maximumConcurrentCastShards);
            std::vector<std::future<HttpResponse>> retries;
            for (auto offset = batchBegin; offset < batchEnd; ++offset) {
                retries.push_back(std::async(std::launch::async, requestCastShard, unresolved[offset],
                                             std::chrono::milliseconds(std::chrono::seconds(75))));
            }
            for (auto offset = batchBegin; offset < batchEnd; ++offset) {
                const auto index = unresolved[offset];
                auto response = retries[offset - batchBegin].get();
                auto detailText = extractOutputText(juce::JSON::parse(response.body));
                juce::String detailError;
                if (response.connected && response.status >= 200 && response.status < 300 &&
                    !response.cancelled && !response.timedOut &&
                    validateCastDetailShard(manifestText, castShards[index], detailText, detailError)) {
                    castDetailTexts[index] = std::move(detailText);
                    if (progress) progress({AiSongStage::Blueprint, index + 1, castShards.size(), 2,
                                            "recovered cast detail shard; prior shards preserved"});
                } else {
                    stillMissing.push_back(index);
                }
            }
        }
        unresolved = std::move(stillMissing);
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
    applyExplicitRhythmRequest(result, direction);
    SongComposer::normalizePlan(result);
    OperationalJournal::write("OK", "CHECKPOINT",
        "authoritative protagonist bound to cast identity " +
        juce::String::fromUTF8(result.narrativeSpine.protagonistInstrumentId.c_str()));
    if (ElectronicRoleContract::requiresMotionOwner(result) &&
        ElectronicRoleContract::motionOwnerCount(result) != 1) {
        error = "OpenAI cast violated the electronic motion-owner contract";
        return {};
    }
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return {};
    }

    // Phase two authors the concrete MIDI in bounded, independently retryable blocks.
    // A completed block is merged immediately, so a later transport failure never
    // discards already validated musical work.
    std::vector<std::vector<std::size_t>> blocks;
    std::vector<std::size_t> orderedInstruments(result.instruments.size());
    std::iota(orderedInstruments.begin(), orderedInstruments.end(), std::size_t{});
    std::stable_sort(orderedInstruments.begin(), orderedInstruments.end(), [&](auto left, auto right) {
        const auto& a = result.instruments[left];
        const auto& b = result.instruments[right];
        const auto familyA = static_cast<int>(voiceDefinition(a.sourceVoice).family);
        const auto familyB = static_cast<int>(voiceDefinition(b.sourceVoice).family);
        if (familyA != familyB) return familyA < familyB;
        return a.contentLaneId < b.contentLaneId;
    });
    for (std::size_t begin = 0; begin < orderedInstruments.size();
         begin += instrumentsPerPerformanceBlock) {
        std::vector<std::size_t> block;
        const auto end = std::min(orderedInstruments.size(),
                                  begin + instrumentsPerPerformanceBlock);
        for (auto index = begin; index < end; ++index) block.push_back(orderedInstruments[index]);
        blocks.push_back(std::move(block));
    }
    if (blocks.empty()) {
        error = "AI blueprint contains no instrument cast";
        return {};
    }

    PerformanceScore assembledScore;
    auto requestSerial = std::size_t{};
    auto completedBlocks = std::size_t{};
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
        for (std::size_t batchBegin = 0; batchBegin < pending.size();
             batchBegin += instrumentsPerRepairShard * maximumConcurrentRepairShards) {
            const auto batchEnd = std::min(pending.size(), batchBegin +
                instrumentsPerRepairShard * maximumConcurrentRepairShards);
            struct PendingRequest {
                std::vector<std::size_t> indices;
                std::set<std::string> replacementIds;
                std::size_t serial{};
                std::future<HttpResponse> response;
            };
            std::vector<PendingRequest> requests;
            for (auto begin = batchBegin; begin < batchEnd; begin += instrumentsPerRepairShard) {
                const auto end = std::min(batchEnd, begin + instrumentsPerRepairShard);
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
                    direction, outputText, result, shard, serial, recoveryAttempt) +
                    "\nGENERIC CONSTRAINT RECOVERY: return only these unresolved instruments. Preserve every accepted "
                    "cell elsewhere and perform only the listed operations. supply_missing_identity creates concrete MIDI "
                    "for an empty requested lane; extend_coverage adds placements in missing active regions; develop_phrase "
                    "authors separated musical statements; resolve_narrative adds or transforms material inside the "
                    "blueprint's resolution window and ends on a stable terminal-harmony pitch; "
                    "establish_thematic_relationship uses the protagonist theme_id while retaining an independent contour. "
                    "Do not add density unrelated to the measured evidence. A musical_objective improves the score but is "
                    "not permission to redesign it. The following descriptors are authoritative:\n" +
                    performanceConstraintBrief(result, assembledScore, shard, requestedCastCount > 0);
                if (!replacementIds.empty()) {
                    shardPrompt += "\nSURGICAL REPLACEMENT MODE: the following instrument material will be replaced "
                        "transactionally because adding notes cannot create its missing phrase boundaries: ";
                    for (const auto& id : replacementIds)
                        shardPrompt << juce::String::fromUTF8(id.c_str()) << " ";
                    shardPrompt += "\nReturn a complete performance for each replacement identity, preserving its role, "
                        "register, tonal function and relationship to the form. Author at least the measured phrase minimum "
                        "as musically complete statements separated by literal gaps of at least three quarters of a bar. "
                        "Use rests, withdrawal and re-entry; do not cover the entire timeline with touching or overlapping notes. "
                        "Other listed identities remain additive repairs. No other instrument may appear.";
                }
                if (!emptyInstrumentIds.empty()) {
                    shardPrompt += "\nZERO-EVENT CONTRACT: each following identity currently has no accepted MIDI and "
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
                    "\"},\"max_output_tokens\":7000,\"prompt_cache_key\":" +
                    juce::JSON::toString(juce::var(cacheKey)) + ",\"input\":" +
                    juce::JSON::toString(juce::var(shardPrompt)) +
                    ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_performance_block\","
                    "\"strict\":true,\"schema\":" + performanceBlockSchema + "}}}";
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
                    lastBlockError = shardError.isNotEmpty() ? shardError : apiErrorMessage(response);
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
                const auto missing = uncoveredInstruments(result, candidateScore, request.indices);
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
            juce::String(static_cast<int>(blocks.size())) + " · " +
            juce::String(static_cast<int>(indices.size())) + " parts"});

        const auto serial = requestSerial++;
        const auto blockPrompt = performanceBlockPrompt(direction, outputText, result, indices,
                                                         serial, attempt);
        const auto cacheKey = "pulso-performance-" + juce::String::toHexString(
            static_cast<juce::int64>(seed));
        const auto blockBody = juce::String("{\"model\":\"") + model +
            "\",\"background\":true,\"reasoning\":{\"effort\":\"" + realizationReasoningEffort +
            "\"},\"max_output_tokens\":16000,\"prompt_cache_key\":" +
            juce::JSON::toString(juce::var(cacheKey)) + ",\"input\":" +
            juce::JSON::toString(juce::var(blockPrompt)) +
            ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_performance_block\","
            "\"strict\":true,\"schema\":" + performanceBlockSchema + "}}}";
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
            if (requestedCastCount > 0 || containsEssential(indices)) {
                lastBlockError = requestedCastCount > 0
                    ? "OpenAI could not complete the explicitly requested cast"
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
            lastBlockError = blockText.isEmpty()
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
            if (requestedCastCount > 0 || containsEssential(indices)) {
                lastBlockError = requestedCastCount > 0
                    ? "OpenAI returned invalid music for the explicitly requested cast"
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

        mergePerformanceBlock(assembledScore, std::move(parsedScore), serial);
        if (!normalizeAssembledScore(assembledScore,
                "block " + juce::String(static_cast<int>(displayBlock + 1)) + " initial merge")) {
            lastBlockError = "Global performance capacity was exceeded while assembling authored blocks";
            return false;
        }
        reportMarginalBarAcceptances(
            assembledScore, indices,
            "block " + juce::String(static_cast<int>(displayBlock + 1)) + " initial");
        const auto missing = uncoveredInstruments(result, assembledScore, indices);
        if (!missing.empty()) {
            lastBlockError = juce::String(static_cast<int>(missing.size())) +
                " instruments were underwritten in block " +
                juce::String(static_cast<int>(displayBlock + 1));
            OperationalJournal::write("WARN", "VALIDATION",
                "block " + juce::String(static_cast<int>(displayBlock + 1)) +
                " initial deficits: " + performanceDeficitBrief(result, assembledScore, missing));
            auto unresolved = missing;
            for (auto recoveryAttempt = attempt + 1;
                 !unresolved.empty() && recoveryAttempt <= 3; ++recoveryAttempt)
                unresolved = recoverGranular(unresolved, recoveryAttempt, displayBlock);
            if (unresolved.empty()) return true;
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
        if (!authorBlock(blocks[index], 1, index)) {
            error = "Incremental GPT performance failed after bounded recovery: " + lastBlockError;
            return {};
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

    std::vector<std::size_t> allInstruments;
    allInstruments.reserve(result.instruments.size());
    for (std::size_t index = 0; index < result.instruments.size(); ++index)
        allInstruments.push_back(index);
    reportMarginalBarAcceptances(
        result.performanceScore, allInstruments, "final assembled score");
    auto stillMissing = uncoveredInstruments(result, result.performanceScore, allInstruments);
    if (!stillMissing.empty()) {
        const auto finalDeficits = performanceDeficitBrief(
            result, result.performanceScore, stillMissing);
        const auto finalConstraints = SelectiveRepair::performanceConstraints(
            result, result.performanceScore, stillMissing, requestedCastCount > 0);
        const auto blockingTargets = SelectiveRepair::blockingTargets(finalConstraints);
        const auto editorialTargets = SelectiveRepair::editorialTargets(finalConstraints);
        if (!editorialTargets.empty())
            OperationalJournal::write("WARN", "EDITORIAL",
                "complete score retains non-blocking musical objectives | " +
                performanceConstraintBrief(result, result.performanceScore,
                    editorialTargets, requestedCastCount > 0));
        if (!blockingTargets.empty()) {
            error = "Incremental GPT score violated a blocking identity commitment: " +
                performanceConstraintBrief(result, result.performanceScore,
                    blockingTargets, requestedCastCount > 0);
            OperationalJournal::write("ERROR", "CONSTRAINT", error);
            return {};
        }
        // Only an unrequested, non-essential identity with zero concrete MIDI is
        // retired. Quantitative and narrative findings preserve the authored track.
        stillMissing.clear();
        for (const auto& constraint : finalConstraints)
            if (!constraint.blocksPublication && constraint.evidence.notes == 0)
                stillMissing.push_back(constraint.evidence.instrumentIndex);
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
            " empty unrequested part(s); preserving authored musical objectives"});
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
                return {};
            }
            stillMissing.clear();
            for (const auto& constraint : convergedConstraints)
                if (constraint.evidence.notes == 0)
                    stillMissing.push_back(constraint.evidence.instrumentIndex);
        }
        if (!stillMissing.empty() || result.performanceScore.empty() || result.instruments.empty()) {
            error = "Incremental GPT score retained an empty identity after bounded convergence";
            return {};
        }
    }
    const auto protagonistPreserved = result.narrativeSpine.protagonistInstrumentId.empty() ||
        std::any_of(result.instruments.begin(), result.instruments.end(), [&](const auto& instrument) {
            return instrument.id == result.narrativeSpine.protagonistInstrumentId;
        });
    if (!protagonistPreserved) {
        error = "Incremental GPT score lost its declared protagonist";
        return {};
    }
    if (ElectronicRoleContract::requiresMotionOwner(result) &&
        ElectronicRoleContract::motionOwnerCount(result) != 1) {
        error = "Incremental GPT score must preserve exactly one electronic motion owner";
        return {};
    }
    if (requestedCastCount > 0 && result.instruments.size() != requestedCastCount) {
        error = "Incremental GPT score changed the explicit cast-size contract";
        return {};
    }
    // Restore the main/0.55.0 publication boundary. At this point the response has
    // already passed strict JSON parsing, the requested cast identity contract and
    // final hard-constraint classification. Quantitative and narrative objectives are
    // retained as editorial evidence. Rendering and MIDI-integrity validation happen
    // once in PluginProcessor; editorial scores must not erase this completed plan.
    OperationalJournal::write("OK", "CHECKPOINT",
        "complete AI score accepted; hard constraints passed and musical objectives remain editorial; "
        "publishing for final MIDI-integrity render");
    error.clear();
    return result;

#if 0
    // Experimental blocking editorial audition retained for source comparison only.
    // The incremental writers deliberately own only bounded groups of instruments. Their
    // blocks are now auditioned together before publication; previously this function
    // returned here and the accurate creative/soundscape findings could only become UI
    // warnings. Bounded editorial shards replace only the exact audible culprits.
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
    if (SelectiveRepair::publicationReady(initialReport)) {
        OperationalJournal::write("OK", "AUDITION", "score passed without editorial repair");
        error.clear();
        return result;
    }

    // Match the publication policy used by PULSO 0.55.0/main: once the AI score has
    // passed schema, cast, coverage and performance-block validation, creative audit
    // findings are advisory. They must never erase a complete composition. The final
    // host render still applies the independent MIDI-integrity gate for unsafe timing,
    // durations, orphan events and unresolved tonal collisions.
    OperationalJournal::write("WARN", "AUDITION",
        "complete AI score published with editorial observations; MIDI integrity "
        "will be verified after the final render");
    error.clear();
    return result;

    // A failed or merely partial repair must not turn an otherwise complete AI
    // composition into an empty result.
    auto bestPlan = result;
    auto bestPattern = initialPattern;
    auto bestReport = initialReport;
    std::set<std::string> repairedInstrumentIds;
    auto completedRepairs = std::size_t{};
    juce::String terminalReason = "audible quality remained below the publication threshold";
    const auto repairDeadline = std::min(overallDeadline,
        std::chrono::steady_clock::now() + std::chrono::minutes(3));

    for (auto repairPass = 1; repairPass <= 2; ++repairPass) {
        if (token.stop_requested()) {
            error = "Generation cancelled";
            return {};
        }
        auto diagnosis = SelectiveRepair::diagnose(bestPlan, bestPattern, bestReport, 6);
        if (!diagnosis.needed) break;
        if (repairPass > 1 && diagnosis.instrumentIndices.size() > 1) {
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
        juce::String targetList;
        for (const auto& id : targetIds) targetList += juce::String::fromUTF8(id.c_str()) + ",";
        OperationalJournal::write("INFO", "REPAIR", "pass " + juce::String(repairPass) +
            "/2 | targets=" + targetList + " | before " + audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, completedBlocks + repairPass - 1,
            blocks.size() + 2, repairPass, "selective musical repair " +
            juce::String(repairPass) + "/2 · " +
            juce::String(static_cast<int>(diagnosis.instrumentIndices.size())) + " part(s)"});

        std::vector<std::vector<std::size_t>> repairShards;
        for (std::size_t begin = 0; begin < diagnosis.instrumentIndices.size();
             begin += instrumentsPerRepairShard) {
            repairShards.emplace_back(
                diagnosis.instrumentIndices.begin() + static_cast<std::ptrdiff_t>(begin),
                diagnosis.instrumentIndices.begin() + static_cast<std::ptrdiff_t>(
                    std::min(diagnosis.instrumentIndices.size(), begin + instrumentsPerRepairShard)));
        }
        PerformanceScore repairedMaterial;
        std::set<std::string> completedTargetIds;
        auto repairSerial = std::size_t{};
        auto makeRepairBody = [&](const std::vector<std::size_t>& indices,
                                  int attempt, std::size_t shardIndex) {
            auto shardDiagnosis = diagnosis;
            shardDiagnosis.instrumentIndices = indices;
            const auto repairPrompt = selectiveRepairPrompt(
                direction, outputText, bestPlan, shardDiagnosis);
            const auto repairCacheKey = "pulso-repair-" + juce::String::toHexString(
                static_cast<juce::int64>(seed)) + "-p" + juce::String(repairPass) +
                "-s" + juce::String(static_cast<int>(shardIndex + 1)) +
                "-a" + juce::String(attempt);
            return juce::String("{\"model\":\"") + model +
                "\",\"background\":true,\"reasoning\":{\"effort\":\"" +
                realizationReasoningEffort +
                "\"},\"max_output_tokens\":8000,\"prompt_cache_key\":" +
                juce::JSON::toString(juce::var(repairCacheKey)) + ",\"input\":" +
                juce::JSON::toString(juce::var(repairPrompt)) +
                ",\"text\":{\"format\":{\"type\":\"json_schema\","
                "\"name\":\"pulso_selective_repair_shard\",\"strict\":true,\"schema\":" +
                performanceBlockSchema + "}}}";
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
                                                               shards[shardIndex]);
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
                             begin += instrumentsPerRepairShard) {
                            unresolved.emplace_back(missing.begin() + static_cast<std::ptrdiff_t>(begin),
                                missing.begin() + static_cast<std::ptrdiff_t>(
                                    std::min(missing.size(), begin + instrumentsPerRepairShard)));
                        }
                        terminalReason = "repair shard omitted " +
                            juce::String(static_cast<int>(missing.size())) + " instrument part(s)";
                    }
                }
            }
            return unresolved;
        };

        auto unresolved = executeRepairRound(repairShards, 1,
            std::min(repairRemaining, std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::seconds(90))));
        if (!unresolved.empty() && !token.stop_requested()) {
            const auto retryRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                repairDeadline - std::chrono::steady_clock::now());
            if (retryRemaining >= std::chrono::seconds(25)) {
                OperationalJournal::write("WARN", "RECOVERY", "repair pass " +
                    juce::String(repairPass) + " retrying only " +
                    juce::String(static_cast<int>(unresolved.size())) + " unresolved shard(s)");
                unresolved = executeRepairRound(unresolved, 2,
                    std::min(retryRemaining,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::seconds(75))));
            }
        }
        if (!unresolved.empty()) {
            terminalReason = "selective repair retained " +
                juce::String(static_cast<int>(completedTargetIds.size())) + "/" +
                juce::String(static_cast<int>(targetIds.size())) +
                " instruments; unresolved shards remain after bounded recovery";
            OperationalJournal::write("WARN", "REPAIR", terminalReason);
        }
        if (completedTargetIds.empty() || repairedMaterial.empty()) {
            OperationalJournal::write("WARN", "REPAIR",
                "no valid repair shard was available; preserving the prior score");
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
        OperationalJournal::write("INFO", "REPAIR", "pass " + juce::String(repairPass) +
            "/2 result | " + audibleAuditSummary(candidateReport));
        const auto safer = candidateReport.production.ready &&
            SelectiveRepair::deficit(candidateReport) + 0.01 <
                SelectiveRepair::deficit(bestReport);
        if (!safer && !SelectiveRepair::publicationReady(candidateReport)) {
            terminalReason = "selective repair did not produce a safer audible candidate";
            OperationalJournal::write("WARN", "REPAIR", terminalReason);
            break;
        }
        bestPlan = std::move(candidatePlan);
        bestPattern = candidatePattern;
        bestReport = candidateReport;
        repairedInstrumentIds.insert(completedTargetIds.begin(), completedTargetIds.end());
        ++completedRepairs;
        if (SelectiveRepair::publicationReady(bestReport)) {
            OperationalJournal::write("OK", "GATE", "strict audible contract passed after " +
                juce::String(static_cast<int>(completedRepairs)) + " repair pass(es)");
            if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
                blocks.size() + 2, repairPass, "selective repair passed the strict audible gate"});
            error.clear();
            return bestPlan;
        }
    }
    if (SelectiveRepair::editoriallyAcceptable(initialReport, bestReport, completedRepairs)) {
        OperationalJournal::write("OK", "GATE", "editorially accepted after measurable improvement | " +
            audibleAuditSummary(bestReport));
        if (progress) progress({AiSongStage::Validation, blocks.size() + completedRepairs,
            blocks.size() + 2, static_cast<int>(completedRepairs),
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

    error = terminalReason + " | " + audibleAuditSummary(bestReport);
    const auto auditFile = OperationalJournal::writeRejectedAudit(
        bestPlan, bestReport, error, completedRepairs);
    OperationalJournal::write("ERROR", "GATE", error + " | audit=" + auditFile.getFullPathName());
    return {};
#endif
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
                 << ", arrangement_independence=" << juce::String(draftReport.arrangementDensity.independenceScore, 3)
                 << ", arrangement_ready=" << (draftReport.arrangementDensity.ready ? "true" : "false") << ".\n";
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
    auditSummary << "thematic_voice_mappings=" << static_cast<int>(mappedDialoguePlacements)
                 << ", transformed_placements=" << static_cast<int>(transformedPlacements)
                 << ", dialogue_voices=" << static_cast<int>(dialogueVoices.size())
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
            applyExplicitRhythmRequest(revised, direction);
            SongComposer::normalizePlan(revised);
            GenerationContext revisedFoundation = auditFoundation;
            revisedFoundation.rootPitchClass = revised.rootPitchClass;
            revisedFoundation.scale = revised.scale;
            CompositionRenderReport revisedReport;
            [[maybe_unused]] const auto auditedRevision = SongComposer{}.render(
                revised, revisedFoundation, {}, &revisedReport);
            const auto meetsNarrativeTarget = [](const CompositionRenderReport& report) {
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
                return report.production.ready && report.narrative.creativeReady &&
                    (!report.soundscape.active || report.soundscape.ready) &&
                    report.trackViability.ready &&
                    report.narrative.primaryVoiceCoverage >= 0.65 && audibleAuthorship &&
                    memoryReady && bassReady && developmentReady && electronicReady &&
                    report.narrative.densityControl >= 0.82 &&
                    report.narrative.maximumMelodicStepRun <= 5;
            };
            const auto targetDeficit = [](const CompositionRenderReport& report) {
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
                    "note-level repair, not a fresh stylistic rewrite. Preserve the candidate's strong form, harmony, "
                    "cast and successful cells. Rewrite and replace only placements or cells responsible for these "
                    "measured failures. Principal voice coverage must be at least 0.65. A recurring theme must preserve "
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
    applyExplicitRhythmRequest(result, direction);
    result.harmonicLanguage.tonalPolicy = tonalPolicyForDirection(direction.toStdString());
    SongComposer::normalizePlan(result);
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
