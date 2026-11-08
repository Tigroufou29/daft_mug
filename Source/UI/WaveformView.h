#pragma once

#include <vector>

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Non-destructive waveform. Editable mode adds: horizontal zoom (mouse wheel, anchored at the pointer),
// scrolling (scroll bar, Shift + wheel), a time ruler, draggable START / END handles with their times,
// drag-to-select a region, and the live playhead. Only the playback-region parameters change:
// the audio data and the original file are never modified.
class WaveformView : public juce::Component,
                     private juce::Timer,
                     private juce::ScrollBar::Listener
{
public:
    WaveformView (DaftMugProcessor&, int padIndex, bool editable);
    ~WaveformView() override { stopTimer(); }

    void setSample (const SampleHandle&);

    // zoom / region tools (used by the toolbar of the pad editor)
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomToRegion();
    void resetRegion();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void rebuildPeaks();
    void zoomAt (float anchorFraction, float factor);
    void setView (float newZoom, float newStart);
    void updateScrollBar();
    juce::Rectangle<int> waveArea() const;
    float xToFraction (float x) const;
    float fractionToX (float f) const;
    double fractionToSeconds (float f) const;
    void setRegion (float newStart, float newEnd, bool startMoved, bool endMoved);

    DaftMugProcessor& processor;
    const int index;
    const bool editable;

    SampleHandle sample;
    std::vector<std::pair<float, float>> peaks;   // (min, max) per pixel column of the visible range

    juce::RangedAudioParameter* startParam = nullptr;
    juce::RangedAudioParameter* endParam = nullptr;
    float start = 0.0f, end = 1.0f, playhead = -1.0f;
    float loopPoint = -1.0f;   // file fraction of the loop point while the pad is in Loop mode, else < 0

    float zoom = 1.0f;         // 1 = whole file visible
    float viewStart = 0.0f;    // fraction of the file at the left edge
    juce::ScrollBar scrollBar { false };

    enum class Drag { None, Start, End, Select };
    Drag drag = Drag::None;
    float selectAnchor = 0.0f;
    bool selectActive = false;
};
} // namespace dm::ui
