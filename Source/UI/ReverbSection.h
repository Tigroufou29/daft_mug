#pragma once

#include <memory>
#include <vector>

#include "JuceIncludes.h"
#include "ParamRow.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// ONE convolution reverb per pad. A drop-down lists the impulse responses (IR) found in the IR folder; the list starts
// empty-handed ("choose an IR"). "Importer des IR…" copies YOUR files into that folder, they then appear in the list.
// Nothing is bundled. Choosing an item applies it to the pad; "Aucune réverbération" turns the effect off.
class ReverbSection : public juce::Component,
                      private juce::Timer
{
public:
    ReverbSection (DaftMugProcessor&, int padIndex);
    ~ReverbSection() override { stopTimer(); }

    int getPreferredHeight() const noexcept { return 40 + 8 + 36 + 8 + 24; }
    void resized() override;

    // also used by the smoke test
    void refreshList();
    void importFiles (const juce::Array<juce::File>&);
    int getNumIrs() const noexcept { return (int) irFiles.size(); }
    void selectIr (int listIndex);   // -1 = no reverb

private:
    void timerCallback() override;
    void chooseFiles();
    void syncSelection();
    juce::File userIrFolder() const;
    void setTypeParam (int type);

    DaftMugProcessor& processor;
    const int index;
    juce::ComboBox irBox;
    juce::TextButton importButton { dm::U ("Importer des IR…") };
    std::unique_ptr<ParamRow> wetRow;
    juce::Label status;
    std::vector<juce::File> irFiles;
    juce::String listSignature;
    juce::String importMessage;
    std::shared_ptr<juce::FileChooser> chooser;
};
} // namespace dm::ui
