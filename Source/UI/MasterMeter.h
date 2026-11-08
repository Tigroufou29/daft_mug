#pragma once

#include "JuceIncludes.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Stereo output meter: RMS body (300 ms) + peak marker with hold, dB scale, numeric peak / RMS / crest (peak - RMS)
// readouts and a clip light (click the meter to clear the hold and the clip light).
class MasterMeter : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit MasterMeter (DaftMugProcessor&);
    ~MasterMeter() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { resetHold(); }

    // for the smoke test
    float getRmsDb (int ch) const noexcept { return rmsDb[(size_t) ch]; }
    float getPeakDb (int ch) const noexcept { return peakDb[(size_t) ch]; }
    bool isClipping() const noexcept { return clip; }
    bool isLimiting() const noexcept { return limTicks > 0; }
    void tick() { timerCallback(); }

private:
    void timerCallback() override;
    void resetHold();
    float dbToY (float db, juce::Rectangle<float> bar) const;
    void drawBar (juce::Graphics&, juce::Rectangle<float> bar, int ch) const;

    DaftMugProcessor& processor;
    std::array<float, 2> rmsDb { { -100.0f, -100.0f } }, peakDb { { -100.0f, -100.0f } }, holdDb { { -100.0f, -100.0f } };
    std::array<int, 2> holdTicks { { 0, 0 } };
    bool clip = false;
    int limTicks = 0;   // > 0: the limiter acted recently
};
} // namespace dm::ui
