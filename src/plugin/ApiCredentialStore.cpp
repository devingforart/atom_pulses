#include "ApiCredentialStore.h"

#include <algorithm>
#include <mutex>
#include <vector>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincred.h>
#endif

namespace pulso::plugin {
namespace {

constexpr auto credentialTarget = L"PULSO/OpenAI/APIKey";

struct CachedCredential {
    juce::String key;
    ApiCredentialSource source{ApiCredentialSource::None};
    bool initialized{};
};

std::mutex credentialMutex;
CachedCredential credentialCache;

CachedCredential readCredential() {
    CachedCredential result;
    result.initialized = true;
#if JUCE_WINDOWS
    const auto secureStoreDisabled = juce::SystemStats::getEnvironmentVariable(
        "PULSO_DISABLE_SECURE_CREDENTIALS", {}) == "1";
    if (!secureStoreDisabled) {
        PCREDENTIALW credential{};
        if (CredReadW(credentialTarget, CRED_TYPE_GENERIC, 0, &credential) != FALSE) {
            if (credential != nullptr && credential->CredentialBlob != nullptr &&
                credential->CredentialBlobSize > 0) {
                result.key = juce::String::fromUTF8(
                    reinterpret_cast<const char*>(credential->CredentialBlob),
                    static_cast<int>(credential->CredentialBlobSize)).trim();
                if (result.key.isNotEmpty())
                    result.source = ApiCredentialSource::WindowsCredentialManager;
            }
            if (credential != nullptr) CredFree(credential);
        }
    }
#endif
    if (result.key.isEmpty()) {
        result.key = juce::SystemStats::getEnvironmentVariable("OPENAI_API_KEY", {}).trim();
        if (result.key.isNotEmpty()) result.source = ApiCredentialSource::Environment;
    }
    return result;
}

CachedCredential cachedCredential() {
    std::scoped_lock lock(credentialMutex);
    if (!credentialCache.initialized) credentialCache = readCredential();
    return credentialCache;
}

void replaceCache(CachedCredential value) {
    std::scoped_lock lock(credentialMutex);
    credentialCache = std::move(value);
}

} // namespace

juce::String ApiCredentialStore::apiKey() { return cachedCredential().key; }

bool ApiCredentialStore::hasKey() { return cachedCredential().key.isNotEmpty(); }

ApiCredentialSource ApiCredentialStore::source() { return cachedCredential().source; }

bool ApiCredentialStore::isPlausibleKey(const juce::String& candidate) noexcept {
    const auto key = candidate.trim();
    return key.startsWith("sk-") && key.length() >= 24 && !key.containsAnyOf(" \t\r\n");
}

bool ApiCredentialStore::save(const juce::String& candidate, juce::String& error) {
    const auto key = candidate.trim();
    if (!isPlausibleKey(key)) {
        error = "The API key format is invalid";
        return false;
    }
#if JUCE_WINDOWS
    const auto utf8 = key.toUTF8();
    const auto byteCount = static_cast<std::size_t>(utf8.sizeInBytes() - 1);
    std::vector<unsigned char> bytes(byteCount);
    std::copy_n(reinterpret_cast<const unsigned char*>(utf8.getAddress()), byteCount,
                bytes.begin());
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(credentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = bytes.data();
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"OpenAI API key");
    const auto saved = CredWriteW(&credential, 0) != FALSE;
    SecureZeroMemory(bytes.data(), bytes.size());
    if (!saved) {
        error = "Windows Credential Manager could not save the API key (error " +
            juce::String(static_cast<int>(GetLastError())) + ")";
        return false;
    }
    replaceCache({key, ApiCredentialSource::WindowsCredentialManager, true});
    error.clear();
    return true;
#else
    error = "Secure in-plugin key storage is currently available on Windows only";
    return false;
#endif
}

bool ApiCredentialStore::removeSaved(juce::String& error) {
#if JUCE_WINDOWS
    if (CredDeleteW(credentialTarget, CRED_TYPE_GENERIC, 0) == FALSE) {
        const auto code = GetLastError();
        if (code != ERROR_NOT_FOUND) {
            error = "Windows Credential Manager could not remove the API key (error " +
                juce::String(static_cast<int>(code)) + ")";
            return false;
        }
    }
#endif
    replaceCache(readCredential());
    error.clear();
    return true;
}

void ApiCredentialStore::refresh() { replaceCache(readCredential()); }

} // namespace pulso::plugin
