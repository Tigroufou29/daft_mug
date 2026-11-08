#include "PadComponent.h"

#include "LiquidGlassLookAndFeel.h"
#include "ParameterIds.h"

namespace dm::ui
{
using namespace theme;

// ---------------------------------------------------------------------------- GearButton
GearButton::GearButton (Icon i) : icon (i)
{
    gear = makeGearPath (8.5f);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (icon == Icon::Gear ? dm::U ("Ouvrir l'éditeur de ce pad")
                                   : dm::U ("Importer un sample dans ce pad…"));
}

void GearButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (hover ? 0.45f : 0.30f));
    g.fillEllipse (b);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.55f : 0.22f));
    g.drawEllipse (b, 1.0f);
    g.setColour (hover ? juce::Colours::white : text.withAlpha (0.85f));
    if (icon == Icon::Gear)
    {
        g.fillPath (gear, juce::AffineTransform::translation (b.getCentreX(), b.getCentreY()));
        return;
    }

    // folder: back tab + body
    const auto f = juce::Rectangle<float> (13.0f, 10.0f).withCentre (b.getCentre().translated (0.0f, 0.5f));
    juce::Path p;
    p.addRoundedRectangle (f.getX(), f.getY(), 5.5f, 3.5f, 1.2f);
    p.addRoundedRectangle (f.getX(), f.getY() + 2.0f, f.getWidth(), f.getHeight() - 2.0f, 1.6f);
    g.fillPath (p);
}

void GearButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && contains (e.getPosition()) && onClick)
        onClick();
}

// ---------------------------------------------------------------------------- PadComponent
bool PadComponent::isSupportedFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}

PadComponent::PadComponent (DaftMugProcessor& p, int padIndex)
    : processor (p), index (padIndex)
{
    setOpaque (false);
    addAndMakeVisible (gear);
    gear.onClick = [this] { if (onOpenEditor) onOpenEditor (index); };
    addAndMakeVisible (folder);
    folder.onClick = [this] { if (renameBox == nullptr) chooseFile(); };
    refreshFromModel();
}

void PadComponent::resized()
{
    constexpr int size = 26;
    gear.setBounds (8, getHeight() - size - 8, size, size);   // bottom-left corner
    folder.setBounds (8 + size + 6, getHeight() - size - 8, size, size);
    if (renameBox != nullptr)
        renameBox->setBounds (getLocalBounds().withSizeKeepingCentre (getWidth() - 16, 28));
}

void PadComponent::refreshFromModel()
{
    meta = processor.getPadMeta (index);
    setTooltip (meta.state == LoadState::Error
                    ? meta.error
                    : (meta.name.isNotEmpty() ? meta.name : juce::String ("Pad vide")));
    repaint();
}

void PadComponent::setSelected (bool s)
{
    if (selected != s) { selected = s; repaint(); }
}

void PadComponent::tickVisuals (const SamplerEngine& engine)
{
    bool needsRepaint = false;

    const auto count = engine.padTriggerCount (index);
    if (count != lastTriggerCount)
    {
        lastTriggerCount = count;
        flash = 1.0f;
        needsRepaint = true;
    }
    else if (flash > 0.0f)
    {
        flash = juce::jmax (0.0f, flash - 0.07f);
        needsRepaint = true;
    }

    const bool nowPlaying = engine.isPadPlaying (index);
    const float head = nowPlaying ? engine.padPlayhead (index) : -1.0f;
    if (nowPlaying != playing || std::abs (head - playhead) > 0.002f)
    {
        playing = nowPlaying;
        playhead = head;
        needsRepaint = true;
    }

    if (meta.state == LoadState::Loading)
    {
        pulse += 0.2f;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

namespace
{
constexpr float kPadRadius = 11.0f;   // hardware pads are squarer than glass tiles

// One tile of fine rubber grain (fixed seed: the same look every run). Dark pits + a few light specks.
const juce::Image& rubberGrain()
{
    static const juce::Image tile = []
    {
        juce::Image img (juce::Image::ARGB, 160, 160, true);
        juce::Random rnd (20260);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const float r = rnd.nextFloat();
                if (r < 0.46f)       img.setPixelAt (x, y, juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.10f + 0.40f * rnd.nextFloat()));
                else if (r < 0.90f)  img.setPixelAt (x, y, juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.015f + 0.055f * rnd.nextFloat()));
                else if (r < 0.985f) img.setPixelAt (x, y, juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.07f + 0.10f * rnd.nextFloat()));
                // else: untouched
            }
        return img;
    }();
    return tile;
}
} // namespace

void PadComponent::buildRubber()
{
    const auto w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    const auto b = getLocalBounds().toFloat().reduced (2.0f);

    // layer 1: contact shadow + matte black rubber body
    rubberBase = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (rubberBase);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (0.0f, 3.0f).expanded (1.0f), kPadRadius + 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (b.translated (0.0f, 1.5f), kPadRadius);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff25252a), b.getX(), b.getY(),
                                                 juce::Colour (0xff0b0b0d), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, kPadRadius);
    }

    // layer 2: grain, soft centre sheen, bevel (lit from above), outer rim
    rubberTop = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (rubberTop);
        juce::Path shape;
        shape.addRoundedRectangle (b, kPadRadius);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (shape);   // grain and sheen only inside the rounded pad
            g.setTiledImageFill (rubberGrain(), 0, 0, 1.0f);
            g.fillAll();
            // soft sheen: slightly lighter toward the top centre, darker toward the edges
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), b.getCentreX(), b.getY() + b.getHeight() * 0.28f,
                                                     juce::Colours::black.withAlpha (0.28f), b.getCentreX(), b.getBottom() + b.getHeight() * 0.25f, true));
            g.fillAll();
        }

        // bevel: bright on the top edge, dark on the bottom edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), 0.0f, b.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, b.getY() + 18.0f, false));
        g.drawRoundedRectangle (b.reduced (0.75f), kPadRadius - 0.75f, 1.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, b.getBottom() - 22.0f,
                                                 juce::Colours::black.withAlpha (0.55f), 0.0f, b.getBottom(), false));
        g.drawRoundedRectangle (b.reduced (0.75f), kPadRadius - 0.75f, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.drawRoundedRectangle (b, kPadRadius, 1.2f);
    }
}

void PadComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float radius = kPadRadius;
    const bool loaded = meta.state == LoadState::Loaded;
    const auto colour = meta.colour;

    if (rubberBase.getWidth() != getWidth() || rubberBase.getHeight() != getHeight())
        buildRubber();

    g.drawImageAt (rubberBase, 0, 0);

    // trigger flash + glow while playing: the pad lights up from underneath, through the rubber
    // Each hit shifts the light to a clearly different, brighter colour that fades back.
    const float glow = juce::jmax (flash, playing ? 0.45f : 0.0f);
    if (glow > 0.0f)
    {
        const auto hit = colour.withRotatedHue (0.38f).withSaturation (juce::jmax (0.55f, colour.getSaturation())).brighter (0.5f);
        g.setGradientFill (juce::ColourGradient (hit.withAlpha (0.62f * glow), b.getCentreX(), b.getCentreY(),
                                                 hit.darker (0.6f).withAlpha (0.40f * glow), b.getX(), b.getY(), true));
        g.fillRoundedRectangle (b, radius);
    }

    g.drawImageAt (rubberTop, 0, 0);   // grain and bevel stay visible over the light

    if (glow > 0.0f)
    {
        const auto hit = colour.withRotatedHue (0.38f).withSaturation (juce::jmax (0.55f, colour.getSaturation())).brighter (0.9f);
        g.setColour (hit.withAlpha (juce::jmin (1.0f, 1.2f * glow)));
        g.drawRoundedRectangle (b.reduced (0.5f), radius, 2.0f);
    }

    // the pad's colour is kept as a small LED strip under the rubber (the pad itself stays black)
    {
        const auto strip = juce::Rectangle<float> (b.getCentreX() - 22.0f, b.getBottom() - 9.0f, 44.0f, 3.0f);
        g.setColour (colour.withAlpha (loaded ? 0.95f : 0.30f));
        g.fillRoundedRectangle (strip, 1.5f);
        if (loaded)
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillRoundedRectangle (strip.expanded (2.0f, 2.0f), 3.0f);
        }
    }

    if (pressed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.28f));   // pressed in
        g.fillRoundedRectangle (b, radius);
    }

    if (selected)
    {
        g.setColour (accent2.withAlpha (0.9f));
        g.drawRoundedRectangle (b.expanded (1.5f), radius + 1.0f, 2.0f);
    }
    if (dragOver)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.fillRoundedRectangle (b, radius);
        g.setColour (accent);
        g.drawRoundedRectangle (b.reduced (1.0f), radius, 2.0f);
    }

    // pad number
    g.setColour (text.withAlpha (0.75f));
    g.setFont (makeFont (13.0f, true));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), b.toNearestInt().removeFromTop (30).withTrimmedLeft (14),
                juce::Justification::centredLeft);

    // load-state LED
    juce::Colour led = juce::Colours::grey;
    switch (meta.state)
    {
        case LoadState::Empty: led = juce::Colours::white.withAlpha (0.25f); break;
        case LoadState::Loading: led = ledBusy.withAlpha (0.5f + 0.5f * std::sin (pulse)); break;
        case LoadState::Loaded: led = ledOk; break;
        case LoadState::Error: led = ledError; break;
    }
    const auto ledArea = juce::Rectangle<float> (b.getRight() - 22.0f, b.getY() + 12.0f, 10.0f, 10.0f);
    g.setColour (led.withAlpha (0.35f));
    g.fillEllipse (ledArea.expanded (3.0f));
    g.setColour (led);
    g.fillEllipse (ledArea);

    // name (truncated with an ellipsis; the full name is in the tooltip)
    const auto nameArea = b.toNearestInt().reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (34);
    if (meta.state == LoadState::Error)
    {
        g.setColour (ledError);
        g.setFont (makeFont (13.0f, true));
        g.drawFittedText ("Erreur\n" + meta.error, nameArea, juce::Justification::centred, 3);
    }
    else if (meta.name.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (makeFont (16.0f, true));
        g.drawText (meta.name, nameArea, juce::Justification::centred, true);
    }
    else
    {
        g.setColour (textDim);
        g.setFont (makeFont (13.0f));
        g.drawFittedText (meta.state == LoadState::Loading ? dm::U ("Chargement…")
                                                           : dm::U ("Vide\nglisser un fichier ou\ncliquer sur l'icône dossier"),
                          nameArea, juce::Justification::centred, 3);
    }

    // MIDI note, bottom-right
    if (auto* param = processor.apvts.getRawParameterValue (ids::note (index)))
    {
        const int note = juce::roundToInt (param->load());
        g.setColour (textDim);
        g.setFont (makeFont (12.0f));
        g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                    b.toNearestInt().removeFromBottom (30).withTrimmedRight (14), juce::Justification::centredRight);
    }

    // playhead
    if (playing && playhead >= 0.0f)
    {
        const auto bar = juce::Rectangle<float> (b.getX() + 14.0f, b.getBottom() - 7.0f, b.getWidth() - 28.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * playhead), 1.5f);
    }
}

void PadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (renameBox != nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    if (onSelected) onSelected (index);

    emptyClick = (meta.state == LoadState::Empty);   // an empty pad does nothing: use the folder icon or drag & drop
    if (! emptyClick)
        processor.triggerPad (index, 100.0f / 127.0f);   // velocity 100 for mouse hits

    pressed = true;
    repaint();
}

void PadComponent::mouseUp (const juce::MouseEvent&)
{
    if (! pressed)
        return;
    pressed = false;
    repaint();

    if (emptyClick)
    {
        emptyClick = false;
    }
    else
    {
        processor.releasePad (index);   // ends the note in Gate mode
    }
}

void PadComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        beginRename();
}

void PadComponent::beginRename()
{
    if (renameBox != nullptr)
        return;
    renameBox = std::make_unique<ShortcutTextEditor>();
    renameBox->setJustification (juce::Justification::centred);
    renameBox->setFont (makeFont (15.0f, true));
    renameBox->setText (meta.name, false);
    renameBox->setSelectAllWhenFocused (true);
    renameBox->onReturnKey = [this] { commitRename(); };
    renameBox->onEscapeKey = [this] { renameBox.reset(); repaint(); };
    renameBox->onFocusLost = [this] { commitRename(); };
    addAndMakeVisible (*renameBox);
    resized();
    renameBox->grabKeyboardFocus();
}

void PadComponent::commitRename()
{
    if (renameBox == nullptr)
        return;
    const auto newName = renameBox->getText();
    renameBox->onFocusLost = nullptr;   // hiding the box below would otherwise re-enter this function
    renameBox->onReturnKey = nullptr;
    // Defer: destroying the editor from inside its own callback is not safe.
    renameBox->setVisible (false);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PadComponent> (this), newName]
    {
        if (safe == nullptr) return;
        safe->renameBox.reset();
        safe->processor.setPadName (safe->index, newName);
    });
}

void PadComponent::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choisir un sample pour le pad " + juce::String (index + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<PadComponent> (this)] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();
                              if (safe != nullptr && result.existsAsFile())
                                  safe->processor.loadSampleAsync (safe->index, result);
                          });
}

void PadComponent::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, dm::U ("Charger un sample…"));
    menu.addItem (2, dm::U ("Éditer ce pad (rouage)"));
    menu.addItem (3, "Renommer");
    menu.addItem (4, "Vider le pad", meta.state != LoadState::Empty);
    menu.addItem (5, dm::U ("Découper en pads…"), meta.state == LoadState::Loaded);
    menu.addItem (6, dm::U ("Mutate (variantes de réglages)…"), meta.state == LoadState::Loaded);
    menu.addSeparator();

    juce::PopupMenu colours;
    for (int i = 0; i < 16; ++i)
    {
        juce::PopupMenu::Item item ("Couleur " + juce::String (i + 1));
        item.itemID = 100 + i;
        item.colour = DaftMugProcessor::defaultPadColour (i);
        item.isTicked = (meta.colour == DaftMugProcessor::defaultPadColour (i));
        colours.addItem (item);
    }
    menu.addSubMenu ("Couleur", colours);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<PadComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) safe->chooseFile();
                            else if (result == 2) { if (safe->onOpenEditor) safe->onOpenEditor (safe->index); }
                            else if (result == 3) safe->beginRename();
                            else if (result == 4) safe->processor.clearPad (safe->index);
                            else if (result == 5) { if (safe->onSlice) safe->onSlice (safe->index); }
                            else if (result == 6) { if (safe->onMutate) safe->onMutate (safe->index); }
                            else if (result >= 100) safe->processor.setPadColour (safe->index, DaftMugProcessor::defaultPadColour (result - 100));
                        });
}

bool PadComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isSupportedFile (juce::File (f)))
            return true;
    return false;
}

void PadComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
        if (isSupportedFile (juce::File (f)))
        {
            processor.loadSampleAsync (index, juce::File (f));
            break;   // one file per pad
        }
}
} // namespace dm::ui
