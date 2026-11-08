#pragma once

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm
{
enum class PresetKind { Pad, Kit };

struct PresetInfo
{
    juce::File file;
    juce::String name;
    juce::String category;   // "" = no category
};

constexpr int kPresetSchemaVersion = 1;

// Pad presets (.dmpad) and kit presets (.dmkit), stored as small XML files.
//  - A pad preset holds every sound parameter of ONE pad (EQ, effects, reverb choice, envelope...). It is pad independent: a
//    preset saved from pad 3 loads into pad 8. The MIDI note is NOT part of it (the pad keeps its own). The sample file is
//    stored only when asked.
//  - A kit preset holds the 16 pads (parameters incl. MIDI notes, names, colours, sample paths, custom IR) and the global
//    voice settings.
// Whitelist only: parameters of the plugin + pad meta. Never anything else - in particular NEVER an API key or any secret.
// Message thread only.
class PresetManager
{
public:
    explicit PresetManager (DaftMugProcessor& p) : processor (p) {}

    juce::File getFolder (PresetKind kind) const;   // <DAFTMUG_PRESET_DIR or user data>/DaftMug/Presets/{Pads|Kits}
    juce::Array<PresetInfo> list (PresetKind kind) const;

    juce::Result savePad (int pad, const juce::String& name, const juce::String& category, bool includeSample);
    juce::Result loadPad (int pad, const juce::File& file);
    juce::Result saveKit (const juce::String& name, const juce::String& category);
    juce::Result loadKit (const juce::File& file);
    // Writes a kit to an arbitrary file. With relativePaths, sample / IR paths are stored relative to the kit file's folder
    // (portable kit); loadKit resolves relative paths against the folder of the kit file it reads.
    juce::Result saveKitTo (const juce::File& dest, const juce::String& name, const juce::String& category, bool relativePaths);

    static juce::String sanitizeFileName (const juce::String& name);

private:
    void writeParams (juce::XmlElement& parent, const juce::String& prefix, bool withNote) const;
    int readParams (const juce::XmlElement& parent, const juce::String& prefix, bool withNote);
    juce::File fileFor (PresetKind kind, const juce::String& name, const juce::String& category) const;

    DaftMugProcessor& processor;
};
} // namespace dm
