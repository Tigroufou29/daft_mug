#pragma once

#include <functional>
#include <memory>

#include "EnvelopeSection.h"
#include "EqSection.h"
#include "FxSection.h"
#include "ReverbSection.h"
#include "JuceIncludes.h"
#include "ShortcutTextEditor.h"
#include "PadControls.h"
#include "PluginProcessor.h"
#include "WaveformView.h"

namespace dm::ui
{
// Independent editor of ONE pad, opened from the gear icon. It only ever touches the parameters
// and the sample of that pad. The body scrolls vertically so later sections (EQ, reverb, effects)
// can be added without squeezing the layout.
//
// Sections: waveform (zoom, region, reverse) - tone & level - envelope & fades - sample & export.
class PadEditorPanel : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    PadEditorPanel (DaftMugProcessor&, int padIndex);

    // dropping an audio file anywhere on the editor loads it into THIS pad
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    ~PadEditorPanel() override;

    std::function<void()> onBack;
    std::function<void (int)> onMutate;  // opens Mutate for this pad
    std::function<void (int)> onSlice;   // opens Slice to Pads for this pad
    std::function<void (int)> onAi;      // opens the Daft Mug AI assistant for this pad

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    // Space bar = play this pad (held for Gate mode), so every change can be auditioned
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    // Scrollable body
    class Content : public juce::Component
    {
    public:
        Content (PadEditorPanel& owner, DaftMugProcessor&, int padIndex);
        int layoutFor (int width);   // lays the children out and returns the total height
        void paint (juce::Graphics&) override;

        PadEditorPanel& panel;
        DaftMugProcessor& processor;
        const int index;

        juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Tout" }, regionButton { dm::U ("Zoom région") },
                         resetRegionButton { dm::U ("Réinitialiser la région") };
        juce::ToggleButton reverseButton { "Reverse" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;
        WaveformView waveform;
        PadControls controls;
        EnvelopeSection envelope;
        EqSection eq;
        FxSection fx;
        ReverbSection reverb;

        juce::TextButton loadButton { dm::U ("Charger un sample…") };
        juce::TextButton clearButton { "Vider" };
        juce::ToggleButton normalizeButton { "Normaliser" };
        juce::TextButton exportButton { dm::U ("Exporter en WAV…") };
        juce::Label exportStatus;

        // tempo card: detected / typed original tempo, target tempo, time-stretch without pitch change
        juce::Label tempoInfo, tempoMessage, origLabel { {}, dm::U ("Tempo d'origine") }, targetLabel { {}, dm::U ("Tempo cible") };
        juce::Slider origSlider, targetSlider;
        juce::TextButton detectButton { dm::U ("Détecter") }, halveButton { dm::U ("÷ 2") }, doubleButton { dm::U ("× 2") },
                         hostButton { dm::U ("Tempo de l'hôte") }, applyButton { dm::U ("Appliquer") }, resetTempoButton { dm::U ("Rétablir l'original") };
        void updateTempo (const PadMeta&);

    private:
        struct Card { juce::Rectangle<int> bounds; juce::String title; };
        std::vector<Card> cards;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void chooseFile();
    void chooseExportFile();
    void runExport (const juce::File&);
    void relayoutContent();

    DaftMugProcessor& processor;
    const int index;

    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    juce::TextButton presetButton { dm::U ("Presets ▾") }, aiButton { dm::U ("Assistant IA…") },
                     playButton { dm::U ("▶  Écouter (Espace)") };
    ShortcutTextEditor nameEditor;
    juce::Label stateLabel;
    juce::Viewport viewport;
    Content content;
    std::unique_ptr<juce::FileChooser> chooser;
    bool wasDown = false, spaceDown = false;
};
} // namespace dm::ui
