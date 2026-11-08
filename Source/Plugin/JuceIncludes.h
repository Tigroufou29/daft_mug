#pragma once

// Single place that pulls the JUCE modules used by the plugin layer and the UI.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dm
{
// JUCE's String(const char*) only accepts ASCII. Every literal containing accents or symbols
// must go through this helper (source files are UTF-8).
inline juce::String U (const char* utf8) { return juce::String::fromUTF8 (utf8); }
} // namespace dm
