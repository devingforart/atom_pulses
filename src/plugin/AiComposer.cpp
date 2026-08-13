#include "AiComposer.h"

#include "core/Scale.h"
#include "core/TonalContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <initializer_list>
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

const juce::String songPlanSchema = R"json({
  "type":"object",
  "properties":{
    "title":{"type":"string"},
    "key":{"type":"string"},
    "summary":{"type":"string"},
    "root_pitch_class":{"type":"integer"},
    "mode":{"type":"string","enum":["major","minor","dorian","mixolydian"]},
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
    "motif_intervals":{"type":"array","items":{"type":"integer"},"minItems":3,"maxItems":8},
    "instruments":{"type":"array","minItems":12,"maxItems":36,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},
        "instrument":{"type":"string","enum":["kick_drum","snare_clap","hi_hats","timpani","taiko_ensemble","latin_percussion","shakers","cymbals","orchestral_percussion","piano","harp","violin_1","violin_2","viola","cello","contrabass","string_ensemble","chamber_strings","flute","piccolo","alto_flute","oboe","english_horn","clarinet","bass_clarinet","bassoon","contrabassoon","french_horns","trumpets","trombones","bass_trombone","tuba","brass_ensemble","woodwind_ensemble","choir","mallets","celesta","vibraphone","marimba","tubular_bells","electric_bass","sub_synth","analog_pad","poly_synth","lead_synth","guitar","ambient_texture"]},
        "name":{"type":"string"},
        "source_voice":{"type":"string","enum":["core_drums","low_percussion","high_percussion","sub_bass","movement_bass","harmonic_foundation","harmonic_pulse","harmonic_upper","lead","countermelody","atmosphere","transitions","snare_clap","closed_hats","open_hats_shaker"]},
        "role":{"type":"string"},"minimum_pitch":{"type":"integer"},"maximum_pitch":{"type":"integer"},
        "octave_shift":{"type":"integer"},"activity":{"type":"number"},
        "prominence":{"type":"number"},"doubling":{"type":"number"},
        "orchestral_function":{"type":"string","enum":["foundation","body","extension","counterpoint","color","transition"]},
        "articulation_intent":{"type":"string","enum":["natural","legato","staccato","detached","sustained","swelling","tremolo","pizzicato","ostinato"]},
        "divisi_voices":{"type":"integer"},
        "live_device":{"type":"string","enum":["auto","Drum Rack","Instrument Rack","Simpler","Sampler","Drift","Meld","Wavetable","Operator","Analog","Electric","Tension","Collision","Granulator III"]},
        "live_preset_intent":{"type":"string"},
        "active_sections":{"type":"array","maxItems":20,"items":{"type":"string"}}
      },"required":["id","instrument","name","source_voice","role","minimum_pitch","maximum_pitch","octave_shift","activity","prominence","doubling","orchestral_function","articulation_intent","divisi_voices","live_device","live_preset_intent","active_sections"],"additionalProperties":false
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
  "required":["title","key","summary","root_pitch_class","mode","rhythm_language","harmonic_language","orchestration_language","chord_palette","motif_intervals","instruments","rhythm_motifs","voices","sections"],
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
HttpResponse performWindowsRequest(const juce::String& body, const juce::String& apiKey,
                                   std::stop_token token, std::chrono::milliseconds budget) {
    HttpResponse result;
    if (token.stop_requested()) {
        result.cancelled = true;
        return result;
    }

    const auto timeoutMs = std::clamp(static_cast<int>(budget.count()), 1000, 120000);
    const auto session = WinHttpOpen(L"PULSO/0.17.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
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
    const auto request = WinHttpOpenRequest(connection, L"POST", L"/v1/responses",
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
    const auto bodyBytes = static_cast<DWORD>(utf8Body.sizeInBytes() - 1);
    auto sent = WinHttpSendRequest(request, headers.toWideCharPointer(),
        static_cast<DWORD>(-1L), const_cast<char*>(utf8Body.getAddress()), bodyBytes, bodyBytes, 0) != FALSE;
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
#endif

HttpResponse performRequest(const juce::String& body, const juce::String& apiKey,
                            std::stop_token token, std::chrono::milliseconds budget) {
#if JUCE_WINDOWS
    return performWindowsRequest(body, apiKey, token, budget);
#else
    HttpResponse result;
    if (token.stop_requested()) {
        result.cancelled = true;
        return result;
    }

    juce::WebInputStream stream(
        juce::URL("https://api.openai.com/v1/responses").withPOSTData(body), true);
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
#endif
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

juce::String requestRevisedSongPlan(const juce::String& prompt, const juce::String& apiKey,
                                    std::stop_token token, std::chrono::milliseconds budget) {
    if (token.stop_requested()) return {};
    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":20000,\"input\":" +
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
    const auto prompt = juce::String(
        "You are the long-form composition architect for PULSO. Design one complete song, not a loop. "
        "Create a narratively inevitable form with introduction, thematic statements, contrast, development, "
        "a true climax and a conclusive ending. Recurring sections must share a recognisable motif while changing "
        "orchestration, register, harmony or rhythm. Design a variable ensemble rather than four fixed layers. "
        "Choose 7-15 execution voices from the supplied IDs; give every voice an independent function, interaction rule, "
        "activity and register. Use core_drums plus low_percussion and high_percussion as distinct rhythmic strata, "
        "multiple complementary harmonic voices, independent bass functions, melodic dialogue, atmosphere and "
        "transitions when musically justified. Do not activate every voice in every section. "
        "Above those execution voices, design an orchestra of 12-36 instrument instances from the supplied catalog. "
        "The orchestra has three coordinated departments: rhythm/percussion, harmony/orchestral fabric and melodic "
        "speakers. source_voice is the playable archetype feeding an instrument, not its identity, and MUST reference "
        "an id present in the voices array. Assign each instance "
        "a distinct role, playable register, prominence, restrained doubling probability and optional named sections. "
        "For every instrument, author orchestral_function, articulation_intent and divisi_voices. The functions "
        "foundation, body, extension, counterpoint, color and transition are compositional responsibilities: distribute "
        "them across harmonic families so strings, winds, brass, keys and hybrid layers contribute independent, "
        "voice-led material rather than cloning one chord track. Use harmonic_depth, counterpoint_activity, divisi_depth, "
        "articulation_contrast, family_dialogue and hybrid_production to define how that ensemble thinks. "
        "Choose live_device only from the supplied Ableton-native device enum and describe the desired installed sound "
        "in live_preset_intent with concise English browser-search nouns, even when the user writes in another language "
        "(for example: solo cello, closed hi-hat, chamber strings, warm analog pad). Never invent a factory preset name. "
        "The local Live resolver matches that intent against installed content and will never treat an empty Rack, "
        "Sampler or Simpler container as a playable sound. "
        "Use strings, winds, brass, keyboards, electronics and percussion only when the creative direction benefits. "
        "Rotate foreground ownership between instruments and families; a lead source may become flute, cello, violin, "
        "oboe or synth in different phrases without losing thematic identity. Build chamber reductions, antiphonal "
        "answers, divisi, octave doublings and rare tutti arrivals. Never make every instrument play continuously and "
        "never turn orchestration into indiscriminate unison doubling. Bass remains an independent bridge between "
        "harmony and rhythm. Write orchestration_language as the global timbral argument and use active_sections to "
        "reserve colours for meaningful moments. Instrument names must be clear DAW track names. Complexity must come "
        "from coordinated independence, negative space and long-range development, never indiscriminate density. "
        "Treat active_voices as an available cast, not a command to play continuously: design implied entrances, "
        "responses, withdrawals, breath before arrivals, tension plateaus and genuine low-density descents. "
        "Harmonic tension must follow the dramatic curve. minimum_pitch and maximum_pitch are MIDI pitches. "
        "The key label, root_pitch_class and mode define the binding perceptual tonal centre for ordinary requests. "
        "Use consolidated tonality by default: structural notes, chord roots, basses and pitch-class sets remain in "
        "the home scale, while extensions, inversions and voice leading create richness inside it. A chromatic melodic "
        "passing or neighbour note must be short, weak, approached stepwise and immediately resolve stepwise. Never let "
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
        "tonic and form the immutable thematic DNA. Target duration: " + juce::String(targetSeconds) +
        " seconds; tempo: " + juce::String(bpm, 1) + " BPM; meter: " + juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. Creative direction: " + direction;

    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"reasoning\":{\"effort\":\"medium\"},\"max_output_tokens\":20000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_song_plan\","
        "\"strict\":true,\"schema\":" + songPlanSchema + "}}}";

    constexpr auto totalAiBudget = std::chrono::seconds(210);
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
        error = "OpenAI returned no structured song plan";
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
                 << ", musical_quality=" << juce::String(draftReport.musical.overall, 3) << ".\n";
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
        "Repair soloist monopoly, fake symphonic density, four-chord cycling, decorative complexity, generic repetition or arbitrary novelty. Use "
        "rhythm masks as musical cells and sparse mutations as development; do not merely add density. The final "
        "JSON must be self-contained. Original creative direction: ") + direction +
        auditSummary + "\nCandidate plan to critique and revise:\n" + outputText;
    if (const auto revisedText = requestRevisedSongPlan(criticPrompt, apiKey, token,
            std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(50))));
        revisedText.isNotEmpty()) {
        SongPlan revised;
        juce::String criticError;
        if (parseSongPlanJson(revisedText, targetSeconds, totalBars, bpm, beatsPerBar,
                              seed, revised, criticError,
                              tonalPolicyForDirection(direction.toStdString()))) {
            result = std::move(revised);
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
            if (const auto* sections = instrument->getProperty("active_sections").getArray())
                for (const auto& section : *sections)
                    assignment.activeSections.push_back(section.toString().trim().toStdString());
            result.instruments.push_back(std::move(assignment));
        }
    }
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
    SongComposer::normalizePlan(result);
    if (result.title.empty()) result.title = "Untitled Song";
    if (result.key.empty()) result.key = "Unspecified key";
    return true;
}

} // namespace pulso::plugin
