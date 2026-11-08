#include "PadEditorPanel.h"

#include "LiquidGlassLookAndFeel.h"
#include "PadExporter.h"
#include "ParameterIds.h"
#include "PadComponent.h"
#include "PresetUi.h"

namespace dm::ui
{
using namespace theme;

// ============================================================================ scrollable body
PadEditorPanel::Content::Content (PadEditorPanel& owner, DaftMugProcessor& p, int padIndex)
    : panel (owner), processor (p), index (padIndex),
      waveform (p, padIndex, true), controls (p, padIndex, true), envelope (p, padIndex), eq (p, padIndex), fx (p, padIndex), reverb (p, padIndex)
{
    for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton, &regionButton, &resetRegionButton })
        addAndMakeVisible (*b);
    zoomOutButton.setTooltip (dm::U ("Zoom arrière (ou molette)"));
    zoomInButton.setTooltip (dm::U ("Zoom avant (ou molette sur la forme d'onde)"));
    fitButton.setTooltip ("Afficher tout le fichier");
    regionButton.setTooltip (dm::U ("Zoomer sur la région lue"));
    resetRegionButton.setTooltip (dm::U ("Début = 0 %, fin = 100 % (le fichier n'est jamais modifié)"));
    zoomOutButton.onClick = [this] { waveform.zoomOut(); };
    zoomInButton.onClick = [this] { waveform.zoomIn(); };
    fitButton.onClick = [this] { waveform.zoomFit(); };
    regionButton.onClick = [this] { waveform.zoomToRegion(); };
    resetRegionButton.onClick = [this] { waveform.resetRegion(); };

    addAndMakeVisible (reverseButton);
    reverseButton.setClickingTogglesState (true);
    reverseButton.setTooltip (dm::U ("Joue la région à l'envers (sans modifier le fichier)"));
    reverseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ids::reverse (index), reverseButton);

    addAndMakeVisible (waveform);
    addAndMakeVisible (controls);
    addAndMakeVisible (envelope);
    addAndMakeVisible (eq);
    addAndMakeVisible (fx);
    addAndMakeVisible (reverb);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    normalizeButton.setClickingTogglesState (true);
    exportStatus.setFont (makeFont (12.0f));
    exportStatus.setColour (juce::Label::textColourId, textDim);
    for (juce::Component* c : { (juce::Component*) &normalizeButton, (juce::Component*) &exportButton, (juce::Component*) &exportStatus })
        addAndMakeVisible (c);

    // --- tempo
    for (auto* sl : { &origSlider, &targetSlider })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setRange (40.0, 240.0, 0.1);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
        sl->setTextValueSuffix (" BPM");
        sl->setDoubleClickReturnValue (true, 120.0);
    }
    origSlider.setTooltip (dm::U ("Tempo du sample tel qu'il est (détecté automatiquement, corrigez-le si besoin)"));
    targetSlider.setTooltip (dm::U ("Tempo auquel le pad doit jouer. La hauteur (tonalité) ne change pas."));
    for (auto* l : { &origLabel, &targetLabel })
    {
        l->setFont (makeFont (13.0f));
        l->setColour (juce::Label::textColourId, textDim);
    }
    tempoInfo.setFont (makeFont (13.0f, true));
    tempoInfo.setColour (juce::Label::textColourId, accent2);
    tempoMessage.setFont (makeFont (12.0f));
    tempoMessage.setColour (juce::Label::textColourId, textDim);
    for (juce::Component* c : { (juce::Component*) &tempoInfo, (juce::Component*) &tempoMessage, (juce::Component*) &origLabel, (juce::Component*) &targetLabel,
                                (juce::Component*) &origSlider, (juce::Component*) &targetSlider, (juce::Component*) &detectButton, (juce::Component*) &halveButton,
                                (juce::Component*) &doubleButton, (juce::Component*) &hostButton, (juce::Component*) &applyButton, (juce::Component*) &resetTempoButton })
        addAndMakeVisible (c);

    detectButton.setTooltip (dm::U ("Analyse le sample pour trouver son tempo (fonctionne sur les boucles, pas sur les sons isolés)"));
    halveButton.setTooltip (dm::U ("Divise le tempo d'origine par 2 (si la détection a trouvé le double)"));
    doubleButton.setTooltip (dm::U ("Double le tempo d'origine (si la détection a trouvé la moitié)"));
    hostButton.setTooltip (dm::U ("Règle le tempo cible sur celui de votre logiciel de musique (plug-in uniquement) et applique"));
    applyButton.setTooltip (dm::U ("Calcule la version étirée du sample (sans changer la hauteur). Le fichier d'origine n'est jamais modifié."));
    resetTempoButton.setTooltip (dm::U ("Revient au tempo d'origine du sample"));

    detectButton.onClick = [this] { processor.redetectTempo (index); };
    halveButton.onClick = [this] { const auto m = processor.getPadMeta (index); if (m.originalBpm > 0.0) processor.setPadTempo (index, m.originalBpm * 0.5, m.targetBpm); };
    doubleButton.onClick = [this] { const auto m = processor.getPadMeta (index); if (m.originalBpm > 0.0) processor.setPadTempo (index, m.originalBpm * 2.0, m.targetBpm); };
    applyButton.onClick = [this] { processor.setPadTempo (index, origSlider.getValue(), targetSlider.getValue()); };
    resetTempoButton.onClick = [this] { processor.setPadTempo (index, origSlider.getValue(), 0.0); };
    hostButton.onClick = [this]
    {
        const double bpm = processor.getHostBpm();
        if (bpm > 0.0) { targetSlider.setValue (juce::jlimit (40.0, 240.0, bpm), juce::dontSendNotification); processor.setPadTempo (index, origSlider.getValue(), bpm); }
    };
}

void PadEditorPanel::Content::updateTempo (const PadMeta& m)
{
    const bool loaded = m.state == LoadState::Loaded && m.original != nullptr;
    for (juce::Component* c : { (juce::Component*) &origSlider, (juce::Component*) &targetSlider, (juce::Component*) &detectButton, (juce::Component*) &halveButton,
                                (juce::Component*) &doubleButton, (juce::Component*) &applyButton, (juce::Component*) &resetTempoButton })
        c->setEnabled (loaded && ! m.stretching);
    hostButton.setEnabled (loaded && ! m.stretching && processor.getHostBpm() > 0.0);

    if (! origSlider.isMouseButtonDown())
        origSlider.setValue (m.originalBpm > 0.0 ? juce::jlimit (40.0, 240.0, m.originalBpm) : 120.0, juce::dontSendNotification);
    if (! targetSlider.isMouseButtonDown())
        targetSlider.setValue (m.targetBpm > 0.0 ? juce::jlimit (40.0, 240.0, m.targetBpm) : (m.originalBpm > 0.0 ? juce::jlimit (40.0, 240.0, m.originalBpm) : 120.0),
                               juce::dontSendNotification);

    juce::String info;
    if (! loaded) info = dm::U ("Chargez un sample pour détecter son tempo.");
    else if (m.stretching) info = dm::U ("Calcul de l'étirement…");
    else if (m.originalBpm <= 0.0) info = dm::U ("Tempo non détecté (son isolé ?) : saisissez-le à la main.");
    else
    {
        info = dm::U ("Tempo d'origine : ") + juce::String (m.originalBpm, 1) + " BPM  ("
               + (m.tempoConfidence >= 0.35f ? dm::U ("détection fiable") : (m.tempoConfidence >= 0.15f ? dm::U ("détection incertaine : vérifiez, ÷2 / ×2 si besoin") : dm::U ("détection douteuse : vérifiez à l'oreille"))) + ")";
        if (m.targetBpm > 0.0 && m.originalBpm > 0.0 && std::abs (m.targetBpm - m.originalBpm) > 0.01)
            info += dm::U ("   →   joué à ") + juce::String (m.targetBpm, 1) + " BPM (× " + juce::String (m.originalBpm / m.targetBpm, 2) + " en durée)";
    }
    tempoInfo.setText (info, juce::dontSendNotification);
    tempoMessage.setText (m.tempoMessage, juce::dontSendNotification);
}

int PadEditorPanel::Content::layoutFor (int width)
{
    cards.clear();
    const int pad = 22, gap = 18, cardPad = 20, titleH = 30;
    const int inner = juce::jmax (300, width - 2 * pad);
    int y = 6;

    auto openCard = [&] (const juce::String& title, int bodyHeight)
    {
        Card c;
        c.title = title;
        c.bounds = { pad, y, inner, cardPad + titleH + bodyHeight + cardPad };
        cards.push_back (c);
        const auto body = juce::Rectangle<int> (pad + cardPad, y + cardPad + titleH, inner - 2 * cardPad, bodyHeight);
        y += c.bounds.getHeight() + gap;
        return body;
    };

    // --- sample & export (first card): load, clear, normalise, export
    {
        auto body = openCard (dm::U ("SAMPLE ET EXPORT"), 40 + 10 + 36);
        auto row = body.removeFromTop (40);
        loadButton.setBounds (row.removeFromLeft (190));      row.removeFromLeft (10);
        clearButton.setBounds (row.removeFromLeft (90));
        body.removeFromTop (10);
        normalizeButton.setBounds (body.removeFromLeft (120)); body.removeFromLeft (8);
        exportButton.setBounds (body.removeFromLeft (170));   body.removeFromLeft (12);
        exportStatus.setBounds (body);
    }

    // --- tempo (second card)
    {
        auto body = openCard (dm::U ("TEMPO ET ÉTIREMENT (SANS CHANGER LA TONALITÉ)"), 24 + 8 + 36 + 8 + 36 + 6 + 20);
        tempoInfo.setBounds (body.removeFromTop (24));
        body.removeFromTop (8);
        auto r1 = body.removeFromTop (36);
        origLabel.setBounds (r1.removeFromLeft (120));
        origSlider.setBounds (r1.removeFromLeft (juce::jmin (420, r1.getWidth() - 270)));
        r1.removeFromLeft (12);
        detectButton.setBounds (r1.removeFromLeft (100).reduced (0, 3)); r1.removeFromLeft (6);
        halveButton.setBounds (r1.removeFromLeft (60).reduced (0, 3));   r1.removeFromLeft (6);
        doubleButton.setBounds (r1.removeFromLeft (60).reduced (0, 3));
        body.removeFromTop (8);
        auto r2 = body.removeFromTop (36);
        targetLabel.setBounds (r2.removeFromLeft (120));
        targetSlider.setBounds (r2.removeFromLeft (juce::jmin (420, r2.getWidth() - 400)));
        r2.removeFromLeft (12);
        hostButton.setBounds (r2.removeFromLeft (150).reduced (0, 3));   r2.removeFromLeft (6);
        applyButton.setBounds (r2.removeFromLeft (100).reduced (0, 3));  r2.removeFromLeft (6);
        resetTempoButton.setBounds (r2.removeFromLeft (150).reduced (0, 3));
        body.removeFromTop (6);
        tempoMessage.setBounds (body.removeFromTop (20));
    }
    // --- waveform
    {
        auto body = openCard (dm::U ("FORME D'ONDE"), 36 + 8 + 250);
        auto bar = body.removeFromTop (36);
        zoomOutButton.setBounds (bar.removeFromLeft (40));  bar.removeFromLeft (6);
        zoomInButton.setBounds (bar.removeFromLeft (40));   bar.removeFromLeft (6);
        fitButton.setBounds (bar.removeFromLeft (70));      bar.removeFromLeft (6);
        regionButton.setBounds (bar.removeFromLeft (110));  bar.removeFromLeft (14);
        resetRegionButton.setBounds (bar.removeFromLeft (190)); bar.removeFromLeft (14);
        reverseButton.setBounds (bar.removeFromLeft (110));
        body.removeFromTop (8);
        waveform.setBounds (body);
    }
    // --- tone & level (+ velocity, MIDI)
    {
        auto body = openCard (dm::U ("TONALITÉ, NIVEAU ET MIDI"), controls.getPreferredHeight());
        controls.setBounds (body.withWidth (juce::jmin (760, body.getWidth())));
    }
    // --- envelope
    {
        auto body = openCard (dm::U ("ENVELOPPE ET FONDUS"), envelope.getPreferredHeight());
        envelope.setBounds (body);
    }
    // --- equaliser
    {
        auto body = openCard (dm::U ("ÉGALISEUR 4 BANDES"), eq.getPreferredHeight());
        eq.setBounds (body);
    }
    // --- effects
    {
        auto body = openCard (dm::U ("EFFETS — SATURATION, BITCRUSHER, CHORUS, DELAY"), fx.getPreferredHeight());
        fx.setBounds (body);
    }
    // --- convolution reverb
    {
        auto body = openCard (dm::U ("RÉVERBÉRATION À CONVOLUTION"), reverb.getPreferredHeight());
        reverb.setBounds (body);
    }
    return y + 6;
}

void PadEditorPanel::Content::paint (juce::Graphics& g)
{
    for (const auto& c : cards)
    {
        paintGlass (g, c.bounds.toFloat(), 18.0f, juce::Colours::white, 0.0f, 0.7f);
        g.setColour (textDim);
        g.setFont (makeFont (12.0f, true));
        g.drawText (c.title, c.bounds.getX() + 20, c.bounds.getY() + 14, c.bounds.getWidth() - 40, 20, juce::Justification::centredLeft);
    }
}

// ============================================================================ panel
bool PadEditorPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (PadComponent::isSupportedFile (juce::File (f))) return true;
    return false;
}

void PadEditorPanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (PadComponent::isSupportedFile (juce::File (f)))
        {
            processor.loadSampleAsync (index, juce::File (f));
            break;
        }
}

PadEditorPanel::PadEditorPanel (DaftMugProcessor& p, int padIndex)
    : processor (p), index (padIndex), content (*this, p, padIndex)
{
    setOpaque (true);
    addAndMakeVisible (presetButton);
    presetButton.setTooltip (dm::U ("Enregistrer ou charger un preset de ce pad (réglages sonores)"));
    presetButton.onClick = [this] { showPadPresetMenu (presetButton, processor, index); };
    addAndMakeVisible (aiButton);
    aiButton.setTooltip (dm::U ("Daft Mug AI (facultatif) : conseils et recettes de réglages, toujours à valider par vous"));
    aiButton.onClick = [this] { if (onAi) onAi (index); };
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (playButton);
    playButton.setTooltip (dm::U ("Joue le pad avec les réglages actuels (maintenir pour le mode Gate). Raccourci : barre Espace"));
    playButton.onStateChange = [this]
    {
        const bool down = playButton.isDown();
        if (down && ! wasDown) processor.triggerPad (index, 100.0f / 127.0f);
        else if (! down && wasDown) processor.releasePad (index);
        wasDown = down;
    };

    nameEditor.setFont (makeFont (22.0f, true));
    nameEditor.setTextToShowWhenEmpty ("Nom du pad", textDim);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    nameEditor.onReturnKey = [this] { processor.setPadName (index, nameEditor.getText()); nameEditor.giveAwayKeyboardFocus(); };
    nameEditor.onFocusLost = [this] { processor.setPadName (index, nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    stateLabel.setFont (makeFont (13.0f));
    stateLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (stateLabel);

    content.setBufferedToImage (true);   // scrolling = blitting a cached image; only dirty regions are redrawn
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    content.loadButton.onClick = [this] { chooseFile(); };
    content.clearButton.onClick = [this] { processor.clearPad (index); };
    content.exportButton.onClick = [this] { chooseExportFile(); };

    // The panel itself owns the keyboard focus (Space = play): buttons / combo boxes must not steal it
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    std::function<void (juce::Component&)> noFocus = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::ComboBox*> (child) != nullptr)
                child->setWantsKeyboardFocus (false);
            noFocus (*child);
        }
    };
    noFocus (*this);

    processor.addChangeListener (this);
    refresh();
}

void PadEditorPanel::parentHierarchyChanged()
{
    if (getParentComponent() != nullptr)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PadEditorPanel> (this)]
                                         { if (safe != nullptr && safe->isShowing() && ! safe->hasKeyboardFocus (true)) safe->grabKeyboardFocus(); });
}

bool PadEditorPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (! spaceDown)
        {
            spaceDown = true;
            processor.triggerPad (index, 100.0f / 127.0f);
        }
        return true;
    }
    return false;
}

bool PadEditorPanel::keyStateChanged (bool)
{
    if (spaceDown && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
    {
        spaceDown = false;
        processor.releasePad (index);
    }
    return false;
}

PadEditorPanel::~PadEditorPanel()
{
    processor.removeChangeListener (this);
    if (wasDown || spaceDown) processor.releasePad (index);
    viewport.setViewedComponent (nullptr, false);
}

void PadEditorPanel::refresh()
{
    const auto meta = processor.getPadMeta (index);
    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (meta.name, false);

    switch (meta.state)
    {
        case LoadState::Empty: stateLabel.setText ("Aucun sample", juce::dontSendNotification); break;
        case LoadState::Loading: stateLabel.setText (dm::U ("Chargement…"), juce::dontSendNotification); break;
        case LoadState::Error: stateLabel.setText ("Erreur : " + meta.error, juce::dontSendNotification); break;
        case LoadState::Loaded:
        {
            const double seconds = meta.sample ? meta.sample->numFrames() / meta.sample->sampleRate : 0.0;
            stateLabel.setText (meta.file.getFileName() + dm::U ("   ·   ") + juce::String (seconds, 2) + dm::U (" s   ·   ")
                                    + juce::String (meta.sample ? meta.sample->sampleRate / 1000.0 : 0.0, 1) + " kHz",
                                juce::dontSendNotification);
            break;
        }
    }
    const auto sample = meta.state == LoadState::Loaded ? meta.sample : SampleHandle();
    content.waveform.setSample (sample);
    content.envelope.setSample (sample);
    content.eq.setSample (sample);
    content.updateTempo (meta);
    content.clearButton.setEnabled (meta.state != LoadState::Empty);
    content.exportButton.setEnabled (meta.state == LoadState::Loaded);
    repaint();
}

void PadEditorPanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choisir un sample pour le pad " + juce::String (index + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<PadEditorPanel> (this)] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();
                              if (safe != nullptr && result.existsAsFile())
                                  safe->processor.loadSampleAsync (safe->index, result);
                          });
}

void PadEditorPanel::chooseExportFile()
{
    const auto meta = processor.getPadMeta (index);
    if (meta.state != LoadState::Loaded || meta.sample == nullptr)
        return;

    const auto baseName = juce::File::createLegalFileName (meta.name.isNotEmpty() ? meta.name : "Pad " + juce::String (index + 1));
    chooser = std::make_unique<juce::FileChooser> (dm::U ("Exporter le pad en WAV"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (baseName + ".wav"),
                                                   "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<PadEditorPanel> (this)] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();
                              if (safe != nullptr && result != juce::File())
                                  safe->runExport (result.withFileExtension ("wav"));
                          });
}

void PadEditorPanel::runExport (const juce::File& file)
{
    const auto meta = processor.getPadMeta (index);
    if (meta.sample == nullptr)
        return;

    ExportOptions options;
    options.bits = 24;            // 24 bits, fréquence d'origine
    options.sampleRate = 0.0;
    options.normalize = content.normalizeButton.getToggleState();

    const auto params = processor.readPadParams (index);   // what the pad plays right now
    options.reverb = processor.createReverbForExport (index, options.sampleRate > 0.0 ? options.sampleRate : meta.sample->sampleRate);
    const auto sample = meta.sample;

    content.exportButton.setEnabled (false);
    content.exportStatus.setText (dm::U ("Export en cours…"), juce::dontSendNotification);

    // Rendering runs on a background thread; the UI stays responsive.
    juce::Thread::launch ([safe = juce::Component::SafePointer<PadEditorPanel> (this), sample, params, options, file]
    {
        const auto result = exportPadToWav (sample, params, options, file);
        juce::MessageManager::callAsync ([safe, result, file]
        {
            if (safe == nullptr)
                return;
            safe->content.exportButton.setEnabled (true);
            if (result.wasOk())
            {
                safe->content.exportStatus.setText (dm::U ("Exporté : ") + file.getFullPathName(), juce::dontSendNotification);
            }
            else
            {
                safe->content.exportStatus.setText ({}, juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, dm::U ("Export impossible"),
                                                        result.getErrorMessage());
            }
        });
    });
}

void PadEditorPanel::paint (juce::Graphics& g)
{
    // opaque backdrop: the editor fully replaces the pad grid while it is open
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), (float) getWidth() * 0.2f, 0.0f,
                                             accent.withAlpha (0.0f), (float) getWidth() * 0.6f, (float) getHeight() * 0.7f, true));
    g.fillAll();

    const auto colour = processor.getPadMeta (index).colour;
    g.setColour (colour);
    g.fillRoundedRectangle (34.0f, 78.0f, 40.0f, 4.0f, 2.0f);

}

void PadEditorPanel::relayoutContent()
{
    const int w = viewport.getMaximumVisibleWidth();
    content.setSize (w, content.layoutFor (w));
}

void PadEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 14);
    auto top = area.removeFromTop (44);
    backButton.setBounds (top.removeFromLeft (120).reduced (0, 4));
    top.removeFromLeft (14);
    presetButton.setBounds (top.removeFromRight (130).reduced (0, 4));
    top.removeFromRight (8);
    aiButton.setBounds (top.removeFromRight (150).reduced (0, 4));
    top.removeFromRight (8);
    playButton.setBounds (top.removeFromRight (170).reduced (0, 4));
    nameEditor.setBounds (top.removeFromLeft (juce::jmin (420, top.getWidth() - 200)));
    area.removeFromTop (22);
    stateLabel.setBounds (area.removeFromTop (22).withTrimmedLeft (10));
    area.removeFromTop (6);

    viewport.setBounds (getLocalBounds().withTrimmedTop (area.getY()));
    relayoutContent();
}
} // namespace dm::ui
