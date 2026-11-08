#pragma once

#include <functional>
#include <memory>

#include "AiCatalog.h"
#include "JuceIncludes.h"
#include "SecretStore.h"

namespace dm::ai
{
struct HttpRequest
{
    juce::String method = "GET", url, body;
    juce::String apiKey;            // sent ONLY as the x-goog-api-key header by the real transport, never in the URL
    int timeoutMs = 20000;
};

struct HttpResponse
{
    int status = 0;                 // 0 = no answer (offline, DNS, timeout...)
    juce::String body, error;
};

using Transport = std::function<HttpResponse (const HttpRequest&)>;
Transport makeDefaultTransport();   // HTTPS via the OS network stack, run from a worker thread

enum class Mode { Settings, Recipe, Explain };

struct Question
{
    Mode mode = Mode::Settings;
    juce::String request;           // the user's text
    juce::String sampleType;        // chosen by the user (Kick, Snare, Noise...), may be empty
    juce::String padName;
};

// "Daft Mug AI": optional advisor. Never in the audio path; never applies anything by itself.
// Not saved in the plug-in state; only two NON-secret preferences (enabled, model) are stored, in the user settings file.
// Message thread only (network work runs on a worker thread, callbacks come back on the message thread).
class AiService
{
public:
    AiService (std::unique_ptr<SecretStore> store, Transport transport, juce::File settingsFile = {});
    ~AiService();

    static constexpr const char* kHost = "https://generativelanguage.googleapis.com/";
    static juce::File defaultSettingsFile();

    bool isEnabled() const noexcept { return enabled; }
    void setEnabled (bool on);
    juce::String getModel() const { return model; }
    void setModel (const juce::String& m);
    juce::StringArray getKnownModels() const { return models; }

    bool hasKey();
    juce::String getKey();                              // only for the show / hide button
    juce::Result setKey (const juce::String& key);      // validates the format, stores it in the secure store
    juce::Result clearKey();
    bool storageIsPersistent() const { return store->isPersistent(); }
    juce::String storageDescription() const { return store->description(); }

    bool isBusy() const noexcept { return busy; }
    // Lists the models usable with generateContent (also proves that the key works).
    void testConnection (std::function<void (juce::Result, juce::StringArray)> done);
    // Sends one question; `done` receives the model's JSON text (to be validated by parseRecipe / parseExplanation).
    void ask (const Question&, const Catalog&, std::function<void (juce::Result, juce::String)> done);

    // exposed for tests
    static juce::String redact (const juce::String& text, const juce::String& key);
    static juce::String buildSystemPrompt (Mode, const Catalog&);
    static juce::String buildUserPrompt (const Question&, const Catalog&);
    static bool isValidModelName (const juce::String&);

private:
    void saveSettings() const;
    void run (HttpRequest, std::function<void (HttpResponse)> done);
    static juce::String httpErrorText (const HttpResponse&, const juce::String& key);

    std::unique_ptr<SecretStore> store;
    Transport transport;
    juce::File settingsFile;
    bool enabled = false, busy = false;   // opt-in: the AI stays off until the user turns it on
    juce::String model, cachedKey;
    bool keyLoaded = false;
    juce::StringArray models;
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AiService)
};
} // namespace dm::ai
