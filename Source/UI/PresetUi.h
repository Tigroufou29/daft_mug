#pragma once

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// "Presets" menu of the pad editor: save (settings only, or with the sample), load (by category), delete, open folder.
void showPadPresetMenu (juce::Component& anchor, DaftMugProcessor& processor, int pad);

// "Kits" menu of the main window: save / load / delete the 16 pads, open folder.
void showKitPresetMenu (juce::Component& anchor, DaftMugProcessor& processor);
} // namespace dm::ui
