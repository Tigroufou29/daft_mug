#pragma once

#include "JuceIncludes.h"
#include "ParamRow.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Explains the three different "fades" at a glance:
//   top lane    ATTACK / DECAY / SUSTAIN / RELEASE : the envelope of every hit (starts at the trigger)
//   bottom lane FADE-IN / FADE-OUT                  : fades tied to the START and END of the played region
class EnvelopeView : public juce::Component,
                     private juce::Timer
{
public:
    EnvelopeView (DaftMugProcessor&, int padIndex);
    ~EnvelopeView() override { stopTimer(); }

    void setSample (const SampleHandle& s) { sample = s; repaint(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    struct Values
    {
        float attack = 0, decay = 0, sustain = 1, release = 15, fadeIn = 0, fadeOut = 0, regionSeconds = 1;
        bool operator== (const Values& o) const noexcept
        {
            return juce::approximatelyEqual (attack, o.attack) && juce::approximatelyEqual (decay, o.decay)
                   && juce::approximatelyEqual (sustain, o.sustain) && juce::approximatelyEqual (release, o.release)
                   && juce::approximatelyEqual (fadeIn, o.fadeIn) && juce::approximatelyEqual (fadeOut, o.fadeOut)
                   && juce::approximatelyEqual (regionSeconds, o.regionSeconds);
        }
    };
    Values readValues() const;

    DaftMugProcessor& processor;
    const int index;
    SampleHandle sample;
    Values values;
};

// Graph + the six sliders (all automatable, one set per pad).
class EnvelopeSection : public juce::Component
{
public:
    EnvelopeSection (DaftMugProcessor&, int padIndex);

    void setSample (const SampleHandle& s) { graph.setSample (s); }
    int getPreferredHeight() const noexcept { return 190 + 3 * 36 + 8; }
    void resized() override;

private:
    EnvelopeView graph;
    ParamRow attack, decay, sustain, release, fadeIn, fadeOut;
};
} // namespace dm::ui
