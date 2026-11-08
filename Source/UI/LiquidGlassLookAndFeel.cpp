#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

juce::Font makeFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void paintGlass (juce::Graphics& g, juce::Rectangle<float> b, float radius,
                 juce::Colour tint, float tintStrength, float brightness)
{
    // body
    juce::ColourGradient body (juce::Colours::white.withAlpha (0.10f * brightness), b.getX(), b.getY(),
                               juce::Colours::white.withAlpha (0.035f * brightness), b.getX(), b.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (b, radius);

    if (tintStrength > 0.0f)
    {
        juce::ColourGradient t (tint.withAlpha (0.42f * tintStrength), b.getX(), b.getY(),
                                tint.withAlpha (0.10f * tintStrength), b.getRight(), b.getBottom(), false);
        g.setGradientFill (t);
        g.fillRoundedRectangle (b, radius);
    }

    // specular highlight on the upper edge
    auto top = b.reduced (radius * 0.45f, 0.0f).removeFromTop (b.getHeight() * 0.32f).translated (0.0f, 1.5f);
    juce::ColourGradient spec (juce::Colours::white.withAlpha (0.16f * brightness), 0.0f, top.getY(),
                               juce::Colours::white.withAlpha (0.0f), 0.0f, top.getBottom(), false);
    g.setGradientFill (spec);
    g.fillRoundedRectangle (top, radius * 0.6f);

    // luminous border
    // neon rim: cyan at the top-left fading to magenta at the bottom-right, like the logo
    juce::ColourGradient border (accent2.withAlpha (0.50f * brightness), b.getX(), b.getY(),
                                 accent3.withAlpha (0.20f * brightness), b.getRight(), b.getBottom(), false);
    g.setGradientFill (border);
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
}

juce::Path makeGearPath (float r)
{
    juce::Path p;
    constexpr int teeth = 8;
    const float step = juce::MathConstants<float>::twoPi / (float) teeth;
    const float outer = r, inner = r * 0.76f;

    auto pt = [&] (float radius, float angle) { return juce::Point<float> (radius * std::cos (angle), radius * std::sin (angle)); };

    bool first = true;
    for (int i = 0; i < teeth; ++i)
    {
        const float c = (float) i * step;
        const juce::Point<float> pts[] = { pt (inner, c - 0.50f * step), pt (inner, c - 0.26f * step), pt (outer, c - 0.15f * step),
                                           pt (outer, c + 0.15f * step), pt (inner, c + 0.26f * step) };
        for (auto q : pts)
        {
            if (first) { p.startNewSubPath (q); first = false; }
            else p.lineTo (q);
        }
    }
    p.closeSubPath();
    p.addEllipse (-r * 0.30f, -r * 0.30f, r * 0.60f, r * 0.60f);
    p.setUsingNonZeroWinding (false);   // even-odd: the centre is a hole
    return p;
}

LiquidGlassLookAndFeel::LiquidGlassLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bgTop);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white.withAlpha (0.08f));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent2);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::white.withAlpha (0.07f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::white.withAlpha (0.12f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf01a0e36));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.55f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf01a0e36));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::white.withAlpha (0.2f));
}

void LiquidGlassLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                               float, float, juce::Slider::SliderStyle style, juce::Slider&)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        g.setColour (text);
        return;
    }
    const float cy = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> track ((float) x, cy - 3.0f, (float) width, 6.0f);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (track, 3.0f);

    auto filled = track.withRight (sliderPos);
    juce::ColourGradient grad (accent, filled.getX(), 0.0f, accent2, juce::jmax (filled.getRight(), filled.getX() + 1.0f), 0.0f, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (filled, 3.0f);

    const float r = 8.0f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (sliderPos - r, cy - r + 1.5f, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.fillEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour (accent.withAlpha (0.8f));
    g.drawEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
}

void LiquidGlassLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool toggled = b.getToggleState();
    paintGlass (g, area, 10.0f, toggled ? accent : bg, toggled ? 1.0f : 0.0f, down ? 1.5f : (highlighted ? 1.3f : 1.0f));
}

void LiquidGlassLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    paintGlass (g, area, 10.0f, accent, on ? 1.0f : 0.0f, down ? 1.5f : (highlighted ? 1.3f : 1.0f));
    g.setColour (text);
    g.setFont (makeFont (13.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void LiquidGlassLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isDown, int, int, int, int, juce::ComboBox&)
{
    auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    paintGlass (g, area, 9.0f, juce::Colours::white, 0.0f, isDown ? 1.4f : 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (textDim);
    g.fillPath (arrow);
}

void LiquidGlassLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited())
        return;
    g.setColour (l.findColour (juce::Label::textColourId));
    g.setFont (l.getFont());
    g.drawText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), true);
}

juce::Font LiquidGlassLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return makeFont (13.0f, true); }
juce::Font LiquidGlassLookAndFeel::getComboBoxFont (juce::ComboBox&) { return makeFont (13.0f); }
juce::Font LiquidGlassLookAndFeel::getPopupMenuFont() { return makeFont (14.0f); }
} // namespace dm::ui
