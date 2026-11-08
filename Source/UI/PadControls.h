#pragma once

#include <memory>

#include "HardwareStyle.h"
#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// The sound controls of ONE pad (volume, pitch, pan, velocity sensitivity, play mode, MIDI note + learn).
// Every control is bound to that pad's own automatable parameters, so each pad stays independent.
// Sliders accept keyboard entry: click the value, type a number (dB, st, %...), press Enter.
class PadControls : public juce::Component,
                    private juce::Timer
{
public:
    PadControls (DaftMugProcessor&, int padIndex, bool showAdvanced = false);
    ~PadControls() override { stopTimer(); setLookAndFeel (nullptr); }

    void resized() override;
    int getPreferredHeight() const noexcept { return advanced ? 11 * rowHeight + 8 : 2 * knobRowHeight + 8; }

private:
    void timerCallback() override;

    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addRow (Row& row, const juce::String& title, const juce::String& paramId);

    static constexpr int rowHeight = 36;
    static constexpr int knobRowHeight = 112;   // compact (side panel) layout: rotary knobs
    void styleAsKnob (Row&, juce::Colour cap);
    std::unique_ptr<hw::HardwareLookAndFeel> hwLook;   // declared first among the widgets: destroyed after them

    DaftMugProcessor& processor;
    const int index;

    const bool advanced;
    Row gain, pitch, pan, velocity, velCurve, note, loopStart, loopFade, choke;
    juce::ToggleButton chromaticButton { dm::U ("Mode chromatique (tout le clavier MIDI)") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chromaticAttachment;
    juce::Label modeLabel;
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    juce::ToggleButton learnButton { "MIDI Learn" };
};
} // namespace dm::ui
