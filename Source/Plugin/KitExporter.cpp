#include "KitExporter.h"

#include "Presets.h"

namespace dm
{
std::vector<KitExportItem> KitExporter::prepare (DaftMugProcessor& proc, int bits, double sampleRate, bool normalize)
{
    std::vector<KitExportItem> items;
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto meta = proc.getPadMeta (pad);
        if (meta.sample == nullptr) continue;   // empty pads and failed loads are skipped

        KitExportItem it;
        it.pad = pad;
        auto name = PresetManager::sanitizeFileName (meta.name);
        if (name.isEmpty()) name = "Pad";
        it.baseName = juce::String (pad + 1).paddedLeft ('0', 2) + "_" + name;
        it.sample = meta.sample;
        it.params = proc.readPadParams (pad);
        it.options.bits = bits;
        it.options.sampleRate = sampleRate;
        it.options.normalize = normalize;
        it.options.reverb = proc.createReverbForExport (pad, sampleRate > 0.0 ? sampleRate : meta.sample->sampleRate);
        items.push_back (std::move (it));
    }
    return items;
}

KitExportReport KitExporter::run (const std::vector<KitExportItem>& items, const juce::File& folder)
{
    KitExportReport rep;
    if (! folder.createDirectory())
    {
        rep.failed = (int) items.size();
        rep.problems.add (dm::U ("Impossible de créer le dossier : ") + folder.getFullPathName());
        return rep;
    }
    for (const auto& it : items)
    {
        juce::File dest = folder.getChildFile (it.baseName + ".wav");
        for (int n = 2; dest.exists() && n < 1000; ++n) dest = folder.getChildFile (it.baseName + "_" + juce::String (n) + ".wav");
        const auto r = exportPadToWav (it.sample, it.params, it.options, dest);
        if (r.wasOk()) { ++rep.written; rep.files.add (dest.getFileName()); }
        else { ++rep.failed; rep.problems.add (juce::String (it.pad + 1) + " : " + r.getErrorMessage()); }
    }
    return rep;
}
} // namespace dm
