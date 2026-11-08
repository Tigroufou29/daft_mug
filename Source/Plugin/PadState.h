#pragma once

#include "JuceIncludes.h"
#include "SampleData.h"

namespace dm
{
enum class LoadState { Empty, Loading, Loaded, Error };

// Non-automatable, per-pad information (name, colour, file). Owned by the processor and
// protected by its lock; the UI works on copies.
struct PadMeta
{
    juce::String name;               // empty = unnamed
    bool userRenamed = false;        // true: importing a sample never overwrites the name
    juce::Colour colour;
    juce::File file;
    juce::File customIr;             // impulse response chosen for "Reverb perso" (saved with the project)
    juce::String reverbStatus;       // "" = fine, otherwise a message to show (IR missing, unreadable...)
    LoadState state = LoadState::Empty;
    juce::String error;
    SampleHandle sample;             // what the pad plays and the waveform shows (tempo-adapted copy when a target tempo is set)
    SampleHandle original;           // decoded file, never modified
    double originalBpm = 0.0;        // detected (or typed by the user); 0 = unknown
    float tempoConfidence = 0.0f;    // of the detection, 0..1
    double targetBpm = 0.0;          // 0 = play at the original tempo
    bool stretching = false;         // a tempo-adapted copy is being computed
    juce::String tempoMessage;       // "" or a short explanation (ratio out of range...)
};
} // namespace dm
