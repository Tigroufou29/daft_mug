#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "JuceIncludes.h"
#include "Mutate.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Full-window overlay "Mutate": generates variants of the pad's SOUND SETTINGS (pitch, pan, volume, attack, EQ, reverb amount),
// lets you hear each one on the pad itself, compare, and keep one. The sample is never modified; the previous settings can
// be restored with "Annuler".
class MutatePanel : public juce::Component,
                    private juce::Timer
{
public:
    MutatePanel (DaftMugProcessor&, int pad);
    ~MutatePanel() override;

    std::function<void()> onBack;
    void resized() override;
    void paint (juce::Graphics&) override;

    // exposed for the smoke test
    void generate();
    void audition (int variant);
    juce::Result applyChosen();
    int numVariants() const noexcept { return (int) variants.size(); }
    const std::vector<MutateValues>& getVariants() const noexcept { return variants; }
    void choose (int variant) { chosen = variant; repaint(); }

private:
    struct Card : juce::Component
    {
        MutatePanel& owner;
        int index;
        Card (MutatePanel& o, int i) : owner (o), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override;
    };

    void timerCallback() override;
    void stopAudition();
    MutateConfig currentConfig() const;
    juce::String describe (int variant) const;

    DaftMugProcessor& processor;
    const int pad;
    MutateValues base;
    std::vector<MutateValues> variants;
    std::vector<std::unique_ptr<Card>> cards;
    int chosen = 0, playing = -1, quietTicks = 0;
    uint32_t seed = 1;

    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    juce::Label title, help, info;
    std::array<juce::ToggleButton, 6> enableButtons;   // pitch, pan, volume, attack, EQ, reverb
    juce::Slider amountSlider;
    juce::Label amountLabel;
    juce::ComboBox countBox;
    juce::TextButton generateButton { dm::U ("Générer de nouvelles variantes") }, applyButton { dm::U ("Appliquer la variante choisie") },
                     undoButton { dm::U ("Annuler (revenir aux réglages d'avant)") };
    juce::Rectangle<int> gridArea;
};
} // namespace dm::ui
