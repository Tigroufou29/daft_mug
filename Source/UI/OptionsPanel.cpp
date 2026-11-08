#include "OptionsPanel.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

OptionsPanel::OptionsPanel (DaftMugProcessor& p)
    : processor (p),
      maxVoices (p.apvts, dm::U ("Voix maximum"), ids::maxVoices, dm::U ("Nombre maximum de sons simultanés (tous pads confondus)"), 170),
      voicesPerPad (p.apvts, dm::U ("Voix par pad"), ids::voicesPerPad, dm::U ("Nombre maximum de lectures simultanées d'un même pad"), 170),
      masterRow (p.apvts, dm::U ("Volume maître"), ids::masterGain, dm::U ("Niveau général de sortie, avant le limiteur"), 170),
      ceilingRow (p.apvts, dm::U ("Plafond du limiteur"), ids::limiterCeiling, dm::U ("Niveau maximum que la sortie ne dépassera jamais"), 170)
{
    setOpaque (true);
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };

    addAndMakeVisible (aiButton);
    addAndMakeVisible (termsButton);
    aiButton.setTooltip (dm::U ("Clé API Gemini personnelle, modèle, activation de l'assistant facultatif Daft Mug AI"));
    aiButton.onClick = [this] { if (onAiSettings) onAiSettings(); };
    termsButton.onClick = [this] { if (onTerms) onTerms(); };

    addAndMakeVisible (masterRow);
    addAndMakeVisible (ceilingRow);
    addAndMakeVisible (limiterButton);
    limiterButton.setTooltip (dm::U ("Limiteur à anticipation de 1,5 ms : la sortie ne dépasse jamais le plafond. Ajoute 1,5 ms de latence, constante."));
    limiterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::limiterOn, limiterButton);
    addAndMakeVisible (maxVoices);
    addAndMakeVisible (voicesPerPad);

    stealLabel.setText (dm::U ("Quand la limite est atteinte"), juce::dontSendNotification);
    stealLabel.setColour (juce::Label::textColourId, textDim);
    stealLabel.setFont (makeFont (13.0f));
    addAndMakeVisible (stealLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ids::stealPolicy)))
        stealBox.addItemList (choice->choices, 1);
    addAndMakeVisible (stealBox);
    stealAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ids::stealPolicy, stealBox);

    help.setFont (makeFont (13.0f));
    help.setColour (juce::Label::textColourId, textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (dm::U ("Couper la plus ancienne : la voix qui joue depuis le plus longtemps s'éteint en 4 ms pour laisser la place.\n"
                         "Couper la plus faible : la voix actuellement la moins audible s'éteint.\n"
                         "Ignorer la nouvelle note : les sons en cours ne sont jamais coupés ; la nouvelle note est simplement ignorée.\n\n"
                         "Ces réglages sont des paramètres du plug-in : ils sont enregistrés avec le projet de votre STAN."),
                 juce::dontSendNotification);
    addAndMakeVisible (help);

   #if JUCE_WINDOWS
    rendererLabel.setText (dm::U ("Rendu de l'interface"), juce::dontSendNotification);
    rendererLabel.setColour (juce::Label::textColourId, textDim);
    rendererLabel.setFont (makeFont (13.0f));
    addAndMakeVisible (rendererLabel);
    rendererBox.addItem (dm::U ("Automatique (recommandé)"), 1);
    rendererBox.addItem (dm::U ("Logiciel (processeur)"), 2);
    rendererBox.addItem (dm::U ("Accéléré par la carte graphique (Direct2D)"), 3);
    const auto mode = UiSettings::getRenderer();
    rendererBox.setSelectedId (mode == "software" ? 2 : (mode == "gpu" ? 3 : 1), juce::dontSendNotification);
    rendererBox.onChange = [this]
    {
        const int id = rendererBox.getSelectedId();
        UiSettings::setRenderer (id == 2 ? "software" : (id == 3 ? "gpu" : "auto"));
        if (onRendererChanged) onRendererChanged();
    };
    addAndMakeVisible (rendererBox);
    rendererHelp.setFont (makeFont (12.0f));
    rendererHelp.setColour (juce::Label::textColourId, textDim);
    rendererHelp.setJustificationType (juce::Justification::topLeft);
    rendererHelp.setText (dm::U ("Si l'interface est lente (réglages de pad, redimensionnement), essayez l'autre mode : l'effet est immédiat. "
                                 "Cela ne change rien au son."), juce::dontSendNotification);
    addAndMakeVisible (rendererHelp);
   #endif
}

void OptionsPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), (float) getWidth() * 0.2f, 0.0f,
                                             accent.withAlpha (0.0f), (float) getWidth() * 0.6f, (float) getHeight() * 0.7f, true));
    g.fillAll();

    g.setColour (text);
    g.setFont (makeFont (24.0f, true));
    g.drawText ("Options", 170, 18, 300, 44, juce::Justification::centredLeft);

    paintGlass (g, card.toFloat(), 18.0f, juce::Colours::white, 0.0f, 0.7f);
    g.setColour (textDim);
    g.setFont (makeFont (12.0f, true));
    g.drawText ("VOIX ET POLYPHONIE", card.getX() + 20, card.getY() + 14, 400, 20, juce::Justification::centredLeft);

    paintGlass (g, outputCard.toFloat(), 18.0f, juce::Colours::white, 0.0f, 0.7f);
    g.setColour (textDim);
    g.drawText ("SORTIE", outputCard.getX() + 20, outputCard.getY() + 14, 400, 20, juce::Justification::centredLeft);

   #if JUCE_WINDOWS
    paintGlass (g, displayCard.toFloat(), 18.0f, juce::Colours::white, 0.0f, 0.7f);
    g.setColour (textDim);
    g.drawText ("AFFICHAGE", displayCard.getX() + 20, displayCard.getY() + 14, 400, 20, juce::Justification::centredLeft);
   #endif
}

void OptionsPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 14);
    backButton.setBounds (area.removeFromTop (44).removeFromLeft (120).reduced (0, 4));
    area.removeFromTop (20);

    card = area.removeFromTop (272).withWidth (juce::jmin (820, area.getWidth()));
    auto body = card.reduced (20, 20).withTrimmedTop (28);
    maxVoices.setBounds (body.removeFromTop (36));
    voicesPerPad.setBounds (body.removeFromTop (36));
    body.removeFromTop (8);
    auto row = body.removeFromTop (36);
    stealLabel.setBounds (row.removeFromLeft (170));
    stealBox.setBounds (row.removeFromLeft (300).reduced (0, 3));
    body.removeFromTop (14);
    help.setBounds (body);

    area.removeFromTop (14);
    outputCard = area.removeFromTop (178).withWidth (juce::jmin (820, area.getWidth()));
    {
        auto obody = outputCard.reduced (20, 20).withTrimmedTop (28);
        masterRow.setBounds (obody.removeFromTop (36));
        ceilingRow.setBounds (obody.removeFromTop (36));
        obody.removeFromTop (6);
        limiterButton.setBounds (obody.removeFromTop (32).withTrimmedLeft (170));
    }

   #if JUCE_WINDOWS
    area.removeFromTop (14);
    displayCard = area.removeFromTop (112).withWidth (juce::jmin (820, area.getWidth()));
    auto dbody = displayCard.reduced (20, 20).withTrimmedTop (28);
    auto drow = dbody.removeFromTop (36);
    rendererLabel.setBounds (drow.removeFromLeft (170));
    rendererBox.setBounds (drow.removeFromLeft (340).reduced (0, 3));
    dbody.removeFromTop (4);
    rendererHelp.setBounds (dbody);
   #endif

    area.removeFromTop (12);
    auto brow = area.removeFromTop (38);
    aiButton.setBounds (brow.removeFromLeft (240));
    brow.removeFromLeft (10);
    termsButton.setBounds (brow.removeFromLeft (240));
}
} // namespace dm::ui
