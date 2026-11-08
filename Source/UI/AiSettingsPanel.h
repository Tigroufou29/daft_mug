#pragma once

#include <functional>

#include "JuceIncludes.h"
#include "ShortcutTextEditor.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Options > IA et confidentialité: personal Gemini API key, connection test, model, on/off switch, information about the data
// sent to Google and about possible costs, link to the terms of use. The key is stored ONLY in the OS secure store (see
// SecretStore.h) and is hidden unless the user presses "Afficher".
class AiSettingsPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit AiSettingsPanel (DaftMugProcessor&);
    ~AiSettingsPanel() override;

    std::function<void()> onBack;
    std::function<void()> onTerms;

    void paint (juce::Graphics&) override;
    void resized() override;

    // exposed for the smoke test
    juce::Result saveKeyFromField();
    void testConnection();
    juce::String getStatusText() const { return status.getText(); }
    bool isKeyFieldMasked() const { return keyField.getPasswordCharacter() != 0; }
    void toggleReveal() { setRevealed (! revealed); }
    juce::String getKeyFieldText() const { return keyField.getText(); }
    void setEnabledFromUi (bool on);
    void setKeyFieldTextForTest (const juce::String& t) { keyField.setText (t, false); }

private:
    void timerCallback() override;
    void refresh();
    void setStatus (const juce::String& s, juce::Colour c);
    void setRevealed (bool on);
    void fillModels();

    DaftMugProcessor& processor;
    bool revealed = false;
    int revealTicks = 0;
    juce::Colour statusColour;

    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    juce::ToggleButton enableButton { dm::U ("Activer Daft Mug AI (facultatif)") };
    ShortcutTextEditor keyField;
    juce::TextButton saveKeyButton { dm::U ("Enregistrer la clé") }, revealButton { "Afficher" }, clearKeyButton { dm::U ("Effacer la clé") },
                     testButton { dm::U ("Authentifier la clé API") }, termsButton { dm::U ("Conditions d'utilisation…") };
    juce::Label status, keyLabel, modelLabel;
    juce::ComboBox modelBox;
    juce::Rectangle<int> infoArea, statusArea;   // `status` only stores the text (painted wrapped in paint())
};
} // namespace dm::ui
