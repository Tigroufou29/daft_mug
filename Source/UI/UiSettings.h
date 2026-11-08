#pragma once

#include "JuceIncludes.h"

namespace dm::ui
{
// Small per-user interface settings (NOT part of the project / preset, never contains secrets).
// Stored in <user data>/DaftMug/ui.settings
struct UiSettings
{
    // "auto" (JUCE decides), "software" (CPU) or "gpu" (Direct2D on Windows)
    static juce::String getRenderer()
    {
        auto v = open().getValue ("renderer", "auto");
        return v == "software" || v == "gpu" ? v : "auto";
    }

    static void setRenderer (const juce::String& mode)
    {
        auto props = open();
        props.setValue ("renderer", mode);
        props.saveIfNeeded();
    }

private:
    static juce::PropertiesFile open()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "DaftMug";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = "DaftMug";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return juce::PropertiesFile (o.getDefaultFile().getSiblingFile ("ui.settings"), o);
    }
};
} // namespace dm::ui
