#include "FxSection.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

ParamRow& FxSection::row (const char* label, const char* key, const juce::String& tip)
{
    owned.push_back (std::make_unique<ParamRow> (processor.apvts, dm::U (label), ids::fx (index, key), tip, 90));
    addAndMakeVisible (*owned.back());
    return *owned.back();
}

FxSection::FxSection (DaftMugProcessor& p, int padIndex) : processor (p), index (padIndex)
{
    // 0 saturation | 1 bitcrusher | 2 chorus | 3 delay
    blocks[0].title = "SATURATION";
    addAndMakeVisible (satBox);
    satBox.addItem ("Off", 1);
    satBox.addItem ("Douce (tanh)", 2);
    satBox.addItem ("Franche (clip)", 3);
    satBox.addItem ("Tube (asym.)", 4);
    satAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::fx (index, "sat_mode"), satBox);
    blocks[0].rows = { &satBox, &row ("Drive", "sat_drive", dm::U ("Pousse le signal dans la saturation.")),
                       &row ("Sortie", "sat_out", dm::U ("Compense le niveau après saturation.")),
                       &row ("Mix", "sat_mix", dm::U ("Dosage son sec / saturé.")) };

    blocks[1].title = "BITCRUSHER";
    blocks[1].rows = { &row ("Bits", "crush_bits", dm::U ("Résolution : 16 = propre, 1 = très dégradé.")),
                       &row ("Rate", "crush_rate", dm::U ("Réduction de fréquence d'échantillonnage (1 = aucune, 32 = très crade).")),
                       &row ("Mix", "crush_mix", dm::U ("0 % = désactivé.")) };

    blocks[2].title = "CHORUS";
    blocks[2].rows = { &row ("Mix", "chorus_mix", dm::U ("0 % = désactivé.")),
                       &row ("Rate", "chorus_rate", dm::U ("Vitesse de la modulation.")),
                       &row ("Depth", "chorus_depth", dm::U ("Profondeur de la modulation.")) };

    blocks[3].title = "DELAY";
    blocks[3].rows = { &row ("Mix", "delay_mix", dm::U ("0 % = désactivé.")),
                       &row ("Time", "delay_time", dm::U ("Temps entre les répétitions, 10–1000 ms.")),
                       &row ("Feedback", "delay_fb", dm::U ("Nombre de répétitions (max 95 %).")), &pingPong };
    addAndMakeVisible (pingPong);
    pingPong.setClickingTogglesState (true);
    pingPong.setTooltip (dm::U ("Les répétitions rebondissent entre gauche et droite."));
    ppAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::fx (index, "delay_pp"), pingPong);
}

void FxSection::resized()
{
    auto area = getLocalBounds();
    const int colGap = 22, rowGap = 18;
    const int colW = (area.getWidth() - colGap) / 2;
    const int blockH = kTitleH + kBlockRows * kRowH;
    for (int b = 0; b < 4; ++b)
    {
        const int cx = (b % 2) * (colW + colGap), cy = (b / 2) * (blockH + rowGap);
        auto r = juce::Rectangle<int> (cx, cy, colW, blockH);
        blocks[(size_t) b].area = r;
        r.removeFromTop (kTitleH);
        for (auto* c : blocks[(size_t) b].rows)
        {
            auto line = r.removeFromTop (kRowH);
            if (c == &satBox || c == &pingPong)
                c->setBounds (line.withTrimmedLeft (90).withWidth (juce::jmin (200, line.getWidth() - 90)).reduced (0, 4));
            else
                c->setBounds (line);
        }
    }
}

void FxSection::paint (juce::Graphics& g)
{
    g.setFont (makeFont (12.0f, true));
    for (auto& b : blocks)
    {
        g.setColour (accent2);
        g.drawText (b.title, b.area.withHeight (kTitleH), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine (b.area.getY() + kTitleH - 4, (float) b.area.getX(), (float) b.area.getRight());
    }
}
} // namespace dm::ui
