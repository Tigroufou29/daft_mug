#include "Presets.h"

#include "ParameterIds.h"

namespace dm
{
namespace
{
juce::String padPrefix (int pad) { return "pad" + juce::String (pad + 1).paddedLeft ('0', 2) + "_"; }
bool isNoteId (const juce::String& suffix) { return suffix == "note"; }
const char* extensionFor (PresetKind k) { return k == PresetKind::Pad ? ".dmpad" : ".dmkit"; }
}

juce::String PresetManager::sanitizeFileName (const juce::String& name)
{
    auto s = name.trim().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()àâäçéèêëîïôöùûüÀÉÈ").trim();
    while (s.startsWithChar ('.')) s = s.substring (1);
    return s.substring (0, 80);
}

juce::File PresetManager::getFolder (PresetKind kind) const
{
    auto root = juce::SystemStats::getEnvironmentVariable ("DAFTMUG_PRESET_DIR", {});
    juce::File base = root.isNotEmpty() ? juce::File (root)
                                        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DaftMug").getChildFile ("Presets");
    return base.getChildFile (kind == PresetKind::Pad ? "Pads" : "Kits");
}

juce::File PresetManager::fileFor (PresetKind kind, const juce::String& name, const juce::String& category) const
{
    auto dir = getFolder (kind);
    const auto cat = sanitizeFileName (category);
    if (cat.isNotEmpty()) dir = dir.getChildFile (cat);
    return dir.getChildFile (sanitizeFileName (name) + extensionFor (kind));
}

juce::Array<PresetInfo> PresetManager::list (PresetKind kind) const
{
    juce::Array<PresetInfo> out;
    const auto dir = getFolder (kind);
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, juce::String ("*") + extensionFor (kind)))
    {
        PresetInfo info;
        info.file = f;
        info.name = f.getFileNameWithoutExtension();
        if (auto xml = juce::parseXML (f))
        {
            info.name = xml->getStringAttribute ("name", info.name);
            info.category = xml->getStringAttribute ("category");
        }
        out.add (info);
    }
    struct Sorter { static int compareElements (const PresetInfo& a, const PresetInfo& b)
    {
        const int c = a.category.compareIgnoreCase (b.category);
        return c != 0 ? c : a.name.compareIgnoreCase (b.name);
    } } sorter;
    out.sort (sorter);
    return out;
}

void PresetManager::writeParams (juce::XmlElement& parent, const juce::String& prefix, bool withNote) const
{
    for (auto* p : processor.getParameters())
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = prm->paramID;
            if (! id.startsWith (prefix)) continue;
            const auto suffix = id.substring (prefix.length());
            if (! withNote && isNoteId (suffix)) continue;
            auto* e = parent.createNewChildElement ("PARAM");
            e->setAttribute ("id", suffix);
            e->setAttribute ("value", (double) prm->convertFrom0to1 (prm->getValue()));
        }
}

int PresetManager::readParams (const juce::XmlElement& parent, const juce::String& prefix, bool withNote)
{
    int applied = 0;
    for (auto* e : parent.getChildWithTagNameIterator ("PARAM"))
    {
        const auto suffix = e->getStringAttribute ("id");
        if (suffix.isEmpty() || (! withNote && isNoteId (suffix))) continue;
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (prefix + suffix)))   // unknown ids are ignored
        {
            const auto range = prm->getNormalisableRange();
            const float v = juce::jlimit (range.start, range.end, (float) e->getDoubleAttribute ("value", (double) range.start));
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
            ++applied;
        }
    }
    return applied;
}

juce::Result PresetManager::savePad (int pad, const juce::String& name, const juce::String& category, bool includeSample)
{
    if (pad < 0 || pad >= kNumPads) return juce::Result::fail ("Pad invalide");
    if (sanitizeFileName (name).isEmpty()) return juce::Result::fail (dm::U ("Nom de preset invalide"));

    juce::XmlElement root ("DAFTMUG_PAD_PRESET");
    root.setAttribute ("schema", kPresetSchemaVersion);
    root.setAttribute ("name", name.trim());
    root.setAttribute ("category", category.trim());
    writeParams (root, padPrefix (pad), false);

    const auto meta = processor.getPadMeta (pad);
    if (meta.customIr != juce::File())
        root.createNewChildElement ("IR")->setAttribute ("file", meta.customIr.getFullPathName());
    if (includeSample && meta.file != juce::File())
    {
        auto* s = root.createNewChildElement ("SAMPLE");
        s->setAttribute ("file", meta.file.getFullPathName());
        s->setAttribute ("name", meta.name);
        s->setAttribute ("renamed", meta.userRenamed);
    }

    const auto file = fileFor (PresetKind::Pad, name, category);
    if (! file.getParentDirectory().createDirectory()) return juce::Result::fail (dm::U ("Impossible de créer le dossier des presets"));
    if (! root.writeTo (file)) return juce::Result::fail (dm::U ("Écriture impossible : ") + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result PresetManager::loadPad (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= kNumPads) return juce::Result::fail ("Pad invalide");
    if (file.getSize() > 4 * 1024 * 1024) return juce::Result::fail (dm::U ("Fichier trop volumineux pour un preset"));
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("DAFTMUG_PAD_PRESET")) return juce::Result::fail (dm::U ("Fichier de preset illisible"));
    if (xml->getIntAttribute ("schema", 0) > kPresetSchemaVersion) return juce::Result::fail (dm::U ("Preset créé par une version plus récente de Daft Mug"));

    readParams (*xml, padPrefix (pad), false);
    if (auto* ir = xml->getChildByName ("IR"))
        processor.setCustomIr (pad, juce::File (ir->getStringAttribute ("file")));
    if (auto* s = xml->getChildByName ("SAMPLE"))
    {
        const juce::File sample (s->getStringAttribute ("file"));
        if (sample.existsAsFile())
        {
            processor.loadSampleAsync (pad, sample);
            if (s->getBoolAttribute ("renamed")) processor.setPadName (pad, s->getStringAttribute ("name"));
        }
        else
            return juce::Result::fail (dm::U ("Preset chargé, mais son sample est introuvable : ") + sample.getFileName());
    }
    return juce::Result::ok();
}

juce::Result PresetManager::saveKit (const juce::String& name, const juce::String& category)
{
    if (sanitizeFileName (name).isEmpty()) return juce::Result::fail (dm::U ("Nom de kit invalide"));
    return saveKitTo (fileFor (PresetKind::Kit, name, category), name, category, false);
}

juce::Result PresetManager::saveKitTo (const juce::File& file, const juce::String& name, const juce::String& category, bool relativePaths)
{
    if (sanitizeFileName (name).isEmpty()) return juce::Result::fail (dm::U ("Nom de kit invalide"));
    const auto baseDir = file.getParentDirectory();
    auto pathFor = [&] (const juce::File& f)
    {
        if (f == juce::File()) return juce::String();
        return relativePaths ? f.getRelativePathFrom (baseDir).replaceCharacter ('\\', '/') : f.getFullPathName();
    };

    juce::XmlElement root ("DAFTMUG_KIT");
    root.setAttribute ("schema", kPresetSchemaVersion);
    root.setAttribute ("name", name.trim());
    root.setAttribute ("category", category.trim());
    writeParams (root, "global_", true);
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        auto* e = root.createNewChildElement ("PAD");
        e->setAttribute ("index", pad);
        const auto meta = processor.getPadMeta (pad);
        e->setAttribute ("name", meta.name);
        e->setAttribute ("renamed", meta.userRenamed);
        e->setAttribute ("colour", juce::String::toHexString ((int) meta.colour.getARGB()));
        e->setAttribute ("file", pathFor (meta.file));
        e->setAttribute ("ir", pathFor (meta.customIr));
        writeParams (*e, padPrefix (pad), true);
    }

    if (! file.getParentDirectory().createDirectory()) return juce::Result::fail (dm::U ("Impossible de créer le dossier des presets"));
    if (! root.writeTo (file)) return juce::Result::fail (dm::U ("Écriture impossible : ") + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result PresetManager::loadKit (const juce::File& file)
{
    if (file.getSize() > 4 * 1024 * 1024) return juce::Result::fail (dm::U ("Fichier trop volumineux pour un kit"));
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("DAFTMUG_KIT")) return juce::Result::fail (dm::U ("Fichier de kit illisible"));
    if (xml->getIntAttribute ("schema", 0) > kPresetSchemaVersion) return juce::Result::fail (dm::U ("Kit créé par une version plus récente de Daft Mug"));

    readParams (*xml, "global_", true);
    int missing = 0;
    for (auto* e : xml->getChildWithTagNameIterator ("PAD"))
    {
        const int pad = e->getIntAttribute ("index", -1);
        if (pad < 0 || pad >= kNumPads) continue;
        readParams (*e, padPrefix (pad), true);

        if (e->hasAttribute ("colour"))
        {
            juce::Colour c ((juce::uint32) e->getStringAttribute ("colour").getHexValue64());
            if (! c.isTransparent()) processor.setPadColour (pad, c);
        }
        const auto baseDir = file.getParentDirectory();
        auto resolve = [&] (const juce::String& path) -> juce::File
        {
            if (path.isEmpty()) return {};
            if (juce::File::isAbsolutePath (path))
            {
                const juce::File f (path);
                if (f.exists()) return f;
                // absolute path from another machine: look for the same file name next to the kit (collected projects)
                for (const char* sub : { "Samples", "IR", "" })
                {
                    const auto alt = baseDir.getChildFile (sub).getChildFile (f.getFileName());
                    if (alt.existsAsFile()) return alt;
                }
                return f;
            }
            return baseDir.getChildFile (path);
        };
        const juce::File ir = resolve (e->getStringAttribute ("ir"));
        processor.setCustomIr (pad, ir);

        const juce::File sample = resolve (e->getStringAttribute ("file"));
        if (sample.getFullPathName().isEmpty())
            processor.clearPad (pad);
        else
        {
            if (! sample.existsAsFile()) ++missing;
            // reset the "renamed" flag of the previous content first, so auto-naming applies unless the kit stored a user name
            processor.setPadName (pad, e->getBoolAttribute ("renamed") ? e->getStringAttribute ("name") : juce::String());
            processor.loadSampleAsync (pad, sample);   // a missing file is reported on the pad itself
        }
    }
    return missing > 0 ? juce::Result::fail (juce::String (missing) + dm::U (" sample(s) du kit sont introuvables (signalés sur les pads)"))
                       : juce::Result::ok();
}
} // namespace dm
