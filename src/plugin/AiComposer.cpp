#include "AiComposer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pulso::plugin {
namespace {

constexpr auto model = "gpt-5.6-terra";
constexpr std::array layerNames{"harmony", "melody", "bass", "drums"};
constexpr std::array layerChannels{3, 2, 1, 10};

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
        "the chord movement and drums must reinforce the same phrasing. Avoid random scale runs, repetitive grids and "
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

    auto url = juce::URL("https://api.openai.com/v1/responses").withPOSTData(body);
    auto statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders("Content-Type: application/json\r\nAuthorization: Bearer " + apiKey + "\r\n")
            .withConnectionTimeoutMs(45000)
            .withStatusCode(&statusCode));
    if (stream == nullptr) {
        error = "Could not connect to OpenAI";
        return result;
    }
    const auto responseText = stream->readEntireStreamAsString();
    if (statusCode < 200 || statusCode >= 300) {
        error = "OpenAI HTTP " + juce::String(statusCode);
        if (const auto parsed = juce::JSON::parse(responseText); !parsed.isVoid()) {
            if (const auto* object = parsed.getDynamicObject()) {
                if (const auto* apiError = object->getProperty("error").getDynamicObject())
                    error += ": " + apiError->getProperty("message").toString();
            }
        }
        return result;
    }
    const auto response = juce::JSON::parse(responseText);
    const auto outputText = extractOutputText(response);
    if (outputText.isEmpty()) {
        error = "OpenAI returned no structured composition";
        return result;
    }
    parseCompositionJson(outputText, bars, result, error);
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
            result.pattern.notes.push_back({start,
                std::min(duration, result.pattern.lengthBeats - start), pitch, velocity,
                layerChannels[layer]});
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
    if (result.title.isEmpty()) result.title = "Untitled Idea";
    if (result.key.isEmpty()) result.key = "Unspecified key";
    return true;
}

} // namespace pulso::plugin
