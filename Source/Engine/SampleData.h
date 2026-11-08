#pragma once

#include <memory>
#include <string>
#include <vector>

namespace dm
{
// Immutable decoded audio. Built on a background thread, then shared (read-only) with the
// audio thread through a shared_ptr<const SampleData>. The original file is never modified.
struct SampleData
{
    std::vector<float> left;
    std::vector<float> right;   // identical to `left` for mono files
    double sampleRate = 44100.0;
    std::string name;

    int numFrames() const noexcept { return static_cast<int> (left.size()); }
    bool isValid() const noexcept { return ! left.empty() && right.size() == left.size(); }
};

using SampleHandle = std::shared_ptr<const SampleData>;
} // namespace dm
