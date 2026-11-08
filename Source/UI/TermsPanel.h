#pragma once

#include <functional>

#include "JuceIncludes.h"
#include "ShortcutTextEditor.h"

namespace dm::ui
{
// Terms of use / privacy page, reachable from Options > IA et confidentialité. The text below is a DRAFT that must be
// reviewed (and completed: publisher, licence, contact) before any distribution; a check box alone does not settle legal duties.
class TermsPanel : public juce::Component
{
public:
    TermsPanel();
    std::function<void()> onBack;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::String getText();

private:
    juce::TextButton backButton { juce::String::fromUTF8 ("\xE2\x86\x90  Retour") };
    ShortcutTextEditor body;
};
} // namespace dm::ui
