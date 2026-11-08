#include "MasterMeter.h"

#include "HardwareStyle.h"
#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

namespace
{
constexpr float kMinDb = -60.0f, kMaxDb = 0.0f;
float toDb (float lin) { return lin > 1.0e-5f ? juce::Decibels::gainToDecibels (lin) : -100.0f; }
void paintScrewsSmall (juce::Graphics& g, juce::Rectangle<float> b)
{
    for (float fy : { 0.0f, 1.0f })
        for (float fx : { 0.0f, 1.0f })
            hw::paintScrew (g, { b.getX() + 7.0f + fx * (b.getWidth() - 14.0f), b.getY() + 7.0f + fy * (b.getHeight() - 14.0f) }, 3.2f);
}
juce::String fmt (float db) { return db <= -99.0f ? juce::String (dm::U ("−∞")) : juce::String (db, 1); }
}

MasterMeter::MasterMeter (DaftMugProcessor& p) : processor (p)
{
    setTooltip (dm::U ("Niveau de sortie : segments allumés = RMS, segment clair = crête (pic) avec maintien. Cliquez pour effacer le maintien et le voyant de saturation."));
    startTimerHz (30);
}

void MasterMeter::resetHold()
{
    clip = false;
    holdDb = { { -100.0f, -100.0f } };
    repaint();
}

void MasterMeter::timerCallback()
{
    const auto lv = processor.readMasterLevels();
    const float peaks[2] = { toDb (lv.peakL), toDb (lv.peakR) };
    const float rms[2] = { toDb (lv.rmsL), toDb (lv.rmsR) };
    const auto oldPeak = peakDb, oldRms = rmsDb, oldHold = holdDb;
    const bool oldClip = clip;
    for (size_t c = 0; c < 2; ++c)
    {
        // peak: instant rise, 24 dB/s fall
        peakDb[c] = peaks[c] > peakDb[c] ? peaks[c] : juce::jmax (peaks[c], peakDb[c] - 0.8f);
        // rms: already integrated by the audio thread; light smoothing for a steady bar
        rmsDb[c] += (rms[c] - rmsDb[c]) * 0.5f;
        if (peaks[c] >= holdDb[c]) { holdDb[c] = peaks[c]; holdTicks[c] = 45; }   // hold 1.5 s
        else if (holdTicks[c] > 0) --holdTicks[c];
        else holdDb[c] = juce::jmax (-100.0f, holdDb[c] - 0.8f);
        if (peaks[c] >= -0.2f) clip = true;
    }
    const bool oldLim = limTicks > 0;
    if (processor.getLimiterReduction() > 0.005f) limTicks = 8;   // ~270 ms hold so a short action stays visible
    else if (limTicks > 0) --limTicks;
    bool changed = clip != oldClip || oldLim != (limTicks > 0);
    for (size_t c = 0; c < 2 && ! changed; ++c)
        changed = std::abs (peakDb[c] - oldPeak[c]) > 0.05f || std::abs (rmsDb[c] - oldRms[c]) > 0.05f || std::abs (holdDb[c] - oldHold[c]) > 0.05f;
    if (changed)
        repaint();
}

float MasterMeter::dbToY (float db, juce::Rectangle<float> bar) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    return bar.getBottom() - t * bar.getHeight();
}

namespace
{
constexpr float kSegDb = 1.5f;   // one LED segment = 1.5 dB

juce::Colour segmentColour (float db)
{
    if (db >= -1.5f)  return juce::Colour (0xffff3b30);   // red
    if (db >= -4.5f)  return juce::Colour (0xffff9f1c);   // orange
    if (db >= -12.0f) return juce::Colour (0xffe9d83a);   // yellow
    return juce::Colour (0xff3ddc84);                     // green
}
}

// One LED column: segments light up from the bottom up to the RMS level; one bright segment follows the peak,
// one dimmer segment holds the peak for 1.5 s (red when the peak reached 0 dBFS).
void MasterMeter::drawBar (juce::Graphics& g, juce::Rectangle<float> bar, int ch) const
{
    hw::paintLcd (g, bar.expanded (2.0f, 2.0f));
    const int n = (int) std::lround ((kMaxDb - kMinDb) / kSegDb);
    const float segH = bar.getHeight() / (float) n;
    const float rms = rmsDb[(size_t) ch], peak = peakDb[(size_t) ch], hold = holdDb[(size_t) ch];
    const int peakSeg = juce::jlimit (-1, n - 1, (int) std::floor ((peak - kMinDb) / kSegDb));
    const int holdSeg = juce::jlimit (-1, n - 1, (int) std::floor ((hold - kMinDb) / kSegDb));

    for (int i = 0; i < n; ++i)
    {
        const float lo = kMinDb + (float) i * kSegDb;
        const auto col = segmentColour (lo);
        const auto r = juce::Rectangle<float> (bar.getX(), bar.getBottom() - (float) (i + 1) * segH + 1.0f, bar.getWidth(), segH - 1.6f);
        const bool lit = rms >= lo + kSegDb * 0.5f;
        const bool isPeak = i == peakSeg && peak > kMinDb;
        const bool isHold = i == holdSeg && hold > kMinDb && holdSeg != peakSeg;

        g.setColour (col.withAlpha (0.12f));
        g.fillRect (r);
        if (lit)
        {
            g.setColour (col);
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (r.withHeight (1.0f));   // glint on the lit segment
        }
        if (isPeak)
        {
            g.setColour (col.brighter (0.7f));
            g.fillRect (r);
        }
        else if (isHold)
        {
            g.setColour ((hold >= -0.2f ? juce::Colour (0xffff3b30) : col).withAlpha (0.85f));
            g.fillRect (r);
        }
    }
}

void MasterMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    hw::paintPanel (g, b, 8.0f, false);
    paintScrewsSmall (g, b);

    auto area = b.reduced (8.0f, 12.0f);
    hw::drawEngraved (g, "NIVEAU", area.removeFromTop (14.0f), 10.5f);
    area.removeFromTop (3.0f);

    // CLIP and LIM lamps (round, lit with a halo)
    {
        auto row = area.removeFromTop (30.0f);
        auto lamp = [&] (juce::Rectangle<float> cell, const juce::String& name, bool on, juce::Colour col)
        {
            auto l = cell.removeFromTop (14.0f).withSizeKeepingCentre (11.0f, 11.0f);
            if (on) { g.setColour (col.withAlpha (0.40f)); g.fillEllipse (l.expanded (4.0f)); }
            g.setGradientFill (juce::ColourGradient (on ? col.brighter (0.6f) : col.darker (0.85f), l.getX() + 3.0f, l.getY() + 2.0f,
                                                     on ? col.darker (0.2f) : col.darker (0.97f), l.getRight(), l.getBottom(), false));
            g.fillEllipse (l);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.drawEllipse (l, 1.0f);
            hw::drawEngraved (g, name, cell, 8.5f);
        };
        lamp (row.removeFromLeft (row.getWidth() * 0.5f), "CLIP", clip, juce::Colour (0xffff3b30));
        lamp (row, "LIM", limTicks > 0, juce::Colour (0xffffa31a));
    }
    area.removeFromTop (6.0f);

    // read-out window: PIC / RMS / crest
    {
        auto lcd = area.removeFromBottom (72.0f);
        hw::paintLcd (g, lcd);
        const float pk = juce::jmax (holdDb[0], holdDb[1]);
        const float rm = juce::jmax (rmsDb[0], rmsDb[1]);
        auto inner = lcd.reduced (6.0f, 4.0f);
        auto line = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
        {
            auto r = inner.removeFromTop (21.0f);
            g.setColour (hw::engraving.withAlpha (0.55f));
            g.setFont (makeFont (8.5f, true));
            g.drawText (label, r.removeFromLeft (28.0f), juce::Justification::centredLeft);
            g.setColour (c);
            g.setFont (hw::monoFont (13.0f));
            g.drawText (value, r, juce::Justification::centredRight);
        };
        line ("PIC", fmt (pk), pk >= -0.2f ? juce::Colour (0xffff5040) : hw::lcdAmber);
        line ("RMS", fmt (rm), hw::lcdAmber);
        line (dm::U ("Δ"), (pk > -99.0f && rm > -99.0f) ? juce::String (pk - rm, 1) : juce::String ("--"), juce::Colour (0xff6fd6e8));
    }
    area.removeFromBottom (6.0f);
    area.removeFromBottom (14.0f);   // room for the L / R captions

    // two LED columns with the dB scale between them (like a console meter bridge)
    const float centreW = 26.0f;
    const float barW = juce::jmax (8.0f, (area.getWidth() - centreW - 8.0f) * 0.5f);
    const auto barL = area.removeFromLeft (barW).reduced (0.0f, 2.0f);
    auto centre = area.removeFromLeft (centreW + 8.0f);
    const auto barR = area.removeFromLeft (barW).reduced (0.0f, 2.0f);

    g.setFont (makeFont (9.0f, true));
    for (float db : { 0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = dbToY (db, barL);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.fillRect (barL.getRight() + 2.0f, y - 0.5f, 3.0f, 1.0f);
        g.fillRect (barR.getX() - 5.0f, y - 0.5f, 3.0f, 1.0f);
        g.setColour (db >= -3.0f ? juce::Colour (0xffff7b6e) : hw::engraving.withAlpha (0.8f));
        g.drawText (juce::String ((int) db), centre.withY (y - 6.0f).withHeight (12.0f).reduced (4.0f, 0.0f), juce::Justification::centred);
    }
    drawBar (g, barL, 0);
    drawBar (g, barR, 1);
    hw::drawEngraved (g, "L", juce::Rectangle<float> (barL.getX(), barL.getBottom() + 4.0f, barL.getWidth(), 12.0f), 10.0f);
    hw::drawEngraved (g, "R", juce::Rectangle<float> (barR.getX(), barR.getBottom() + 4.0f, barR.getWidth(), 12.0f), 10.0f);
}
} // namespace dm::ui
