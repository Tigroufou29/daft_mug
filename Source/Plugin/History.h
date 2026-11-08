#pragma once

#include <functional>
#include <map>

#include "JuceIncludes.h"

namespace dm
{
// Global undo / redo (message thread only).
//
// Two kinds of entries:
//  - parameter edits made in the interface: every host gesture (slider drag, combo choice, button, waveform handle...)
//    becomes ONE entry (value before -> value after). Changes that come from the host (automation) have no gesture
//    and are never recorded, so Ctrl+Z never fights the host.
//  - pad actions (rename, colour, sample load / clear, tempo) recorded by the processor with their own undo / redo.
// Entries made within 350 ms of each other (e.g. applying a preset or a Mutate variant writes many parameters) form
// ONE step. The history is emptied when a whole project / kit is restored.
class History : private juce::AudioProcessorParameter::Listener
{
public:
    explicit History (juce::AudioProcessor& p) : processor (p)
    {
        for (auto* prm : processor.getParameters())
            prm->addListener (this);
    }

    ~History() override
    {
        for (auto* prm : processor.getParameters())
            prm->removeListener (this);
    }

    // ---- recording ----
    // The change has ALREADY been made: only the two directions are stored.
    void record (const juce::String& label, std::function<void()> undoFn, std::function<void()> redoFn)
    {
        if (applying || suppressed > 0 || ! juce::MessageManager::existsAndIsCurrentThread()) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (! hasRecorded || now - lastRecordMs > 350u)
            manager.beginNewTransaction (label);
        lastRecordMs = now;
        hasRecorded = true;
        manager.perform (new Entry (std::move (undoFn), std::move (redoFn)), label);
        notify();
    }

    // while alive, nothing is recorded (internal changes that have their own undo, project restore...)
    struct Suppress
    {
        explicit Suppress (History& h) : owner (h) { ++owner.suppressed; }
        ~Suppress() { --owner.suppressed; }
        History& owner;
    };

    // ---- use ----
    bool isRecording() const { return ! applying && suppressed == 0; }
    bool canUndo() const { return manager.canUndo(); }
    bool canRedo() const { return manager.canRedo(); }
    juce::String undoLabel() const { return manager.getUndoDescription(); }
    juce::String redoLabel() const { return manager.getRedoDescription(); }

    bool undo()
    {
        if (! manager.canUndo()) return false;
        const juce::ScopedValueSetter<bool> guard (applying, true);
        const bool ok = manager.undo();
        hasRecorded = false;
        notify();
        return ok;
    }

    bool redo()
    {
        if (! manager.canRedo()) return false;
        const juce::ScopedValueSetter<bool> guard (applying, true);
        const bool ok = manager.redo();
        hasRecorded = false;
        notify();
        return ok;
    }

    void clear()
    {
        manager.clearUndoHistory();
        gestures.clear();
        hasRecorded = false;
        notify();
    }

    std::function<void()> onChange;   // called after every record / undo / redo / clear

private:
    struct Entry : juce::UndoableAction
    {
        Entry (std::function<void()> u, std::function<void()> r) : undoFn (std::move (u)), redoFn (std::move (r)) {}
        bool perform() override { if (first) { first = false; return true; } redoFn(); return true; }
        bool undo() override { undoFn(); return true; }
        std::function<void()> undoFn, redoFn;
        bool first = true;
    };

    struct Gesture { int depth = 0; float startValue = 0.0f; };

    void notify() { if (onChange) onChange(); }

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int index, bool starting) override
    {
        if (applying || suppressed > 0 || ! juce::MessageManager::existsAndIsCurrentThread()) return;
        auto* prm = index >= 0 && index < processor.getParameters().size() ? processor.getParameters()[index] : nullptr;
        if (prm == nullptr) return;

        auto& g = gestures[index];
        if (starting)
        {
            if (g.depth++ == 0) g.startValue = prm->getValue();
            return;
        }
        if (g.depth == 0 || --g.depth > 0) return;

        const float before = g.startValue, after = prm->getValue();
        if (std::abs (after - before) < 1.0e-6f) return;

        auto setTo = [prm, this] (float v)
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (v);
            prm->endChangeGesture();
        };
        record (prm->getName (40), [setTo, before] { setTo (before); }, [setTo, after] { setTo (after); });
    }

    juce::AudioProcessor& processor;
    juce::UndoManager manager { 30000, 80 };
    std::map<int, Gesture> gestures;
    bool applying = false;
    int suppressed = 0;
    bool hasRecorded = false;
    juce::uint32 lastRecordMs = 0;
};
} // namespace dm
