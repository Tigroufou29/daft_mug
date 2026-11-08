#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "EngineConfig.h"
#include "Convolution.h"
#include "Eq.h"
#include "PadParams.h"
#include "SampleData.h"
#include "SpscQueue.h"

namespace dm
{
using ReverbHandle = std::shared_ptr<ConvInstance>;   // one per pad; mutated only by the audio thread once handed over

struct MidiEvent
{
    int offset = 0;          // sample offset inside the block
    bool noteOn = false;     // false = Note Off
    int note = 0;
    float velocity = 1.0f;   // 0..1
};

// Real-time safe sampler core: 16 independent pads sharing a fixed voice pool.
//
// Threading contract
//  - message thread : setPadSample / triggerPad / releasePad / stopAll / collectGarbage
//                     (all wait-free, single producer: only ONE non-audio thread may call them)
//  - audio thread   : setPadParams / setSettings / process / allSoundOff
//  - any thread     : the read-only monitoring getters
// The audio thread never allocates, never locks and never touches the file system.
class SamplerEngine
{
public:
    SamplerEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();   // audio thread (or while audio is stopped)

    // ---- message thread ----
    bool setPadSample (int pad, SampleHandle sample);   // nullptr clears the pad
    // Hands a convolution reverb (built off-thread: ConvInstance allocates) to a pad; nullptr removes it.
    bool setPadReverb (int pad, ReverbHandle reverb);
    bool triggerPad (int pad, float velocity);          // mouse / UI hit
    bool releasePad (int pad);                          // mouse up (matters in Gate mode)
    bool stopAll();
    void collectGarbage();                              // frees retired samples off the audio thread

    // ---- audio thread ----
    void setPadParams (int pad, const PadParams& params);
    void setSettings (const EngineSettings& settings);
    void allSoundOff();   // MIDI CC 120/123: fast release of every voice
    // Adds the voices to outL/outR (the caller clears the buffers). `events` must be sorted by offset.
    void process (float* outL, float* outR, int numFrames, const MidiEvent* events, int numEvents);

    // ---- monitoring (any thread, relaxed) ----
    int activeVoices() const noexcept { return activeVoiceCount.load (std::memory_order_relaxed); }
    int padVoices (int pad) const noexcept { return padVoiceCount[(size_t) pad].load (std::memory_order_relaxed); }
    bool isPadPlaying (int pad) const noexcept { return padVoices (pad) > 0; }
    float padPlayhead (int pad) const noexcept { return padPlayheadValue[(size_t) pad].load (std::memory_order_relaxed); }
    uint32_t padTriggerCount (int pad) const noexcept { return padTriggerCounter[(size_t) pad].load (std::memory_order_relaxed); }

    // ---- spectrum analyser tap (any thread) ----
    // The audio thread copies the signal of ONE pad (after its EQ, mono) into a ring buffer; the UI reads the latest samples.
    static constexpr int kAnalyzerSize = 8192;   // power of two
    void setAnalyzerPad (int pad) noexcept { analyzerPad.store (pad, std::memory_order_relaxed); }
    // Copies the most recent `count` samples (count <= kAnalyzerSize / 2) and returns the running write counter
    // (unchanged = no new audio since the last call). A read racing with the audio thread only blurs the newest samples.
    uint32_t readAnalyzer (float* dst, int count) const noexcept
    {
        const uint32_t w = analyzerWrite.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            dst[i] = analyzerRing[(w - (uint32_t) count + (uint32_t) i) & (uint32_t) (kAnalyzerSize - 1)];
        return w;
    }

private:
    enum class EnvState : int { Attack, Decay, Sustain, Release };

    struct Voice
    {
        bool active = false;
        int pad = 0;
        int triggerKey = 0;           // MIDI note, or kPadTriggerBase + pad for mouse hits
        SampleHandle sample;

        // region playback (all positions in source frames)
        double position = 0.0;        // frames elapsed inside the region
        int startFrame = 0;
        int endFrame = 0;
        int regionLength = 1;
        bool reverse = false;
        double rateRatio = 1.0;       // source rate / engine rate
        float fadeInFrames = 0.0f;
        float fadeOutFrames = 0.0f;

        // live (smoothed) parameters. Volume and pan are NOT here: they are applied after the pad's
        // effect chain, on the pad bus.
        float velocityGain = 1.0f;
        double curRatio = 1.0;
        double keyRatio = 1.0;        // chromatic transposition of this hit (note - pad note)

        // loop mode
        bool loop = false;
        bool looped = false;          // has wrapped at least once (the fade-in only applies to the first pass)
        double loopStart = 0.0;       // frames inside the region
        double loopFade = 0.0;        // cross-fade length in source frames

        // envelope
        PlayMode mode = PlayMode::OneShot;
        EnvState env = EnvState::Attack;
        float level = 0.0f;
        float attackStep = 1.0f, decayStep = 1.0f, sustainLevel = 1.0f;
        float releaseSeconds = 0.015f;
        float releaseStep = 0.0f;

        uint64_t age = 0;
    };

    struct PadTarget
    {
        float gainL = 1.0f, gainR = 1.0f;   // volume * pan law
        double ratio = 1.0;                 // pitch as a speed ratio
    };

    struct Command
    {
        enum class Type : int { None, SetSample, SetReverb, Trigger, Release, StopAll };
        Type type = Type::None;
        int pad = 0;
        float velocity = 1.0f;
        SampleHandle sample;
        ReverbHandle reverb;
    };

    void drainCommands();
    void computeTargets();
    void handleMidi (const MidiEvent& e);
    void startPad (int pad, int key, float velocity, int keySemitones = 0);
    void releaseKey (int key);
    void releaseVoice (Voice& v, float seconds) noexcept;
    void killVoice (Voice& v) noexcept;
    Voice* allocateVoice (int pad);
    void allocateBuses (int capacity);
    void processChunk (float* outL, float* outR, int n, const MidiEvent* events, int numEvents, int base);
    void renderVoices (int pos, int n);
    void renderVoice (Voice& v, float* outL, float* outR, int n);
    void mixPads (float* outL, float* outR, int n);
    void publishMonitoring();

    double sr = 44100.0;
    float smoothCoef = 0.01f;
    std::array<Voice, kMaxVoices> voices {};
    std::array<PadParams, kNumPads> params {};
    std::array<PadTarget, kNumPads> targets {};
    std::array<SampleHandle, kNumPads> samples {};
    EngineSettings settings;
    uint64_t ageCounter = 0;

    // Per-pad signal path:  voices -> pad bus -> EQ (-> future effects) -> volume / pan -> output
    int busCapacity = 0;
    std::array<std::vector<float>, kNumPads> busL, busR;
    std::array<PadEq, kNumPads> eqs {};
    std::array<PadFx, kNumPads> fxs {};
    std::array<float, kNumPads> curGainL {}, curGainR {};   // smoothed volume * pan law
    std::array<int, kNumPads> tailFrames {};                // keeps the pad chain running after the last voice ended
    std::array<bool, kNumPads> busDirty {};
    std::array<ReverbHandle, kNumPads> reverbs {};
    std::array<float, kNumPads> reverbMix {};                // smoothed wet level
    std::vector<float> rvScratchL, rvScratchR;

    SpscQueue<Command, 256> commands;
    SpscQueue<SampleHandle, 256> retired;
    SpscQueue<ReverbHandle, 64> retiredReverbs;

    std::atomic<int> activeVoiceCount { 0 };
    std::array<std::atomic<int>, kNumPads> padVoiceCount {};
    std::array<std::atomic<float>, kNumPads> padPlayheadValue {};
    std::array<std::atomic<uint32_t>, kNumPads> padTriggerCounter {};
    std::array<float, kAnalyzerSize> analyzerRing {};
    std::atomic<uint32_t> analyzerWrite { 0 };
    std::atomic<int> analyzerPad { -1 };
};
} // namespace dm
