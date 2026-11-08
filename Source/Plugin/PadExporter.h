#pragma once

#include "JuceIncludes.h"
#include "PadParams.h"
#include "SamplerEngine.h"
#include "SampleData.h"

namespace dm
{
struct ExportOptions
{
    int bits = 24;             // 16, 24 or 32 (32 = floating point)
    double sampleRate = 0.0;   // 0 = keep the sample's own rate
    bool normalize = false;    // peak-normalise to -0.1 dBFS
    ReverbHandle reverb;       // convolution reverb of the pad (built for the export rate), may be null
};

// Renders what the pad actually plays (region, pitch, volume, pan) offline with the same engine
// as live playback, then writes a stereo WAV. Pure function: safe to run on a background thread.
// EQ, effects and the convolution reverb are rendered too (with their tails).
juce::Result exportPadToWav (const SampleHandle& sample, PadParams params,
                             const ExportOptions& options, const juce::File& destination);
} // namespace dm
