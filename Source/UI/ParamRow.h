#pragma once

#include <memory>

#include "JuceIncludes.h"
#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
// One labelled slider bound to one automatable parameter. Click the value to type a number
// (units are accepted: "12 ms", "-6 dB", "50 %"), Enter validates, double-click resets to default.
class ParamRow : public juce::Component
{
public:
    ParamRow (juce::AudioProcessorValueTreeState& apvts, const juce::String& title, const juce::String& paramId,
              const juce::String& tooltip = {}, int labelWidth = 100)
        : labelW (labelWidth)
    {
        label.setText (title, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setFont (makeFont (13.0f));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 96, 24);
        slider.setTextBoxIsEditable (true);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);

        if (tooltip.isNotEmpty())
        {
            label.setTooltip (tooltip);
            slider.setTooltip (tooltip);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromLeft (labelW));
        slider.setBounds (r.reduced (0, 4));
    }

    juce::Label label;
    juce::Slider slider;

private:
    int labelW;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace dm::ui
