#pragma once

#include <functional>
#include <vector>

#include "AiCatalog.h"
#include "AiService.h"
#include "JuceIncludes.h"
#include "ShortcutTextEditor.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Daft Mug AI assistant for ONE pad. Gemini only advises: its answer is validated against the white list (AiCatalog), shown
// as a list of steps, and NOTHING is applied until the user presses "Prévisualiser". The local audio engine plays the result.
//   Prévisualiser  : applies the checked steps temporarily (a snapshot of the previous values is taken first)
//   Comparer       : switches between the original and the proposal
//   Accepter       : keeps the proposal
//   Restaurer      : puts the previous values back
// Closing the panel without accepting restores the previous values.
class AiAssistantPanel : public juce::Component,
                         private juce::ListBoxModel
{
public:
    AiAssistantPanel (DaftMugProcessor&, int pad);
    ~AiAssistantPanel() override;

    std::function<void()> onBack;
    std::function<void()> onOpenSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

    // actions, also used by the smoke test
    void setQuestion (ai::Mode, const juce::String& sampleType, const juce::String& request);
    void send();
    void handleAnswer (juce::Result, const juce::String& json);
    const ai::Recipe& getRecipe() const noexcept { return recipe; }
    void setStepChecked (int row, bool checked);
    void preview();
    void toggleCompare();
    void accept();
    void restore();
    bool isShowingProposal() const noexcept { return showingProposal; }
    bool isAccepted() const noexcept { return accepted; }
    juce::String getOutputText() const { return output.getText(); }
    juce::String getStatusText() const { return statusText; }

private:
    int getNumRows() override { return (int) recipe.steps.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { setStepChecked (row, ! checked[(size_t) row]); }

    ai::Recipe filtered() const;
    void applyProposal();
    void showOriginal();
    void setStatus (const juce::String& s, bool error = false);
    void updateButtons();
    void refreshAvailability();
    void playPad();   // plays the pad now (released after the sample length, for Gate mode)

    DaftMugProcessor& processor;
    const int pad;
    ai::Catalog catalog;
    ai::Recipe recipe;
    std::vector<bool> checked;
    std::vector<float> originals;   // value of each step's parameter before any proposal (for the "before -> after" display)
    ai::Snapshot before;
    bool showingProposal = false, accepted = false, waiting = false;
    ai::Mode mode = ai::Mode::Settings;
    juce::String statusText;
    bool statusIsError = false;

    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    juce::Label title;
    juce::ComboBox modeBox, typeBox;
    ShortcutTextEditor prompt, output;
    juce::TextButton sendButton { "Envoyer" }, previewButton { dm::U ("Prévisualiser") }, compareButton { "Comparer" },
                     acceptButton { "Accepter" }, restoreButton { dm::U ("Restaurer les valeurs précédentes") },
                     settingsButton { dm::U ("Options IA…") };
    juce::ListBox stepList;
    juce::Rectangle<int> statusArea, hintArea;
};
} // namespace dm::ui
