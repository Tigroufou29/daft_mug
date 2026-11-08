#pragma once

#include <memory>

#include "SampleData.h"

namespace dm
{
struct TempoResult
{
    double bpm = 0.0;          // 0 = nothing plausible found
    float confidence = 0.0f;   // 0..1 (rough): below ~0.2 treat the result as a guess
};

// Estimates the tempo of a loop / phrase from its onset strength (autocorrelation, folded into 75..165 BPM:
// the half / double tempo is a frequent ambiguity, the UI offers x2 and /2).
// Very short samples (< 1.5 s: kicks, snares, hits) return bpm = 0.
TempoResult detectTempo (const SampleData& sample);

// Time-stretch WITHOUT changing the pitch (WSOLA: waveform-similarity overlap-add). ratio = output length / input length,
// accepted range 0.5 .. 2.0 (ratio 2 = twice as long = half the tempo). Returns nullptr when the ratio is out of range or the
// sample is invalid. The input is never modified. Works best on loops and percussive material; extreme ratios on sustained
// tonal sounds produce some roughness.
std::shared_ptr<SampleData> timeStretch (const SampleData& in, double ratio);
} // namespace dm
