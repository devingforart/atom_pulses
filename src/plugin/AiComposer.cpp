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
    "groove_family":{"type":"string","enum":["deep_progressive_house","organic_progressive","driving_house","hybrid"]},
    "motif_intervals":{"type":"array","items":{"type":"integer"},"minItems":3,"maxItems":8},
    "chord_degrees":{"type":"array","items":{"type":"integer"},"minItems":2,"maxItems":12},
    "rhythm_motifs":{"type":"array","minItems":1,"maxItems":6,"items":{
      "type":"object","properties":{
        "id":{"type":"string"},"bars":{"type":"integer"},
        "steps_per_bar":{"type":"integer","enum":[8,16]},
        "kick":{"type":"string"},"snare_clap":{"type":"string"},
        "closed_hats":{"type":"string"},"open_hats_shaker":{"type":"string"},
        "low_percussion":{"type":"string"},"high_percussion":{"type":"string"}
      },
      "required":["id","bars","steps_per_bar","kick","snare_clap","closed_hats","open_hats_shaker","low_percussion","high_percussion"],
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
        "maximum_pitch":{"type":"integer"}
      },
      "required":["id","function","interaction","activity","syncopation","minimum_pitch","maximum_pitch"],
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
      "required":["name","function","harmonic_direction","motif_treatment","bars","energy","tension","density","motif_variant","active_voices","kick_state","kick_continuity","percussion_density","rhythmic_syncopation","swing","rhythm_motif_id","rhythm_mutations","rhythm_gestures"],
      "additionalProperties":false
    }}
  },
  "required":["title","key","summary","root_pitch_class","mode","groove_family","motif_intervals","chord_degrees","rhythm_motifs","voices","sections"],
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
};

HttpResponse performRequest(const juce::String& body, const juce::String& apiKey,
                            std::stop_token token, std::chrono::milliseconds budget) {
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
}

juce::String apiErrorMessage(const HttpResponse& response) {
    auto error = response.timedOut ? juce::String("OpenAI request reached its time budget")
               : response.cancelled ? juce::String("Generation cancelled")
               : !response.connected ? juce::String("Could not connect to OpenAI")
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
        "\",\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":9000,\"input\":" +
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
        "\",\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":16000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_composition\","
        "\"strict\":true,\"schema\":" + schema() + "}}}";

    const auto http = performRequest(body, apiKey, token, std::chrono::seconds(30));
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
        "Choose 7-15 voices from the supplied IDs; give every voice an independent function, interaction rule, "
        "activity and register. Use core_drums plus low_percussion and high_percussion as distinct rhythmic strata, "
        "multiple complementary harmonic voices, independent bass functions, melodic dialogue, atmosphere and "
        "transitions when musically justified. Do not activate every voice in every section. Complexity must come "
        "from coordinated independence, negative space and long-range development, never indiscriminate density. "
        "Treat active_voices as an available cast, not a command to play continuously: design implied entrances, "
        "responses, withdrawals, breath before arrivals, tension plateaus and genuine low-density descents. "
        "Harmonic tension must follow the dramatic curve. minimum_pitch and maximum_pitch are MIDI pitches. "
        "The key label, root_pitch_class and mode MUST describe exactly the same tonal centre. Chromatic notes are "
        "reserved for brief prepared passing motion that resolves by semitone; structural notes remain diatonic. "
        "Write a deliberate rhythm score for every section. kick_state defines its stable identity and "
        "kick_continuity says whether quarter-note anchors are mandatory. Use four_on_floor as the normal driving "
        "foundation for house and progressive-house requests. Create contrast through explicit rhythm_gestures: "
        "mute or remove kicks before transitions, add occasional double kicks or pickups, then restore the established "
        "groove after breaks. Every exception must have structural purpose and gestures must remain rare. Kick, "
        "snare/clap, closed hats, open hats/shaker and percussion are independent voices. Never choose a breakbeat "
        "unless the user explicitly requests one. percussion_density, rhythmic_syncopation and swing are 0 to 1; "
        "bar_offset is local to its section and beat is zero-based. Invent 1-6 reusable rhythm_motifs as open "
        "one-to-four-bar rhythmic DNA. Each lane mask has exactly bars * steps_per_bar characters: 0 is silence, "
        "1 is a normal hit and 2 is an accent. Do not default every motif to a generic grid: internally consider "
        "at least three rhythm solutions, then choose the one whose kick, clap, hats and two percussion lanes form "
        "the clearest conversation. Sections develop a shared motif through sparse rhythm_mutations rather than "
        "replacing it arbitrarily. Every mutation needs an audible dramatic purpose. Preserve silence, asymmetry, "
        "call-and-response and recognizable lineage across the full song. The macro kick contract still wins when "
        "the user explicitly requests constant quarter-note kick or when a section deliberately mutes it. "
        "The section bars MUST sum exactly to ") + juce::String(totalBars) + ". Use between 5 and 14 sections. Energy, tension "
        "and density are values from 0 to 1. Chord degrees use 0-6. Motif intervals are semitones relative to the "
        "tonic and form the immutable thematic DNA. Target duration: " + juce::String(targetSeconds) +
        " seconds; tempo: " + juce::String(bpm, 1) + " BPM; meter: " + juce::String(beatsPerBar, 2) +
        " quarter-note beats per bar. Creative direction: " + direction;

    const auto body = juce::String("{\"model\":\"") + model +
        "\",\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":9000,\"input\":" +
        juce::JSON::toString(juce::var(prompt)) +
        ",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"pulso_song_plan\","
        "\"strict\":true,\"schema\":" + songPlanSchema + "}}}";

    constexpr auto totalAiBudget = std::chrono::seconds(65);
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
                           seed, result, error)) return {};
    if (token.stop_requested()) {
        error = "Generation cancelled";
        return {};
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
        "Before selecting the revision, silently compare at least three plausible rhythmic developments. Audit "
        "dance-floor foundation, motif lineage, kick-bass interlock, meaningful silence, phrase-level cause and "
        "effect, orchestral breathing, contrast and climax. Repair generic repetition or arbitrary novelty. Use "
        "rhythm masks as musical cells and sparse mutations as development; do not merely add density. The final "
        "JSON must be self-contained. Original creative direction: ") + direction +
        "\nCandidate plan to critique and revise:\n" + outputText;
    if (const auto revisedText = requestRevisedSongPlan(criticPrompt, apiKey, token,
            std::min(remaining, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(10))));
        revisedText.isNotEmpty()) {
        SongPlan revised;
        juce::String criticError;
        if (parseSongPlanJson(revisedText, targetSeconds, totalBars, bpm, beatsPerBar,
                              seed, revised, criticError))
            result = std::move(revised);
    }
    applyExplicitRhythmRequest(result, direction);
    SongComposer::normalizePlan(result);
    return result;
}

bool AiComposer::parseSongPlanJson(const juce::String& text, int targetSeconds,
                                   int requestedBars, double bpm, double beatsPerBar,
                                   std::uint64_t seed, SongPlan& result,
                                   juce::String& error) {
    const auto parsed = juce::JSON::parse(text);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr || requestedBars < 8 || requestedBars > 512) {
        error = "Song-plan JSON is invalid";
        return false;
    }
    result = {};
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
    if (const auto groove = grooveFamilyFromKey(
            object->getProperty("groove_family").toString().toStdString()))
        result.grooveFamily = *groove;
    if (const auto* motif = object->getProperty("motif_intervals").getArray())
        for (const auto& value : *motif) result.motifIntervals.push_back(static_cast<int>(value));
    if (const auto* chords = object->getProperty("chord_degrees").getArray())
        for (const auto& value : *chords) result.chordDegrees.push_back(static_cast<int>(value));
    if (const auto* motifs = object->getProperty("rhythm_motifs").getArray()) {
        for (const auto& item : *motifs) {
            const auto* motif = item.getDynamicObject();
            if (motif == nullptr) continue;
            result.rhythmMotifs.push_back({
                motif->getProperty("id").toString().trim().toStdString(),
                static_cast<int>(motif->getProperty("bars")),
                static_cast<int>(motif->getProperty("steps_per_bar")),
                motif->getProperty("kick").toString().toStdString(),
                motif->getProperty("snare_clap").toString().toStdString(),
                motif->getProperty("closed_hats").toString().toStdString(),
                motif->getProperty("open_hats_shaker").toString().toStdString(),
                motif->getProperty("low_percussion").toString().toStdString(),
                motif->getProperty("high_percussion").toString().toStdString()});
        }
    }
    if (const auto* voices = object->getProperty("voices").getArray()) {
        for (const auto& item : *voices) {
            const auto* voice = item.getDynamicObject();
            if (voice == nullptr) continue;
            const auto id = voiceIdFromKey(voice->getProperty("id").toString().toStdString());
            if (!id) continue;
            result.voices.push_back({*id,
                voice->getProperty("function").toString().trim().toStdString(),
                voice->getProperty("interaction").toString().trim().toStdString(),
                static_cast<double>(voice->getProperty("activity")),
                static_cast<double>(voice->getProperty("syncopation")),
                static_cast<int>(voice->getProperty("minimum_pitch")),
                static_cast<int>(voice->getProperty("maximum_pitch"))});
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
