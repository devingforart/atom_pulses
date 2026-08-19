#include "AiComposer.h"

#include "core/Scale.h"
#include "core/TonalContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <initializer_list>
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

constexpr auto model = "gpt-5.6-terra";
constexpr std::array layerNames{"harmony", "melody", "bass", "drums"};
constexpr std::array layerChannels{3, 2, 1, 10};
constexpr std::array layerVoices{VoiceId::HarmonicFoundation, VoiceId::Lead,
                                 VoiceId::SubBass, VoiceId::CoreDrums};

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
)json") + R"json(    "motif_intervals":{"type":"array","items":{"type":"integer"},"minItems":3,"maxItems":8},
    "instruments":{"type":"array","minItems":8,"maxItems":36,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},
        "instrument":{"type":"string","enum":["kick_drum","snare_clap","hi_hats","timpani","taiko_ensemble","latin_percussion","shakers","cymbals","orchestral_percussion","piano","harp","violin_1","violin_2","viola","cello","contrabass","string_ensemble","chamber_strings","flute","piccolo","alto_flute","oboe","english_horn","clarinet","bass_clarinet","bassoon","contrabassoon","french_horns","trumpets","trombones","bass_trombone","tuba","brass_ensemble","woodwind_ensemble","choir","mallets","celesta","vibraphone","marimba","tubular_bells","electric_bass","sub_synth","analog_pad","poly_synth","lead_synth","guitar","ambient_texture"]},
        "name":{"type":"string"},
        "source_voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
        "role":{"type":"string"},"minimum_pitch":{"type":"integer"},"maximum_pitch":{"type":"integer"},
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
      },"required":["id","instrument","name","source_voice","role","minimum_pitch","maximum_pitch","octave_shift","activity","prominence","doubling","orchestral_function","articulation_intent","divisi_voices","live_device","live_preset_intent","timbre_signature","active_sections"],"additionalProperties":false
    }},
    "rhythm_motifs":{"type":"array","minItems":2,"maxItems":6,"items":{
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
      "cells":{"type":"array","minItems":1,"maxItems":64,"items":{
        "type":"object","properties":{
          "id":{"type":"string"},"theme_id":{"type":"string"},
          "narrative_function":{"type":"string","enum":["establish","question","answer","develop","withdraw","intensify","resolve","support"]},
          "length_beats":{"type":"number"},
          "owned_voices":{"type":"array","minItems":1,"maxItems":15,"items":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}},
          "notes":{"type":"array","maxItems":768,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"duration":{"type":"number"},"pitch":{"type":"integer"},
            "velocity":{"type":"integer"},"voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
            "metric_intent":{"type":"string","enum":["strict_grid"]}
          },"required":["beat","duration","pitch","velocity","voice","metric_intent"],"additionalProperties":false}},
          "controls":{"type":"array","maxItems":384,"items":{"type":"object","properties":{
            "beat":{"type":"number"},"controller":{"type":"integer"},"value":{"type":"integer"},
            "voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]}
          },"required":["beat","controller","value","voice"],"additionalProperties":false}}
        },"required":["id","theme_id","narrative_function","length_beats","owned_voices","notes","controls"],"additionalProperties":false
      }},
      "placements":{"type":"array","maxItems":512,"items":{"type":"object","properties":{
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
  "required":["title","key","summary","root_pitch_class","mode","production_language","rhythm_language","harmonic_language","orchestration_language","timbre_palette","chord_palette","motif_intervals","instruments","rhythm_motifs","voices","performance_score","sections"],
  "additionalProperties":false
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
    const auto session = WinHttpOpen(L"PULSO/0.31.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
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
        "\",\"background\":true,\"reasoning\":{\"effort\":\"medium\"},"
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

bool AiComposer::structuredOutputSchemaIsValid() {
    return !juce::JSON::parse(schema()).isVoid();
}

bool AiComposer::songPlanSchemaIsValid() {
    return !juce::JSON::parse(songPlanSchema).isVoid();
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
        "\",\"reasoning\":{\"effort\":\"medium\"},\"max_output_tokens\":16000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_composition\","
        "\"strict\":true,\"schema\":" + schema() + "}}}";

    const auto http = performRequest(body, apiKey, token, std::chrono::seconds(60));
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
        "Above those execution voices, design a production cast of 8-36 instrument instances from the supplied catalog. "
        "Use the smallest cast that can realize the requested depth. It has three coordinated departments: rhythm and "
        "percussion, harmonic fabric, and hooks or melodic speakers. source_voice is the playable archetype feeding an instrument, not its identity, and MUST reference "
        "an id present in the voices array. Every execution voice used by performance_score MUST have at least one "
        "explicit instrument owner with a compatible source_voice; never rely on an unnamed or automatically invented "
        "orchestral owner. A guitar or synth may own countermelody when its orchestral_function is counterpoint. Assign each instance "
        "a distinct role, playable register, prominence, restrained doubling probability and optional named sections. "
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
        "This is the authoritative compositional layer, not an optional sketch. Give every cell a stable theme_id "
        "shared by its recognisable transformations and a narrative_function describing what it does. Across active "
        "lead, countermelody, bass and harmonic voices, placements must author at least 65 percent of their available "
        "timeline, concentrating that authorship in statements, answers, developments, breakdown memory, climax and "
        "resolution. Procedural continuity may connect authored phrases but must never invent the principal hook, bass "
        "argument or cadence. Write movement-bass cells as coherent four-to-eight-bar lines with pocket, breath and a "
        "recognisable developed return; do not represent a bass argument as unrelated isolated notes. "
        "The audible result, not the JSON labels, is graded: at least 55 percent of rendered lead/counter notes and 60 "
        "percent of rendered movement-bass notes must come from performance_score. Author at least 30 percent of the "
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
        "that must be explicitly authored and leave gaps only when procedural continuity is desired. Never create an "
        "unmarked global silence longer than two bars. If complete silence is structurally intended, write the exact "
        "phrase full silence in that section's function; otherwise preserve sparse rhythmic, harmonic or atmospheric "
        "continuity. Repetition is permitted only when musically intentional: no "
        "foreground or bass cell may repeat verbatim more than twice, and a stable percussion cell no more than four "
        "The primary hook is the exception to novelty: state one identifiable two-to-four-bar nucleus, recall its onset pattern and contour in at least one quarter of later hook windows, and develop it by cadence, register, orchestration or one controlled interval change. Hook response must derive from that nucleus rather than introduce unrelated material. "
        "times before a materially different companion cell answers it. Use new cells and interleaved placements for "
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
        "octave_shift is strictly an octave displacement: -24, -12, 0, 12 or 24 semitones. "
        "Every authored note and placement MUST declare metric_intent=strict_grid. Source MIDI timing is always exact; "
        "anticipation, feel and microtiming belong exclusively to PULSO's reversible Human Performance playback layer. "
        "Undeclared decimal timing is invalid. Keep the score sparse enough to fit, but never delegate the principal "
        "hook, response, movement bass, harmonic identity, cadence or defining groove to procedural fallback. Reuse "
        "authored cells through placements and purposeful transformations so primary authorship remains above 65 percent. "
        "For consolidated tonality, pitched performance notes must respect the chord and tonal narrative already authored. "
        "Target duration: " + juce::String(targetSeconds) +
        " seconds; tempo: " + juce::String(bpm, 1) + " BPM; meter: " + juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. Creative direction: " + direction;

    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"background\":true,\"reasoning\":{\"effort\":\"high\"},"
        "\"max_output_tokens\":100000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_song_plan\","
        "\"strict\":true,\"schema\":" + songPlanSchema + "}}}";

    // Full-song architecture can legitimately require several minutes of reasoning. Background
    // Responses keeps the UI cancellable while short, independently bounded polls avoid a single
    // fragile HTTP connection owning the full composition lifetime.
    constexpr auto totalAiBudget = std::chrono::minutes(12);
    const auto aiStarted = std::chrono::steady_clock::now();
    if (progress) progress(AiSongStage::Architecture);
    const auto http = performRequest(body, apiKey, token, totalAiBudget);
    if (!http.connected || http.status < 200 || http.status >= 300 ||
        http.cancelled || http.timedOut) {
        error = apiErrorMessage(http);
        return result;
    }
    const auto outputText = extractOutputText(juce::JSON::parse(http.body));
    if (outputText.isEmpty()) {
        error = structuredResponseError(http.body,
            "OpenAI returned no structured song plan");
        return result;
    }
    if (!parseSongPlanJson(outputText, targetSeconds, totalBars, bpm, beatsPerBar,
                           seed, result, error,
                           tonalPolicyForDirection(direction.toStdString()))) return {};
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return {};
    }
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
    if (progress) progress(AiSongStage::Critique);
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
        "Audit performance_score at note level: cells must differ materially in rhythm, contour, register, duration, "
        "dynamics and ownership; placements must cover the form while preserving real rests. Treat repaired accidental "
        "silence windows, long verbatim runs and repeated rendered bars as concrete defects: replace them with related "
        "question/answer or developmental cells, not cosmetic velocity changes. If thematic_voice_mappings is zero or "
        "dialogue_voices is below three, create meaningful cross-instrument mappings and transformations across separate "
        "sections; do not merely double the same notes. Rewrite only weak cells "
        "and their placements while keeping strong material intact. Every related statement, answer and development "
        "must carry the same theme_id, while narrative_function must express its actual role. Raise "
        "primary_voice_authorship_coverage to at least 0.65, foreground_ai_authorship_ratio to at least 0.55, "
        "movement_bass_ai_authorship_ratio to at least 0.60, groove_authorship_coverage to at least 0.30, "
        "narrative_thematic_recall to at least 0.40, "
        "audible_thematic_similarity into a recognisable but developed 0.66-0.96 band, "
        "literal_thematic_return_ratio below 0.70, thematic_development above 0.55, "
        "density_control to at least 0.82, "
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
            std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(4))));
        revisedText.isNotEmpty()) {
        SongPlan revised;
        juce::String criticError;
        if (parseSongPlanJson(revisedText, targetSeconds, totalBars, bpm, beatsPerBar,
                              seed, revised, criticError,
                              tonalPolicyForDirection(direction.toStdString()))) {
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
                     report.electronicProduction.maximumKicklessBarsAfter <= 12 &&
                     report.electronicProduction.maximumLowEndGapBarsAfter <= 12);
                const auto audibleAuthorship =
                    (report.narrative.foregroundNotes < 8 ||
                     report.narrative.foregroundAiAuthorshipRatio >= 0.55) &&
                    (report.narrative.movementBassNotes < 8 ||
                     report.narrative.movementBassAiAuthorshipRatio >= 0.60) &&
                    (!report.electronicProduction.active ||
                     report.narrative.grooveAuthorshipCoverage >= 0.30);
                return report.production.ready && report.narrative.creativeReady &&
                    report.narrative.primaryVoiceCoverage >= 0.65 && audibleAuthorship &&
                    memoryReady && bassReady && developmentReady && electronicReady &&
                    report.narrative.densityControl >= 0.82 &&
                    report.narrative.maximumMelodicStepRun <= 5;
            };
            const auto targetDeficit = [](const CompositionRenderReport& report) {
                auto deficit = std::max(0.0, 0.65 - report.narrative.primaryVoiceCoverage) * 1.8;
                if (report.narrative.foregroundNotes >= 8)
                    deficit += std::max(0.0, 0.55 - report.narrative.foregroundAiAuthorshipRatio) * 1.5;
                if (report.narrative.movementBassNotes >= 8)
                    deficit += std::max(0.0, 0.60 - report.narrative.movementBassAiAuthorshipRatio) * 1.5;
                if (report.electronicProduction.active)
                    deficit += std::max(0.0, 0.30 - report.narrative.grooveAuthorshipCoverage) * 1.2;
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
                deficit += std::max(0.0,
                    static_cast<double>(report.narrative.maximumMelodicStepRun) - 5.0) * 0.08;
                if (report.electronicProduction.active) {
                    deficit += std::max(0.0, 0.68 - report.electronicProduction.score);
                    deficit += std::max(0.0,
                        static_cast<double>(report.electronicProduction.maximumRhythmRun) - 6.0) * 0.03;
                    deficit += std::max(0.0,
                        static_cast<double>(report.electronicProduction.maximumKicklessBarsAfter) - 12.0) * 0.04;
                    deficit += std::max(0.0,
                        static_cast<double>(report.electronicProduction.maximumLowEndGapBarsAfter) - 12.0) * 0.035;
                }
                return deficit;
            };
            const auto candidateQuality = [](const CompositionRenderReport& report) {
                const auto electronic = report.electronicProduction.active
                    ? report.electronicProduction.score : report.production.score;
                return report.narrative.score * 0.48 + electronic * 0.24 +
                    report.musical.overall * 0.18 + report.production.score * 0.10;
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
                    "reach 0.55, movement-bass AI ratio 0.60, groove coverage 0.30, and scalar runs must stop after "
                    "five steps. Club drum and low-end gaps must remain at twelve bars or less. Repair pass ") +
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
                    ". Original direction: " + direction + "\nCandidate to repair:\n" + bestRevisionText;
                if (const auto focusedText = requestRevisedSongPlan(focusedPrompt, apiKey, token,
                        std::min(remainingForRepair,
                            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(4))));
                    focusedText.isNotEmpty()) {
                    SongPlan focused;
                    juce::String focusedError;
                    if (parseSongPlanJson(focusedText, targetSeconds, totalBars, bpm, beatsPerBar,
                                          seed, focused, focusedError,
                                          tonalPolicyForDirection(direction.toStdString()))) {
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
                        if (const auto voice = voiceIdFromKey(note->getProperty("voice").toString().toStdString()))
                            parsedCell.notes.push_back({
                                static_cast<double>(note->getProperty("beat")),
                                static_cast<double>(note->getProperty("duration")),
                                static_cast<int>(note->getProperty("pitch")),
                                static_cast<int>(note->getProperty("velocity")), *voice,
                                metricIntentFromKey(note->getProperty("metric_intent").toString().toStdString())});
                    }
                if (const auto* controls = cell->getProperty("controls").getArray())
                    for (const auto& controlItem : *controls) {
                        const auto* control = controlItem.getDynamicObject();
                        if (control == nullptr) continue;
                        if (const auto voice = voiceIdFromKey(control->getProperty("voice").toString().toStdString()))
                            parsedCell.controls.push_back({
                                static_cast<double>(control->getProperty("beat")),
                                static_cast<int>(control->getProperty("controller")),
                                static_cast<int>(control->getProperty("value")), *voice});
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
