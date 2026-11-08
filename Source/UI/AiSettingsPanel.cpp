#include "AiSettingsPanel.h"

#include "AiService.h"
#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

AiSettingsPanel::AiSettingsPanel (DaftMugProcessor& p) : processor (p)
{
    setOpaque (true);
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };

    addAndMakeVisible (enableButton);
    enableButton.onClick = [this] { setEnabledFromUi (enableButton.getToggleState()); };

    keyLabel.setText (dm::U ("Votre clé API Gemini personnelle"), juce::dontSendNotification);
    keyLabel.setFont (makeFont (13.0f));
    keyLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (keyLabel);

    keyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    keyField.setMultiLine (false);
    keyField.setFont (makeFont (15.0f));
    keyField.setTextToShowWhenEmpty (dm::U ("Collez ici votre clé (jamais affichée par défaut)"), textDim);
    keyField.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    keyField.setColour (juce::TextEditor::textColourId, text);
    keyField.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.15f));
    keyField.setSelectAllWhenFocused (false);
    keyField.onReturnKey = [this] { const auto r = saveKeyFromField(); if (r.failed()) setStatus (r.getErrorMessage(), ledError); };
    addAndMakeVisible (keyField);

    for (auto* b : { &saveKeyButton, &revealButton, &clearKeyButton, &testButton, &termsButton }) addAndMakeVisible (*b);
    saveKeyButton.onClick = [this] { const auto r = saveKeyFromField(); if (r.failed()) setStatus (r.getErrorMessage(), ledError); };
    revealButton.onClick = [this] { toggleReveal(); };
    clearKeyButton.onClick = [this]
    {
        auto& ai = processor.getAi();
        const auto r = ai.clearKey();
        keyField.clear();
        setRevealed (false);
        setStatus (r.wasOk() ? dm::U ("Clé effacée de cet ordinateur.") : r.getErrorMessage(), r.wasOk() ? textDim : ledError);
        refresh();
    };
    testButton.onClick = [this] { testConnection(); };
    termsButton.onClick = [this] { if (onTerms) onTerms(); };

    modelLabel.setText (dm::U ("Modèle Gemini"), juce::dontSendNotification);
    modelLabel.setFont (makeFont (13.0f));
    modelLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (modelLabel);
    addAndMakeVisible (modelBox);
    modelBox.onChange = [this]
    {
        const auto m = modelBox.getText();
        if (m.isNotEmpty() && modelBox.getSelectedId() > 0) processor.getAi().setModel (m);
    };

    status.setFont (makeFont (13.5f));

    refresh();
    startTimerHz (4);
}

AiSettingsPanel::~AiSettingsPanel() { stopTimer(); keyField.clear(); }

void AiSettingsPanel::setStatus (const juce::String& s, juce::Colour c)
{
    statusColour = c;
    status.setText (s, juce::dontSendNotification);
    repaint();
}

void AiSettingsPanel::refresh()
{
    auto& ai = processor.getAi();
    enableButton.setToggleState (ai.isEnabled(), juce::dontSendNotification);
    const bool on = ai.isEnabled();
    const bool hasKey = ai.hasKey();
    keyField.setEnabled (true);
    saveKeyButton.setEnabled (true);
    revealButton.setEnabled (hasKey || keyField.getText().isNotEmpty());
    clearKeyButton.setEnabled (hasKey);
    testButton.setEnabled (on && hasKey && ! ai.isBusy());
    modelBox.setEnabled (on && ai.getKnownModels().size() > 0);
    if (! hasKey) keyField.setTextToShowWhenEmpty (dm::U ("Collez ici votre clé (jamais affichée par défaut)"), textDim);
    else if (! revealed && keyField.getText().isEmpty()) keyField.setTextToShowWhenEmpty (dm::U ("Une clé est enregistrée (masquée)"), textDim);
    fillModels();
    if (status.getText().isEmpty())
        setStatus (! on ? dm::U ("IA désactivée : le sampler fonctionne normalement.")
                        : (hasKey ? dm::U ("Clé enregistrée. Utilisez « Authentifier la clé API ».") : dm::U ("Aucune clé enregistrée.")),
                   textDim);
    repaint();
}

void AiSettingsPanel::fillModels()
{
    auto& ai = processor.getAi();
    auto names = ai.getKnownModels();
    const auto current = ai.getModel();
    if (current.isNotEmpty()) names.addIfNotAlreadyThere (current);
    juce::StringArray shown;
    for (int i = 0; i < modelBox.getNumItems(); ++i) shown.add (modelBox.getItemText (i));
    if (shown != names)
    {
        modelBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i) modelBox.addItem (names[i], i + 1);
        if (names.isEmpty()) modelBox.setTextWhenNothingSelected (dm::U ("(testez la connexion pour lister les modèles)"));
    }
    const int idx = names.indexOf (current);
    if (idx >= 0) modelBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void AiSettingsPanel::timerCallback()
{
    if (revealed && ++revealTicks > 40) setRevealed (false);   // hides itself after ~10 s
    auto& ai = processor.getAi();
    testButton.setEnabled (ai.isEnabled() && ai.hasKey() && ! ai.isBusy());
}

void AiSettingsPanel::setRevealed (bool on)
{
    auto& ai = processor.getAi();
    if (on)
    {
        if (keyField.getText().isEmpty()) keyField.setText (ai.getKey(), false);
        keyField.setPasswordCharacter (0);
        revealed = true;
        revealTicks = 0;
        revealButton.setButtonText ("Masquer");
    }
    else
    {
        // a stored key is never kept in the field once hidden; a key being typed stays until it is saved
        if (ai.hasKey() && keyField.getText() == ai.getKey()) keyField.clear();
        keyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        revealed = false;
        revealButton.setButtonText ("Afficher");
    }
    refresh();
}

void AiSettingsPanel::setEnabledFromUi (bool on)
{
    auto& ai = processor.getAi();
    ai.setEnabled (on);
    enableButton.setToggleState (on, juce::dontSendNotification);
    setStatus (on ? (ai.hasKey() ? dm::U ("IA activée. Utilisez « Authentifier la clé API ».") : dm::U ("IA activée : saisissez votre clé API."))
                  : dm::U ("IA désactivée : le sampler fonctionne normalement."), textDim);
    refresh();
}

juce::Result AiSettingsPanel::saveKeyFromField()
{
    auto& ai = processor.getAi();
    const auto typed = keyField.getText();
    if (typed.isEmpty()) return juce::Result::fail (dm::U ("Collez d'abord votre clé dans le champ."));
    const auto r = ai.setKey (typed);
    if (r.failed()) return r;
    keyField.clear();
    setRevealed (false);
    juce::String msg = dm::U ("Clé enregistrée.");
    if (! ai.storageIsPersistent()) msg << dm::U (" (conservée en mémoire pour cette session seulement)");
    setStatus (msg, ledOk);
    refresh();
    return juce::Result::ok();
}

void AiSettingsPanel::testConnection()
{
    auto& ai = processor.getAi();
    setStatus (dm::U ("Test en cours…"), ledBusy);
    juce::Component::SafePointer<AiSettingsPanel> safe (this);
    ai.testConnection ([safe] (juce::Result r, juce::StringArray models)
    {
        if (safe == nullptr) return;
        if (r.wasOk()) safe->setStatus (dm::U ("Connexion réussie : ") + juce::String (models.size()) + dm::U (" modèle(s) utilisable(s)."), ledOk);
        else safe->setStatus (r.getErrorMessage(), ledError);
        safe->refresh();
    });
}

void AiSettingsPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (text);
    g.setFont (makeFont (22.0f, true));
    g.drawText (dm::U ("IA et confidentialité"), 170, 12, 500, 44, juce::Justification::centredLeft);

    g.setColour (statusColour);
    g.fillEllipse ((float) statusArea.getX(), (float) statusArea.getY() + 5.0f, 10.0f, 10.0f);
    g.setFont (makeFont (13.5f));
    g.drawFittedText (status.getText(), statusArea.withTrimmedLeft (22), juce::Justification::topLeft, 2, 1.0f);

    g.setColour (textDim);
    g.setFont (makeFont (12.5f));
    juce::String info;
    info << dm::U ("DONNÉES TRANSMISES À GOOGLE (uniquement quand vous envoyez une demande)\n"
                   "Le texte de votre demande, le type de sample que vous choisissez, le nom du pad, la liste des réglages disponibles avec leurs valeurs actuelles pour ce pad, "
                   "et votre clé (pour vous identifier). Jamais : vos fichiers audio, les chemins de vos fichiers, votre projet complet, ni les autres pads. "
                   "Ne saisissez pas d'informations confidentielles. Le traitement par Google dépend de votre compte et de vos conditions Gemini.\n\n"
                   "COÛTS\nL'API Gemini peut être limitée par des quotas ou payante selon votre compte Google et le modèle : ces coûts sont à votre charge. "
                   "Daft Mug ne peut pas les plafonner pour vous.\n\n"
                   "CLÉ\n") << processor.getAi().storageDescription()
         << dm::U ("\n\nL'IA est facultative : le moteur audio est entièrement local et fonctionne sans clé, sans internet et sans IA.");
    g.drawFittedText (info, infoArea, juce::Justification::topLeft, 20, 1.0f);
}

void AiSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    backButton.setBounds (area.removeFromTop (44).removeFromLeft (120).reduced (0, 4));
    area.removeFromTop (10);
    auto col = area.withWidth (juce::jmin (900, area.getWidth()));

    enableButton.setBounds (col.removeFromTop (30));
    col.removeFromTop (8);
    keyLabel.setBounds (col.removeFromTop (22));
    keyField.setBounds (col.removeFromTop (34));
    col.removeFromTop (8);
    auto row = col.removeFromTop (36);
    saveKeyButton.setBounds (row.removeFromLeft (160).reduced (0, 2));  row.removeFromLeft (8);
    revealButton.setBounds (row.removeFromLeft (110).reduced (0, 2));   row.removeFromLeft (8);
    clearKeyButton.setBounds (row.removeFromLeft (140).reduced (0, 2)); row.removeFromLeft (8);
    testButton.setBounds (row.removeFromLeft (180).reduced (0, 2));
    col.removeFromTop (8);
    statusArea = col.removeFromTop (40);
    col.removeFromTop (8);
    auto mrow = col.removeFromTop (34);
    modelLabel.setBounds (mrow.removeFromLeft (120));
    modelBox.setBounds (mrow.removeFromLeft (380).reduced (0, 2));
    mrow.removeFromLeft (12);
    termsButton.setBounds (mrow.removeFromLeft (230).reduced (0, 2));
    col.removeFromTop (14);
    infoArea = col.withTrimmedBottom (8);
}
} // namespace dm::ui
