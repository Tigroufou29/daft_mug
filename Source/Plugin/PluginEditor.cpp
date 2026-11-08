#include "PluginEditor.h"
#include "UI/SystemInfo.h"
#include "UI/UiSettings.h"
#include "UI/PresetUi.h"

#include "ParameterIds.h"
#include "BinaryData.h"

namespace dm
{
using namespace ui;
using namespace ui::theme;

DaftMugEditor::DaftMugEditor (DaftMugProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    for (int i = 0; i < kNumPads; ++i)
    {
        pads[(size_t) i] = std::make_unique<PadComponent> (processor, i);
        pads[(size_t) i]->onSelected = [this] (int pad) { selectPad (pad); };
        pads[(size_t) i]->onOpenEditor = [this] (int pad) { openPadEditor (pad); };
        pads[(size_t) i]->onSlice = [this] (int pad) { openSlicer (pad); };
        pads[(size_t) i]->onMutate = [this] (int pad) { openMutate (pad); };
        addAndMakeVisible (*pads[(size_t) i]);
    }

    selectedTitle.setFont (makeFont (22.0f, true));
    selectedTitle.setColour (juce::Label::textColourId, text);
    selectedInfo.setFont (makeFont (13.0f));
    selectedInfo.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (selectedTitle);
    addAndMakeVisible (selectedInfo);
    editButton.setLookAndFeel (&hwLook);
    addAndMakeVisible (editButton);
    addAndMakeVisible (meter);
    editButton.onClick = [this] { openPadEditor (selectedPad); };
    addAndMakeVisible (kitButton);
    kitButton.setTooltip (dm::U ("Enregistrer ou charger un kit (les 16 pads)"));
    kitButton.onClick = [this] { ui::showKitPresetMenu (kitButton, processor); };
    addAndMakeVisible (optionsButton);
    optionsButton.onClick = [this] { openOptions(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    undoButton.onClick = [this] { doUndo(); };
    redoButton.onClick = [this] { doRedo(); };
    processor.history.onChange = [safe = juce::Component::SafePointer<DaftMugEditor> (this)] { if (safe != nullptr) safe->updateHistoryButtons(); };
    updateHistoryButtons();

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (900, 600, 3840, 2400);
    setSize (1180, 790);

    selectPad (0);
    processor.addChangeListener (this);
    startTimerHz (30);
    updateResourceText();
}

DaftMugEditor::~DaftMugEditor()
{
    stopTimer();
    processor.history.onChange = nullptr;
    editButton.setLookAndFeel (nullptr);
    aiPanel.reset();
    aiSettingsPanel.reset();
    termsPanel.reset();
    processor.removeChangeListener (this);
    processor.cancelMidiLearn();
    padEditor.reset();
    optionsPanel.reset();
    slicePanel.reset();
    mutatePanel.reset();
    quickControls.reset();
    quickWaveform.reset();
    setLookAndFeel (nullptr);
}

void DaftMugEditor::selectPad (int pad)
{
    selectedPad = juce::jlimit (0, kNumPads - 1, pad);
    for (int i = 0; i < kNumPads; ++i)
        pads[(size_t) i]->setSelected (i == selectedPad);

    // rebuild the quick panel so every control is bound to the newly selected pad
    quickControls.reset();
    quickWaveform.reset();
    quickWaveform = std::make_unique<WaveformView> (processor, selectedPad, false);
    quickControls = std::make_unique<PadControls> (processor, selectedPad);
    addAndMakeVisible (*quickWaveform);
    addAndMakeVisible (*quickControls);

    changeListenerCallback (nullptr);
    resized();
}

void DaftMugEditor::openPadEditor (int pad)
{
    aiPanel.reset();
    aiSettingsPanel.reset();
    termsPanel.reset();
    selectPad (pad);
    padEditor = std::make_unique<PadEditorPanel> (processor, pad);
    padEditor->onBack = [this]
    {
        // closing from inside the panel's own callback: defer the destruction
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->closePadEditor(); });
    };
    optionsPanel.reset();
    slicePanel.reset();
    mutatePanel.reset();
    padEditor->onMutate = [this] (int p)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this), p]
                                         { if (safe != nullptr) safe->openMutate (p); });
    };
    padEditor->onAi = [this] (int p)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this), p]
                                         { if (safe != nullptr) safe->openAiAssistant (p); });
    };
    padEditor->onSlice = [this] (int p)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this), p]
                                         { if (safe != nullptr) safe->openSlicer (p); });
    };
    addAndMakeVisible (*padEditor);
    layoutOverlays();
    padEditor->toFront (true);
}

void DaftMugEditor::openSlicer (int pad)
{
    aiPanel.reset();
    aiSettingsPanel.reset();
    termsPanel.reset();
    if (processor.getPadMeta (pad).state != LoadState::Loaded) return;
    selectPad (pad);
    padEditor.reset();
    optionsPanel.reset();
    slicePanel = std::make_unique<ui::SlicePanel> (processor, pad);
    slicePanel->onBack = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->closeSlicer(); });
    };
    addAndMakeVisible (*slicePanel);
    layoutOverlays();
    slicePanel->toFront (true);
}

void DaftMugEditor::closeSlicer()
{
    slicePanel.reset();
}

void DaftMugEditor::openMutate (int pad)
{
    aiPanel.reset();
    aiSettingsPanel.reset();
    termsPanel.reset();
    if (processor.getPadMeta (pad).state != LoadState::Loaded) return;
    selectPad (pad);
    padEditor.reset();
    optionsPanel.reset();
    slicePanel.reset();
    mutatePanel = std::make_unique<ui::MutatePanel> (processor, pad);
    mutatePanel->onBack = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->closeMutate(); });
    };
    addAndMakeVisible (*mutatePanel);
    layoutOverlays();
    mutatePanel->toFront (true);
}

// ---- standalone window: maximise button and F11 full screen (never touches a host's plug-in window)
juce::DocumentWindow* DaftMugEditor::getStandaloneWindow() const
{
    if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return nullptr;
    return findParentComponentOfClass<juce::DocumentWindow>();
}

void DaftMugEditor::applyRenderer()
{
   #if JUCE_WINDOWS
    auto* peer = getPeer();
    if (peer == nullptr) return;
    const auto mode = ui::UiSettings::getRenderer();
    if (mode == "auto") return;   // keep what JUCE chose
    const auto engines = peer->getAvailableRenderingEngines();
    for (int i = 0; i < engines.size(); ++i)
    {
        const bool software = engines[i].containsIgnoreCase ("software");
        const bool direct2d = engines[i].containsIgnoreCase ("direct2d");
        if ((mode == "software" && software) || (mode == "gpu" && direct2d))
        {
            if (peer->getCurrentRenderingEngine() != i) peer->setCurrentRenderingEngine (i);
            break;
        }
    }
   #endif
}

bool DaftMugEditor::anyOverlayOpen() const noexcept
{
    return padEditor != nullptr || optionsPanel != nullptr || slicePanel != nullptr || mutatePanel != nullptr
           || aiPanel != nullptr || aiSettingsPanel != nullptr || termsPanel != nullptr;
}

void DaftMugEditor::updateMainVisibility()
{
    const bool hide = anyOverlayOpen();
    if (hide == mainHidden) return;
    mainHidden = hide;
    for (auto& p : pads) p->setVisible (! hide);
    meter.setVisible (! hide);
    for (juce::Component* c : { (juce::Component*) &selectedTitle, (juce::Component*) &selectedInfo, (juce::Component*) &editButton,
                                (juce::Component*) quickWaveform.get(), (juce::Component*) quickControls.get() })
        if (c != nullptr) c->setVisible (! hide);
}

void DaftMugEditor::parentHierarchyChanged()
{
    if (! rendererDone && getPeer() != nullptr)
    {
        rendererDone = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->applyRenderer(); });
    }
    if (windowButtonsDone || getStandaloneWindow() == nullptr) return;
    // wait until the standalone wrapper has finished setting its window up
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                     { if (safe != nullptr) safe->enableStandaloneWindowButtons(); });
}

void DaftMugEditor::enableStandaloneWindowButtons()
{
    auto* w = getStandaloneWindow();
    if (w == nullptr || windowButtonsDone) return;
    windowButtonsDone = true;
    w->setResizable (true, false);
    w->setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton, false);
    if (w->isUsingNativeTitleBar())
    {
        // the native title bar takes its buttons from the window style: recreate the native window to apply the new set
        w->setUsingNativeTitleBar (false);
        w->setUsingNativeTitleBar (true);
    }
}

void DaftMugEditor::doUndo()
{
    if (processor.history.undo()) processor.sendChangeMessage();
}

void DaftMugEditor::doRedo()
{
    if (processor.history.redo()) processor.sendChangeMessage();
}

void DaftMugEditor::updateHistoryButtons()
{
    const auto& h = processor.history;
    undoButton.setEnabled (h.canUndo());
    redoButton.setEnabled (h.canRedo());
    undoButton.setTooltip (h.canUndo() ? dm::U ("Annuler : ") + h.undoLabel() + "  (Ctrl+Z)" : dm::U ("Rien à annuler"));
    redoButton.setTooltip (h.canRedo() ? dm::U ("Rétablir : ") + h.redoLabel() + "  (Ctrl+Y)" : dm::U ("Rien à rétablir"));
}

bool DaftMugEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl+Z / Cmd+Z = undo, Ctrl+Y / Ctrl+Shift+Z / Cmd+Shift+Z = redo (text fields keep their own text undo: they handle the key first)
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { doUndo(); return true; }
    if (key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { doRedo(); return true; }

    auto* w = getStandaloneWindow();
    if (w == nullptr) return false;
    auto& desktop = juce::Desktop::getInstance();
    if (key == juce::KeyPress::F11Key)
    {
        desktop.setKioskModeComponent (desktop.getKioskModeComponent() == w ? nullptr : w, false);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && desktop.getKioskModeComponent() == w)
    {
        desktop.setKioskModeComponent (nullptr);
        return true;
    }
    return false;
}

void DaftMugEditor::closeOverlays()
{
    aiPanel.reset();   // restores the previous values when a proposal was only previewed
    aiSettingsPanel.reset();
    termsPanel.reset();
    padEditor.reset();
    optionsPanel.reset();
    slicePanel.reset();
    mutatePanel.reset();
}

void DaftMugEditor::openAiSettings()
{
    closeOverlays();
    aiSettingsPanel = std::make_unique<ui::AiSettingsPanel> (processor);
    aiSettingsPanel->onBack = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) { safe->closeOverlays(); safe->openOptions(); } });
    };
    aiSettingsPanel->onTerms = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->openTerms (true); });
    };
    addAndMakeVisible (*aiSettingsPanel);
    layoutOverlays();
    aiSettingsPanel->toFront (true);
}

void DaftMugEditor::openTerms (bool backToAi, bool backToMain)
{
    closeOverlays();
    termsPanel = std::make_unique<ui::TermsPanel>();
    termsPanel->onBack = [this, backToAi, backToMain]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this), backToAi, backToMain]
                                         { if (safe != nullptr) { safe->closeOverlays(); if (backToMain) return; if (backToAi) safe->openAiSettings(); else safe->openOptions(); } });
    };
    addAndMakeVisible (*termsPanel);
    layoutOverlays();
    termsPanel->toFront (true);
}

void DaftMugEditor::openAiAssistant (int pad)
{
    selectPad (pad);
    closeOverlays();
    aiPanel = std::make_unique<ui::AiAssistantPanel> (processor, pad);
    aiPanel->onBack = [this, pad]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this), pad]
                                         { if (safe != nullptr) { safe->closeOverlays(); safe->openPadEditor (pad); } });
    };
    aiPanel->onOpenSettings = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->openAiSettings(); });
    };
    addAndMakeVisible (*aiPanel);
    layoutOverlays();
    aiPanel->toFront (true);
}

void DaftMugEditor::closeMutate()
{
    mutatePanel.reset();
}

void DaftMugEditor::openOptions()
{
    aiPanel.reset();
    aiSettingsPanel.reset();
    termsPanel.reset();
    slicePanel.reset();
    mutatePanel.reset();
    padEditor.reset();
    optionsPanel = std::make_unique<ui::OptionsPanel> (processor);
    optionsPanel->onBack = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->closeOptions(); });
    };
    optionsPanel->onAiSettings = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->openAiSettings(); });
    };
    optionsPanel->onRendererChanged = [this] { applyRenderer(); };
    optionsPanel->onTerms = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DaftMugEditor> (this)]
                                         { if (safe != nullptr) safe->openTerms (false); });
    };
    addAndMakeVisible (*optionsPanel);
    layoutOverlays();
    optionsPanel->toFront (true);
}

void DaftMugEditor::closeOptions()
{
    optionsPanel.reset();
}

void DaftMugEditor::layoutOverlays()
{
    const auto r = getLocalBounds().withTrimmedTop (headerArea.getHeight()).withTrimmedBottom (statusArea.getHeight());
    if (padEditor != nullptr) padEditor->setBounds (r);
    if (optionsPanel != nullptr) optionsPanel->setBounds (r);
    if (slicePanel != nullptr) slicePanel->setBounds (r);
    if (mutatePanel != nullptr) mutatePanel->setBounds (r);
    if (aiSettingsPanel != nullptr) aiSettingsPanel->setBounds (r);
    if (aiPanel != nullptr) aiPanel->setBounds (r);
    if (termsPanel != nullptr) termsPanel->setBounds (r);
}

void DaftMugEditor::closePadEditor()
{
    padEditor.reset();
}

void DaftMugEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& p : pads)
        p->refreshFromModel();

    const auto meta = processor.getPadMeta (selectedPad);
    selectedTitle.setText ("Pad " + juce::String (selectedPad + 1).paddedLeft ('0', 2)
                               + (meta.name.isNotEmpty() ? dm::U ("  ·  ") + meta.name : juce::String()),
                           juce::dontSendNotification);
    selectedInfo.setText (meta.state == LoadState::Loaded
                              ? meta.file.getFileName()
                              : (meta.state == LoadState::Error ? "Erreur : " + meta.error
                                                                  : juce::String (dm::U ("Aucun sample — cliquez le pad ou glissez un fichier"))),
                          juce::dontSendNotification);
    if (quickWaveform != nullptr)
        quickWaveform->setSample (meta.state == LoadState::Loaded ? meta.sample : SampleHandle());
}

void DaftMugEditor::applyLearnedNote()
{
    int pad = -1, note = -1;
    if (! processor.consumeLearnedNote (pad, note))
        return;
    if (auto* param = processor.apvts.getParameter (ids::note (pad)))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) note));
        param->endChangeGesture();
    }
}

void DaftMugEditor::updateResourceText()
{
    auto mb = [] (size_t bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0), bytes < 100u * 1024u * 1024u ? 1 : 0) + " Mo"; };
    const auto t = dm::U ("CPU audio : ") + juce::String (juce::roundToInt (processor.getCpuLoad() * 100.0f)) + " %"
                   + dm::U ("     ·     RAM samples : ") + mb (processor.getSampleMemoryBytes())
                   + dm::U ("     ·     RAM processus : ") + mb (ui::getProcessMemoryBytes());
    if (t != resourceText) { resourceText = t; repaint (statusArea); }
}

void DaftMugEditor::timerCallback()
{
    updateMainVisibility();
    processor.housekeeping();
    applyLearnedNote();

    for (auto& p : pads)
        p->tickVisuals (processor.getEngine());

    const auto midiCount = processor.getMidiActivityCount();
    if (midiCount != lastMidiCount)
    {
        lastMidiCount = midiCount;
        midiFlash = 1.0f;
        repaint (headerArea);
    }
    else if (midiFlash > 0.0f)
    {
        midiFlash = juce::jmax (0.0f, midiFlash - 0.08f);
        repaint (headerArea);
    }

    if (++resourceTick >= 15)   // twice a second
    {
        resourceTick = 0;
        updateResourceText();
    }

    const int now = processor.getEngine().activeVoices();
    if (now != voices)
    {
        voices = now;
        repaint (statusArea);
    }
}

// The logo opens the terms of use
void DaftMugEditor::mouseDown (const juce::MouseEvent& e)
{
    if (logoHit.contains (e.getPosition()))
        openTerms (false, true);
}

void DaftMugEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (logoHit.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void DaftMugEditor::paint (juce::Graphics& g)
{
    // background picture (cover-fit, cached at the window size) under a dark veil so the glass pads stay readable
    static const juce::Image bgSource = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    g.fillAll (bgTop);
    if (bgSource.isValid())
    {
        if (backgroundCache.getWidth() != getWidth() || backgroundCache.getHeight() != getHeight())
        {
            const float scale = juce::jmax ((float) getWidth() / (float) bgSource.getWidth(), (float) getHeight() / (float) bgSource.getHeight());
            const int sw = juce::roundToInt ((float) bgSource.getWidth() * scale), sh = juce::roundToInt ((float) bgSource.getHeight() * scale);
            const auto scaled = bgSource.rescaled (sw, sh, juce::Graphics::highResamplingQuality);
            backgroundCache = scaled.getClippedImage ({ (sw - getWidth()) / 2, (sh - getHeight()) / 2, getWidth(), getHeight() });
        }
        g.drawImageAt (backgroundCache, 0, 0);
    }
    g.setColour (bgTop.withAlpha (0.18f));   // the picture is already dark: light veil only
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (bgTop.withAlpha (0.55f), 0.0f, 0.0f, bgTop.withAlpha (0.0f), 0.0f, (float) headerArea.getHeight() + 30.0f, false));
    g.fillRect (getLocalBounds().withHeight (headerArea.getHeight() + 30));

    // header
    static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    float logoRight = 28.0f;
    if (logo.isValid())
    {
        const auto box = headerArea.toFloat().reduced (0.0f, 4.0f).withX (14.0f).withWidth (300.0f);
        logoHit = box.toNearestInt().withWidth ((int) (headerArea.getHeight() - 8) * logo.getWidth() / juce::jmax (1, logo.getHeight()));
        g.drawImage (logo, box, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
        logoRight = 14.0f + juce::jmin (300.0f, (headerArea.getHeight() - 8.0f) * (float) logo.getWidth() / (float) logo.getHeight()) + 18.0f;
    }
    else
    {
        g.setColour (text);
        g.setFont (makeFont (26.0f, true));
        g.drawText ("DAFT MUG", headerArea.withTrimmedLeft (28), juce::Justification::centredLeft);
        logoRight = 190.0f;
    }
    g.setColour (textDim);
    g.setFont (makeFont (13.0f));
    g.drawText ("Sampler 16 pads", headerArea.withTrimmedLeft ((int) logoRight), juce::Justification::centredLeft);

    // MIDI badge: lights up with real incoming Note On/Off events (from a keyboard or the DAW)
    auto headerCopy = headerArea;
    auto midiBadge = headerCopy.removeFromRight (110).reduced (10, 38).toFloat();
    paintGlass (g, midiBadge, 10.0f, ledOk, 0.10f + 0.9f * midiFlash, 1.0f + 0.4f * midiFlash);
    g.setColour (text);
    g.setFont (makeFont (12.0f, true));
    g.drawText ("MIDI IN", midiBadge, juce::Justification::centred);

    // side panel glass
    ui::hw::paintPanel (g, sidePanelArea.toFloat(), 10.0f, true);

    // status bar (dark band: readable over the background picture)
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (statusArea);
    g.setColour (textDim.brighter (0.3f));
    g.setFont (makeFont (12.0f));
    g.drawText ("Voix actives : " + juce::String (voices) + " / " + juce::String (kMaxVoices)
                    + dm::U ("     ·     Pad sélectionné : ") + juce::String (selectedPad + 1)
                    + dm::U ("     ·     Notes MIDI ") + juce::MidiMessage::getMidiNoteName (kFirstMidiNote, true, true, 3)
                    + dm::U (" → ") + juce::MidiMessage::getMidiNoteName (kFirstMidiNote + kNumPads - 1, true, true, 3) + dm::U (" par défaut"),
                statusArea.withTrimmedLeft (28), juce::Justification::centredLeft);
    g.drawText (resourceText, statusArea.withTrimmedRight (24), juce::Justification::centredRight);
}

void DaftMugEditor::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (112);
    statusArea = area.removeFromBottom (32);
    area = area.reduced (24, 8);

    sidePanelArea = area.removeFromRight (juce::jlimit (330, 420, area.getWidth() / 3));
    area.removeFromRight (18);
    meter.setBounds (area.removeFromLeft (92));   // level meter, left of the pads (under the logo)
    area.removeFromLeft (18);

    // square-ish 4x4 grid centred in the remaining space
    gridArea = area;
    const int gap = 14;
    const int cell = juce::jmin ((gridArea.getWidth() - 3 * gap) / 4, (gridArea.getHeight() - 3 * gap) / 4);
    const int gridW = cell * 4 + gap * 3, gridH = cell * 4 + gap * 3;
    const auto origin = gridArea.getCentre() - juce::Point<int> (gridW / 2, gridH / 2);

    // Same order as the approved mock-up: pad 1 top-left, reading left to right, top to bottom.
    for (int i = 0; i < kNumPads; ++i)
    {
        const int col = i % 4, row = i / 4;
        pads[(size_t) i]->setBounds (origin.x + col * (cell + gap), origin.y + row * (cell + gap), cell, cell);
    }

    auto side = sidePanelArea.reduced (20, 18);
    selectedTitle.setBounds (side.removeFromTop (30));
    selectedInfo.setBounds (side.removeFromTop (20));
    side.removeFromTop (8);
    if (quickWaveform != nullptr) quickWaveform->setBounds (side.removeFromTop (110));
    side.removeFromTop (10);
    if (quickControls != nullptr) quickControls->setBounds (side.removeFromTop (quickControls->getPreferredHeight()));
    side.removeFromTop (10);
    editButton.setBounds (side.removeFromTop (38));

    undoButton.setBounds (headerArea.withTrimmedRight (452).removeFromRight (100).reduced (0, 38));
    redoButton.setBounds (headerArea.withTrimmedRight (342).removeFromRight (100).reduced (0, 38));
    optionsButton.setBounds (headerArea.withTrimmedRight (122).removeFromRight (100).reduced (0, 38));
    kitButton.setBounds (headerArea.withTrimmedRight (232).removeFromRight (100).reduced (0, 38));
    layoutOverlays();
}
} // namespace dm
