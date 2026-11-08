#pragma once

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm
{
struct CollectReport
{
    int samplesCopied = 0;        // distinct sample files copied
    int irsCopied = 0;
    int missing = 0;              // pads whose file could not be found (left untouched)
    int renamed = 0;              // files renamed to avoid a name collision
    int libraryIrs = 0;           // pads using a library reverb (Plate, Hall...): not copied, they come from the IR folder
    juce::StringArray problems;
    juce::File kitFile;
};

// Gathers everything the 16 pads need into ONE folder so the kit can move to another machine:
//   <folder>/Samples/   decoded-source audio files (one copy per distinct file, name collisions get _2, _3 ...)
//   <folder>/IR/        custom impulse responses
//   <folder>/<name>.dmkit   the kit, with paths RELATIVE to the folder (load it with "Charger un kit depuis un fichier...")
// The original files are never modified or deleted. The pads are re-attached to the copies (same sound, new path), so the
// current project becomes portable too: the host project then references the collected files.
// Message thread; copies are synchronous (at most 16 files).
class Collector
{
public:
    explicit Collector (DaftMugProcessor& p) : processor (p) {}
    juce::Result collect (const juce::File& folder, const juce::String& kitName, CollectReport& report);

private:
    juce::File copyUnique (const juce::File& source, const juce::File& dir, CollectReport& report);
    DaftMugProcessor& processor;
};
} // namespace dm
