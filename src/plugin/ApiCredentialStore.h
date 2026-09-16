#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

namespace pulso::plugin {

enum class ApiCredentialSource : std::uint8_t {
    None = 0,
    WindowsCredentialManager,
    Environment
};

// Process-wide credential access. The secret is never part of AudioProcessor state,
// ValueTree state, logs or project files. On Windows it is persisted by Credential
// Manager for the current user; OPENAI_API_KEY remains a read-only compatibility path.
class ApiCredentialStore final {
public:
    [[nodiscard]] static juce::String apiKey();
    [[nodiscard]] static bool hasKey();
    [[nodiscard]] static ApiCredentialSource source();
    [[nodiscard]] static bool isPlausibleKey(const juce::String&) noexcept;
    [[nodiscard]] static bool save(const juce::String&, juce::String& error);
    [[nodiscard]] static bool removeSaved(juce::String& error);
    static void refresh();
};

} // namespace pulso::plugin
