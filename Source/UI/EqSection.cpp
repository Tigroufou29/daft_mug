#include "EqSection.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

namespace
{
constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f, kMaxGain = 18.0f;
}

juce::Colour EqView::bandColour (int band)
{
    static const juce::uint32 c[] = { 0xffff9a3d, 0xff4dc8ff, 0xff8f7bff, 0xffff5c9a };
    return juce::Colour (c[juce::jlimit (0, 3, band)]);
}

// ================================================================================== EqView
EqView::EqView (DaftMugProcessor& p, int padIndex) : processor (p), index (padIndex)
{
    for (int b = 0; b < kEqBands; ++b)
    {
        freqP[(size_t) b] = processor.apvts.getParameter (ids::eqBand (index, b, "freq"));
        gainP[(size_t) b] = processor.apvts.getParameter (ids::eqBand (index, b, "gain"));
        qP[(size_t) b] = processor.apvts.getParameter (ids::eqBand (index, b, "q"));
    }
    eq = processor.readPadParams (index).eq;
    processor.setAnalyzerPad (index);
    startTimerHz (30);
}

double EqView::sampleRate() const
{
    const double sr = processor.getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

void EqView::setSelectedBand (int band)
{
    selected = juce::jlimit (0, kEqBands - 1, band);
    repaint();
}

juce::Rectangle<float> EqView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (40.0f).withTrimmedRight (14.0f).withTrimmedTop (12.0f).withTrimmedBottom (24.0f);
}

float EqView::freqToX (float f) const
{
    const auto r = plotArea();
    const float t = std::log10 (juce::jlimit (kMinFreq, kMaxFreq, f) / kMinFreq) / 3.0f;   // 20 Hz .. 20 kHz = 3 decades
    return r.getX() + t * r.getWidth();
}

float EqView::xToFreq (float x) const
{
    const auto r = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
    return kMinFreq * std::pow (10.0f, t * 3.0f);
}

float EqView::gainToY (float g) const
{
    const auto r = plotArea();
    return r.getCentreY() - juce::jlimit (-kMaxGain, kMaxGain, g) / kMaxGain * (r.getHeight() * 0.5f);
}

float EqView::yToGain (float y) const
{
    const auto r = plotArea();
    return juce::jlimit (-kMaxGain, kMaxGain, (r.getCentreY() - y) / (r.getHeight() * 0.5f) * kMaxGain);
}

juce::Point<float> EqView::nodePosition (int band) const
{
    const auto& b = eq.band[(size_t) band];
    const bool hasGain = b.type != EqType::HighPass && b.type != EqType::LowPass;
    return { freqToX (b.freq), gainToY (hasGain ? b.gainDb : 0.0f) };
}

int EqView::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 16.0f;
    for (int b = 0; b < kEqBands; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDist) { best = b; bestDist = d; }
    }
    return best;
}

void EqView::updateLiveSpectrum()
{
    constexpr int n = 4096, points = 128;
    static thread_local std::vector<float> buf, win;
    buf.resize (n);
    if (win.size() != (size_t) n)
    {
        win.resize (n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
    }
    if (liveSmooth.size() != (size_t) points) liveSmooth.assign (points, 0.0f);

    const uint32_t w = processor.getEngine().readAnalyzer (buf.data(), n);
    const bool fresh = w != lastWrite;
    lastWrite = w;

    bool any = false;
    if (fresh)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) { buf[(size_t) i] *= win[(size_t) i]; peak = juce::jmax (peak, std::abs (buf[(size_t) i])); }
        const double sr = sampleRate();
        for (int k = 0; k < points; ++k)
        {
            const double f = kMinFreq * std::pow ((double) kMaxFreq / kMinFreq, (double) k / (points - 1));
            float v = 0.0f;
            if (f < sr * 0.48 && peak > 1.0e-6f)
            {
                const double coeff = 2.0 * std::cos (juce::MathConstants<double>::twoPi * f / sr);
                double s1 = 0.0, s2 = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double s0 = (double) buf[(size_t) i] + coeff * s1 - s2;
                    s2 = s1; s1 = s0;
                }
                const double mag2 = (s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((double) n * (double) n);
                // 0 dB = full-scale sine; +4.5 dB / octave tilt (like pro analysers) so music looks balanced
                const double level = 10.0 * std::log10 (mag2 + 1e-14) + 12.0 + 4.5 * std::log2 (f / 1000.0);
                v = juce::jlimit (0.0f, 1.0f, (float) ((level + 85.0) / 85.0));
            }
            // fast rise, slow fall
            liveSmooth[(size_t) k] = v > liveSmooth[(size_t) k] ? liveSmooth[(size_t) k] + (v - liveSmooth[(size_t) k]) * 0.6f
                                                               : liveSmooth[(size_t) k] * 0.90f;
            any = any || liveSmooth[(size_t) k] > 0.004f;
        }
    }
    else
    {
        for (auto& v : liveSmooth) { v *= 0.88f; any = any || v > 0.004f; }
    }

    if (any || ! live.empty())
    {
        live = liveSmooth;
        if (! any) { live.clear(); }
        repaint();
    }
}

void EqView::timerCallback()
{
    updateLiveSpectrum();
    const auto now = processor.readPadParams (index).eq;
    bool changed = now.on != eq.on;
    for (int b = 0; b < kEqBands && ! changed; ++b)
    {
        const auto& a = now.band[(size_t) b];
        const auto& o = eq.band[(size_t) b];
        changed = a.on != o.on || a.type != o.type || ! juce::approximatelyEqual (a.freq, o.freq)
                  || ! juce::approximatelyEqual (a.gainDb, o.gainDb) || ! juce::approximatelyEqual (a.q, o.q);
    }
    if (changed)
    {
        eq = now;
        repaint();
    }
}

void EqView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    paintGlass (g, bounds, 12.0f, juce::Colours::white, 0.0f, 0.8f);
    const auto plot = plotArea();

    // --- grid
    g.setFont (makeFont (10.5f));
    for (float db = -kMaxGain; db <= kMaxGain + 0.1f; db += 6.0f)
    {
        const float y = gainToY (db);
        g.setColour (juce::Colours::white.withAlpha (juce::approximatelyEqual (db, 0.0f) ? 0.28f : 0.08f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (textDim);
        g.drawText (juce::String ((int) db), (int) bounds.getX() + 4, (int) y - 8, 32, 16, juce::Justification::centredRight);
    }
    const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqs)
    {
        const float x = freqToX (f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (textDim);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                    (int) x - 20, (int) plot.getBottom() + 4, 40, 16, juce::Justification::centred);
    }

    // --- live analyser: moves with the sound that this pad plays (after the EQ)
    if (live.size() > 1)
    {
        juce::Path lp;
        const int count = (int) live.size();
        for (int k = 0; k < count; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (count - 1);
            const float y = plot.getBottom() - live[(size_t) k] * plot.getHeight() * 0.92f;
            if (k == 0) lp.startNewSubPath (x, y); else lp.lineTo (x, y);
        }
        juce::Path lf (lp);
        lf.lineTo (plot.getRight(), plot.getBottom());
        lf.lineTo (plot.getX(), plot.getBottom());
        lf.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent3.withAlpha (0.55f), 0.0f, plot.getY(), accent.withAlpha (0.10f), 0.0f, plot.getBottom(), false));
        g.fillPath (lf);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.strokePath (lp, juce::PathStrokeType (1.4f));
    }

    // --- response = sum of the active bands (in dB)
    const double sr = sampleRate();
    std::array<BiquadCoeffs, kEqBands> coeffs;
    std::array<bool, kEqBands> active {};
    for (int b = 0; b < kEqBands; ++b)
    {
        coeffs[(size_t) b] = designBand (eq.band[(size_t) b], sr);
        active[(size_t) b] = eq.on && ! isBandNeutral (eq.band[(size_t) b]);
    }

    juce::Path curve;
    const int steps = juce::jmax (2, (int) plot.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
        const double f = xToFreq (x);
        double db = 0.0;
        for (int b = 0; b < kEqBands; ++b)
            if (active[(size_t) b])
                db += magnitudeDb (coeffs[(size_t) b], f, sr);
        const float y = gainToY ((float) db);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }

    juce::Path fill (curve);
    fill.lineTo (plot.getRight(), gainToY (0.0f));
    fill.lineTo (plot.getX(), gainToY (0.0f));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent2.withAlpha (0.30f), 0.0f, plot.getY(), accent.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (eq.on ? accent2 : textDim.withAlpha (0.6f));
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // --- nodes
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto pt = nodePosition (b);
        const auto col = bandColour (b).withAlpha (eq.band[(size_t) b].on && eq.on ? 1.0f : 0.35f);
        const float r = b == selected ? 9.0f : 7.0f;
        if (b == selected || b == hovered)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (pt.x - r - 5.0f, pt.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
        }
        g.setColour (col);
        g.fillEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.setFont (makeFont (10.0f, true));
        g.drawText (juce::String (b + 1), (int) (pt.x - r), (int) (pt.y - r), (int) (r * 2.0f), (int) (r * 2.0f), juce::Justification::centred);
    }
}

void EqView::setParam (juce::RangedAudioParameter* p, float value)
{
    if (p == nullptr) return;
    const auto range = p->getNormalisableRange();
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, value)));
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0) return;
    selected = dragging;
    if (onBandSelected) onBandSelected (dragging);
    freqP[(size_t) dragging]->beginChangeGesture();
    gainP[(size_t) dragging]->beginChangeGesture();
    repaint();
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    setParam (freqP[(size_t) dragging], xToFreq (e.position.x));
    const auto type = eq.band[(size_t) dragging].type;
    if (type != EqType::HighPass && type != EqType::LowPass)
        setParam (gainP[(size_t) dragging], std::round (yToGain (e.position.y) * 10.0f) / 10.0f);
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    freqP[(size_t) dragging]->endChangeGesture();
    gainP[(size_t) dragging]->endChangeGesture();
    dragging = -1;
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b < 0) return;
    for (auto* p : { freqP[(size_t) b], gainP[(size_t) b], qP[(size_t) b] })
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int b = nodeAt (e.position);
    if (b < 0) return;
    auto* p = qP[(size_t) b];
    const float q = eq.band[(size_t) b].q * std::exp (wheel.deltaY * (wheel.isReversed ? -0.8f : 0.8f));
    p->beginChangeGesture();
    setParam (p, q);
    p->endChangeGesture();
}

// ================================================================================== EqSection
EqSection::EqSection (DaftMugProcessor& p, int padIndex)
    : processor (p), index (padIndex), wave (p, padIndex, false), view (p, padIndex)
{
    addAndMakeVisible (wave);
    addAndMakeVisible (view);
    view.onBandSelected = [this] (int b) { selectBand (b); };

    addAndMakeVisible (eqOnButton);
    eqOnButton.setClickingTogglesState (true);
    eqOnButton.setTooltip (dm::U ("Active ou coupe tout l'égaliseur de ce pad (fondu sans clic)"));
    eqOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::eqOn (index), eqOnButton);

    for (int b = 0; b < kEqBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        btn.setButtonText (juce::String (b + 1));
        btn.setClickingTogglesState (true);
        btn.setRadioGroupId (4242);
        btn.setColour (juce::TextButton::buttonOnColourId, EqView::bandColour (b));
        btn.setTooltip (dm::U ("Bande ") + juce::String (b + 1));
        btn.onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (btn);
    }

    addAndMakeVisible (soloButton);
    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip (dm::U ("Solo : n'écouter que la zone de fréquences de la bande sélectionnée (filtre passe-bande à sa fréquence et son Q) "
                                  "pour repérer une fréquence désagréable. Suit la bande choisie. Aide d'écoute : non enregistrée, sans effet sur l'export."));
    soloButton.onClick = [this] { processor.setEqSolo (index, soloButton.getToggleState() ? selectedBand : -1); };
    addAndMakeVisible (bandOnButton);
    bandOnButton.setClickingTogglesState (true);
    addAndMakeVisible (typeBox);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ids::eqBand (index, 0, "type"))))
    {
        typeBox.addItem ("Bell (cloche)", 1);
        typeBox.addItem (dm::U ("Low shelf (graves)"), 2);
        typeBox.addItem (dm::U ("High shelf (aigus)"), 3);
        typeBox.addItem (dm::U ("High-pass (coupe les graves)"), 4);
        typeBox.addItem (dm::U ("Low-pass (coupe les aigus)"), 5);
        juce::ignoreUnused (choice);
    }

    addAndMakeVisible (dynBox);
    dynBox.addItem ("Off (EQ statique)", 1);
    dynBox.addItem (dm::U ("Compress (agit quand le niveau monte)"), 2);
    dynBox.addItem (dm::U ("Expand (agit quand le niveau baisse)"), 3);
    dynBox.setTooltip (dm::U ("EQ dynamique : le gain de la bande dépend du niveau mesuré dans la bande."));

    selectBand (0);
    startTimerHz (10);
}

void EqSection::selectBand (int band)
{
    band = juce::jlimit (0, kEqBands - 1, band);
    if (band == selectedBand) return;
    selectedBand = band;
    if (soloButton.getToggleState()) processor.setEqSolo (index, band);   // the listening follows the selected band
    view.setSelectedBand (band);
    bandButtons[(size_t) band].setToggleState (true, juce::dontSendNotification);

    // re-bind every control to the newly selected band (attachments first, then the widgets)
    bandOnAttachment.reset();
    typeAttachment.reset();
    freqRow.reset();
    gainRow.reset();
    qRow.reset();
    dynAttachment.reset();
    for (auto& r : dynRows) r.reset();

    bandOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::eqBand (index, band, "on"), bandOnButton);
    typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ids::eqBand (index, band, "type"), typeBox);

    freqRow = std::make_unique<ParamRow> (processor.apvts, dm::U ("Fréquence"), ids::eqBand (index, band, "freq"),
                                          dm::U ("Fréquence centrale de la bande (ou de coupure pour un filtre). Saisie possible : « 2,5 k »."), 110);
    gainRow = std::make_unique<ParamRow> (processor.apvts, "Gain", ids::eqBand (index, band, "gain"),
                                          dm::U ("Renforce (+) ou atténue (−) les fréquences de la bande, en dB."), 110);
    qRow = std::make_unique<ParamRow> (processor.apvts, "Q", ids::eqBand (index, band, "q"),
                                       dm::U ("Largeur de la bande : un Q élevé = étroit et précis, un Q faible = large et doux."), 110);
    addAndMakeVisible (*freqRow);
    addAndMakeVisible (*gainRow);
    addAndMakeVisible (*qRow);

    dynAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ids::eqBand (index, band, "dyn"), dynBox);
    struct Spec { const char* key; const char* label; const char* tip; };
    const Spec specs[6] = {
        { "thr", "Threshold", "Niveau (dans la bande) à partir duquel la dynamique agit." },
        { "ratio", "Ratio", "Force de l'effet : 1:1 = aucun, 20:1 = très marqué." },
        { "att", "Attack", "Rapidité de réaction quand le niveau passe le seuil." },
        { "rel", "Release", "Temps de retour au repos." },
        { "knee", "Knee", "Douceur du passage au seuil (0 = net)." },
        { "range", "Range", "Excès de niveau (en dB, après ratio) nécessaire pour atteindre le gain complet de la bande." } };
    for (int k = 0; k < 6; ++k)
    {
        dynRows[(size_t) k] = std::make_unique<ParamRow> (processor.apvts, specs[k].label, ids::eqBand (index, band, specs[k].key),
                                                          dm::U (specs[k].tip), 90);
        addAndMakeVisible (*dynRows[(size_t) k]);
    }
    resized();
    timerCallback();
}

void EqSection::timerCallback()
{
    // a high-pass / low-pass has no gain: grey the control out
    if (selectedBand < 0 || gainRow == nullptr) return;
    const int type = juce::roundToInt (processor.apvts.getRawParameterValue (ids::eqBand (index, selectedBand, "type"))->load());
    const bool hasGain = type < 3;
    gainRow->setEnabled (hasGain);
    gainRow->setAlpha (hasGain ? 1.0f : 0.4f);

    const bool dyn = juce::roundToInt (processor.apvts.getRawParameterValue (ids::eqBand (index, selectedBand, "dyn"))->load()) > 0;
    for (auto& r : dynRows)
        if (r != nullptr) { r->setEnabled (dyn); r->setAlpha (dyn ? 1.0f : 0.4f); }
}

void EqSection::paint (juce::Graphics& g)
{
    using namespace theme;
    g.setColour (accent2);
    g.setFont (makeFont (12.0f, true));
    g.drawText (dm::U ("DYNAMIQUE DE LA BANDE"), 0, dynTitleY, 300, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawHorizontalLine (dynTitleY + 24, 0.0f, (float) getWidth());
}

void EqSection::resized()
{
    auto area = getLocalBounds();
    wave.setBounds (area.removeFromTop (90));
    area.removeFromTop (10);
    view.setBounds (area.removeFromTop (262));
    area.removeFromTop (14);

    auto head = area.removeFromTop (40);
    eqOnButton.setBounds (head.removeFromLeft (110).reduced (0, 3));
    head.removeFromLeft (16);
    for (auto& b : bandButtons)
    {
        b.setBounds (head.removeFromLeft (44).reduced (0, 3));
        head.removeFromLeft (6);
    }
    head.removeFromLeft (14);
    soloButton.setBounds (head.removeFromLeft (150).reduced (0, 3));
    head.removeFromLeft (6);
    bandOnButton.setBounds (head.removeFromLeft (130).reduced (0, 3));
    head.removeFromLeft (10);
    typeBox.setBounds (head.removeFromLeft (230).reduced (0, 3));

    area.removeFromTop (6);
    const auto rows = area.withWidth (juce::jmin (760, area.getWidth()));
    auto r = rows;
    if (freqRow) freqRow->setBounds (r.removeFromTop (36));
    if (gainRow) gainRow->setBounds (r.removeFromTop (36));
    if (qRow) qRow->setBounds (r.removeFromTop (36));

    r.removeFromTop (12);
    dynTitleY = r.getY();
    r.removeFromTop (26);
    dynBox.setBounds (r.removeFromTop (36).withTrimmedLeft (0).withWidth (330).reduced (0, 4));
    auto dynArea = r.withWidth (juce::jmin (1000, r.getWidth()));
    const int colW = dynArea.getWidth() / 2;
    for (int k = 0; k < 6; ++k)
        if (dynRows[(size_t) k])
            dynRows[(size_t) k]->setBounds (dynArea.getX() + (k / 3) * colW, dynArea.getY() + (k % 3) * 36, colW - 20, 36);
}
} // namespace dm::ui
