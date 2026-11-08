#pragma once

// Daft Mug audio engine: compile-time configuration.
// The engine (Source/Engine) is plain C++17 and has NO dependency on JUCE,
// so it can be unit-tested anywhere and shared by the VST3 and the standalone.

namespace dm
{
constexpr int kNumPads = 16;               // exactly 16 pads, 4 x 4
constexpr int kMaxVoices = 32;             // global polyphony limit
constexpr int kMaxVoicesPerPad = 8;        // layered hits of the same pad
constexpr int kFirstMidiNote = 36;         // pad 1 = C1 (36), pad 16 = D#2 (51)
constexpr int kPadTriggerBase = 1000;      // trigger id used for mouse hits: base + pad

enum class PlayMode : int
{
    OneShot = 0,   // plays to the end, Note Off is ignored
    Gate = 1,      // plays while the note is held, short release on Note Off
    Retrigger = 2, // a new hit cuts the previous hits of the same pad and restarts
    Loop = 3       // loops the region (from the loop point, with a cross-fade) while the MIDI note is held;
                   // a mouse click starts it and the next click stops it
};
constexpr int kNumChokeGroups = 8;         // choke groups 1..8 (0 = none)
} // namespace dm
