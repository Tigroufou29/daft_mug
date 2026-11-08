#pragma once

#include <vector>

#include "JuceIncludes.h"
#include "PadExporter.h"
#include "PluginProcessor.h"

namespace dm
{
struct KitExportItem
{
    int pad = 0;
    juce::String baseName;       // "01_Kick"
    SampleHandle sample;
    PadParams params;
    ExportOptions options;       // includes this pad's own reverb instance
};

struct KitExportReport
{
    int written = 0, failed = 0;
    juce::StringArray files, problems;
};

// Renders every loaded pad of the kit to its own WAV file ("01_Name.wav" ...) with the same offline renderer as the single-pad
// export (region, pitch, volume, pan, EQ, effects, reverb tail). Existing files are never overwritten: a number is appended.
class KitExporter
{
public:
    // message thread: snapshots what each pad plays right now (parameters, sample, reverb instance)
    static std::vector<KitExportItem> prepare (DaftMugProcessor&, int bits, double sampleRate, bool normalize);
    // any thread
    static KitExportReport run (const std::vector<KitExportItem>&, const juce::File& folder);
};
} // namespace dm
