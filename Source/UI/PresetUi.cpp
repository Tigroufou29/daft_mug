#include "PresetUi.h"

#include "Presets.h"
#include "Collector.h"
#include "KitExporter.h"

namespace dm::ui
{
namespace
{
void message (juce::Component& owner, const juce::String& title, const juce::String& text)
{
    juce::ignoreUnused (owner);
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, text);
}

// Asynchronous dialog (no modal loop: safe inside a plugin host). `extraChoices` adds a combo box when not empty.
void askNameDialog (juce::Component& owner, const juce::String& title, const juce::String& text, const juce::String& defaultName,
                    const juce::StringArray& extraChoices, std::function<void (juce::String, juce::String, int)> onOk)
{
    auto* w = new juce::AlertWindow (title, text, juce::MessageBoxIconType::NoIcon, &owner);
    w->addTextEditor ("name", defaultName, "Nom");
    w->addTextEditor ("category", {}, dm::U ("Catégorie (ex. Kicks, Snares, FX) — facultatif"));
    if (! extraChoices.isEmpty())
        w->addComboBox ("extra", extraChoices, dm::U ("Contenu"));
    w->addButton ("Enregistrer", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int result)
    {
        if (result != 1) return;
        auto* combo = w->getComboBoxComponent ("extra");
        onOk (w->getTextEditorContents ("name"), w->getTextEditorContents ("category"), combo != nullptr ? combo->getSelectedItemIndex() : 0);
    }), true);
}

void addList (juce::PopupMenu& menu, const juce::Array<PresetInfo>& list, int idBase)
{
    juce::String current = "\x01";
    juce::PopupMenu sub;
    auto flush = [&] (const juce::String& cat)
    {
        if (cat != "\x01" && sub.getNumItems() > 0)
            menu.addSubMenu (cat.isEmpty() ? dm::U ("Sans catégorie") : cat, sub);
        sub = juce::PopupMenu();
    };
    for (int i = 0; i < list.size(); ++i)
    {
        if (list[i].category != current) { flush (current); current = list[i].category; }
        sub.addItem (idBase + i, list[i].name);
    }
    flush (current);
}

void confirmDelete (juce::Component& owner, const PresetInfo& info)
{
    juce::Component::SafePointer<juce::Component> safe (&owner);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (dm::U ("Supprimer"))
                                      .withMessage (dm::U ("Supprimer définitivement « ") + info.name + dm::U (" » ?"))
                                      .withButton ("Supprimer").withButton ("Annuler"),
                                  [info] (int r) { if (r == 1) info.file.deleteFile(); });
}

void runMenu (juce::Component& anchor, DaftMugProcessor& processor, PresetKind kind, int pad)
{
    auto manager = std::make_shared<PresetManager> (processor);
    const auto list = manager->list (kind);
    const bool isPad = kind == PresetKind::Pad;

    juce::PopupMenu menu, load, del;
    menu.addItem (1, isPad ? dm::U ("Enregistrer les réglages de ce pad…") : dm::U ("Enregistrer le kit actuel…"));
    if (list.isEmpty())
    {
        load.addItem (2, dm::U ("(aucun preset)"), false);
        del.addItem (3, dm::U ("(aucun preset)"), false);
    }
    else
    {
        addList (load, list, 100);
        addList (del, list, 1000);
    }
    menu.addSubMenu (isPad ? dm::U ("Charger dans ce pad") : dm::U ("Charger un kit"), load);
    menu.addSubMenu (dm::U ("Supprimer"), del);
    menu.addSeparator();
    menu.addItem (4, dm::U ("Ouvrir le dossier des presets"));
    if (! isPad)
    {
        menu.addItem (5, dm::U ("Charger un kit depuis un fichier…"));
        menu.addItem (6, dm::U ("Collecter les fichiers du kit dans un dossier…"));
        menu.addItem (7, dm::U ("Exporter tous les pads en WAV (24 bits)…"));
    }

    juce::Component::SafePointer<juce::Component> safe (&anchor);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safe, manager, list, kind, pad, isPad, proc = &processor] (int id)
                        {
                            if (safe == nullptr || id == 0) return;
                            auto& owner = *safe;
                            if (id == 1)
                            {
                                const juce::StringArray extra = isPad ? juce::StringArray { dm::U ("Réglages seuls"), dm::U ("Réglages + sample (chemin du fichier)") }
                                                                      : juce::StringArray();
                                askNameDialog (owner, isPad ? dm::U ("Enregistrer un preset de pad") : dm::U ("Enregistrer un kit"),
                                               isPad ? dm::U ("Réglages sonores de ce pad (la note MIDI n'en fait pas partie).")
                                                     : dm::U ("Les 16 pads : réglages, notes MIDI, samples (chemins), noms et couleurs."),
                                               {}, extra,
                                               [safe, manager, pad, isPad] (juce::String name, juce::String category, int content)
                                               {
                                                   if (safe == nullptr) return;
                                                   const auto r = isPad ? manager->savePad (pad, name, category, content == 1)
                                                                        : manager->saveKit (name, category);
                                                   message (*safe, dm::U ("Presets"), r.wasOk() ? dm::U ("Enregistré : ") + name : r.getErrorMessage());
                                               });
                            }
                            else if (id == 5 || id == 6 || id == 7)
                            {
                                auto chooser = std::make_shared<juce::FileChooser> (id == 5 ? dm::U ("Charger un kit") : (id == 6 ? dm::U ("Dossier de destination (sera créé s'il n'existe pas)") : dm::U ("Dossier d'export des WAV (les fichiers existants ne sont jamais écrasés)")),
                                                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                                                    id == 5 ? "*.dmkit" : "");
                                const auto flags = id == 5 ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                                           : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
                                chooser->launchAsync (flags, [safe, manager, chooser, id, proc] (const juce::FileChooser& fc)
                                {
                                    const auto f = fc.getResult();
                                    if (safe == nullptr || f == juce::File()) return;
                                    if (id == 5)
                                    {
                                        const auto r = manager->loadKit (f);
                                        if (! r.wasOk()) message (*safe, dm::U ("Kits"), r.getErrorMessage());
                                        return;
                                    }
                                    if (id == 7)
                                    {
                                        auto items = std::make_shared<std::vector<KitExportItem>> (KitExporter::prepare (*proc, 24, 0.0, false));
                                        if (items->empty()) { message (*safe, dm::U ("Export du kit"), dm::U ("Aucun pad chargé à exporter.")); return; }
                                        juce::Thread::launch ([safe, items, f]
                                        {
                                            const auto rep = KitExporter::run (*items, f);
                                            juce::MessageManager::callAsync ([safe, rep, f]
                                            {
                                                if (safe == nullptr) return;
                                                juce::String t;
                                                t << rep.written << dm::U (" fichier(s) WAV écrit(s) dans :\n") << f.getFullPathName();
                                                if (rep.failed > 0) t << "\n\n" << rep.failed << dm::U (" échec(s) :\n") << rep.problems.joinIntoString ("\n");
                                                message (*safe, dm::U ("Export du kit"), t);
                                            });
                                        });
                                        return;
                                    }
                                    {
                                        CollectReport rep;
                                        const auto r = Collector (*proc).collect (f, f.getFileName(), rep);
                                        juce::String text;
                                        if (! r.wasOk()) text = r.getErrorMessage();
                                        else
                                        {
                                            text << rep.samplesCopied << dm::U (" sample(s) et ") << rep.irsCopied << dm::U (" réponse(s) impulsionnelle(s) copiés dans :\n")
                                                 << f.getFullPathName() << "\n\n" << dm::U ("Kit portable : ") << rep.kitFile.getFileName();
                                            if (rep.renamed > 0) text << "\n" << rep.renamed << dm::U (" fichier(s) renommé(s) pour éviter un conflit de nom.");
                                            if (rep.libraryIrs > 0) text << "\n" << dm::U ("Les réverbérations de la bibliothèque (Plate, Hall…) ne sont pas copiées : elles viennent du dossier IR de l'autre machine.");
                                            if (rep.missing > 0) text << "\n\n" << dm::U ("Fichiers introuvables (laissés tels quels) :\n") << rep.problems.joinIntoString ("\n");
                                            else if (! rep.problems.isEmpty()) text << "\n\n" << rep.problems.joinIntoString ("\n");
                                            text << "\n\n" << dm::U ("Le projet pointe maintenant vers ces copies.");
                                        }
                                        message (*safe, dm::U ("Collecte du kit"), text);
                                    }
                                });
                            }
                            else if (id == 4)
                            {
                                auto d = manager->getFolder (kind);
                                d.createDirectory();
                                d.revealToUser();
                            }
                            else if (id >= 1000)
                                confirmDelete (owner, list[id - 1000]);
                            else if (id >= 100)
                            {
                                const auto& info = list[id - 100];
                                const auto r = isPad ? manager->loadPad (pad, info.file) : manager->loadKit (info.file);
                                if (! r.wasOk())
                                    message (owner, dm::U ("Presets"), r.getErrorMessage());
                            }
                        });
}
} // namespace

void showPadPresetMenu (juce::Component& anchor, DaftMugProcessor& processor, int pad) { runMenu (anchor, processor, PresetKind::Pad, pad); }
void showKitPresetMenu (juce::Component& anchor, DaftMugProcessor& processor) { runMenu (anchor, processor, PresetKind::Kit, 0); }
} // namespace dm::ui
