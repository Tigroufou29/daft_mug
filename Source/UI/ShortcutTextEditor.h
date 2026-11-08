#pragma once

#include "JuceIncludes.h"

namespace dm::ui
{
// TextEditor with explicit "select all": Ctrl+A on Windows / Linux, Cmd+A on macOS
// (juce::ModifierKeys::isCommandDown() is Ctrl on Windows and Cmd on macOS). Clicking into the field selects its whole text,
// so a pad can be renamed by simply typing.
class ShortcutTextEditor : public juce::TextEditor
{
public:
    ShortcutTextEditor() { setSelectAllWhenFocused (true); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        if (key.getModifiers().isCommandDown() && ! key.getModifiers().isShiftDown() && (code == 'A' || code == 'a'))
        {
            selectAll();
            return true;
        }
        return juce::TextEditor::keyPressed (key);
    }
};
} // namespace dm::ui
