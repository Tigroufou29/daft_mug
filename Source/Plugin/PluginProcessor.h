#pragma once

#include <array>
#include <atomic>

#include <map>

#include "JuceIncludes.h"
#include "PadState.h"
#include "Mutate.h"
#include <memory>
#include "SamplerEngine.h"
#include "Limiter.h"
#include "History.h"
#include <optional>

namespace dm
{
namespace ai { class AiService; }

constexpr int kStateSchemaVersion = 1;
constexpr double kMaxSampleSeconds = 300.0;

class DaftMugProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster
{
public:
    DaftMugProcessor();
    ~DaftMugProcessor() override;

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Daft Mug"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- pads (message thread) ----
    PadMeta getPadMeta (int pad) const;
    void loadSampleAsync (int pad, const juce::File& file);
    void clearPad (int pad);

    // ---- tempo (message thread) ----
    // Original tempo (detected at load, editable) and target tempo: the pad then plays a time-stretched copy (pitch unchanged).
    // target <= 0 or original <= 0 = original audio. The file is never modified.
    void setPadTempo (int pad, double originalBpm, double targetBpm);
    void redetectTempo (int pad);
    double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }   // 0 = host gives none
    // ---- resources (any thread) ----
    float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }    // share of the audio time budget used by Daft Mug, 0..1
    size_t getSampleMemoryBytes() const;                                                       // decoded samples held in memory
    void setPadName (int pad, const juce::String& newName);   // marks the pad as user-renamed
    void setPadColour (int pad, juce::Colour colour);
    static juce::Colour defaultPadColour (int pad);

    void triggerPad (int pad, float velocity);
    void releasePad (int pad);
    void housekeeping();   // call from a UI timer: frees retired samples

    // ---- MIDI learn ----
    void startMidiLearn (int pad) { learnPad.store (pad); }
    void cancelMidiLearn() { learnPad.store (-1); }
    int getMidiLearnPad() const { return learnPad.load(); }
    bool consumeLearnedNote (int& pad, int& note);   // polled by the UI

    const SamplerEngine& getEngine() const { return engine; }
    void setAnalyzerPad (int pad) noexcept { engine.setAnalyzerPad (pad); }   // which pad feeds the EQ spectrum analyser (-1 = none)
    // ---- master output level (any thread): true peak-ish sample peak since the last call, and RMS (300 ms integration) ----
    struct MasterLevels { float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0; };   // linear, 1.0 = 0 dBFS
    MasterLevels readMasterLevels() noexcept
    {
        MasterLevels l;
        l.peakL = meterPeakL.exchange (0.0f, std::memory_order_relaxed);
        l.peakR = meterPeakR.exchange (0.0f, std::memory_order_relaxed);
        l.rmsL = std::sqrt (meterMsL.load (std::memory_order_relaxed));
        l.rmsR = std::sqrt (meterMsR.load (std::memory_order_relaxed));
        return l;
    }
    // how much the limiter pulled the level down in the last block (0 = nothing, 0.5 = -6 dB); any thread
    float getLimiterReduction() const noexcept { return limiter.lastReduction(); }
    uint32_t getMidiActivityCount() const noexcept { return midiActivity.load (std::memory_order_relaxed); }
    PadParams readPadParams (int pad) const noexcept;   // current parameter values of one pad

    // ---- Slice to Pads (message thread) ----
    // Every slice is a NON-destructive view of the source sample: the pads share the same decoded audio and differ only by
    // their start / end. The sample file is never modified. One level of undo.
    struct SliceAssignment { int pad = 0; double startFraction = 0.0, endFraction = 1.0; juce::String name; };
    juce::Result applySlices (int sourcePad, const std::vector<SliceAssignment>& slices);
    bool canUndoSlices() const noexcept { return ! sliceUndo.empty(); }
    void undoSlices();

    // Audition of a region of a pad (Slice to Pads): overrides start / end of that pad WITHOUT touching its parameters
    // (the host never sees it). Per-hit values are captured at the trigger, so clear it a moment after the hit.
    void setPreviewRegion (int pad, float start, float end) noexcept
    {
        if (pad < 0 || pad >= kNumPads) return;
        previewStart[(size_t) pad].store (start); previewEnd[(size_t) pad].store (end); previewActive[(size_t) pad].store (true);
    }
    void clearPreviewRegion (int pad) noexcept { if (pad >= 0 && pad < kNumPads) previewActive[(size_t) pad].store (false); }

    // ---- Mutate (message thread) ----
    // A variant is a set of parameter values; the sample is never modified. The preview overrides the pad's values for the
    // engine only (the host never sees it, nothing is stored): clear it when done. Applying writes real parameters (with
    // host gestures) and keeps one level of undo.
    MutateValues readMutateValues (int pad) const;
    bool padHasReverb (int pad) const;
    void setPreviewMutation (int pad, const MutateValues& v) noexcept;
    void clearPreviewMutation (int pad) noexcept { if (pad >= 0 && pad < kNumPads) mutatePreview[(size_t) pad].active.store (false); }
    void applyMutation (int pad, const MutateValues& v);
    bool canUndoMutation() const noexcept { return mutateUndoPad >= 0; }
    void undoMutation();

    // ---- EQ band listening (solo): a monitoring aid, NOT a parameter: not automatable, never saved ----
    void setEqSolo (int pad, int band) noexcept { if (pad >= 0 && pad < kNumPads) eqSolo[(size_t) pad].store (band); }
    int getEqSolo (int pad) const noexcept { return pad >= 0 && pad < kNumPads ? eqSolo[(size_t) pad].load() : -1; }

    // ---- convolution reverb (message thread) ----
    static constexpr int kNumIrTypes = 6;   // None, Plate, Hall, Stadium, SFX, Custom
    static const char* irTypeName (int type);
    juce::StringArray getIrSearchFolders() const;          // where Plate.wav / Hall.wav / ... are looked for
    juce::File findIrFile (int type) const;                 // first match for a preset type (not for Custom)
    void setCustomIr (int pad, const juce::File& file);
    juce::String getReverbStatus (int pad) const;           // "" when fine
    ReverbHandle createReverbForExport (int pad, double rate) const;   // fresh instance at `rate` (decodes the IR synchronously), null if none
    void pollReverb();                                      // checks the reverb parameters, loads IRs off-thread

    juce::AudioProcessorValueTreeState apvts;
    History history { *this };   // global undo / redo (declared after apvts: it listens to every parameter)

    // ---- Daft Mug AI (optional advisor). Created on first use, so scanning / loading the plug-in never touches the disk or the
    // credential store. Nothing from it is ever saved in the plug-in state. Message thread only.
    ai::AiService& getAi();
    void setAiForTesting (std::unique_ptr<ai::AiService> service);


private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void finishLoad (int pad, const juce::File& file, int generation,
                     std::shared_ptr<SampleData> data, const juce::String& error);
    void applyStateTree (const juce::ValueTree& tree);
    // undo / redo of pad actions: the sample-related part of a pad (file, audio, tempo, name) is swapped back and forth
    void restoreSampleState (int pad, const PadMeta& meta);
    void commitTempoHistory (int pad);
    std::array<std::optional<PadMeta>, kNumPads> loadBefore, tempoBefore;   // snapshots taken before the change, recorded when it completes
    std::array<bool, kNumPads> loadUndoable {};

    struct ParamPtrs
    {
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* note = nullptr;
        std::atomic<float>* vel = nullptr;
        std::atomic<float>* start = nullptr;
        std::atomic<float>* end = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* fadeIn = nullptr;
        std::atomic<float>* fadeOut = nullptr;
        std::atomic<float>* reverse = nullptr;
        std::atomic<float>* velCurve = nullptr;
        std::atomic<float>* loopStart = nullptr;
        std::atomic<float>* loopFade = nullptr;
        std::atomic<float>* choke = nullptr;
        std::atomic<float>* chromatic = nullptr;
        std::atomic<float>* eqOn = nullptr;
        struct EqBandPtrs { std::atomic<float>* on = nullptr; std::atomic<float>* type = nullptr; std::atomic<float>* freq = nullptr;
                            std::atomic<float>* gain = nullptr; std::atomic<float>* q = nullptr;
                            std::atomic<float>* dyn = nullptr; std::atomic<float>* thr = nullptr; std::atomic<float>* ratio = nullptr;
                            std::atomic<float>* att = nullptr; std::atomic<float>* rel = nullptr; std::atomic<float>* knee = nullptr; std::atomic<float>* range = nullptr; };
        std::array<EqBandPtrs, kEqBands> eq {};
        enum FxIndex { SatMode, SatDrive, SatOut, SatMix, CrushBits, CrushRate, CrushMix, ChorusMix, ChorusRate, ChorusDepth,
                       DelayMix, DelayTime, DelayFb, DelayPp, NumFx };
        std::array<std::atomic<float>*, NumFx> fx {};
        std::atomic<float>* rvType = nullptr;
        std::atomic<float>* rvWet = nullptr;
    };
    std::atomic<float>* maxVoicesParam = nullptr;
    std::atomic<float>* voicesPerPadParam = nullptr;
    std::atomic<float>* stealParam = nullptr;
    std::atomic<float>* masterGainParam = nullptr;
    std::atomic<float>* limiterOnParam = nullptr;
    std::atomic<float>* limiterCeilingParam = nullptr;
    Limiter limiter;
    float masterGainLast = 1.0f;   // audio thread

    SamplerEngine engine;
    std::array<ParamPtrs, kNumPads> paramPtrs {};
    std::array<MidiEvent, 512> midiEvents {};

    mutable juce::CriticalSection metaLock;   // never taken on the audio thread
    std::array<PadMeta, kNumPads> pads {};
    std::array<std::atomic<int>, kNumPads> loadGeneration {};
    std::array<int, kNumPads> tempoGeneration {};   // message thread only: cancels stretches in flight
    struct TempoSaved { bool valid = false; double original = 0.0, target = 0.0; };
    std::array<TempoSaved, kNumPads> tempoRestore {};   // applied by finishLoad when a project is restored
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<float> cpuLoad { 0.0f };
    void startStretch (int pad);
    void finishStretch (int pad, int generation, SampleHandle original, std::shared_ptr<SampleData> stretched);

    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f }, meterMsL { 0.0f }, meterMsR { 0.0f };
    std::atomic<uint32_t> midiActivity { 0 };   // counts incoming Note On/Off (drives the MIDI badge)
    std::atomic<int> learnPad { -1 };
    std::atomic<int> learnedPad { -1 };
    std::atomic<int> learnedNote { -1 };

    juce::ThreadPool loaderPool { 1 };

    struct ReverbKey
    {
        int type = 0;
        juce::String path;
        double rate = 0.0;
        bool operator== (const ReverbKey& o) const { return type == o.type && path == o.path && juce::approximatelyEqual (rate, o.rate); }
    };
    std::array<ReverbKey, kNumPads> reverbApplied {};
    std::array<int, kNumPads> reverbGeneration {};
    // Decoded samples already in memory, shared between pads that use the same file (16 slices of one loop = ONE copy).
    std::map<juce::String, std::weak_ptr<SampleData>> sampleCache;   // message thread only
    static juce::String sampleCacheKey (const juce::File& f)
    {
        return f.getFullPathName() + "@" + juce::String (f.getLastModificationTime().toMilliseconds()) + "@" + juce::String (f.getSize());
    }
    std::map<juce::String, KernelHandle> kernelCache;   // key: path + rate (message thread only)
    struct SliceUndoEntry { int pad = 0; juce::File file; juce::String name; bool renamed = false; float start = 0.0f, end = 1.0f; };
    std::vector<SliceUndoEntry> sliceUndo;
    std::array<std::atomic<float>, kNumPads> previewStart {}, previewEnd {};
    struct MutatePreview { std::atomic<bool> active { false }; std::array<std::atomic<float>, 9> v {}; };   // pitch, pan, gain, attack, reverb, eq1-4
    std::array<MutatePreview, kNumPads> mutatePreview {};
    int mutateUndoPad = -1;
    MutateValues mutateUndoValues;
    std::array<std::atomic<bool>, kNumPads> previewActive {};
    std::atomic<double> engineRate { 0.0 };
    std::array<std::atomic<int>, kNumPads> eqSolo {};   // -1 = none (set in the constructor)
    std::unique_ptr<ai::AiService> aiService;
    struct ReverbPoller : juce::Timer
    {
        explicit ReverbPoller (DaftMugProcessor& o) : owner (o) {}
        void timerCallback() override { owner.pollReverb(); }
        DaftMugProcessor& owner;
    } reverbPoller { *this };

    JUCE_DECLARE_WEAK_REFERENCEABLE (DaftMugProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaftMugProcessor)
};
} // namespace dm
