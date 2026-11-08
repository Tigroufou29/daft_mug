#include "AiAssistantPanel.h"

#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

AiAssistantPanel::AiAssistantPanel (DaftMugProcessor& p, int padIndex)
    : processor (p), pad (padIndex), catalog (p, padIndex)
{
    setOpaque (true);
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };

    title.setFont (makeFont (22.0f, true));
    title.setColour (juce::Label::textColourId, text);
    title.setText (dm::U ("Daft Mug AI  ·  ") + processor.getPadMeta (pad).name, juce::dontSendNotification);
    addAndMakeVisible (title);

    modeBox.addItem (dm::U ("Proposer des réglages"), 1);
    modeBox.addItem (dm::U ("Recette de sound design"), 2);
    modeBox.addItem (dm::U ("Expliquer / apprendre"), 3);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { mode = static_cast<ai::Mode> (modeBox.getSelectedId() - 1); updateButtons(); };
    addAndMakeVisible (modeBox);

    int id = 1;
    for (const char* t : { "Kick", "Caisse claire", "Charleston / cymbale", "Percussion", "Bruit / FX", "Voix", "Tonal / synthé", "Autre / je ne sais pas" })
        typeBox.addItem (dm::U (t), id++);
    typeBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (typeBox);

    prompt.setMultiLine (true, true);
    prompt.setReturnKeyStartsNewLine (false);
    prompt.setFont (makeFont (15.0f));
    prompt.setTextToShowWhenEmpty (dm::U ("Décrivez ce que vous voulez, par ex. « Rends ce kick plus lourd et plus sombre »"), textDim);
    prompt.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    prompt.setColour (juce::TextEditor::textColourId, text);
    prompt.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.15f));
    prompt.onReturnKey = [this] { send(); };
    addAndMakeVisible (prompt);

    output.setMultiLine (true, true);
    output.setReadOnly (true);
    output.setCaretVisible (false);
    output.setFont (makeFont (14.0f));
    output.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    output.setColour (juce::TextEditor::textColourId, text);
    output.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.12f));
    addAndMakeVisible (output);

    stepList.setModel (this);
    stepList.setRowHeight (52);
    stepList.setColour (juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    stepList.setColour (juce::ListBox::outlineColourId, juce::Colours::white.withAlpha (0.12f));
    stepList.setOutlineThickness (1);
    addAndMakeVisible (stepList);

    for (auto* b : { &sendButton, &previewButton, &compareButton, &acceptButton, &restoreButton, &settingsButton })
        addAndMakeVisible (*b);
    sendButton.onClick = [this] { send(); };
    previewButton.onClick = [this] { preview(); };
    compareButton.onClick = [this] { toggleCompare(); };
    acceptButton.onClick = [this] { accept(); };
    restoreButton.onClick = [this] { restore(); };
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };

    refreshAvailability();
    updateButtons();
}

AiAssistantPanel::~AiAssistantPanel()
{
    processor.releasePad (pad);   // a Gate-mode audition must never stay stuck
    // leaving without accepting: the previous values come back
    if (showingProposal && ! accepted) ai::restoreSnapshot (catalog, before);
}

void AiAssistantPanel::playPad()
{
    const auto meta = processor.getPadMeta (pad);
    if (meta.state != LoadState::Loaded || meta.sample == nullptr)
    {
        setStatus (dm::U ("Ce pad ne contient pas de sample : chargez-en un pour écouter le résultat."), true);
        return;
    }
    processor.triggerPad (pad, 100.0f / 127.0f);
    const int ms = juce::jlimit (300, 6000, (int) (1000.0 * meta.sample->numFrames() / juce::jmax (1.0, meta.sample->sampleRate)));
    juce::Timer::callAfterDelay (ms, [safe = juce::Component::SafePointer<AiAssistantPanel> (this)]
    {
        if (safe != nullptr)
            safe->processor.releasePad (safe->pad);   // ends a Gate-mode note; harmless in the other modes
    });
}

void AiAssistantPanel::refreshAvailability()
{
    auto& svc = processor.getAi();
    if (! svc.isEnabled())
        setStatus (dm::U ("L'IA est désactivée. Activez-la dans Options › IA et confidentialité (facultatif : le sampler fonctionne sans)."), true);
    else if (! svc.hasKey())
        setStatus (dm::U ("Aucune clé API. Saisissez la vôtre dans Options › IA et confidentialité."), true);
    else
        setStatus (dm::U ("Prêt. Votre demande, le type de sample, le nom du pad et les réglages actuels de ce pad seront envoyés à Google (jamais l'audio)."));
}

void AiAssistantPanel::setStatus (const juce::String& s, bool error)
{
    statusText = s;
    statusIsError = error;
    repaint();
}

void AiAssistantPanel::setQuestion (ai::Mode m, const juce::String& sampleType, const juce::String& request)
{
    mode = m;
    modeBox.setSelectedId ((int) m + 1, juce::dontSendNotification);
    for (int i = 0; i < typeBox.getNumItems(); ++i)
        if (typeBox.getItemText (i) == sampleType) typeBox.setSelectedItemIndex (i, juce::dontSendNotification);
    prompt.setText (request, false);
    updateButtons();
}

void AiAssistantPanel::send()
{
    if (waiting) return;
    auto& svc = processor.getAi();
    // the question must describe the ORIGINAL values: take the proposal off first if it is on
    if (showingProposal && ! accepted) showOriginal();

    ai::Question q;
    q.mode = mode;
    q.request = prompt.getText();
    q.sampleType = typeBox.getText();
    q.padName = processor.getPadMeta (pad).name;
    recipe = {};
    checked.clear();
    stepList.updateContent();
    output.clear();
    waiting = true;
    setStatus (dm::U ("Demande envoyée à Gemini…"));
    updateButtons();

    juce::Component::SafePointer<AiAssistantPanel> safe (this);
    svc.ask (q, catalog, [safe] (juce::Result r, juce::String json)
    {
        if (safe != nullptr) safe->handleAnswer (r, json);
    });
}

void AiAssistantPanel::handleAnswer (juce::Result r, const juce::String& json)
{
    waiting = false;
    recipe = {};
    checked.clear();
    if (r.failed()) { setStatus (r.getErrorMessage(), true); stepList.updateContent(); updateButtons(); return; }

    if (mode == ai::Mode::Explain)
    {
        juce::String answer;
        const auto pr = ai::parseExplanation (json, answer);
        if (pr.failed()) setStatus (dm::U ("Réponse refusée : ") + pr.getErrorMessage(), true);
        else { output.setText (answer, false); setStatus (dm::U ("Explication reçue (rien n'est modifié).")); }
    }
    else
    {
        const auto pr = ai::parseRecipe (json, catalog, recipe);
        if (pr.failed())
        {
            recipe = {};
            setStatus (dm::U ("Réponse refusée par le contrôle de sécurité : ") + pr.getErrorMessage(), true);
        }
        else
        {
            checked.assign (recipe.steps.size(), true);
            originals.clear();
            for (const auto& st : recipe.steps)
                originals.push_back (catalog.find (st.key) != nullptr ? catalog.currentValue (*catalog.find (st.key)) : 0.0f);   // the original values: any proposal was taken off before asking
            juce::String t;
            t << recipe.title << "\n\n" << recipe.summary;
            if (recipe.unsupported.isNotEmpty()) t << dm::U ("\n\nNon réalisable avec Daft Mug : ") << recipe.unsupported;
            if (! recipe.rejected.empty())
            {
                t << dm::U ("\n\nÉtapes refusées par le contrôle de sécurité (jamais appliquées) :");
                for (const auto& rj : recipe.rejected) t << "\n- " << rj.what << " : " << rj.reason;
            }
            output.setText (t, false);
            setStatus (recipe.steps.empty() ? dm::U ("Aucune étape valide dans la réponse.")
                                            : juce::String ((int) recipe.steps.size()) + dm::U (" étape(s) valide(s). Rien n'est appliqué : cliquez sur « Prévisualiser »."));
        }
    }
    stepList.updateContent();
    stepList.repaint();
    updateButtons();
}

ai::Recipe AiAssistantPanel::filtered() const
{
    ai::Recipe r = recipe;
    r.steps.clear();
    for (size_t i = 0; i < recipe.steps.size(); ++i)
        if (i < checked.size() && checked[i]) r.steps.push_back (recipe.steps[i]);
    return r;
}

void AiAssistantPanel::applyProposal()
{
    if (! showingProposal) before = ai::takeSnapshot (catalog);
    else ai::restoreSnapshot (catalog, before);   // re-apply from the original (the selection may have changed)
    ai::applyRecipe (catalog, filtered());
    showingProposal = true;
}

void AiAssistantPanel::showOriginal()
{
    ai::restoreSnapshot (catalog, before);
    showingProposal = false;
}

void AiAssistantPanel::setStepChecked (int row, bool c)
{
    if (row < 0 || row >= (int) checked.size()) return;
    checked[(size_t) row] = c;
    if (showingProposal && ! accepted) applyProposal();   // live: the proposal follows the selection
    stepList.repaint();
    updateButtons();
}

void AiAssistantPanel::preview()
{
    if (recipe.steps.empty() || waiting) return;
    accepted = false;
    applyProposal();
    setStatus (dm::U ("Proposition appliquée temporairement et jouée. « Écouter l'original » compare ; « Accepter » la garde ; « Retour » sans accepter remet les valeurs d'avant."));
    updateButtons();
    playPad();
}

void AiAssistantPanel::toggleCompare()
{
    if (! before.valid && ! showingProposal) return;
    if (showingProposal) { showOriginal(); setStatus (dm::U ("Vous écoutez l'original.")); }
    else { applyProposal(); setStatus (dm::U ("Vous écoutez la proposition.")); }
    updateButtons();
    playPad();
}

void AiAssistantPanel::accept()
{
    if (recipe.steps.empty()) return;
    if (! showingProposal) applyProposal();
    accepted = true;
    setStatus (dm::U ("Proposition acceptée : « Retour » vous ramène à l'éditeur du pad avec ces réglages. « Restaurer » remet les valeurs d'avant tant que ce panneau est ouvert."));
    updateButtons();
}

void AiAssistantPanel::restore()
{
    if (! before.valid) return;
    ai::restoreSnapshot (catalog, before);
    showingProposal = false;
    accepted = false;
    setStatus (dm::U ("Valeurs précédentes restaurées."));
    updateButtons();
}

void AiAssistantPanel::updateButtons()
{
    auto& svc = processor.getAi();
    const bool canAsk = svc.isEnabled() && svc.hasKey() && ! waiting;
    const bool hasRecipe = ! recipe.steps.empty() && mode != ai::Mode::Explain;
    sendButton.setEnabled (canAsk);
    previewButton.setEnabled (hasRecipe && ! waiting);
    compareButton.setEnabled (hasRecipe && before.valid);
    compareButton.setButtonText (showingProposal ? dm::U ("Écouter l'original") : dm::U ("Écouter la proposition"));
    acceptButton.setEnabled (hasRecipe && ! accepted);
    restoreButton.setEnabled (before.valid && (showingProposal || accepted));
    typeBox.setEnabled (mode != ai::Mode::Explain);
    repaint();
}

void AiAssistantPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) recipe.steps.size()) return;
    const auto& st = recipe.steps[(size_t) row];
    if (selected) { g.setColour (accent.withAlpha (0.25f)); g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 6.0f); }
    const bool on = checked[(size_t) row];
    g.setColour (on ? ledOk : textDim);
    g.drawRoundedRectangle (10.0f, 8.0f, 16.0f, 16.0f, 3.0f, 1.5f);
    if (on) g.fillRoundedRectangle (14.0f, 12.0f, 8.0f, 8.0f, 2.0f);

    const auto* info = catalog.find (st.key);
    juce::String line = juce::String (row + 1) + ". " + st.name;
    g.setColour (on ? text : textDim);
    g.setFont (makeFont (14.0f, true));
    g.drawText (line, 36, 4, w - 40, 22, juce::Justification::centredLeft, true);
    if (info != nullptr)
    {
        g.setColour (accent2);
        g.setFont (makeFont (12.5f));
        g.drawText (st.key + ":  " + catalog.formatValue (*info, originals[(size_t) row]) + "  →  " + catalog.formatValue (*info, st.value),
                    36, 24, w - 40, 18, juce::Justification::centredLeft, true);
    }
    g.setColour (textDim);
    g.setFont (makeFont (12.0f));
    g.drawText (st.explanation, 36, 38, w - 40, 14, juce::Justification::centredLeft, true);
}

void AiAssistantPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (statusIsError ? ledError : textDim);
    g.setFont (makeFont (13.0f));
    g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 2, 1.0f);
    g.setColour (textDim);
    g.setFont (makeFont (12.0f));
    g.drawFittedText (dm::U ("Gemini conseille ; le moteur audio local exécute. Rien n'est appliqué sans votre action."), hintArea, juce::Justification::centredRight, 1, 1.0f);
}

void AiAssistantPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    auto top = area.removeFromTop (44);
    backButton.setBounds (top.removeFromLeft (120).reduced (0, 4));
    top.removeFromLeft (14);
    settingsButton.setBounds (top.removeFromRight (130).reduced (0, 4));
    title.setBounds (top);
    area.removeFromTop (6);

    auto row = area.removeFromTop (34);
    modeBox.setBounds (row.removeFromLeft (230).reduced (0, 2));  row.removeFromLeft (10);
    typeBox.setBounds (row.removeFromLeft (220).reduced (0, 2));  row.removeFromLeft (10);
    hintArea = row;
    area.removeFromTop (8);

    auto prow = area.removeFromTop (74);
    sendButton.setBounds (prow.removeFromRight (130).reduced (0, 18));
    prow.removeFromRight (10);
    prompt.setBounds (prow);
    area.removeFromTop (6);
    statusArea = area.removeFromTop (40);

    auto bottom = area.removeFromBottom (44);
    previewButton.setBounds (bottom.removeFromLeft (150).reduced (0, 4)); bottom.removeFromLeft (8);
    compareButton.setBounds (bottom.removeFromLeft (190).reduced (0, 4)); bottom.removeFromLeft (8);
    acceptButton.setBounds (bottom.removeFromLeft (120).reduced (0, 4));  bottom.removeFromLeft (8);
    restoreButton.setBounds (bottom.removeFromLeft (270).reduced (0, 4));
    area.removeFromBottom (6);

    stepList.setBounds (area.removeFromLeft (juce::jmax (420, area.getWidth() * 58 / 100)));
    area.removeFromLeft (12);
    output.setBounds (area);
}
} // namespace dm::ui
