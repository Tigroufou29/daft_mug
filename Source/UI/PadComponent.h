#pragma once

#include <functional>
#include <memory>

#include "JuceIncludes.h"
#include "ShortcutTextEditor.h"
#include "PluginProcessor.h"

namespace dm::ui
{
// Small gear button in the bottom-left corner of a pad. It is a CHILD component of the pad,
// so a click on it never reaches the pad's own mouse handler (= never triggers the sound).
class GearButton : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    enum class Icon { Gear, Folder };
    explicit GearButton (Icon icon = Icon::Gear);
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool hover = false;
    Icon icon;
    juce::Path gear;
};

class PadComponent : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::SettableTooltipClient
{
public:
    PadComponent (DaftMugProcessor&, int padIndex);

    void refreshFromModel();   // re-read name / colour / state after a model change
    void tickVisuals (const SamplerEngine&);   // called ~30 Hz: trigger flash, playhead
    void setSelected (bool shouldBeSelected);
    int getIndex() const noexcept { return index; }
    void beginRename();

    std::function<void (int)> onOpenEditor;
    std::function<void (int)> onSelected;
    std::function<void (int)> onSlice;          // "Découper en pads…"
    std::function<void (int)> onMutate;         // "Mutate…"

    // juce::Component
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    static bool isSupportedFile (const juce::File&);

private:
    void showContextMenu();
    void chooseFile();
    void commitRename();

    DaftMugProcessor& processor;
    const int index;
    PadMeta meta;
    GearButton gear;
    GearButton folder { GearButton::Icon::Folder };   // bottom-left, next to the gear: import a sample
    std::unique_ptr<ShortcutTextEditor> renameBox;
    std::unique_ptr<juce::FileChooser> chooser;

    // rubber-pad look: two cached layers (shadow + black body, then grain + bevel on top), rebuilt on resize
    juce::Image rubberBase, rubberTop;
    void buildRubber();

    bool selected = false;
    bool pressed = false;
    bool dragOver = false;
    bool emptyClick = false;
    bool playing = false;
    float flash = 0.0f;
    float playhead = -1.0f;
    uint32_t lastTriggerCount = 0;
    float pulse = 0.0f;
};
} // namespace dm::ui
