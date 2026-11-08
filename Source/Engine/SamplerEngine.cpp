#include "SamplerEngine.h"

#include <algorithm>
#include <cmath>

namespace dm
{
namespace
{
constexpr float kChokeSeconds = 0.004f;      // retrigger / voice stealing fade-out
constexpr float kSmoothingSeconds = 0.008f;  // live parameter smoothing (gain / pan / pitch)
constexpr float kSilenceDb = -60.0f;
constexpr float kPadTailSeconds = 0.5f;      // pad chain keeps running this long after its last voice (EQ ring-out, later: delay / reverb)

inline float dbToGain (float db) noexcept
{
    return db <= kSilenceDb ? 0.0f : std::pow (10.0f, db * 0.05f);
}

inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

SamplerEngine::SamplerEngine()
{
    for (auto& c : padVoiceCount) c.store (0);
    for (auto& p : padPlayheadValue) p.store (-1.0f);
    for (auto& t : padTriggerCounter) t.store (0);
    for (int i = 0; i < kNumPads; ++i)
        params[(size_t) i].midiNote = kFirstMidiNote + i;
    allocateBuses (4096);
    computeTargets();
    for (int i = 0; i < kNumPads; ++i)
    {
        curGainL[(size_t) i] = targets[(size_t) i].gainL;
        curGainR[(size_t) i] = targets[(size_t) i].gainR;
    }
}

void SamplerEngine::allocateBuses (int capacity)
{
    capacity = std::max(capacity, 64);
    if (capacity <= busCapacity) return;
    busCapacity = capacity;
    for (int i = 0; i < kNumPads; ++i)
    {
        busL[(size_t) i].assign ((size_t) capacity, 0.0f);
        busR[(size_t) i].assign ((size_t) capacity, 0.0f);
    }
    rvScratchL.assign ((size_t) capacity, 0.0f);
    rvScratchR.assign ((size_t) capacity, 0.0f);
}

void SamplerEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothCoef = 1.0f - std::exp (-1.0f / (kSmoothingSeconds * (float) sr));
    allocateBuses (maxBlockSize);   // (prepare runs outside the audio callback)
    for (auto& e : eqs) e.prepare (sr);
    for (auto& f : fxs) f.prepare (sr);
    reset();
}

void SamplerEngine::reset()
{
    for (auto& v : voices)
        if (v.active)
            killVoice (v);
    for (int i = 0; i < kNumPads; ++i)
    {
        std::fill (busL[(size_t) i].begin(), busL[(size_t) i].end(), 0.0f);
        std::fill (busR[(size_t) i].begin(), busR[(size_t) i].end(), 0.0f);
        eqs[(size_t) i].reset();
        fxs[(size_t) i].reset();
        if (reverbs[(size_t) i]) reverbs[(size_t) i]->reset();
        reverbMix[(size_t) i] = 0.0f;
        tailFrames[(size_t) i] = 0;
        busDirty[(size_t) i] = false;
    }
    publishMonitoring();
}

// ------------------------------------------------------------------ message thread
bool SamplerEngine::setPadSample (int pad, SampleHandle sample)
{
    if (pad < 0 || pad >= kNumPads) return false;
    Command c;
    c.type = Command::Type::SetSample;
    c.pad = pad;
    c.sample = std::move (sample);
    return commands.push (std::move (c));
}

bool SamplerEngine::setPadReverb (int pad, ReverbHandle reverb)
{
    if (pad < 0 || pad >= kNumPads) return false;
    Command c;
    c.type = Command::Type::SetReverb;
    c.pad = pad;
    c.reverb = std::move (reverb);
    return commands.push (std::move (c));
}

bool SamplerEngine::triggerPad (int pad, float velocity)
{
    if (pad < 0 || pad >= kNumPads) return false;
    Command c;
    c.type = Command::Type::Trigger;
    c.pad = pad;
    c.velocity = clampf (velocity, 0.0f, 1.0f);
    return commands.push (std::move (c));
}

bool SamplerEngine::releasePad (int pad)
{
    if (pad < 0 || pad >= kNumPads) return false;
    Command c;
    c.type = Command::Type::Release;
    c.pad = pad;
    return commands.push (std::move (c));
}

bool SamplerEngine::stopAll()
{
    Command c;
    c.type = Command::Type::StopAll;
    return commands.push (std::move (c));
}

void SamplerEngine::collectGarbage()
{
    SampleHandle h;
    while (retired.pop (h))
        h.reset();   // last reference dropped here, on the message thread
    ReverbHandle r;
    while (retiredReverbs.pop (r))
        r.reset();
}

// ------------------------------------------------------------------ audio thread
void SamplerEngine::setPadParams (int pad, const PadParams& p)
{
    if (pad >= 0 && pad < kNumPads)
        params[(size_t) pad] = p;
}

void SamplerEngine::setSettings (const EngineSettings& s)
{
    settings.maxVoices = std::clamp (s.maxVoices, 1, kMaxVoices);
    settings.maxVoicesPerPad = std::clamp (s.maxVoicesPerPad, 1, kMaxVoicesPerPad);
    settings.stealPolicy = s.stealPolicy;
}

void SamplerEngine::allSoundOff()
{
    for (auto& v : voices)
        if (v.active && v.env != EnvState::Release)
            releaseVoice (v, kChokeSeconds);
}

void SamplerEngine::computeTargets()
{
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto& p = params[(size_t) i];
        auto& t = targets[(size_t) i];

        const float gain = dbToGain (p.gainDb);
        const float angle = (clampf (p.pan, -1.0f, 1.0f) + 1.0f) * 0.78539816339f;   // 0 .. pi/2
        t.gainL = gain * std::min (1.0f, 1.41421356f * std::cos (angle));
        t.gainR = gain * std::min (1.0f, 1.41421356f * std::sin (angle));
        t.ratio = std::pow (2.0, (double) p.pitchSemitones / 12.0);
    }
}

void SamplerEngine::drainCommands()
{
    Command c;
    while (commands.pop (c))
    {
        switch (c.type)
        {
            case Command::Type::SetSample:
            {
                // Hits already playing keep their own reference to the old sample and finish normally.
                SampleHandle old = std::move (samples[(size_t) c.pad]);
                samples[(size_t) c.pad] = std::move (c.sample);
                if (old && ! retired.push (std::move (old)))
                    old.reset();   // queue full: rare, freed here as a last resort
                break;
            }
            case Command::Type::SetReverb:
            {
                ReverbHandle old = std::move (reverbs[(size_t) c.pad]);
                reverbs[(size_t) c.pad] = std::move (c.reverb);
                reverbMix[(size_t) c.pad] = 0.0f;
                if (old && ! retiredReverbs.push (std::move (old)))
                    old.reset();
                break;
            }
            case Command::Type::Trigger:  startPad (c.pad, kPadTriggerBase + c.pad, c.velocity); break;
            case Command::Type::Release:  releaseKey (kPadTriggerBase + c.pad); break;
            case Command::Type::StopAll:  allSoundOff(); break;
            case Command::Type::None: break;
        }
        c = Command {};
    }
}

void SamplerEngine::handleMidi (const MidiEvent& e)
{
    if (e.noteOn)
    {
        bool taken = false;   // note used by a loaded, non-chromatic pad
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const auto& p = params[(size_t) pad];
            if (p.midiNote == e.note)
                startPad (pad, e.note, e.velocity);
            if (! p.chromatic && p.midiNote == e.note && samples[(size_t) pad] && samples[(size_t) pad]->isValid())
                taken = true;
        }
        // chromatic pads play every other note, transposed relative to their own note (the free notes of the keyboard)
        if (! taken)
            for (int pad = 0; pad < kNumPads; ++pad)
            {
                const auto& p = params[(size_t) pad];
                if (p.chromatic && p.midiNote != e.note)
                    startPad (pad, e.note, e.velocity, e.note - p.midiNote);
            }
    }
    else
    {
        releaseKey (e.note);
    }
}

SamplerEngine::Voice* SamplerEngine::allocateVoice (int pad)
{
    // Only "sounding" voices (not already fading out) count against the limits.
    int soundingOfPad = 0, sounding = 0;
    Voice* oldestOfPad = nullptr;
    for (auto& v : voices)
        if (v.active && v.env != EnvState::Release)
        {
            ++sounding;
            if (v.pad == pad)
            {
                ++soundingOfPad;
                if (oldestOfPad == nullptr || v.age < oldestOfPad->age) oldestOfPad = &v;
            }
        }

    // 1. per-pad limit
    if (soundingOfPad >= settings.maxVoicesPerPad)
    {
        if (settings.stealPolicy == StealPolicy::IgnoreNew || oldestOfPad == nullptr)
            return nullptr;
        releaseVoice (*oldestOfPad, kChokeSeconds);   // click-free: short fade, the new hit takes another slot
        --sounding;
    }

    // 2. global limit
    if (sounding >= settings.maxVoices)
    {
        if (settings.stealPolicy == StealPolicy::IgnoreNew)
            return nullptr;

        Voice* victim = nullptr;
        float victimScore = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active || v.env == EnvState::Release) continue;
            const float score = settings.stealPolicy == StealPolicy::Quietest
                                    ? v.level * v.velocityGain * std::max (targets[(size_t) v.pad].gainL, targets[(size_t) v.pad].gainR)   // lower = quieter = better victim
                                    : (float) v.age;                                     // lower = older = better victim
            if (victim == nullptr || score < victimScore) { victim = &v; victimScore = score; }
        }
        if (victim != nullptr)
            releaseVoice (*victim, kChokeSeconds);
    }

    // 3. a free physical slot
    for (auto& v : voices)
        if (! v.active) return &v;

    // 4. physical pool exhausted (every slot sounding or fading): cut the oldest fading voice, else the oldest
    Voice* victim = nullptr;
    for (auto& v : voices)
    {
        const bool better = victim == nullptr
                            || (v.env == EnvState::Release && victim->env != EnvState::Release)
                            || ((v.env == EnvState::Release) == (victim->env == EnvState::Release) && v.age < victim->age);
        if (better) victim = &v;
    }
    if (victim != nullptr)
        killVoice (*victim);   // hard cut: only when more than kMaxVoices hits overlap
    return victim;
}

void SamplerEngine::startPad (int pad, int key, float velocity, int keySemitones)
{
    const auto& sample = samples[(size_t) pad];
    if (! sample || ! sample->isValid()) return;

    const auto& p = params[(size_t) pad];

    // Loop pad hit with the mouse while it loops: the click toggles it off
    if (p.mode == PlayMode::Loop && key >= kPadTriggerBase)
    {
        bool stopped = false;
        for (auto& v : voices)
            if (v.active && v.pad == pad && v.loop && v.triggerKey >= kPadTriggerBase && v.env != EnvState::Release)
            {
                releaseVoice (v, std::max (v.releaseSeconds, kChokeSeconds));
                stopped = true;
            }
        if (stopped) return;
    }

    if (p.mode == PlayMode::Retrigger || p.mode == PlayMode::Loop)
        for (auto& v : voices)
            if (v.active && v.pad == pad && v.env != EnvState::Release)
                releaseVoice (v, kChokeSeconds);

    // choke group: the other pads of the same group fade out (open hi-hat cut by the closed one)
    if (p.chokeGroup > 0)
        for (auto& v : voices)
            if (v.active && v.pad != pad && v.env != EnvState::Release && params[(size_t) v.pad].chokeGroup == p.chokeGroup)
                releaseVoice (v, kChokeSeconds);

    Voice* v = allocateVoice (pad);
    if (v == nullptr) return;

    const int frames = sample->numFrames();
    const float s0 = clampf (p.startFraction, 0.0f, 1.0f);
    const float s1 = clampf (p.endFraction, 0.0f, 1.0f);
    int startFrame = static_cast<int> (std::floor (std::min (s0, s1) * (float) frames));
    int endFrame = static_cast<int> (std::ceil (std::max (s0, s1) * (float) frames));
    startFrame = std::min (startFrame, frames - 1);
    endFrame = std::min (std::max (endFrame, startFrame + 1), frames);

    const float sens = clampf (p.velocitySensitivity, 0.0f, 1.0f);
    const float vel = clampf (velocity, 0.0f, 1.0f);
    const float exponent = std::pow (2.0f, clampf (p.velocityCurve, -1.0f, 1.0f));   // 0.5 .. 2
    const float velGain = (1.0f - sens) + sens * std::pow (vel, exponent);

    *v = Voice {};
    v->active = true;
    v->pad = pad;
    v->triggerKey = key;
    v->sample = sample;
    v->position = 0.0;
    v->startFrame = startFrame;
    v->endFrame = endFrame;
    v->regionLength = endFrame - startFrame;
    v->reverse = p.reverse;
    v->rateRatio = sample->sampleRate / sr;
    v->fadeInFrames = std::max (0.0f, p.fadeInSeconds) * (float) sample->sampleRate;
    v->fadeOutFrames = std::max (0.0f, p.fadeOutSeconds) * (float) sample->sampleRate;

    v->velocityGain = velGain;
    v->curRatio = targets[(size_t) pad].ratio;   // a new hit starts exactly on the pitch target: no glide-in
    v->keyRatio = std::pow (2.0, (double) clampf ((float) keySemitones, -96.0f, 96.0f) / 12.0);

    if (p.mode == PlayMode::Loop)
    {
        v->loop = true;
        const double len = (double) v->regionLength;
        v->loopStart = clampf (p.loopStartFraction, 0.0f, 0.95f) * len;
        const double loopLen = len - v->loopStart;
        const double want = (double) std::max (0.0f, p.loopCrossfadeSeconds) * sample->sampleRate;
        v->loopFade = std::min (want, loopLen * 0.5);
        v->fadeOutFrames = 0.0f;   // a loop has no end: the region fade-out does not apply
    }

    v->mode = p.mode;
    v->env = EnvState::Attack;
    v->level = 0.0f;
    const float attack = std::max (p.attackSeconds, p.declickSeconds);
    v->attackStep = 1.0f / std::max (1.0f, attack * (float) sr);
    v->sustainLevel = clampf (p.sustain, 0.0f, 1.0f);
    const float decay = std::max (p.decaySeconds, 0.001f);
    v->decayStep = (1.0f - v->sustainLevel) / std::max (1.0f, decay * (float) sr);
    v->releaseSeconds = std::max (p.releaseSeconds, 0.001f);
    v->age = ++ageCounter;

    padTriggerCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
}

void SamplerEngine::releaseKey (int key)
{
    for (auto& v : voices)
        if (v.active && v.triggerKey == key && v.env != EnvState::Release
            && (v.mode == PlayMode::Gate || (v.mode == PlayMode::Loop && key < kPadTriggerBase)))
            releaseVoice (v, v.releaseSeconds);
}

void SamplerEngine::releaseVoice (Voice& v, float seconds) noexcept
{
    v.env = EnvState::Release;
    v.releaseStep = std::max (v.level, 0.0001f) / std::max (1.0f, seconds * (float) sr);
}

void SamplerEngine::killVoice (Voice& v) noexcept
{
    if (v.sample)
        retired.push (SampleHandle (std::move (v.sample)));   // if the queue is full the temporary frees it here (rare)
    v.sample.reset();
    v.active = false;
}

void SamplerEngine::renderVoice (Voice& v, float* outL, float* outR, int n)
{
    const float* srcL = v.sample->left.data();
    const float* srcR = v.sample->right.data();
    const auto& target = targets[(size_t) v.pad];
    const float velGain = v.velocityGain;
    const double lastIndex = (double) (v.endFrame - 1);
    const double firstIndex = (double) v.startFrame;
    const double regionLen = (double) v.regionLength;

    for (int i = 0; i < n; ++i)
    {
        // --- envelope
        switch (v.env)
        {
            case EnvState::Attack:
                v.level += v.attackStep;
                if (v.level >= 1.0f)
                {
                    v.level = 1.0f;
                    v.env = v.sustainLevel < 1.0f ? EnvState::Decay : EnvState::Sustain;
                }
                break;
            case EnvState::Decay:
                v.level -= v.decayStep;
                if (v.level <= v.sustainLevel) { v.level = v.sustainLevel; v.env = EnvState::Sustain; }
                break;
            case EnvState::Sustain: break;
            case EnvState::Release:
                v.level -= v.releaseStep;
                if (v.level <= 0.0f) { killVoice (v); return; }
                break;
        }

        // --- end of region (a loop never ends: it wraps below)
        if (v.position >= regionLen && ! v.loop) { killVoice (v); return; }

        // --- live pitch, smoothed per sample (no zipper noise, no clicks)
        v.curRatio += (target.ratio - v.curRatio) * (double) smoothCoef;

        // --- read the sample (linear interpolation), forwards or backwards
        auto readAt = [&] (double pos, float& l, float& rr)
        {
            double r = v.reverse ? lastIndex - pos : firstIndex + pos;
            r = r < firstIndex ? firstIndex : (r > lastIndex ? lastIndex : r);
            const int i0 = static_cast<int> (r);
            const int i1 = (i0 + 1 <= v.endFrame - 1) ? i0 + 1 : i0;
            const float frac = static_cast<float> (r - (double) i0);
            l = srcL[i0] + (srcL[i1] - srcL[i0]) * frac;
            rr = srcR[i0] + (srcR[i1] - srcR[i0]) * frac;
        };
        float l, rr;
        readAt (v.position, l, rr);

        // loop seam: over the last loopFade frames the tail is cross-faded (linear) with the start of the loop
        if (v.loop && v.loopFade > 1.0 && v.position >= regionLen - v.loopFade)
        {
            const double t = (v.position - (regionLen - v.loopFade)) / v.loopFade;
            float hl, hr;
            readAt (v.loopStart + (v.position - (regionLen - v.loopFade)), hl, hr);
            const float b = (float) t, a = 1.0f - b;   // linear: no level bump on a steady tone
            l = l * a + hl * b;
            rr = rr * a + hr * b;
        }

        // --- region fades (tied to the start / end of the region, not to the trigger)
        float fade = 1.0f;
        if (v.fadeInFrames > 0.0f && ! v.looped)
            fade = std::min (fade, (float) (v.position / (double) v.fadeInFrames));
        if (v.fadeOutFrames > 0.0f)
            fade = std::min (fade, (float) ((regionLen - v.position) / (double) v.fadeOutFrames));
        fade = fade < 0.0f ? 0.0f : fade;

        const float amp = v.level * fade * velGain;
        outL[i] += l * amp;
        outR[i] += rr * amp;

        v.position += v.curRatio * v.keyRatio * v.rateRatio;
        if (v.loop && v.position >= regionLen)
        {
            v.position = v.loopStart + v.loopFade + (v.position - regionLen);   // the seam already played the first loopFade frames
            if (v.position >= regionLen) v.position = v.loopStart;               // extreme pitch: stay inside
            v.looped = true;
        }
    }
}

void SamplerEngine::renderVoices (int pos, int n)
{
    for (auto& v : voices)
        if (v.active)
        {
            const auto pad = (size_t) v.pad;
            busDirty[pad] = true;
            renderVoice (v, busL[pad].data() + pos, busR[pad].data() + pos, n);
        }
}

// Pad stage: EQ on the pad bus, then volume / pan (smoothed per sample), summed into the output.
void SamplerEngine::mixPads (float* outL, float* outR, int n)
{
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto i = (size_t) pad;
        const bool rvOn = reverbs[i] && params[i].reverbWet > 0.0f;
        const float rvTail = rvOn ? (float) reverbs[i]->tailFrames() / (float) sr : 0.0f;
        const int tail = (int) (std::max (std::max (kPadTailSeconds, fxTailSeconds (params[i].fx)), rvTail) * (float) sr);
        if (busDirty[i])
            tailFrames[i] = tail;
        else if (tailFrames[i] > 0)
            tailFrames[i] = std::max (0, tailFrames[i] - n);

        const bool active = busDirty[i] || tailFrames[i] > 0;
        if (! active)
            continue;

        float* bl = busL[i].data();
        float* br = busR[i].data();

        fxs[i].setParams (params[i].fx);
        fxs[i].processPre (bl, br, n);
        eqs[i].setParams (params[i].eq);
        eqs[i].process (bl, br, n);
        if (analyzerPad.load (std::memory_order_relaxed) == pad)
        {
            uint32_t w = analyzerWrite.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k)
                analyzerRing[(w + (uint32_t) k) & (uint32_t) (kAnalyzerSize - 1)] = 0.5f * (bl[k] + br[k]);
            analyzerWrite.store (w + (uint32_t) n, std::memory_order_release);
        }
        fxs[i].processPost (bl, br, n);

        if (reverbs[i])
        {
            const float wetT = params[i].reverbWet;
            if (wetT > 0.0f || reverbMix[i] > 1.0e-4f)
            {
                float* sl = rvScratchL.data();
                float* sr2 = rvScratchR.data();
                reverbs[i]->process (bl, br, sl, sr2, n);
                float m = reverbMix[i];
                for (int k = 0; k < n; ++k)
                {
                    m += (wetT - m) * smoothCoef;
                    bl[k] += sl[k] * m;
                    br[k] += sr2[k] * m;
                }
                reverbMix[i] = std::fabs (wetT - m) < 1.0e-4f ? wetT : m;
            }
            else if (reverbs[i]->isDirty())
            {
                reverbs[i]->reset();
                reverbMix[i] = 0.0f;
            }
        }

        const float tgtL = targets[i].gainL, tgtR = targets[i].gainR;
        float gl = curGainL[i], gr = curGainR[i];
        for (int k = 0; k < n; ++k)
        {
            gl += (tgtL - gl) * smoothCoef;
            gr += (tgtR - gr) * smoothCoef;
            outL[k] += bl[k] * gl;
            outR[k] += br[k] * gr;
            bl[k] = 0.0f;
            br[k] = 0.0f;
        }
        curGainL[i] = gl;
        curGainR[i] = gr;
        busDirty[i] = false;
    }
}

void SamplerEngine::publishMonitoring()
{
    int total = 0;
    std::array<int, kNumPads> perPad {};
    std::array<uint64_t, kNumPads> newest {};
    std::array<float, kNumPads> playhead {};
    playhead.fill (-1.0f);

    for (auto& v : voices)
    {
        if (! v.active) continue;
        ++total;
        ++perPad[(size_t) v.pad];
        if (v.age >= newest[(size_t) v.pad] && v.sample)
        {
            newest[(size_t) v.pad] = v.age;
            const int frames = v.sample->numFrames();
            const double frame = v.reverse ? (double) (v.endFrame - 1) - v.position : (double) v.startFrame + v.position;
            playhead[(size_t) v.pad] = frames > 0 ? clampf ((float) (frame / (double) frames), 0.0f, 1.0f) : -1.0f;
        }
    }

    activeVoiceCount.store (total, std::memory_order_relaxed);
    for (int i = 0; i < kNumPads; ++i)
    {
        padVoiceCount[(size_t) i].store (perPad[(size_t) i], std::memory_order_relaxed);
        padPlayheadValue[(size_t) i].store (playhead[(size_t) i], std::memory_order_relaxed);
    }
}

void SamplerEngine::processChunk (float* outL, float* outR, int n, const MidiEvent* events, int numEvents, int base)
{
    // Idle pads: snap the smoothers to their targets, so a new hit never glides in from an old value,
    // and drop the EQ state (it re-initialises exactly on its parameters).
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        const auto i = (size_t) pad;
        if (! busDirty[i] && tailFrames[i] <= 0)
        {
            curGainL[i] = targets[i].gainL;
            curGainR[i] = targets[i].gainR;
            eqs[i].reset();
            if (fxs[i].needsReset()) fxs[i].reset();
            if (reverbs[i] && reverbs[i]->isDirty()) { reverbs[i]->reset(); reverbMix[i] = 0.0f; }
        }
    }

    int pos = 0, e = 0;
    while (pos < n)
    {
        while (e < numEvents && events[e].offset - base <= pos)
            handleMidi (events[e++]);

        const int next = (e < numEvents) ? std::min (n, events[e].offset - base) : n;
        if (next > pos)
            renderVoices (pos, next - pos);
        pos = next;
    }

    // events placed after the last frame (defensive): apply them for the next block
    while (e < numEvents)
        handleMidi (events[e++]);

    mixPads (outL, outR, n);
}

void SamplerEngine::process (float* outL, float* outR, int numFrames, const MidiEvent* events, int numEvents)
{
    computeTargets();   // live parameters: read once per block, smoothed per sample
    drainCommands();

    // Hosts may exceed the block size announced in prepare(): work in slices that fit the pad buses.
    int e0 = 0;
    for (int base = 0; base < numFrames; base += busCapacity)
    {
        const int n = std::min (busCapacity, numFrames - base);
        const bool last = base + n >= numFrames;
        int e1 = e0;
        while (e1 < numEvents && (last || events[e1].offset < base + n))
            ++e1;
        processChunk (outL + base, outR + base, n, events + e0, e1 - e0, base);
        e0 = e1;
    }

    publishMonitoring();
}
} // namespace dm
