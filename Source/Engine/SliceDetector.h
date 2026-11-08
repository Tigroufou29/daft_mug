#pragma once

#include <vector>

namespace dm
{
enum class SliceMode : int { Transients = 0, EqualParts = 1 };

struct SliceConfig
{
    SliceMode mode = SliceMode::Transients;
    float sensitivity = 0.5f;     // 0 = only the strongest attacks .. 1 = also soft ones
    int maxSlices = 16;           // 1 .. 16
    float minSliceMs = 60.0f;     // no slice shorter than this (transient mode)
    int parts = 8;                // equal-parts mode: 1 .. 16
    bool snapToZeroCrossing = true;   // moves each cut to a nearby zero crossing (avoids clicks)
};

// Returns the start frame of every slice, ascending (the slice i runs from point[i] to point[i+1], the last one to the end).
// Never more than config.maxSlices points; never empty (at worst {0}). Pure function, no JUCE, no allocation constraints
// (runs on the message thread, never on the audio thread).
std::vector<int> detectSlicePoints (const float* left, const float* right, int numFrames, double sampleRate, const SliceConfig& config);

// Cleans a user-edited list: sorted, unique, inside [0, numFrames), at least `minFrames` apart, at most maxSlices.
std::vector<int> normaliseSlicePoints (std::vector<int> points, int numFrames, int minFrames, int maxSlices);
} // namespace dm
