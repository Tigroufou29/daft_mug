#include "SlicePanel.h"

#include "LiquidGlassLookAndFeel.h"
#include "NameUtils.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

// ================================================================================== SliceView
SliceView::SliceView() { setMouseCursor (juce::MouseCursor::NormalCursor); }

void SliceView::setSample (SampleHandle s, int pad)
{
    sample = std::move (s);
    firstPad = pad;
    peaksWidth = -1;
    repaint();
}

int SliceView::frameToX (int f) const
{
    const int n = sample ? sample->numFrames() : 1;
    return (int) ((long long) f * (getWidth() - 1) / std::max (1, n));
}

int SliceView::xToFrame (int x) const
{
    const int n = sample ? sample->numFrames() : 1;
    return juce::jlimit (0, std::max (0, n - 1), (int) ((long long) x * n / std::max (1, getWidth() - 1)));
}

int SliceView::minGapFrames() const
{
    return sample ? std::max (1, (int) (0.01 * sample->sampleRate)) : 1;   // 10 ms
}

int SliceView::markerAt (int x) const
{
    for (size_t i = 0; i < points.size(); ++i)
        if (std::abs (frameToX (points[i]) - x) <= 6)
            return (int) i;
    return -1;
}

int SliceView::sliceAt (int frame) const
{
    int s = -1;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i] <= frame) s = (int) i;
    return s;
}

void SliceView::rebuildPeaks()
{
    peaksWidth = getWidth();
    peaks.assign ((size_t) std::max (1, peaksWidth), { 0.0f, 0.0f });
    if (! sample || ! sample->isValid()) return;
    const int n = sample->numFrames();
    for (int x = 0; x < peaksWidth; ++x)
    {
        const int a = (int) ((long long) x * n / peaksWidth), b = std::max (a + 1, (int) ((long long) (x + 1) * n / peaksWidth));
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < std::min (b, n); ++i)
        {
            const float v = 0.5f * (sample->left[(size_t) i] + sample->right[(size_t) i]);
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void SliceView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    paintGlass (g, b, 14.0f, juce::Colours::white, 0.0f, 0.7f);
    if (! sample || ! sample->isValid()) return;
    if (peaksWidth != getWidth()) rebuildPeaks();

    const int h = getHeight(), mid = h / 2;
    const int n = sample->numFrames();

    // slice regions
    for (size_t i = 0; i < points.size(); ++i)
    {
        const int x0 = frameToX (points[i]);
        const int x1 = i + 1 < points.size() ? frameToX (points[i + 1]) : getWidth() - 1;
        const auto col = DaftMugProcessor::defaultPadColour (juce::jlimit (0, kNumPads - 1, firstPad + (int) i));
        g.setColour (col.withAlpha ((int) i == selected ? 0.34f : 0.16f));
        g.fillRect (x0, 6, std::max (1, x1 - x0), h - 12);
    }

    // waveform
    g.setColour (accent2.withAlpha (0.9f));
    for (int x = 0; x < getWidth(); ++x)
    {
        const auto pk = peaks[(size_t) std::min (x, (int) peaks.size() - 1)];
        const int y0 = mid - (int) (pk.second * (float) (mid - 14)), y1 = mid - (int) (pk.first * (float) (mid - 14));
        g.drawVerticalLine (x, (float) y0, (float) std::max (y0 + 1, y1));
    }

    // markers + labels
    g.setFont (makeFont (11.0f, true));
    for (size_t i = 0; i < points.size(); ++i)
    {
        const int x = frameToX (points[i]);
        const bool hot = (int) i == hover || (int) i == dragging;
        g.setColour (hot ? juce::Colours::white : juce::Colours::white.withAlpha (0.75f));
        g.drawVerticalLine (x, 6.0f, (float) (h - 6));
        if (hot) g.drawVerticalLine (x + 1, 6.0f, (float) (h - 6));
        juce::Path tri;
        tri.addTriangle ((float) x - 6.0f, 6.0f, (float) x + 6.0f, 6.0f, (float) x, 16.0f);
        g.fillPath (tri);

        const int x1 = i + 1 < points.size() ? frameToX (points[i + 1]) : getWidth() - 1;
        if (x1 - x > 30)
        {
            g.setColour (text);
            g.drawText (juce::String (firstPad + (int) i + 1), x + 4, h - 22, x1 - x - 6, 16, juce::Justification::centredLeft);
        }
    }
    juce::ignoreUnused (n);
}

void SliceView::mouseMove (const juce::MouseEvent& e)
{
    const int m = markerAt (e.x);
    if (m != hover)
    {
        hover = m;
        setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void SliceView::mouseDown (const juce::MouseEvent& e)
{
    movedMarker = false;
    const int m = markerAt (e.x);
    if (e.mods.isPopupMenu())
    {
        if (m >= 0 && points.size() > 1)
        {
            points.erase (points.begin() + m);
            selected = std::min (selected, (int) points.size() - 1);
            if (onPointsEdited) onPointsEdited();
            repaint();
        }
        return;
    }
    if (m >= 0)
    {
        dragging = m;
        selected = m;
        repaint();
        return;
    }
    const int s = sliceAt (xToFrame (e.x));
    if (s >= 0)
    {
        selected = s;
        previewing = true;
        if (onPreviewStart) onPreviewStart (s);
        repaint();
    }
}

void SliceView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const int gap = minGapFrames();
    const int lo = dragging > 0 ? points[(size_t) dragging - 1] + gap : 0;
    const int hi = dragging + 1 < (int) points.size() ? points[(size_t) dragging + 1] - gap : (sample ? sample->numFrames() - gap : 0);
    points[(size_t) dragging] = juce::jlimit (lo, std::max (lo, hi), xToFrame (e.x));
    movedMarker = true;
    repaint();
}

void SliceView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        dragging = -1;
        if (movedMarker && onPointsEdited) onPointsEdited();
    }
    if (previewing)
    {
        previewing = false;
        if (onPreviewEnd) onPreviewEnd();
    }
}

void SliceView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! sample) return;
    const int m = markerAt (e.x);
    if (m >= 0)
    {
        if (points.size() > 1)
        {
            points.erase (points.begin() + m);
            selected = std::min (selected, (int) points.size() - 1);
        }
    }
    else
    {
        if ((int) points.size() >= maxSlices) return;       // the room left on the pads is the limit
        const int f = xToFrame (e.x);
        for (int p : points) if (std::abs (p - f) < minGapFrames()) return;
        points.push_back (f);
        std::sort (points.begin(), points.end());
    }
    if (onPointsEdited) onPointsEdited();
    repaint();
}

// ================================================================================== SlicePanel
SlicePanel::SlicePanel (DaftMugProcessor& p, int source) : processor (p), sourcePad (source), startPad (source)
{
    setOpaque (true);
    const auto meta = processor.getPadMeta (sourcePad);
    sample = meta.sample;

    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };

    title.setFont (makeFont (22.0f, true));
    title.setColour (juce::Label::textColourId, text);
    title.setText (dm::U ("Découper en pads  ·  ") + meta.name, juce::dontSendNotification);
    addAndMakeVisible (title);

    help.setFont (makeFont (12.5f));
    help.setColour (juce::Label::textColourId, textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (dm::U ("Cliquez une tranche pour l'écouter  ·  Glissez un repère pour le déplacer  ·  Double-clic dans le vide : ajouter un repère  ·  "
                         "Double-clic (ou clic droit) sur un repère : le supprimer.  Le fichier n'est jamais modifié : chaque pad lit simplement une partie du même son."),
                  juce::dontSendNotification);
    addAndMakeVisible (help);

    addAndMakeVisible (view);
    view.setSample (sample, startPad);
    view.onPointsEdited = [this] { updateInfo(); };
    view.onPreviewStart = [this] (int s) { startPreview (s); };
    view.onPreviewEnd = [this] { stopPreview(); };

    modeBox.addItem (dm::U ("Transitoires (détection)"), 1);
    modeBox.addItem (dm::U ("Parts égales"), 2);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { detect(); resized(); };
    addAndMakeVisible (modeBox);

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double step, double v, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
        s.setTextBoxIsEditable (true);
        s.setRange (lo, hi, step);
        s.setValue (v, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { detect(); };
        l.setText (name, juce::dontSendNotification);
        l.setFont (makeFont (13.0f));
        l.setColour (juce::Label::textColourId, textDim);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setup (sensSlider, sensLabel, dm::U ("Sensibilité"), 0.0, 100.0, 1.0, 50.0, " %");
    setup (minSlider, minLabel, dm::U ("Longueur min."), 10.0, 500.0, 1.0, 60.0, " ms");
    setup (partsSlider, partsLabel, "Parts", 2.0, 16.0, 1.0, 8.0, "");
    sensSlider.setTooltip (dm::U ("Haut : détecte aussi les attaques faibles. Bas : seulement les plus fortes."));
    minSlider.setTooltip (dm::U ("Aucune tranche plus courte que cette durée."));

    addAndMakeVisible (snapButton);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setClickingTogglesState (true);
    snapButton.onClick = [this] { detect(); };

    startLabel.setText (dm::U ("Premier pad"), juce::dontSendNotification);
    startLabel.setFont (makeFont (13.0f));
    startLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (startLabel);
    for (int i = 0; i < kNumPads; ++i) startBox.addItem ("Pad " + juce::String (i + 1), i + 1);
    startBox.setSelectedId (startPad + 1, juce::dontSendNotification);
    startBox.onChange = [this]
    {
        startPad = startBox.getSelectedId() - 1;
        view.setFirstPad (startPad);
        view.setMaxSlices (maxSlicesForStart());
        detect();
    };
    addAndMakeVisible (startBox);

    addAndMakeVisible (detectButton);
    addAndMakeVisible (resetButton);
    addAndMakeVisible (applyButton);
    addAndMakeVisible (undoButton);
    detectButton.onClick = [this] { detect(); };
    resetButton.onClick = [this] { view.setPoints ({ 0 }); updateInfo(); };
    applyButton.onClick = [this]
    {
        const auto r = apply();
        info.setText (r.wasOk() ? dm::U ("Découpage appliqué : chaque tranche est sur son pad. Vous pouvez annuler.") : r.getErrorMessage(), juce::dontSendNotification);
    };
    undoButton.onClick = [this] { processor.undoSlices(); updateInfo(); info.setText (dm::U ("Découpage annulé."), juce::dontSendNotification); };

    info.setFont (makeFont (13.0f));
    info.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (info);

    view.setMaxSlices (maxSlicesForStart());
    detect();
    startTimerHz (20);
}

SlicePanel::~SlicePanel()
{
    stopTimer();
    stopPreview();
    restoreSource();
}

void SlicePanel::restoreSource()
{
    processor.clearPreviewRegion (sourcePad);
}

SliceConfig SlicePanel::currentConfig() const
{
    SliceConfig c;
    c.mode = modeBox.getSelectedId() == 2 ? SliceMode::EqualParts : SliceMode::Transients;
    c.sensitivity = (float) sensSlider.getValue() / 100.0f;
    c.minSliceMs = (float) minSlider.getValue();
    c.parts = (int) partsSlider.getValue();
    c.maxSlices = maxSlicesForStart();
    c.snapToZeroCrossing = snapButton.getToggleState();
    return c;
}

void SlicePanel::detect()
{
    if (sample == nullptr || ! sample->isValid()) return;
    view.setMaxSlices (maxSlicesForStart());
    view.setPoints (detectSlicePoints (sample->left.data(), sample->right.data(), sample->numFrames(), sample->sampleRate, currentConfig()));
    view.select (0);
    updateInfo();
}

void SlicePanel::updateInfo()
{
    const int n = (int) view.getPoints().size();
    int occupied = 0;
    for (int i = 0; i < n; ++i)
        if (startPad + i < kNumPads && processor.getPadMeta (startPad + i).state != LoadState::Empty && startPad + i != sourcePad) ++occupied;
    juce::String t = juce::String (n) + (n > 1 ? " tranches" : " tranche") + dm::U (" → pads ") + juce::String (startPad + 1)
                     + (n > 1 ? dm::U ("–") + juce::String (startPad + n) : juce::String());
    if (occupied > 0) t += dm::U ("   ·   ") + juce::String (occupied) + (occupied > 1 ? dm::U (" pads déjà remplis seront remplacés (annulable)") : dm::U (" pad déjà rempli sera remplacé (annulable)"));
    info.setText (t, juce::dontSendNotification);
    undoButton.setEnabled (processor.canUndoSlices());
    applyButton.setEnabled (n > 0 && sample != nullptr);
}

void SlicePanel::timerCallback()
{
    undoButton.setEnabled (processor.canUndoSlices());
    if (previewClearCountdown > 0 && --previewClearCountdown == 0)
        processor.clearPreviewRegion (sourcePad);
}

void SlicePanel::startPreview (int slice)
{
    const auto& pts = view.getPoints();
    if (sample == nullptr || slice < 0 || slice >= (int) pts.size()) return;
    const double n = (double) sample->numFrames();
    const double a = pts[(size_t) slice] / n;
    const double b = (slice + 1 < (int) pts.size() ? pts[(size_t) slice + 1] : sample->numFrames()) / n;
    processor.setPreviewRegion (sourcePad, (float) a, (float) b);   // not a parameter change: the host never sees it
    previewClearCountdown = 8;                                      // cleared ~0.4 s later (per-hit values are captured at the trigger)
    processor.triggerPad (sourcePad, 1.0f);
}

void SlicePanel::stopPreview()
{
    processor.releasePad (sourcePad);   // ends a Gate-mode note; a one-shot simply plays out
}

juce::Result SlicePanel::apply()
{
    stopPreview();
    restoreSource();
    const auto& pts = view.getPoints();
    if (sample == nullptr || pts.empty()) return juce::Result::fail (dm::U ("Rien à répartir"));

    const auto base = juce::String (cleanSampleName (sample->name));
    std::vector<DaftMugProcessor::SliceAssignment> list;
    const int n = std::min ((int) pts.size(), kNumPads - startPad);
    for (int i = 0; i < n; ++i)
    {
        DaftMugProcessor::SliceAssignment a;
        a.pad = startPad + i;
        a.startFraction = (double) pts[(size_t) i] / (double) sample->numFrames();
        a.endFraction = (i + 1 < (int) pts.size() ? (double) pts[(size_t) i + 1] : (double) sample->numFrames()) / (double) sample->numFrames();
        a.name = base + " " + juce::String (i + 1).paddedLeft ('0', 2);
        list.push_back (a);
    }
    const auto r = processor.applySlices (sourcePad, list);
    updateInfo();
    return r;
}

void SlicePanel::paint (juce::Graphics& g)
{
    g.fillAll (bgTop);
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillRect (getLocalBounds());
}

void SlicePanel::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    auto top = r.removeFromTop (44);
    backButton.setBounds (top.removeFromLeft (110).reduced (0, 4));
    top.removeFromLeft (14);
    title.setBounds (top);
    help.setBounds (r.removeFromTop (38));
    r.removeFromTop (6);
    view.setBounds (r.removeFromTop (juce::jmax (180, r.getHeight() - 230)));
    r.removeFromTop (12);

    const bool equal = modeBox.getSelectedId() == 2;
    auto row1 = r.removeFromTop (36);
    modeBox.setBounds (row1.removeFromLeft (230).reduced (0, 3));
    row1.removeFromLeft (14);
    sensLabel.setVisible (! equal); sensSlider.setVisible (! equal); minLabel.setVisible (! equal); minSlider.setVisible (! equal);
    snapButton.setVisible (! equal);
    partsLabel.setVisible (equal); partsSlider.setVisible (equal);
    if (! equal)
    {
        sensLabel.setBounds (row1.removeFromLeft (90));
        sensSlider.setBounds (row1.removeFromLeft (260));
        row1.removeFromLeft (14);
        minLabel.setBounds (row1.removeFromLeft (110));
        minSlider.setBounds (row1.removeFromLeft (260));
    }
    else
    {
        partsLabel.setBounds (row1.removeFromLeft (60));
        partsSlider.setBounds (row1.removeFromLeft (300));
    }
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (36);
    startLabel.setBounds (row2.removeFromLeft (100));
    startBox.setBounds (row2.removeFromLeft (120).reduced (0, 3));
    row2.removeFromLeft (14);
    snapButton.setBounds (row2.removeFromLeft (260).reduced (0, 3));
    row2.removeFromLeft (14);
    detectButton.setBounds (row2.removeFromLeft (110).reduced (0, 3));
    row2.removeFromLeft (8);
    resetButton.setBounds (row2.removeFromLeft (120).reduced (0, 3));
    r.removeFromTop (10);
    info.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    auto row3 = r.removeFromTop (44);
    applyButton.setBounds (row3.removeFromLeft (240).reduced (0, 3));
    row3.removeFromLeft (10);
    undoButton.setBounds (row3.removeFromLeft (220).reduced (0, 3));
}
} // namespace dm::ui
