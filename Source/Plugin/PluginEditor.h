#pragma once

#include <array>
#include <memory>

#include "JuceIncludes.h"
#include "PluginProcessor.h"
#include "UI/LiquidGlassLookAndFeel.h"
#include "UI/MasterMeter.h"
#include "UI/PadComponent.h"
#include "UI/PadControls.h"
#include "UI/HardwareStyle.h"
#include "UI/OptionsPanel.h"
#include "UI/MutatePanel.h"
#include "UI/AiSettingsPanel.h"
#include "UI/AiAssistantPanel.h"
#include "UI/TermsPanel.h"
#include "UI/SlicePanel.h"
#include "UI/PadEditorPanel.h"
#include "UI/WaveformView.h"

namespace dm
{
class DaftMugEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit DaftMugEditor (DaftMugProcessor&);
    ~DaftMugEditor() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    // used by the gear button, the context menu and the smoke test
    void openPadEditor (int pad);
    void openOptions();
    void openSlicer (int pad);
    void openMutate (int pad);
    void openAiSettings();
    void openTerms (bool backToAi, bool backToMain = false);
    void openAiAssistant (int pad);
    ui::AiSettingsPanel* getAiSettingsPanel() { return aiSettingsPanel.get(); }
    ui::AiAssistantPanel* getAiAssistantPanel() { return aiPanel.get(); }
    ui::TermsPanel* getTermsPanel() { return termsPanel.get(); }
    bool isMutateOpen() const noexcept { return mutatePanel != nullptr; }
    ui::MutatePanel* getMutatePanel() { return mutatePanel.get(); }
    bool isSlicerOpen() const noexcept { return slicePanel != nullptr; }
    ui::SlicePanel* getSlicePanel() { return slicePanel.get(); }
    bool isOptionsOpen() const noexcept { return optionsPanel != nullptr; }
    bool isPadEditorOpen() const noexcept { return padEditor != nullptr; }
    ui::PadEditorPanel* getPadEditor() { return padEditor.get(); }
    ui::MasterMeter& getMeter() { return meter; }
    ui::PadComponent* getPad (int pad) { return pads[(size_t) pad].get(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void selectPad (int pad);
    void closePadEditor();
    void closeOptions();
    void closeSlicer();
    void closeMutate();
    void layoutOverlays();
    void applyLearnedNote();

    DaftMugProcessor& processor;
    ui::LiquidGlassLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 500 };

    std::array<std::unique_ptr<ui::PadComponent>, kNumPads> pads;
    int selectedPad = 0;

    // right-hand "selected pad" panel
    juce::Label selectedTitle, selectedInfo;
    std::unique_ptr<ui::WaveformView> quickWaveform;
    std::unique_ptr<ui::PadControls> quickControls;
    ui::hw::HardwareLookAndFeel hwLook;   // declared before the buttons that use it
    juce::TextButton editButton { dm::U ("Éditer ce pad") };

    std::unique_ptr<ui::PadEditorPanel> padEditor;
    ui::MasterMeter meter { processor };
    void updateResourceText();
    void applyRenderer();   // drawing mode chosen in Options (Windows)
    void updateMainVisibility();   // hides the main view while a full-size panel covers it (no hidden redraws)
    bool anyOverlayOpen() const noexcept;
    bool mainHidden = false, rendererDone = false;
    juce::String resourceText;   // "CPU ... RAM ..." shown on the right of the status bar
    int resourceTick = 0;
    juce::Rectangle<int> logoHit;   // clickable logo area (set while painting)
    juce::Image backgroundCache;   // background picture scaled to the window
    std::unique_ptr<ui::OptionsPanel> optionsPanel;
    std::unique_ptr<ui::SlicePanel> slicePanel;
    std::unique_ptr<ui::MutatePanel> mutatePanel;
    void closeOverlays();
    void enableStandaloneWindowButtons();   // standalone app only: adds the maximise button to the window
    juce::DocumentWindow* getStandaloneWindow() const;
    bool windowButtonsDone = false;
    std::unique_ptr<ui::AiSettingsPanel> aiSettingsPanel;
    std::unique_ptr<ui::AiAssistantPanel> aiPanel;
    std::unique_ptr<ui::TermsPanel> termsPanel;
    
    juce::TextButton optionsButton { "Options" };
    juce::TextButton kitButton { dm::U ("Kits ▾") };
    juce::TextButton undoButton { dm::U ("↶ Annuler") }, redoButton { dm::U ("↷ Rétablir") };
    void doUndo();
    void doRedo();
    void updateHistoryButtons();

    juce::Rectangle<int> gridArea, sidePanelArea, headerArea, statusArea;
    int voices = 0;
    uint32_t lastMidiCount = 0;
    float midiFlash = 0.0f;
};
} // namespace dm
