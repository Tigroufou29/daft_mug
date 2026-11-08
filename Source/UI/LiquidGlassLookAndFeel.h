#pragma once

#include "JuceIncludes.h"

namespace dm::ui
{
// Colour tokens of the Liquid Glass dark theme.
namespace theme
{
inline const juce::Colour bgTop    { 0xff070615 };
inline const juce::Colour bgBottom { 0xff160a2e };
inline const juce::Colour accent   { 0xffc040ff };   // magenta / violet of the logo
inline const juce::Colour accent2  { 0xff22e4f4 };   // neon cyan of the logo
inline const juce::Colour accent3  { 0xffff3fc8 };   // hot pink of the logo
inline const juce::Colour text     { 0xfff4f2ff };
inline const juce::Colour textDim  { 0xffa69fc8 };
inline const juce::Colour ledOk    { 0xff59e08a };
inline const juce::Colour ledBusy  { 0xffffc247 };
inline const juce::Colour ledError { 0xffff5c7a };
} // namespace theme

// Frosted-glass panel: translucent fill, soft top highlight, thin luminous border.
void paintGlass (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                 juce::Colour tint = juce::Colours::white, float tintStrength = 0.0f, float brightness = 1.0f);

juce::Font makeFont (float height, bool bold = false);

class LiquidGlassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LiquidGlassLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// Gear glyph used by the per-pad "open editor" button.
juce::Path makeGearPath (float radius);
} // namespace dm::ui
