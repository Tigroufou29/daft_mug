#pragma once

#include "JuceIncludes.h"
#include "LiquidGlassLookAndFeel.h"

// "Studio hardware" look used by the level meter and the selected-pad panel: brushed dark metal, screws,
// rotary knobs with a knurled skirt, amber LCD read-outs, bevelled buttons. Everything here is drawn with
// plain JUCE graphics (no image files), so it scales with the window.
namespace dm::ui::hw
{
inline const juce::Colour lcdAmber { 0xffffb23d };
inline const juce::Colour lcdBack { 0xff08090a };
inline const juce::Colour engraving { 0xffd9d9de };

inline const juce::Image& brushedTile()
{
    static const juce::Image tile = []
    {
        juce::Image img (juce::Image::ARGB, 256, 256, true);
        juce::Random rnd (777);
        for (int y = 0; y < 256; ++y)
        {
            const float streak = rnd.nextFloat();   // a whole brushed line is a little lighter or darker
            for (int x = 0; x < 256; ++x)
            {
                const float n = rnd.nextFloat();
                const float a = 0.015f + 0.05f * streak * n;
                img.setPixelAt (x, y, (streak > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            }
        }
        return img;
    }();
    return tile;
}

inline void paintScrew (juce::Graphics& g, juce::Point<float> c, float r = 4.5f)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - r, c.y - r + 1.0f, r * 2.0f, r * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8f96), c.x - r * 0.5f, c.y - r * 0.6f,
                                             juce::Colour (0xff2a2b2f), c.x + r * 0.6f, c.y + r * 0.8f, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
    const float a = 0.6f + (c.x * 0.37f + c.y * 0.21f);   // each screw a different slot angle
    const auto d = juce::Point<float> (std::cos (a), std::sin (a)) * (r * 0.72f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawLine (c.x - d.x, c.y - d.y, c.x + d.x, c.y + d.y, 1.3f);
}

inline void paintPanel (juce::Graphics& g, juce::Rectangle<float> b, float radius = 8.0f, bool screws = true)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b.translated (0.0f, 2.5f), radius);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35363c), b.getX(), b.getY(),
                                             juce::Colour (0xff17181b), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path p;
        p.addRoundedRectangle (b, radius);
        g.reduceClipRegion (p);
        g.setTiledImageFill (brushedTile(), 0, 0, 1.0f);
        g.fillAll();
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.24f), 0.0f, b.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, b.getY() + 14.0f, false));
    g.drawRoundedRectangle (b.reduced (0.75f), radius - 0.75f, 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (b, radius, 1.2f);
    if (screws && b.getWidth() > 60.0f && b.getHeight() > 60.0f)
        for (float fx : { 0.0f, 1.0f })
            for (float fy : { 0.0f, 1.0f })
                paintScrew (g, { b.getX() + 10.0f + fx * (b.getWidth() - 20.0f), b.getY() + 10.0f + fy * (b.getHeight() - 20.0f) });
}

// recessed display window (LCD / LED read-outs)
inline void paintLcd (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (r.expanded (1.0f).translated (0.0f, 1.0f), 4.0f);
    g.setColour (lcdBack);
    g.fillRoundedRectangle (r, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0.0f, r.getY(),
                                             juce::Colours::black.withAlpha (0.0f), 0.0f, r.getY() + 7.0f, false));
    g.fillRoundedRectangle (r, 3.0f);   // inner shadow at the top
}

inline juce::Font monoFont (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

// engraved small-caps caption (white on metal)
inline void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float h = 10.5f,
                          juce::Justification j = juce::Justification::centred)
{
    g.setFont (makeFont (h, true));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (text, r.translated (0.0f, 1.0f), j, false);
    g.setColour (engraving.withAlpha (0.85f));
    g.drawText (text, r, j, false);
}

class HardwareLookAndFeel : public LiquidGlassLookAndFeel
{
public:
    HardwareLookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, lcdAmber);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141518));
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3b42));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto c = area.getCentre();
        const float outer = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 1.0f;
        const float k = outer * 0.76f;                       // knob (skirt) radius; the ring outside it carries the ticks
        const auto cap = s.findColour (juce::Slider::rotarySliderFillColourId);

        // tick ring: ticks up to the current value light up in the knob's colour
        for (int i = 0; i <= 10; ++i)
        {
            const float t = (float) i / 10.0f, a = startAngle + t * (endAngle - startAngle);
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            const bool lit = t <= pos + 0.001f;
            g.setColour (lit ? cap.brighter (0.4f) : juce::Colours::white.withAlpha (0.28f));
            g.drawLine (c.x + dir.x * (k + 3.5f), c.y + dir.y * (k + 3.5f), c.x + dir.x * (outer), c.y + dir.y * (outer), (i == 0 || i == 10 || i == 5) ? 2.0f : 1.2f);
        }

        // soft shadow, knurled skirt
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - k, c.y - k + 2.5f, k * 2.0f, k * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff55565d), c.x - k * 0.6f, c.y - k * 0.8f,
                                                 juce::Colour (0xff131417), c.x + k * 0.7f, c.y + k * 0.9f, false));
        g.fillEllipse (c.x - k, c.y - k, k * 2.0f, k * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        for (int i = 0; i < 40; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            g.drawLine (c.x + dir.x * k * 0.90f, c.y + dir.y * k * 0.90f, c.x + dir.x * k, c.y + dir.y * k, 1.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.drawEllipse (c.x - k, c.y - k, k * 2.0f, k * 2.0f, 1.0f);

        // coloured cap
        const float cr = k * 0.68f;
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.35f), c.x - cr * 0.5f, c.y - cr * 0.7f,
                                                 cap.darker (0.75f), c.x + cr * 0.6f, c.y + cr * 0.9f, false));
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.drawEllipse (c.x - cr + 1.0f, c.y - cr + 1.0f, cr * 2.0f - 2.0f, cr * 2.0f - 2.0f, 1.0f);

        // pointer
        const float a = startAngle + pos * (endAngle - startAngle);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.drawLine (c.x + dir.x * cr * 0.18f, c.y + dir.y * cr * 0.18f, c.x + dir.x * k * 0.96f, c.y + dir.y * k * 0.96f, 2.6f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        paintKey (g, b.getLocalBounds().toFloat().reduced (0.5f), b.getToggleState(), highlighted, down);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        paintKey (g, r, false, highlighted, down);
        // LED
        const auto led = juce::Rectangle<float> (r.getX() + 9.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (on ? theme::ledOk.withAlpha (0.35f) : juce::Colours::transparentBlack);
        g.fillEllipse (led.expanded (3.0f));
        g.setColour (on ? theme::ledOk : juce::Colour (0xff1c2a20));
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawEllipse (led, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (makeFont (12.5f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (18.0f), juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isDown, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        paintLcd (g, r);
        if (isDown) { g.setColour (lcdAmber.withAlpha (0.10f)); g.fillRoundedRectangle (r, 3.0f); }
        juce::Path arrow;
        const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (lcdAmber);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return monoFont (13.0f); }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LiquidGlassLookAndFeel::createSliderTextBox (s);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (monoFont (12.5f));
        l->setColour (juce::Label::textColourId, lcdAmber);
        l->setColour (juce::Label::backgroundColourId, lcdBack);
        l->setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.10f));
        l->setColour (juce::Label::textWhenEditingColourId, lcdAmber.brighter (0.3f));
        l->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::black);
        l->setColour (juce::Label::outlineWhenEditingColourId, lcdAmber);
        l->setColour (juce::TextEditor::highlightColourId, lcdAmber.withAlpha (0.35f));
        l->setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        return l;
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (l.isBeingEdited()) return;
        const auto bg = l.findColour (juce::Label::backgroundColourId);
        if (! bg.isTransparent())
        {
            paintLcd (g, l.getLocalBounds().toFloat().reduced (0.5f));
        }
        g.setColour (l.findColour (juce::Label::textColourId));
        g.setFont (l.getFont());
        g.drawText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), true);
    }

private:
    static void paintKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool highlighted, bool down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 1.5f), 5.0f);
        const auto top = down ? juce::Colour (0xff1b1c20) : (highlighted ? juce::Colour (0xff4a4b52) : juce::Colour (0xff3d3e44));
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), juce::Colour (0xff1b1c20), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        if (lit)
        {
            g.setColour (theme::accent2.withAlpha (0.30f));
            g.fillRoundedRectangle (r, 5.0f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.05f : 0.22f), 0.0f, r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 10.0f, false));
        g.drawRoundedRectangle (r.reduced (0.75f), 4.25f, 1.3f);
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 5.0f, 1.1f);
    }
};
} // namespace dm::ui::hw
