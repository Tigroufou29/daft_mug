#include "EnvelopeSection.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

EnvelopeView::EnvelopeView (DaftMugProcessor& p, int padIndex) : processor (p), index (padIndex)
{
    values = readValues();
    startTimerHz (20);
}

EnvelopeView::Values EnvelopeView::readValues() const
{
    auto get = [this] (const juce::String& id) { return processor.apvts.getRawParameterValue (id)->load(); };
    Values v;
    v.attack = get (ids::attack (index));
    v.decay = get (ids::decay (index));
    v.sustain = get (ids::sustain (index));
    v.release = get (ids::release (index));
    v.fadeIn = get (ids::fadeIn (index));
    v.fadeOut = get (ids::fadeOut (index));

    const float s = get (ids::start (index)), e = get (ids::end (index));
    v.regionSeconds = sample != nullptr ? (float) (std::abs (e - s) * sample->numFrames() / sample->sampleRate) : 1.0f;
    return v;
}

void EnvelopeView::timerCallback()
{
    const auto now = readValues();
    if (! (now == values))
    {
        values = now;
        repaint();
    }
}

void EnvelopeView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    paintGlass (g, b, 12.0f, juce::Colours::white, 0.0f, 0.8f);

    auto inner = b.reduced (16.0f, 10.0f);
    auto top = inner.removeFromTop (inner.getHeight() * 0.58f);
    inner.removeFromTop (10.0f);
    auto bottom = inner;

    // ------------------------------------------------ lane 1: ADSR (starts at the trigger)
    g.setFont (makeFont (11.0f, true));
    g.setColour (accent);
    g.drawText (dm::U ("ENVELOPPE — à chaque déclenchement : Attack · Decay · Sustain · Release"), top.removeFromTop (16.0f),
                juce::Justification::centredLeft);

    const auto plot = top.reduced (0.0f, 4.0f);
    const float tA = values.attack, tD = values.decay, tR = values.release;
    const float sum = juce::jmax (1.0f, tA + tD + tR);
    // each stage keeps a minimum visible width; the hold segment is fixed (the note is being held)
    auto weight = [&] (float t) { return 0.12f + 0.88f * t / sum; };
    const float wA = weight (tA), wD = weight (tD), wR = weight (tR), wH = 0.30f;
    const float total = wA + wD + wR + wH;
    auto xAt = [&] (float units) { return plot.getX() + plot.getWidth() * units / total; };
    auto yAt = [&] (float level) { return plot.getBottom() - plot.getHeight() * level; };

    juce::Path env;
    env.startNewSubPath (xAt (0.0f), yAt (0.0f));
    env.lineTo (xAt (wA), yAt (1.0f));
    env.lineTo (xAt (wA + wD), yAt (values.sustain));
    env.lineTo (xAt (wA + wD + wH), yAt (values.sustain));
    env.lineTo (xAt (total), yAt (0.0f));

    juce::Path fill (env);
    fill.lineTo (xAt (total), yAt (0.0f));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0.0f, plot.getY(), accent.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (env, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (textDim.withAlpha (0.5f));
    g.drawHorizontalLine ((int) yAt (0.0f), plot.getX(), plot.getRight());
    g.setColour (textDim);
    g.setFont (makeFont (11.0f));
    auto label = [&] (const juce::String& t, float uFrom, float uTo)
    {
        g.drawText (t, juce::Rectangle<float> (xAt (uFrom), plot.getBottom() - 15.0f, xAt (uTo) - xAt (uFrom), 14.0f), juce::Justification::centred);
    };
    label ("A", 0.0f, wA);
    label ("D", wA, wA + wD);
    label ("S  " + juce::String (juce::roundToInt (values.sustain * 100.0f)) + " %", wA + wD, wA + wD + wH);
    label ("R", wA + wD + wH, total);
    g.setColour (textDim.withAlpha (0.6f));
    g.drawText (dm::U ("note relâchée ▸"), juce::Rectangle<float> (xAt (wA + wD + wH) - 90.0f, plot.getY(), 88.0f, 14.0f), juce::Justification::centredRight);

    // ------------------------------------------------ lane 2: region fades (tied to the region start / end)
    g.setFont (makeFont (11.0f, true));
    g.setColour (accent2);
    g.drawText (dm::U ("RÉGION LUE — Fade-in au début de la région · Fade-out à sa fin (indépendants de l'attaque)"),
                bottom.removeFromTop (16.0f), juce::Justification::centredLeft);

    const auto bar = bottom.reduced (0.0f, 3.0f);
    const float region = juce::jmax (0.001f, values.regionSeconds);
    const float fi = juce::jlimit (0.0f, 0.5f, values.fadeIn * 0.001f / region);
    const float fo = juce::jlimit (0.0f, 0.5f, values.fadeOut * 0.001f / region);

    juce::Path shape;
    shape.startNewSubPath (bar.getX(), bar.getBottom());
    shape.lineTo (bar.getX() + bar.getWidth() * fi, bar.getY());
    shape.lineTo (bar.getRight() - bar.getWidth() * fo, bar.getY());
    shape.lineTo (bar.getRight(), bar.getBottom());
    shape.closeSubPath();
    g.setColour (accent2.withAlpha (0.22f));
    g.fillPath (shape);
    g.setColour (accent2);
    g.strokePath (shape, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (textDim);
    g.setFont (makeFont (11.0f));
    g.drawText ("Fade-in " + juce::String (values.fadeIn, values.fadeIn < 10.0f ? 1 : 0) + " ms",
                bar.withWidth (170.0f).translated (6.0f, -2.0f), juce::Justification::topLeft);
    g.drawText ("Fade-out " + juce::String (values.fadeOut, values.fadeOut < 10.0f ? 1 : 0) + " ms",
                bar.withLeft (bar.getRight() - 176.0f).translated (-6.0f, -2.0f), juce::Justification::topRight);
}

// ---------------------------------------------------------------------------- section
EnvelopeSection::EnvelopeSection (DaftMugProcessor& p, int padIndex)
    : graph (p, padIndex),
      attack (p.apvts, "Attack", ids::attack (padIndex), dm::U ("Montée du volume à chaque déclenchement (0 = immédiat)")),
      decay (p.apvts, "Decay", ids::decay (padIndex), dm::U ("Descente vers le niveau Sustain après l'attaque")),
      sustain (p.apvts, "Sustain", ids::sustain (padIndex), dm::U ("Niveau maintenu après le Decay (100 % = pas de descente)")),
      release (p.apvts, "Release", ids::release (padIndex), dm::U ("Durée de l'extinction quand la note MIDI est relâchée (mode Gate)")),
      fadeIn (p.apvts, "Fade-in", ids::fadeIn (padIndex), dm::U ("Fondu progressif au DÉBUT de la région lue")),
      fadeOut (p.apvts, "Fade-out", ids::fadeOut (padIndex), dm::U ("Fondu progressif à la FIN de la région lue"))
{
    addAndMakeVisible (graph);
    for (auto* r : { &attack, &decay, &sustain, &release, &fadeIn, &fadeOut })
        addAndMakeVisible (*r);
}

void EnvelopeSection::resized()
{
    auto area = getLocalBounds();
    graph.setBounds (area.removeFromTop (180));
    area.removeFromTop (10);

    auto left = area.removeFromLeft (area.getWidth() / 2 - 10);
    area.removeFromLeft (20);
    auto right = area;
    for (auto* r : { &attack, &decay, &sustain })
        r->setBounds (left.removeFromTop (36));
    for (auto* r : { &release, &fadeIn, &fadeOut })
        r->setBounds (right.removeFromTop (36));
}
} // namespace dm::ui
