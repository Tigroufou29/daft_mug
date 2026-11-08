#include "Collector.h"

#include <array>
#include <map>

#include "ParameterIds.h"
#include "Presets.h"

namespace dm
{
juce::File Collector::copyUnique (const juce::File& source, const juce::File& dir, CollectReport& report)
{
    if (! dir.createDirectory()) { report.problems.add (dm::U ("Impossible de créer ") + dir.getFullPathName()); return {}; }

    const auto base = source.getFileNameWithoutExtension();
    const auto ext = source.getFileExtension();
    for (int n = 1; n < 1000; ++n)
    {
        const auto name = n == 1 ? base + ext : base + "_" + juce::String (n) + ext;
        const auto dest = dir.getChildFile (name);
        if (dest.existsAsFile())
        {
            // an identical file is already there (earlier collection): reuse it; otherwise it is a collision, try the next name
            if (dest.getSize() == source.getSize() && dest.hasIdenticalContentTo (source)) return dest;
            continue;
        }
        if (! source.copyFileTo (dest)) { report.problems.add (dm::U ("Copie impossible : ") + source.getFileName()); return {}; }
        if (n > 1) ++report.renamed;
        return dest;
    }
    report.problems.add (dm::U ("Trop de fichiers portant le nom ") + source.getFileName());
    return {};
}

juce::Result Collector::collect (const juce::File& folder, const juce::String& kitName, CollectReport& report)
{
    report = {};
    if (PresetManager::sanitizeFileName (kitName).isEmpty()) return juce::Result::fail (dm::U ("Nom de kit invalide"));
    if (! folder.createDirectory()) return juce::Result::fail (dm::U ("Impossible de créer le dossier : ") + folder.getFullPathName());

    // plan first: nothing is touched when the folder cannot be written
    const auto probe = folder.getChildFile (".daftmug_write_test");
    if (! probe.replaceWithText ("x")) return juce::Result::fail (dm::U ("Dossier non accessible en écriture : ") + folder.getFullPathName());
    probe.deleteFile();

    std::map<juce::String, juce::File> copies;   // original full path -> collected copy
    std::array<juce::File, kNumPads> newSample, newIr;

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto meta = processor.getPadMeta (pad);

        auto handle = [&] (const juce::File& original, const char* sub, bool isIr, juce::File& target)
        {
            if (original == juce::File()) return;
            if (! original.existsAsFile()) { ++report.missing; report.problems.add (dm::U ("Introuvable (pad ") + juce::String (pad + 1) + ") : " + original.getFullPathName()); return; }
            const auto key = original.getFullPathName();
            auto it = copies.find (key);
            if (it == copies.end())
            {
                // already inside the destination folder: nothing to copy
                const auto dest = original.isAChildOf (folder) ? original : copyUnique (original, folder.getChildFile (sub), report);
                if (dest == juce::File()) return;
                it = copies.emplace (key, dest).first;
                ++(isIr ? report.irsCopied : report.samplesCopied);
            }
            target = it->second;
        };
        handle (meta.file, "Samples", false, newSample[(size_t) pad]);
        handle (meta.customIr, "IR", true, newIr[(size_t) pad]);

        if (const auto* t = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ids::rvType (pad))))
            if (t->getIndex() >= 1 && t->getIndex() <= 4) ++report.libraryIrs;
    }

    for (int pad = 0; pad < kNumPads; ++pad)
    {
            const auto meta = processor.getPadMeta (pad);
            if (newSample[(size_t) pad] != juce::File() && newSample[(size_t) pad] != meta.file)
                processor.loadSampleAsync (pad, newSample[(size_t) pad]);
            if (newIr[(size_t) pad] != juce::File() && newIr[(size_t) pad] != meta.customIr)
                processor.setCustomIr (pad, newIr[(size_t) pad]);
    }

    // the kit file: pads that could not be copied keep their original (absolute, resolved relative to the kit) path
    report.kitFile = folder.getChildFile (PresetManager::sanitizeFileName (kitName) + ".dmkit");
    const auto r = PresetManager (processor).saveKitTo (report.kitFile, kitName, {}, true);
    if (! r.wasOk()) return r;
    return juce::Result::ok();
}
} // namespace dm
