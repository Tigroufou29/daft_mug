#pragma once

#include "EngineConfig.h"
#include "Effects.h"
#include "Eq.h"

namespace dm
{
// Plain snapshot of the sound parameters of ONE pad. Every pad owns its own copy:
// changing pad 1 can never change pad 2. The plugin layer fills these from the
// host-automatable parameters once per audio block.
//
// Live parameters (followed continuously while a sound plays, with smoothing -> no clicks):
//     gainDb, pan, pitchSemitones, eq (all bands)
// Per-hit parameters (captured when the pad is triggered, they shape that hit):
//     mode, startFraction/endFraction, reverse, attack/decay/sustain/release, fadeIn/fadeOut,
//     velocitySensitivity, velocityCurve
struct PadParams
{
    float gainDb = 0.0f;               // -60 .. +12 dB (<= -60 means silence)
    float pitchSemitones = 0.0f;       // -24 .. +24, transposition (speed changes with pitch)
    float pan = 0.0f;                  // -1 (left) .. +1 (right)
    PlayMode mode = PlayMode::OneShot;
    int midiNote = kFirstMidiNote;     // note that triggers this pad
    float velocitySensitivity = 1.0f;  // 0 = fixed level, 1 = fully velocity controlled
    float velocityCurve = 0.0f;        // -1 .. +1 ; 0 = linear, >0 = harder (needs more force), <0 = softer
    float startFraction = 0.0f;        // non-destructive playback region (0..1 of the file)
    float endFraction = 1.0f;
    bool reverse = false;              // plays the region backwards (the file is never modified)

    // Loop mode: the region plays from its start, then loops from loopStartFraction (0..1 of the REGION, in playing order)
    // to the end of the region; the loop seam is cross-faded over loopCrossfadeSeconds.
    float loopStartFraction = 0.0f;
    float loopCrossfadeSeconds = 0.010f;

    int chokeGroup = 0;                // 0 = none, 1..8: a hit of this pad fades out the other pads of the same group
    bool chromatic = false;            // plays on every MIDI note not used by another loaded pad, transposed relative to midiNote

    // ADSR envelope applied to every hit. "Attack" is the rise at the trigger.
    float attackSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustain = 1.0f;              // 0..1, level held after the decay (1 = no decay)
    float releaseSeconds = 0.015f;     // used when a Gate note ends

    // Fades are tied to the REGION (not to the trigger): fade-in at the start of the region,
    // fade-out at its end. Durations are measured in the source audio.
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;

    float declickSeconds = 0.0015f;    // minimum anti-click ramp at the start of each hit (export uses 0)

    float reverbWet = 0.0f;            // 0 .. 1, level of the convolution reverb (parallel, after the delay); 0 = off
    FxParams fx;                       // saturation, bitcrusher, chorus, delay (chain: sat -> crush -> EQ -> chorus -> delay)
    EqParams eq;                       // 4-band equaliser of this pad (pad bus, after the envelope, before volume / pan)
};

enum class StealPolicy : int
{
    Oldest = 0,      // when the limit is reached, fade out the oldest sounding voice
    Quietest = 1,    // fade out the voice that is currently the quietest
    IgnoreNew = 2    // never cut a playing voice: the new hit is dropped
};

// Global voice management (shared by the 16 pads).
struct EngineSettings
{
    int maxVoices = kMaxVoices;            // 1 .. kMaxVoices
    int maxVoicesPerPad = kMaxVoicesPerPad; // 1 .. kMaxVoicesPerPad
    StealPolicy stealPolicy = StealPolicy::Oldest;
};
} // namespace dm
