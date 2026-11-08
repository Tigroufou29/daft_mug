#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dm
{
// The sound parameters Mutate may vary. Variants are PARAMETER sets: the sample is never touched.
struct MutateValues
{
    float pitchSemitones = 0.0f;    // -24 .. 24
    float pan = 0.0f;               // -1 .. 1
    float gainDb = 0.0f;            // -60 .. 12
    float attackMs = 0.0f;          // 0 .. 2000
    float reverbWet = 0.0f;         // 0 .. 1 (only meaningful when the pad has a reverb)
    std::array<float, 4> eqGainDb { 0.0f, 0.0f, 0.0f, 0.0f };   // -18 .. 18, bands 1-4
};

struct MutateConfig
{
    bool pitch = true, pan = true, gain = false, attack = true, eq = true, reverb = true;
    float amount = 0.5f;            // 0 .. 1 global intensity (scales every range below)

    // maximum deviation at amount = 1
    float pitchRange = 1.0f;        // semitones, +/-
    float panRange = 0.5f;          // +/-
    float gainRange = 3.0f;         // dB, +/-
    float attackRange = 40.0f;      // ms, added (an attack can only get slower than the original)
    float eqRange = 4.0f;           // dB, +/- per band
    float reverbRange = 0.3f;       // +/-
};

// Returns `count` variants (1..16). Variant 0 is ALWAYS the original, unchanged, so it can be compared. The others are
// spread over the allowed ranges (one value per slice of the range for every parameter, shuffled: Latin hypercube), so
// no two variants are alike. Deterministic for a given seed. Parameters that are disabled keep their original value.
// Values never leave the legal range of their parameter.
std::vector<MutateValues> generateVariants (const MutateValues& base, const MutateConfig& config, int count, uint32_t seed);
} // namespace dm
