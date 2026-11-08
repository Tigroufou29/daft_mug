#pragma once

#include <memory>
#include <vector>

#include "JuceIncludes.h"
#include "ParamRow.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Saturation, bitcrusher, chorus, delay of one pad (all automatable). Order of the audio chain:
// saturation -> bitcrusher -> EQ -> chorus -> delay.
class FxSection : public juce::Component
{
public:
    FxSection (DaftMugProcessor&, int padIndex);

    static constexpr int kRowH = 36, kTitleH = 28, kBlockRows = 4;
    int getPreferredHeight() const noexcept { return 2 * (kTitleH + kBlockRows * kRowH) + 18; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Block { juce::String title; std::vector<juce::Component*> rows; juce::Rectangle<int> area; };

    ParamRow& row (const char* label, const char* key, const juce::String& tip);

    DaftMugProcessor& processor;
    const int index;
    std::vector<std::unique_ptr<ParamRow>> owned;
    std::array<Block, 4> blocks;

    juce::ComboBox satBox;
    juce::ToggleButton pingPong { "Ping-pong" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ppAttachment;
};
} // namespace dm::ui
