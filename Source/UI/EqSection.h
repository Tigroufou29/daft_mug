#pragma once

#include <array>
#include <memory>

#include "Eq.h"
#include "JuceIncludes.h"
#include "ParamRow.h"
#include "PluginProcessor.h"
#include "WaveformView.h"

namespace dm::ui
{
// Frequency-response curve of the pad's 4-band EQ. Drag a node: horizontal = frequency, vertical = gain.
// Mouse wheel over a node = Q (bandwidth). Double-click a node = reset the band. Everything goes through the
// automatable parameters (with host gestures), so edits are undoable / recordable in the DAW.
class EqView : public juce::Component,
               private juce::Timer
{
public:
    EqView (DaftMugProcessor&, int padIndex);
    ~EqView() override { stopTimer(); processor.setAnalyzerPad (-1); }

    std::function<void (int)> onBandSelected;
    void setSelectedBand (int band);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour bandColour (int band);

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float> p) const;
    void setParam (juce::RangedAudioParameter* p, float value);
    double sampleRate() const;

    DaftMugProcessor& processor;
    const int index;
    EqParams eq;
    std::vector<float> live, liveSmooth;   // live analyser (post-EQ signal of this pad), 0..1 per log-spaced point
    uint32_t lastWrite = 0;
    void updateLiveSpectrum();
    int selected = 0, hovered = -1, dragging = -1;
    std::array<juce::RangedAudioParameter*, kEqBands> freqP {}, gainP {}, qP {};
};

// Curve + selected-band controls (on, type, frequency, gain, Q).
class EqSection : public juce::Component,
                  private juce::Timer
{
public:
    EqSection (DaftMugProcessor&, int padIndex);
    ~EqSection() override { stopTimer(); processor.setEqSolo (index, -1); }

    void setSample (const SampleHandle& s) { wave.setSample (s); }
    int getPreferredHeight() const noexcept { return kWaveBlock + 262 + 14 + 40 + 6 + 3 * 36 + kDynBlock + 6; }
    static constexpr int kWaveBlock = 90 + 10;   // sample waveform strip above the curve
    static constexpr int kDynBlock = 12 + 26 + 36 + 3 * 36;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void selectBand (int band);
    void timerCallback() override;

    DaftMugProcessor& processor;
    const int index;
    int selectedBand = -1;

    WaveformView wave;
    EqView view;
    juce::ToggleButton eqOnButton { "EQ actif" };
    std::array<juce::TextButton, kEqBands> bandButtons;
    juce::ToggleButton soloButton { dm::U ("Écouter la bande") };
    juce::ToggleButton bandOnButton { dm::U ("Bande active") };
    juce::ComboBox typeBox;
    std::unique_ptr<ParamRow> freqRow, gainRow, qRow;
    juce::ComboBox dynBox;
    std::array<std::unique_ptr<ParamRow>, 6> dynRows;   // threshold, ratio, attack, release, knee, range
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dynAttachment;
    int dynTitleY = 0;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAttachment, bandOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
};
} // namespace dm::ui
