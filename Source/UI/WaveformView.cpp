#include "WaveformView.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

namespace
{
constexpr int kRulerHeight = 20;
constexpr int kScrollBarHeight = 12;
constexpr float kMaxZoom = 512.0f;
constexpr float kMinRegion = 0.0005f;

juce::String formatSeconds (double s)
{
    if (s >= 10.0) return juce::String (s, 1) + " s";
    if (s >= 1.0) return juce::String (s, 2) + " s";
    return juce::String (juce::roundToInt (s * 1000.0)) + " ms";
}
} // namespace

WaveformView::WaveformView (DaftMugProcessor& p, int padIndex, bool isEditable)
    : processor (p), index (padIndex), editable (isEditable)
{
    startParam = processor.apvts.getParameter (ids::start (index));
    endParam = processor.apvts.getParameter (ids::end (index));

    if (editable)
    {
        scrollBar.setAutoHide (false);
        scrollBar.addListener (this);
        addAndMakeVisible (scrollBar);
    }
    startTimerHz (30);
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    auto r = getLocalBounds();
    if (editable)
        r = r.withTrimmedTop (kRulerHeight).withTrimmedBottom (kScrollBarHeight + 2);
    return r;
}

void WaveformView::setSample (const SampleHandle& s)
{
    sample = s;
    zoom = 1.0f;
    viewStart = 0.0f;
    updateScrollBar();
    rebuildPeaks();
    repaint();
}

void WaveformView::resized()
{
    if (editable)
        scrollBar.setBounds (getLocalBounds().removeFromBottom (kScrollBarHeight).reduced (8, 0));
    rebuildPeaks();
}

float WaveformView::xToFraction (float x) const
{
    const float w = juce::jmax (1.0f, (float) getWidth());
    return viewStart + (x / w) / zoom;
}

float WaveformView::fractionToX (float f) const
{
    return (f - viewStart) * zoom * (float) getWidth();
}

double WaveformView::fractionToSeconds (float f) const
{
    return sample != nullptr ? (double) f * sample->numFrames() / sample->sampleRate : 0.0;
}

void WaveformView::rebuildPeaks()
{
    peaks.clear();
    const int w = getWidth();
    if (sample == nullptr || ! sample->isValid() || w <= 0)
        return;

    const auto frames = static_cast<double> (sample->numFrames());
    peaks.reserve ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const auto a = (size_t) juce::jlimit (0.0, frames - 1.0, (double) xToFraction ((float) x) * frames);
        const auto b = (size_t) juce::jlimit ((double) a + 1.0, frames, (double) xToFraction ((float) x + 1.0f) * frames);
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b; ++i)
        {
            const float v = 0.5f * (sample->left[i] + sample->right[i]);
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }
        peaks.emplace_back (lo, hi);
    }
}

// ---------------------------------------------------------------- zoom / scroll
void WaveformView::setView (float newZoom, float newStart)
{
    zoom = juce::jlimit (1.0f, kMaxZoom, newZoom);
    viewStart = juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - 1.0f / zoom), newStart);
    updateScrollBar();
    rebuildPeaks();
    repaint();
}

void WaveformView::zoomAt (float anchor, float factor)
{
    const float ratio = (anchor - viewStart) * zoom;                 // where the anchor sits in the view (0..1)
    const float newZoom = juce::jlimit (1.0f, kMaxZoom, zoom * factor);
    setView (newZoom, anchor - ratio / newZoom);
}

void WaveformView::zoomIn()  { zoomAt (viewStart + 0.5f / zoom, 2.0f); }
void WaveformView::zoomOut() { zoomAt (viewStart + 0.5f / zoom, 0.5f); }
void WaveformView::zoomFit() { setView (1.0f, 0.0f); }

void WaveformView::zoomToRegion()
{
    const float lo = juce::jmin (start, end), hi = juce::jmax (start, end);
    const float span = juce::jmax (hi - lo, 0.002f);
    const float margin = span * 0.1f;
    const float newZoom = 1.0f / juce::jmin (1.0f, span + 2.0f * margin);
    setView (newZoom, lo - margin);
}

void WaveformView::updateScrollBar()
{
    if (! editable) return;
    scrollBar.setRangeLimits (0.0, 1.0);
    scrollBar.setCurrentRange ((double) viewStart, (double) (1.0f / zoom), juce::dontSendNotification);
}

void WaveformView::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    viewStart = juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - 1.0f / zoom), (float) newStart);
    rebuildPeaks();
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! editable || sample == nullptr)
        return;

    const float amount = ! juce::approximatelyEqual (w.deltaY, 0.0f) ? w.deltaY : w.deltaX;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        setView (zoom, viewStart - amount * 0.25f / zoom);
    else
        zoomAt (xToFraction ((float) e.x), amount > 0.0f ? 1.3f : 1.0f / 1.3f);
}

// ---------------------------------------------------------------- region editing
void WaveformView::resetRegion()
{
    setRegion (0.0f, 1.0f, true, true);
}

void WaveformView::setRegion (float newStart, float newEnd, bool startMoved, bool endMoved)
{
    newStart = juce::jlimit (0.0f, 1.0f, newStart);
    newEnd = juce::jlimit (0.0f, 1.0f, newEnd);
    if (newEnd - newStart < kMinRegion)
    {
        if (startMoved && ! endMoved) newStart = juce::jmax (0.0f, newEnd - kMinRegion);
        else newEnd = juce::jmin (1.0f, newStart + kMinRegion);
    }

    auto apply = [] (juce::RangedAudioParameter* prm, float v)
    {
        if (prm == nullptr) return;
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };

    // wrapped in gestures so the host records one undo/automation step
    if (startMoved && startParam != nullptr) startParam->beginChangeGesture();
    if (endMoved && endParam != nullptr) endParam->beginChangeGesture();
    if (startMoved) apply (startParam, newStart);
    if (endMoved) apply (endParam, newEnd);
    if (startMoved && startParam != nullptr) startParam->endChangeGesture();
    if (endMoved && endParam != nullptr) endParam->endChangeGesture();
}

void WaveformView::timerCallback()
{
    const float s = *processor.apvts.getRawParameterValue (ids::start (index));
    const float e = *processor.apvts.getRawParameterValue (ids::end (index));
    const float head = processor.getEngine().isPadPlaying (index) ? processor.getEngine().padPlayhead (index) : -1.0f;
    float lp = -1.0f;
    if (juce::roundToInt (processor.apvts.getRawParameterValue (ids::mode (index))->load()) == (int) PlayMode::Loop)
    {
        const float lo = juce::jmin (s, e), hi = juce::jmax (s, e);
        const float f = *processor.apvts.getRawParameterValue (ids::loopStart (index));
        lp = *processor.apvts.getRawParameterValue (ids::reverse (index)) >= 0.5f ? hi - f * (hi - lo) : lo + f * (hi - lo);
    }
    if (! juce::approximatelyEqual (s, start) || ! juce::approximatelyEqual (e, end) || std::abs (head - playhead) > 0.0005f
        || ! juce::approximatelyEqual (lp, loopPoint))
    {
        loopPoint = lp;
        start = s;
        end = e;
        playhead = head;
        repaint();
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    selectActive = false;
    if (! editable || sample == nullptr)
        return;

    const float x = (float) e.x;
    const float ds = std::abs (x - fractionToX (start));
    const float de = std::abs (x - fractionToX (end));
    if (juce::jmin (ds, de) <= 14.0f)
    {
        drag = ds <= de ? Drag::Start : Drag::End;
        auto* prm = drag == Drag::Start ? startParam : endParam;
        if (prm != nullptr) prm->beginChangeGesture();
    }
    else
    {
        drag = Drag::Select;                   // becomes a real selection only after a few pixels of drag
        selectAnchor = xToFraction (x);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None)
        return;

    const float f = juce::jlimit (0.0f, 1.0f, xToFraction ((float) e.x));

    if (drag == Drag::Select)
    {
        if (! selectActive && e.getDistanceFromDragStart() < 4)
            return;
        if (! selectActive)
        {
            selectActive = true;
            if (startParam != nullptr) startParam->beginChangeGesture();
            if (endParam != nullptr) endParam->beginChangeGesture();
        }
        const float a = juce::jlimit (0.0f, 1.0f, selectAnchor);
        const float lo = juce::jmin (a, f), hi = juce::jmax (a, f);
        auto apply = [] (juce::RangedAudioParameter* prm, float v) { if (prm != nullptr) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        const float hiSafe = hi - lo < kMinRegion ? juce::jmin (1.0f, lo + kMinRegion) : hi;
        apply (startParam, lo);
        apply (endParam, hiSafe);
        return;
    }

    auto* prm = drag == Drag::Start ? startParam : endParam;
    if (prm == nullptr) return;
    const float v = drag == Drag::Start ? juce::jmin (f, end - kMinRegion) : juce::jmax (f, start + kMinRegion);
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (0.0f, 1.0f, v)));
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::Start && startParam != nullptr) startParam->endChangeGesture();
    else if (drag == Drag::End && endParam != nullptr) endParam->endChangeGesture();
    else if (drag == Drag::Select && selectActive)
    {
        if (startParam != nullptr) startParam->endChangeGesture();
        if (endParam != nullptr) endParam->endChangeGesture();
    }
    drag = Drag::None;
    selectActive = false;
}

// ---------------------------------------------------------------- painting
void WaveformView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    paintGlass (g, b, 12.0f, juce::Colours::white, 0.0f, 0.8f);

    if (sample == nullptr || peaks.empty())
    {
        g.setColour (textDim);
        g.setFont (makeFont (13.0f));
        g.drawText (dm::U ("Aucun sample chargé"), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto wave = waveArea().toFloat();
    const float cy = wave.getCentreY();
    const float half = wave.getHeight() * 0.44f;
    const float x0 = fractionToX (juce::jmin (start, end));
    const float x1 = fractionToX (juce::jmax (start, end));

    // time ruler
    if (editable)
    {
        const double totalSeconds = fractionToSeconds (1.0f);
        const double visibleSeconds = totalSeconds / (double) zoom;
        static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100 };
        double step = steps[15];
        for (double s : steps)
            if (s / visibleSeconds * (double) getWidth() >= 80.0) { step = s; break; }

        const double t0 = (double) viewStart * totalSeconds;
        g.setFont (makeFont (11.0f));
        for (double t = std::ceil (t0 / step) * step; t <= t0 + visibleSeconds; t += step)
        {
            const float x = fractionToX ((float) (t / totalSeconds));
            g.setColour (textDim.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, (float) kRulerHeight - 6.0f, wave.getBottom());
            g.setColour (textDim);
            g.drawText (formatSeconds (t), juce::Rectangle<float> (x + 3.0f, 2.0f, 70.0f, 14.0f), juce::Justification::centredLeft);
        }
    }

    // waveform
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float x = (float) i + 0.5f;
        const bool inside = x >= x0 && x <= x1;
        g.setColour (inside ? accent2.withAlpha (0.95f) : textDim.withAlpha (0.35f));
        g.drawVerticalLine ((int) i, cy - peaks[i].second * half - 0.5f, cy - peaks[i].first * half + 0.5f);
    }

    // outside-region shading + handles with their times
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (b.getX(), wave.getY(), juce::jmax (0.0f, x0 - b.getX()), wave.getHeight()));
    g.fillRect (juce::Rectangle<float> (x1, wave.getY(), juce::jmax (0.0f, b.getRight() - x1), wave.getHeight()));

    g.setFont (makeFont (11.0f, true));
    const float lo = juce::jmin (start, end), hi = juce::jmax (start, end);
    for (int h = 0; h < 2; ++h)
    {
        const float x = h == 0 ? x0 : x1;
        if (x < -6.0f || x > b.getRight() + 6.0f) continue;
        g.setColour (accent);
        g.fillRect (x - 1.0f, wave.getY(), 2.0f, wave.getHeight());
        if (editable)
        {
            g.fillRoundedRectangle (x - 5.0f, wave.getY() + 4.0f, 10.0f, 16.0f, 3.0f);
            const auto label = formatSeconds (fractionToSeconds (h == 0 ? lo : hi));
            const float lw = 64.0f;
            const float lx = h == 0 ? juce::jmin (x + 8.0f, b.getRight() - lw - 2.0f) : juce::jmax (x - lw - 8.0f, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (lx, wave.getBottom() - 22.0f, lw, 16.0f, 5.0f);
            g.setColour (text);
            g.drawText (label, juce::Rectangle<float> (lx, wave.getBottom() - 22.0f, lw, 16.0f), juce::Justification::centred);
        }
    }

    if (editable)
    {
        const auto label = dm::U ("Région : ") + formatSeconds (fractionToSeconds (hi) - fractionToSeconds (lo))
                           + "   |   Fichier : " + formatSeconds (fractionToSeconds (1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        const auto pill = juce::Rectangle<float> (b.getRight() - 262.0f, wave.getY() + 4.0f, 256.0f, 16.0f);
        g.fillRoundedRectangle (pill, 5.0f);
        g.setColour (textDim);
        g.drawText (label, pill, juce::Justification::centred);
    }

    if (loopPoint >= 0.0f)
    {
        const float x = fractionToX (loopPoint);
        if (x >= (float) wave.getX() && x <= (float) wave.getRight())
        {
            g.setColour (theme::accent2.withAlpha (0.9f));
            g.fillRect (x - 0.75f, (float) wave.getY(), 1.5f, (float) wave.getHeight());
            g.drawText ("LOOP", juce::Rectangle<float> (x + 3.0f, (float) wave.getY() + 4.0f, 44.0f, 14.0f), juce::Justification::centredLeft);
        }
    }

    if (playhead >= 0.0f)
    {
        g.setColour (juce::Colours::white);
        g.fillRect (fractionToX (playhead) - 0.5f, wave.getY(), 1.5f, wave.getHeight());
    }
}
} // namespace dm::ui
