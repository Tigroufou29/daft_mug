#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "JuceIncludes.h"
#include "PluginProcessor.h"
#include "SliceDetector.h"

namespace dm::ui
{
// Waveform with draggable cut markers. Click a slice to hear it. Double-click empty space: add a cut. Double-click a marker
// (or right-click it): remove it. Drag a marker: move it.
class SliceView : public juce::Component
{
public:
    SliceView();

    void setSample (SampleHandle s, int firstPad);
    void setPoints (std::vector<int> pts) { points = std::move (pts); repaint(); }
    const std::vector<int>& getPoints() const noexcept { return points; }
    void setFirstPad (int pad) { firstPad = pad; repaint(); }
    void setMaxSlices (int n) { maxSlices = n; }
    int getSelected() const noexcept { return selected; }
    void select (int slice) { selected = slice; repaint(); }

    std::function<void()> onPointsEdited;           // user dragged / added / removed a cut
    std::function<void (int)> onPreviewStart;       // slice index
    std::function<void()> onPreviewEnd;

    void paint (juce::Graphics&) override;
    void resized() override { peaksWidth = -1; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    int frameToX (int f) const;
    int xToFrame (int x) const;
    int markerAt (int x) const;
    int sliceAt (int frame) const;
    int minGapFrames() const;
    void rebuildPeaks();

    SampleHandle sample;
    std::vector<int> points;
    std::vector<std::pair<float, float>> peaks;
    int peaksWidth = -1;
    int firstPad = 0, maxSlices = 16;
    int selected = 0, dragging = -1, hover = -1;
    bool previewing = false;
    bool movedMarker = false;
};

// Full-window overlay "Slice to Pads": detect, adjust, listen, distribute on pads, undo.
class SlicePanel : public juce::Component,
                   private juce::Timer
{
public:
    SlicePanel (DaftMugProcessor&, int sourcePad);
    ~SlicePanel() override;

    std::function<void()> onBack;
    void resized() override;
    void paint (juce::Graphics&) override;

    // exposed for the smoke test
    SliceView& getView() { return view; }
    juce::Result apply();
    void detect();

private:
    void timerCallback() override;
    void updateInfo();
    void stopPreview();
    void restoreSource();
    void startPreview (int slice);
    int maxSlicesForStart() const { return kNumPads - startPad; }
    SliceConfig currentConfig() const;

    DaftMugProcessor& processor;
    const int sourcePad;
    int startPad = 0;
    SampleHandle sample;

    SliceView view;
    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    juce::Label title, info, help;
    juce::ComboBox modeBox, startBox;
    juce::Slider sensSlider, minSlider, partsSlider;
    juce::Label sensLabel, minLabel, partsLabel, startLabel;
    juce::ToggleButton snapButton { dm::U ("Caler sur les passages par zéro") };
    juce::TextButton detectButton { dm::U ("Détecter") }, resetButton { dm::U ("Tout effacer") },
                     applyButton { dm::U ("Répartir sur les pads") }, undoButton { dm::U ("Annuler le découpage") };

    // audition goes through the source pad with a temporary start / end override (see DaftMugProcessor::setPreviewRegion)
    int previewClearCountdown = 0;
};
} // namespace dm::ui
