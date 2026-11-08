#pragma once

#include <memory>

#include "JuceIncludes.h"

namespace dm::ai
{
// Where the personal API key lives. The key is NEVER written to the plug-in state, presets, settings files or logs.
class SecretStore
{
public:
    virtual ~SecretStore() = default;
    virtual juce::Result save (const juce::String& secret) = 0;
    virtual juce::String load() = 0;                 // empty when nothing is stored
    virtual juce::Result clear() = 0;
    virtual bool isPersistent() const = 0;           // survives closing the application?
    virtual juce::String description() const = 0;    // honest, user-facing description of where/how the key is kept
};

// Session-only store: the key stays in RAM until the plug-in is unloaded (used where no OS store is wired up, and in tests).
class MemorySecretStore : public SecretStore
{
public:
    juce::Result save (const juce::String& s) override { secret = s; return juce::Result::ok(); }
    juce::String load() override { return secret; }
    juce::Result clear() override { secret = {}; return juce::Result::ok(); }
    bool isPersistent() const override { return false; }
    juce::String description() const override;
private:
    juce::String secret;
};

// Windows: Credential Manager (generic credential, per user). macOS / Linux: session-only for now (see description()).
std::unique_ptr<SecretStore> createDefaultSecretStore();
} // namespace dm::ai
