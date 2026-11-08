#pragma once

#include "JuceIncludes.h"

// Parameter IDs are PERMANENT: hosts store them in projects and automation lanes.
// Never rename an ID; add new ones instead (and migrate old states in setStateInformation).
namespace dm::ids
{
inline juce::String make (int pad, const char* suffix)
{
    return "pad" + juce::String (pad + 1).paddedLeft ('0', 2) + "_" + suffix;
}

inline juce::String gain  (int pad) { return make (pad, "gain"); }
inline juce::String pitch (int pad) { return make (pad, "pitch"); }
inline juce::String pan   (int pad) { return make (pad, "pan"); }
inline juce::String mode  (int pad) { return make (pad, "mode"); }
inline juce::String note  (int pad) { return make (pad, "note"); }
inline juce::String vel   (int pad) { return make (pad, "velsens"); }
inline juce::String start (int pad) { return make (pad, "start"); }
inline juce::String end   (int pad) { return make (pad, "end"); }
inline juce::String attack  (int pad) { return make (pad, "attack"); }
inline juce::String decay   (int pad) { return make (pad, "decay"); }
inline juce::String sustain (int pad) { return make (pad, "sustain"); }
inline juce::String release (int pad) { return make (pad, "release"); }
inline juce::String fadeIn  (int pad) { return make (pad, "fadein"); }
inline juce::String fadeOut (int pad) { return make (pad, "fadeout"); }
inline juce::String reverse (int pad) { return make (pad, "reverse"); }
inline juce::String velCurve (int pad) { return make (pad, "velcurve"); }
inline juce::String loopStart (int pad) { return make (pad, "loopstart"); }
inline juce::String loopFade (int pad) { return make (pad, "loopfade"); }
inline juce::String choke (int pad) { return make (pad, "choke"); }
inline juce::String chromatic (int pad) { return make (pad, "chromatic"); }

// 4-band EQ: pad01_eq_on, pad01_eq1_on / _type / _freq / _gain / _q ... pad01_eq4_*
inline juce::String eqOn (int pad) { return make (pad, "eq_on"); }
inline juce::String eqBand (int pad, int band, const char* what)
{
    return "pad" + juce::String (pad + 1).paddedLeft ('0', 2) + "_eq" + juce::String (band + 1) + "_" + what;
}

// Effects: pad01_fx_sat_mode, _sat_drive, _sat_out, _sat_mix, _crush_bits, _crush_rate, _crush_mix,
//          _chorus_mix, _chorus_rate, _chorus_depth, _delay_mix, _delay_time, _delay_fb, _delay_pp
inline juce::String fx (int pad, const char* what) { return make (pad, (juce::String ("fx_") + what).toRawUTF8()); }

// Convolution reverb: pad01_rv_type (None / Plate / Hall / Stadium / SFX / Custom), pad01_rv_wet (0-100 %)
inline juce::String rvType (int pad) { return make (pad, "rv_type"); }
inline juce::String rvWet (int pad) { return make (pad, "rv_wet"); }

// global (shared by the 16 pads)
inline const char* maxVoices = "global_maxvoices";
inline const char* voicesPerPad = "global_voicesperpad";
inline const char* stealPolicy = "global_steal";
inline const char* masterGain = "global_master";          // -60 .. +12 dB
inline const char* limiterOn = "global_limiter";          // output limiter on / off
inline const char* limiterCeiling = "global_ceiling";     // -12 .. 0 dB
} // namespace dm::ids
