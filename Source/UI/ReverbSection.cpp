#include "ReverbSection.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

namespace
{
constexpr int kOffId = 1, kFirstIrId = 10;

bool isIrFile (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac";
}
}

ReverbSection::ReverbSection (DaftMugProcessor& p, int padIndex) : processor (p), index (padIndex)
{
    addAndMakeVisible (irBox);
    irBox.setTextWhenNothingSelected (dm::U ("Choisir une IR…"));
    irBox.setTextWhenNoChoicesAvailable (dm::U ("Aucune IR : cliquez sur « Importer des IR… »"));
    irBox.setTooltip (dm::U ("Réponses impulsionnelles (vos fichiers) disponibles. Une seule réverbération par pad."));
    irBox.onChange = [this]
    {
        const int id = irBox.getSelectedId();
        if (id == kOffId) selectIr (-1);
        else if (id >= kFirstIrId) selectIr (id - kFirstIrId);
    };

    addAndMakeVisible (importButton);
    importButton.setTooltip (dm::U ("Copie vos fichiers d'IR (WAV, AIFF, FLAC) dans le dossier IR de Daft Mug : ils apparaissent ensuite dans la liste."));
    importButton.onClick = [this] { chooseFiles(); };

    wetRow = std::make_unique<ParamRow> (p.apvts, "Wet", ids::rvWet (index), dm::U ("Quantité de réverbération mélangée au son (0–100 %)."), 60);
    addAndMakeVisible (*wetRow);

    status.setFont (makeFont (12.0f));
    status.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (status);

    refreshList();
    startTimerHz (4);
    timerCallback();
}

juce::File ReverbSection::userIrFolder() const
{
    const auto folders = processor.getIrSearchFolders();
    // folders = [DAFTMUG_IR_DIR if set], <user data>/DaftMug/IR, <source tree IR>: the user folder is second to last
    return juce::File (folders[juce::jmax (0, folders.size() - 2)]);
}

void ReverbSection::setTypeParam (int type)
{
    if (auto* prm = processor.apvts.getParameter (ids::rvType (index)))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) type));
        prm->endChangeGesture();
    }
}

void ReverbSection::refreshList()
{
    std::vector<juce::File> files;
    juce::StringArray seenNames;
    for (const auto& folder : processor.getIrSearchFolders())
    {
        const juce::File dir (folder);
        if (! dir.isDirectory()) continue;
        auto found = dir.findChildFiles (juce::File::findFiles, false);
        found.sort();
        for (const auto& f : found)
            if (isIrFile (f) && ! seenNames.contains (f.getFileName(), true))
            {
                seenNames.add (f.getFileName());
                files.push_back (f);
            }
    }
    // an IR chosen earlier from elsewhere (old project, preset) stays selectable
    const auto custom = processor.getPadMeta (index).customIr;
    if (custom.existsAsFile() && ! seenNames.contains (custom.getFileName(), true))
        files.push_back (custom);

    juce::String sig;
    for (const auto& f : files) sig << f.getFullPathName() << "|";
    if (sig == listSignature) return;
    listSignature = sig;
    irFiles = files;

    irBox.clear (juce::dontSendNotification);
    if (! irFiles.empty())
    {
        irBox.addItem (dm::U ("Aucune réverbération"), kOffId);
        irBox.addSeparator();
        for (size_t i = 0; i < irFiles.size(); ++i)
            irBox.addItem (irFiles[i].getFileNameWithoutExtension(), kFirstIrId + (int) i);
    }
    syncSelection();
}

void ReverbSection::syncSelection()
{
    const int type = juce::roundToInt (processor.apvts.getRawParameterValue (ids::rvType (index))->load());
    int id = 0;   // nothing selected: the list is "empty" until the user picks an IR
    if (type > 0)
    {
        const bool custom = type == DaftMugProcessor::kNumIrTypes - 1;
        const auto file = custom ? processor.getPadMeta (index).customIr : processor.findIrFile (type);
        for (size_t i = 0; i < irFiles.size(); ++i)
            if (irFiles[i] == file || irFiles[i].getFileName() == file.getFileName()) { id = kFirstIrId + (int) i; break; }
    }
    if (irBox.getSelectedId() != id)
        irBox.setSelectedId (id, juce::dontSendNotification);
}

void ReverbSection::selectIr (int listIndex)
{
    if (listIndex < 0)
    {
        setTypeParam (0);
        return;
    }
    if (listIndex >= (int) irFiles.size()) return;
    processor.setCustomIr (index, irFiles[(size_t) listIndex]);   // loads off the audio thread
    setTypeParam (DaftMugProcessor::kNumIrTypes - 1);
}

void ReverbSection::importFiles (const juce::Array<juce::File>& files)
{
    auto dir = userIrFolder();
    if (! dir.createDirectory().wasOk() && ! dir.isDirectory())
    {
        importMessage = dm::U ("Impossible de créer le dossier IR : ") + dir.getFullPathName();
        return;
    }
    int copied = 0;
    for (const auto& f : files)
    {
        if (! f.existsAsFile() || ! isIrFile (f)) continue;
        auto target = dir.getChildFile (f.getFileName());
        if (target == f) { ++copied; continue; }   // already in the folder
        for (int n = 2; target.existsAsFile(); ++n)
            target = dir.getChildFile (f.getFileNameWithoutExtension() + " (" + juce::String (n) + ")" + f.getFileExtension());
        if (f.copyFileTo (target)) ++copied;
    }
    importMessage = copied > 0 ? juce::String (copied) + dm::U (" IR importée(s) dans ") + dir.getFullPathName()
                               : dm::U ("Aucun fichier d'IR valide (WAV, AIFF, FLAC) n'a été importé.");
    refreshList();
}

void ReverbSection::chooseFiles()
{
    chooser = std::make_shared<juce::FileChooser> (dm::U ("Importer des réponses impulsionnelles"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<ReverbSection> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResults().size() > 0)
                                  safe->importFiles (fc.getResults());
                          });
}

void ReverbSection::timerCallback()
{
    if (! irBox.isPopupActive())
        refreshList();
    if (! irBox.isPopupActive())
        syncSelection();

    const int type = juce::roundToInt (processor.apvts.getRawParameterValue (ids::rvType (index))->load());
    wetRow->setEnabled (type > 0);
    wetRow->setAlpha (type > 0 ? 1.0f : 0.4f);

    juce::String info = processor.getReverbStatus (index);
    if (info.isEmpty() && type > 0)
    {
        const auto f = type == DaftMugProcessor::kNumIrTypes - 1 ? processor.getPadMeta (index).customIr : processor.findIrFile (type);
        info = dm::U ("IR : ") + f.getFileName();
    }
    if (info.isEmpty())
        info = importMessage.isNotEmpty() ? importMessage
                                          : (irFiles.empty() ? dm::U ("Aucune IR pour l'instant : importez vos fichiers.") : juce::String());
    status.setText (info, juce::dontSendNotification);
}

void ReverbSection::resized()
{
    auto area = getLocalBounds();
    auto row = area.removeFromTop (40);
    irBox.setBounds (row.removeFromLeft (320).reduced (0, 3));
    row.removeFromLeft (10);
    importButton.setBounds (row.removeFromLeft (170).reduced (0, 3));
    area.removeFromTop (8);
    wetRow->setBounds (area.removeFromTop (36).withWidth (juce::jmin (760, area.getWidth())));
    area.removeFromTop (8);
    status.setBounds (area.removeFromTop (24));
}
} // namespace dm::ui
