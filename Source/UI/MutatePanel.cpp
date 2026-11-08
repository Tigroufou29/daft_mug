#include "MutatePanel.h"

#include "LiquidGlassLookAndFeel.h"

namespace dm::ui
{
using namespace theme;

MutatePanel::MutatePanel (DaftMugProcessor& p, int padIndex) : processor (p), pad (padIndex)
{
    setOpaque (true);
    base = processor.readMutateValues (pad);
    seed = (uint32_t) juce::Time::getMillisecondCounter() | 1u;

    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };

    title.setFont (makeFont (22.0f, true));
    title.setColour (juce::Label::textColourId, text);
    title.setText (dm::U ("Mutate  ·  ") + processor.getPadMeta (pad).name, juce::dontSendNotification);
    addAndMakeVisible (title);

    help.setFont (makeFont (12.5f));
    help.setColour (juce::Label::textColourId, textDim);
    help.setJustificationType (juce::Justification::topLeft);
    help.setText (dm::U ("Cliquez une variante pour l'écouter sur ce pad, puis « Appliquer » pour la garder. La variante 0 est votre réglage actuel (pour comparer). "
                         "Seuls des réglages changent : le sample n'est jamais modifié, et vous pouvez annuler."),
                  juce::dontSendNotification);
    addAndMakeVisible (help);

    const juce::String labels[6] = { "Pitch", "Pan", "Volume", "Attack", "EQ", dm::U ("Réverbe") };
    const bool defaults[6] = { true, true, false, true, true, true };
    for (size_t i = 0; i < 6; ++i)
    {
        enableButtons[i].setButtonText (labels[i]);
        enableButtons[i].setClickingTogglesState (true);
        enableButtons[i].setToggleState (defaults[i], juce::dontSendNotification);
        enableButtons[i].onClick = [this] { generate(); };
        addAndMakeVisible (enableButtons[i]);
    }
    enableButtons[5].setEnabled (processor.padHasReverb (pad));
    if (! processor.padHasReverb (pad))
        enableButtons[5].setTooltip (dm::U ("Ce pad n'a pas de réverbération active : choisissez-en une dans l'éditeur du pad."));
    enableButtons[4].setTooltip (dm::U ("Fait varier le gain des 4 bandes de l'égaliseur."));

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
    amountSlider.setTextBoxIsEditable (true);
    amountSlider.setRange (0.0, 100.0, 1.0);
    amountSlider.setValue (40.0, juce::dontSendNotification);
    amountSlider.setTextValueSuffix (" %");
    amountSlider.setTooltip (dm::U ("Intensité : écart maximal des variantes par rapport au réglage actuel."));
    amountSlider.onValueChange = [this] { generate(); };
    addAndMakeVisible (amountSlider);
    amountLabel.setText (dm::U ("Intensité"), juce::dontSendNotification);
    amountLabel.setFont (makeFont (13.0f));
    amountLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (amountLabel);

    for (int n : { 4, 8, 12, 16 }) countBox.addItem (juce::String (n) + " variantes", n);
    countBox.setSelectedId (8, juce::dontSendNotification);
    countBox.onChange = [this] { generate(); };
    addAndMakeVisible (countBox);

    addAndMakeVisible (generateButton);
    generateButton.onClick = [this] { seed = seed * 1664525u + 1013904223u; generate(); };
    addAndMakeVisible (applyButton);
    applyButton.onClick = [this]
    {
        const auto r = applyChosen();
        info.setText (r.wasOk() ? dm::U ("Variante appliquée au pad. Vous pouvez annuler.") : r.getErrorMessage(), juce::dontSendNotification);
        undoButton.setEnabled (processor.canUndoMutation());
    };
    addAndMakeVisible (undoButton);
    undoButton.onClick = [this]
    {
        processor.undoMutation();
        base = processor.readMutateValues (pad);
        info.setText (dm::U ("Réglages précédents restaurés."), juce::dontSendNotification);
        undoButton.setEnabled (false);
        generate();
    };
    undoButton.setEnabled (processor.canUndoMutation());

    info.setFont (makeFont (13.0f));
    info.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (info);

    generate();
    startTimerHz (20);
}

MutatePanel::~MutatePanel()
{
    stopTimer();
    processor.clearPreviewMutation (pad);
}

MutateConfig MutatePanel::currentConfig() const
{
    MutateConfig c;
    c.pitch = enableButtons[0].getToggleState();
    c.pan = enableButtons[1].getToggleState();
    c.gain = enableButtons[2].getToggleState();
    c.attack = enableButtons[3].getToggleState();
    c.eq = enableButtons[4].getToggleState();
    c.reverb = enableButtons[5].getToggleState() && processor.padHasReverb (pad);
    c.amount = (float) amountSlider.getValue() * 0.01f;
    return c;
}

void MutatePanel::generate()
{
    stopAudition();
    variants = generateVariants (base, currentConfig(), countBox.getSelectedId() > 0 ? countBox.getSelectedId() : 8, seed);
    cards.clear();
    for (int i = 0; i < (int) variants.size(); ++i)
    {
        cards.push_back (std::make_unique<Card> (*this, i));
        addAndMakeVisible (*cards.back());
    }
    chosen = juce::jmin (chosen, (int) variants.size() - 1);
    resized();
    repaint();
}

juce::String MutatePanel::describe (int i) const
{
    const auto& v = variants[(size_t) i];
    juce::String s;
    s << dm::U ("Pitch ") << (v.pitchSemitones >= 0 ? "+" : "") << juce::String (v.pitchSemitones, 2) << " st\n";
    const int pan = juce::roundToInt (v.pan * 100.0f);
    s << "Pan " << (pan == 0 ? juce::String ("C") : juce::String (std::abs (pan)) + (pan < 0 ? " L" : " R")) << "\n";
    s << "Attack " << juce::String (v.attackMs, 1) << " ms\n";
    s << "Volume " << juce::String (v.gainDb, 1) << " dB\n";
    s << "EQ " << juce::String (v.eqGainDb[0], 1) << " / " << juce::String (v.eqGainDb[1], 1) << " / "
      << juce::String (v.eqGainDb[2], 1) << " / " << juce::String (v.eqGainDb[3], 1);
    if (processor.padHasReverb (pad)) s << dm::U ("\nRéverbe ") << juce::roundToInt (v.reverbWet * 100.0f) << " %";
    return s;
}

void MutatePanel::audition (int variant)
{
    if (variant < 0 || variant >= (int) variants.size()) return;
    chosen = variant;
    processor.releasePad (pad);
    processor.setPreviewMutation (pad, variants[(size_t) variant]);
    processor.triggerPad (pad, 1.0f);
    playing = variant;
    quietTicks = 0;
    repaint();
}

void MutatePanel::stopAudition()
{
    processor.clearPreviewMutation (pad);
    playing = -1;
}

void MutatePanel::timerCallback()
{
    if (playing < 0) return;
    // keep the override while the sound plays (pitch / pan / EQ follow it live); drop it once the pad is silent
    if (! processor.getEngine().isPadPlaying (pad))
    {
        if (++quietTicks > 6)   // ~0.3 s: includes the effect tails' start
        {
            processor.clearPreviewMutation (pad);
            playing = -1;
            repaint();
        }
    }
    else
        quietTicks = 0;
}

juce::Result MutatePanel::applyChosen()
{
    if (chosen < 0 || chosen >= (int) variants.size()) return juce::Result::fail (dm::U ("Choisissez une variante"));
    stopAudition();
    processor.applyMutation (pad, variants[(size_t) chosen]);
    base = processor.readMutateValues (pad);
    generate();
    return juce::Result::ok();
}

// ---------------------------------------------------------------------------------------- card
void MutatePanel::Card::paint (juce::Graphics& g)
{
    const bool isChosen = owner.chosen == index, isPlaying = owner.playing == index;
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    paintGlass (g, b, 12.0f, isChosen ? accent2 : juce::Colours::white, isChosen ? 0.35f : 0.0f, 0.8f);
    g.setColour (isPlaying ? ledOk : (isChosen ? accent2 : textDim));
    g.setFont (makeFont (13.0f, true));
    g.drawText (index == 0 ? dm::U ("0 · Original") : juce::String (index) + (isPlaying ? dm::U ("  ▶ en lecture") : juce::String()),
                getLocalBounds().reduced (12, 8).removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (makeFont (11.5f));
    g.drawFittedText (owner.describe (index), getLocalBounds().reduced (12, 30), juce::Justification::topLeft, 7);
}

void MutatePanel::Card::mouseDown (const juce::MouseEvent&) { owner.audition (index); }

void MutatePanel::paint (juce::Graphics& g)
{
    g.fillAll (bgTop);
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillRect (getLocalBounds());
}

void MutatePanel::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    auto top = r.removeFromTop (44);
    backButton.setBounds (top.removeFromLeft (110).reduced (0, 4));
    top.removeFromLeft (14);
    title.setBounds (top);
    help.setBounds (r.removeFromTop (38));
    r.removeFromTop (6);

    auto row1 = r.removeFromTop (36);
    for (auto& b : enableButtons) { b.setBounds (row1.removeFromLeft (100).reduced (0, 3)); }
    row1.removeFromLeft (10);
    countBox.setBounds (row1.removeFromLeft (150).reduced (0, 3));
    r.removeFromTop (4);
    auto row2 = r.removeFromTop (36);
    amountLabel.setBounds (row2.removeFromLeft (80));
    amountSlider.setBounds (row2.removeFromLeft (360));
    row2.removeFromLeft (14);
    generateButton.setBounds (row2.removeFromLeft (260).reduced (0, 3));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (92);
    gridArea = r;
    const int n = (int) cards.size();
    const int cols = n <= 4 ? n : 4;
    const int rows = juce::jmax (1, (n + cols - 1) / juce::jmax (1, cols));
    const int cw = gridArea.getWidth() / juce::jmax (1, cols), ch = juce::jmin (150, gridArea.getHeight() / rows);
    for (int i = 0; i < n; ++i)
        cards[(size_t) i]->setBounds (gridArea.getX() + (i % cols) * cw, gridArea.getY() + (i / cols) * ch, cw, ch);

    bottom.removeFromTop (6);
    info.setBounds (bottom.removeFromTop (24));
    auto row3 = bottom.removeFromTop (44);
    applyButton.setBounds (row3.removeFromLeft (270).reduced (0, 3));
    row3.removeFromLeft (10);
    undoButton.setBounds (row3.removeFromLeft (300).reduced (0, 3));
}
} // namespace dm::ui
