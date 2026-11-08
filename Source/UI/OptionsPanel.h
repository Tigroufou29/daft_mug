#pragma once

#include <functional>
#include <memory>

#include "JuceIncludes.h"
#include "ParamRow.h"
#include "PluginProcessor.h"
#include "UiSettings.h"

namespace dm::ui
{
// Global options. Today: voice management (polyphony limit, per-pad limit, stealing policy).
// The "IA et confidentialité" page of the specification will be added here in the Gemini phase.
class OptionsPanel : public juce::Component
{
public:
    explicit OptionsPanel (DaftMugProcessor&);

    std::function<void()> onBack;
    std::function<void()> onAiSettings;   // Options > IA et confidentialité
    std::function<void()> onTerms;        // conditions d'utilisation
    std::function<void()> onRendererChanged;   // the editor applies the new drawing mode

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DaftMugProcessor& processor;
    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    ParamRow maxVoices, voicesPerPad;
    juce::TextButton aiButton { dm::U ("IA et confidentialité…") }, termsButton { dm::U ("Conditions d'utilisation…") };
    juce::Label stealLabel, help;
    juce::ComboBox stealBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stealAttachment;
    juce::Rectangle<int> card, displayCard, outputCard;
    ParamRow masterRow, ceilingRow;
    juce::ToggleButton limiterButton { dm::U ("Limiteur de sortie (protège contre la saturation)") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAttachment;
    juce::ComboBox rendererBox;
    juce::Label rendererLabel, rendererHelp;
};
} // namespace dm::ui
