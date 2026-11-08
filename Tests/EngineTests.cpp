// Minimal dependency-free tests for the Daft Mug engine (no JUCE needed).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "Convolution.h"
#include "Effects.h"
#include "Eq.h"
#include "Limiter.h"
#include "Mutate.h"
#include "Tempo.h"
#include "SliceDetector.h"
#include "NameUtils.h"
#include "SamplerEngine.h"

using namespace dm;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do { if (! (cond)) { std::printf ("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static SampleHandle makeSample (int frames, float value, double sr = 48000.0)
{
    auto s = std::make_shared<SampleData>();
    s->left.assign ((size_t) frames, value);
    s->right = s->left;
    s->sampleRate = sr;
    s->name = "test";
    return s;
}

struct Rig
{
    SamplerEngine eng;
    std::vector<float> L, R;
    explicit Rig (int block = 512) : L ((size_t) block), R ((size_t) block) { eng.prepare (48000.0, block); }

    void run (const std::vector<MidiEvent>& ev = {})
    {
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        eng.process (L.data(), R.data(), (int) L.size(), ev.data(), (int) ev.size());
    }
};

static float peak (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    float p = 0.0f;
    for (size_t i = from; i < v.size() && i < to; ++i) p = std::fmax (p, std::fabs (v[i]));
    return p;
}

static void testQueue()
{
    std::printf ("SpscQueue\n");
    SpscQueue<int, 4> q;
    int v = 0;
    CHECK (! q.pop (v));
    CHECK (q.push (1)); CHECK (q.push (2)); CHECK (q.push (3));
    CHECK (! q.push (4));   // capacity - 1 usable slots
    CHECK (q.pop (v) && v == 1);
    CHECK (q.push (5));
    CHECK (q.pop (v) && v == 2);
    CHECK (q.pop (v) && v == 3);
    CHECK (q.pop (v) && v == 5);
    CHECK (! q.pop (v));
}

static void testMidiOffsetAndOneShot()
{
    std::printf ("MIDI trigger, offset, one-shot\n");
    Rig r;
    r.eng.setPadSample (0, makeSample (100, 0.5f));
    r.run();   // applies the SetSample command
    r.run ({ { 20, true, kFirstMidiNote, 1.0f } });
    CHECK (peak (r.L, 0, 20) == 0.0f);          // silence before the offset
    CHECK (peak (r.L, 40, 90) > 0.4f);          // sample playing (fade-in done)
    CHECK (r.eng.padTriggerCount (0) == 1);
    r.run();
    CHECK (peak (r.L) == 0.0f);                 // 100 frames long: finished in the previous block
    CHECK (r.eng.activeVoices() == 0);
}

static void testPadIndependence()
{
    std::printf ("Pad independence (gain / pan / pitch)\n");
    Rig r;
    r.eng.setPadSample (0, makeSample (400, 1.0f));
    r.eng.setPadSample (1, makeSample (400, 1.0f));
    PadParams a; a.midiNote = 36; a.gainDb = 0.0f;
    PadParams b; b.midiNote = 37; b.gainDb = -6.0f; b.pan = 1.0f;
    r.eng.setPadParams (0, a);
    r.eng.setPadParams (1, b);
    r.run();
    r.run ({ { 0, true, 37, 1.0f } });
    CHECK (std::fabs (peak (r.R) - 0.501f) < 0.01f);   // -6 dB
    CHECK (peak (r.L) < 0.001f);                       // hard right
    r.run();
    // pitch +12 st doubles the speed: a 400-frame sample ends after ~200 frames
    PadParams c; c.midiNote = 38; c.pitchSemitones = 12.0f;
    r.eng.setPadParams (2, c);
    r.eng.setPadSample (2, makeSample (400, 1.0f));
    r.run();
    r.run ({ { 0, true, 38, 1.0f } });
    CHECK (peak (r.L, 50, 190) > 0.9f);
    CHECK (peak (r.L, 215, 400) == 0.0f);
}

static void testGateAndRetrigger()
{
    std::printf ("Gate release and Retrigger\n");
    Rig r (256);
    r.eng.setPadSample (0, makeSample (48000, 1.0f));
    PadParams g; g.midiNote = 36; g.mode = PlayMode::Gate;
    r.eng.setPadParams (0, g);
    r.run();
    r.run ({ { 0, true, 36, 1.0f } });
    CHECK (r.eng.isPadPlaying (0));
    r.run ({ { 10, false, 36, 0.0f } });
    r.run();   // 256 frames > 15 ms release (720 frames)? not yet
    r.run(); r.run();
    CHECK (! r.eng.isPadPlaying (0));

    PadParams t; t.midiNote = 36; t.mode = PlayMode::Retrigger;
    r.eng.setPadParams (0, t);
    r.run ({ { 0, true, 36, 1.0f } });
    r.run ({ { 0, true, 36, 1.0f } });
    r.run (); r.run ();                 // chokes need 4 ms (192 frames)
    CHECK (r.eng.padVoices (0) == 1);   // only the latest hit remains

    // One-shot ignores Note Off
    PadParams o; o.midiNote = 36; o.mode = PlayMode::OneShot;
    r.eng.setPadParams (0, o);
    r.eng.stopAll(); r.run(); r.run();
    r.run ({ { 0, true, 36, 1.0f } });
    r.run ({ { 0, false, 36, 0.0f } });
    r.run (); r.run ();
    CHECK (r.eng.isPadPlaying (0));
}

static void testPolyphonyAndStealing()
{
    std::printf ("Polyphony limits\n");
    Rig r;
    for (int p = 0; p < kNumPads; ++p)
    {
        r.eng.setPadSample (p, makeSample (48000, 0.1f));
        PadParams pp; pp.midiNote = kFirstMidiNote + p;
        r.eng.setPadParams (p, pp);
    }
    r.run();
    std::vector<MidiEvent> ev;
    for (int i = 0; i < 200; ++i) ev.push_back ({ i, true, kFirstMidiNote + (i % kNumPads), 0.8f });
    r.run (ev);
    CHECK (r.eng.activeVoices() <= kMaxVoices);
    for (int p = 0; p < kNumPads; ++p) CHECK (r.eng.padVoices (p) <= kMaxVoicesPerPad);
    for (float x : r.L) CHECK (std::isfinite (x));
}

static void testMouseAndGarbage()
{
    std::printf ("Mouse trigger queue, sample replacement and garbage collection\n");
    Rig r;
    auto s = makeSample (48000, 0.5f);
    std::weak_ptr<const SampleData> weak = s;
    r.eng.setPadSample (3, std::move (s));
    r.run();
    r.eng.triggerPad (3, 1.0f);
    r.run();
    CHECK (r.eng.isPadPlaying (3));
    r.eng.setPadSample (3, makeSample (10, 0.2f));   // replace while a hit of the old sample plays
    r.run();
    CHECK (r.eng.isPadPlaying (3));
    r.eng.stopAll();
    r.run(); r.run();
    CHECK (! weak.expired());   // retired handles wait for the message thread...
    r.eng.collectGarbage();
    CHECK (weak.expired());     // ...which frees them
}

static void testRegionAndEmpty()
{
    std::printf ("Non-destructive region and empty pads\n");
    Rig r;
    auto s = std::make_shared<SampleData>();
    s->left.assign (1000, 0.0f);
    for (int i = 500; i < 1000; ++i) s->left[(size_t) i] = 1.0f;   // second half is loud
    s->right = s->left;
    s->sampleRate = 48000.0;
    r.eng.setPadSample (0, s);
    PadParams p; p.midiNote = 36; p.startFraction = 0.5f; p.endFraction = 1.0f;
    r.eng.setPadParams (0, p);
    r.run();
    r.run ({ { 0, true, 36, 1.0f }, { 0, true, 99, 1.0f } });   // 99: no pad uses it
    CHECK (peak (r.L, 100, 400) > 0.9f);                         // plays only the loud half
    CHECK ((*s).left[0] == 0.0f);                                // source data untouched
}

// ------------------------------------------------------------------------------------------------ EQ
static std::vector<float> sine (double freq, int frames, double sr = 48000.0, float amp = 0.5f)
{
    std::vector<float> v ((size_t) frames);
    for (int i = 0; i < frames; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * freq * i / sr);
    return v;
}

static double rms (const std::vector<float>& v, size_t from)
{
    double s = 0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { s += (double) v[i] * v[i]; ++n; }
    return std::sqrt (s / (double) std::max<size_t> (1, n));
}

// Measures the gain (dB) of a PadEq at one frequency, after the smoothing has settled.
static double measureDb (const EqParams& params, double freq)
{
    PadEq eq; eq.prepare (48000.0); eq.setParams (params);
    auto in = sine (freq, 48000);
    auto l = in, r = in;
    for (int pos = 0; pos < 48000; pos += 512)
    {
        eq.setParams (params);
        eq.process (l.data() + pos, r.data() + pos, std::min (512, 48000 - pos));
    }
    return 20.0 * std::log10 (rms (l, 24000) / rms (in, 24000));
}

static void testEq()
{
    std::printf ("EQ: filter design vs measured response\n");
    EqParams p;
    p.band[1].gainDb = 12.0f; p.band[1].freq = 1000.0f; p.band[1].q = 1.0f;       // bell +12 dB @ 1 kHz
    const auto bell = designBand (p.band[1], 48000.0);
    CHECK (std::fabs (magnitudeDb (bell, 1000.0, 48000.0) - 12.0) < 0.05);
    CHECK (std::fabs (magnitudeDb (bell, 30.0, 48000.0)) < 0.5);
    CHECK (std::fabs (measureDb (p, 1000.0) - 12.0) < 0.3);
    CHECK (std::fabs (measureDb (p, 60.0)) < 0.6);

    EqParams ls;                                                                    // low shelf +9 dB @ 150 Hz
    ls.band[0].gainDb = 9.0f; ls.band[0].freq = 150.0f;
    CHECK (std::fabs (measureDb (ls, 30.0) - 9.0) < 0.6);
    CHECK (std::fabs (measureDb (ls, 6000.0)) < 0.5);

    EqParams hs;                                                                    // high shelf -9 dB @ 4 kHz
    hs.band[3].gainDb = -9.0f; hs.band[3].freq = 4000.0f;
    CHECK (std::fabs (measureDb (hs, 15000.0) + 9.0) < 0.8);
    CHECK (std::fabs (measureDb (hs, 200.0)) < 0.5);

    EqParams hp;                                                                    // high-pass 1 kHz
    hp.band[0] = { true, EqType::HighPass, 1000.0f, 0.0f, 0.7071f };
    CHECK (std::fabs (measureDb (hp, 1000.0) + 3.0) < 0.4);                        // -3 dB at the cutoff
    CHECK (measureDb (hp, 100.0) < -35.0);                                          // 12 dB/oct
    CHECK (std::fabs (measureDb (hp, 8000.0)) < 0.3);

    EqParams lp;                                                                    // low-pass 1 kHz
    lp.band[3] = { true, EqType::LowPass, 1000.0f, 0.0f, 0.7071f };
    CHECK (std::fabs (measureDb (lp, 1000.0) + 3.0) < 0.4);
    CHECK (measureDb (lp, 10000.0) < -35.0);

    std::printf ("EQ: neutral = transparent, on/off and independence of bands\n");
    {
        EqParams flat;
        PadEq eq; eq.prepare (48000.0);
        auto in = sine (440.0, 4096); auto l = in, r = in;
        eq.setParams (flat); eq.process (l.data(), r.data(), 4096);
        CHECK (l == in && r == in);                                                // bit-exact when nothing is active
    }
    {
        EqParams off = p; off.on = false;
        CHECK (std::fabs (measureDb (off, 1000.0)) < 0.01);                        // whole EQ switched off
        EqParams bandOff = p; bandOff.band[1].on = false;
        CHECK (std::fabs (measureDb (bandOff, 1000.0)) < 0.01);                    // one band switched off
        EqParams two = p; two.band[2].gainDb = -6.0f; two.band[2].freq = 5000.0f; // band 2 and 3 do not interfere
        CHECK (std::fabs (measureDb (two, 1000.0) - 12.0) < 0.6);
        CHECK (std::fabs (measureDb (two, 5000.0) + 6.0) < 1.0);
    }

    std::printf ("EQ: no clicks when a band is toggled or its gain / frequency changes\n");
    {
        auto maxStep = [] (const std::vector<float>& v, size_t from)
        {
            float m = 0; for (size_t i = from + 1; i < v.size(); ++i) m = std::fmax (m, std::fabs (v[i] - v[i - 1])); return m;
        };
        const auto in = sine (1000.0, 48000, 48000.0, 0.3f);
        const float inStep = maxStep (in, 0);

        PadEq eq; eq.prepare (48000.0);
        EqParams a = p;                       // bell +12 dB (x4 amplitude)
        auto l = in, r = in;
        for (int pos = 0; pos < 48000; pos += 128)
        {
            EqParams cur = a;
            if (pos >= 24000) cur.band[1].on = false;                                   // switch the band off at 0.5 s
            if (pos >= 36000) { cur.band[1].on = true; cur.band[1].freq = 1500.0f; }    // back on, frequency jump at 0.75 s
            eq.setParams (cur);
            eq.process (l.data() + pos, r.data() + pos, std::min (128, 48000 - pos));
        }
        // A hard switch of a x4 band would create steps of several times the input step: stay far below that.
        CHECK (maxStep (l, 20000) < inStep * 4.6f);
    }

    std::printf ("EQ inside the engine: per pad, applied before volume, live\n");
    {
        Rig r (512);
        auto s0 = std::make_shared<SampleData>();
        s0->left = sine (1000.0, 96000, 48000.0, 0.2f); s0->right = s0->left; s0->sampleRate = 48000.0;
        r.eng.setPadSample (0, s0); r.eng.setPadSample (1, s0);
        PadParams a; a.midiNote = 36; a.eq.band[1].gainDb = 12.0f; a.eq.band[1].freq = 1000.0f;
        PadParams b; b.midiNote = 37;                                                    // pad 2: flat EQ
        r.eng.setPadParams (0, a); r.eng.setPadParams (1, b);
        r.run();
        r.run ({ { 0, true, 36, 1.0f }, { 0, true, 37, 1.0f } });
        std::vector<float> outA;
        for (int i = 0; i < 10; ++i) { r.run(); outA.insert (outA.end(), r.L.begin(), r.L.end()); }
        // both pads play the same 0.2 sine on the same output: pad 1 adds 0.2, pad 0 adds 0.2 * 4 = 0.8
        const double level = rms (outA, 2000) * std::sqrt (2.0);
        CHECK (std::fabs (level - 1.0) < 0.06);
    }
}

static void testVelocity()
{
    std::printf ("Velocity response\n");
    Rig r;
    r.eng.setPadSample (0, makeSample (4000, 1.0f));
    PadParams p; p.midiNote = 36; p.velocitySensitivity = 1.0f;
    r.eng.setPadParams (0, p);
    r.run();
    r.run ({ { 0, true, 36, 1.0f } });
    const float full = peak (r.L, 100, 400);
    r.eng.allSoundOff(); r.run(); r.run();
    r.run ({ { 0, true, 36, 0.5f } });
    const float half = peak (r.L, 100, 400);
    CHECK (std::fabs (half / full - 0.5f) < 0.02f);       // linear curve at full sensitivity
    r.eng.allSoundOff(); r.run(); r.run();
    p.velocitySensitivity = 0.0f;                          // fixed level: velocity ignored
    r.eng.setPadParams (0, p);
    r.run ({ { 0, true, 36, 0.2f } });
    CHECK (std::fabs (peak (r.L, 100, 400) - full) < 0.01f);
}


// ---------------------------------------------------------------- step 2a tests
static SampleHandle makeRamp (int frames, double sr = 48000.0)
{
    auto s = std::make_shared<SampleData>();
    s->left.resize ((size_t) frames);
    for (int i = 0; i < frames; ++i) s->left[(size_t) i] = (float) i / (float) frames;
    s->right = s->left;
    s->sampleRate = sr;
    return s;
}

// Renders `frames` frames in blocks, starting with `events` in the first block. Returns the left channel.
static std::vector<float> renderLeft (Rig& r, int frames, const std::vector<MidiEvent>& events = {})
{
    std::vector<float> out;
    bool first = true;
    while ((int) out.size() < frames)
    {
        r.run (first ? events : std::vector<MidiEvent> {});
        first = false;
        out.insert (out.end(), r.L.begin(), r.L.end());
    }
    out.resize ((size_t) frames);
    return out;
}

static void testLiveParameters()
{
    std::printf ("Live parameters (no zipper / no clicks)\n");
    Rig r (256);
    r.eng.setPadSample (0, makeSample (96000, 1.0f));
    PadParams p; p.midiNote = 36;
    r.eng.setPadParams (0, p);
    r.run();
    renderLeft (r, 1024, { { 0, true, 36, 1.0f } });       // sound is running at 0 dB

    p.gainDb = -12.0f;                                      // "automation": instant change from the DAW
    r.eng.setPadParams (0, p);
    auto out = renderLeft (r, 2048);
    float maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i) maxStep = std::fmax (maxStep, std::fabs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.01f);                                // a hard jump would be ~0.75
    CHECK (std::fabs (out.back() - 0.2512f) < 0.01f);       // and it really reaches the new level

    p.pan = 1.0f;                                           // pan sweeps live too
    r.eng.setPadParams (0, p);
    out = renderLeft (r, 6000);
    maxStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i) maxStep = std::fmax (maxStep, std::fabs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.01f);
    CHECK (out.back() < 0.001f);                            // left channel faded out smoothly
}

static void testLivePitch()
{
    std::printf ("Live pitch\n");
    Rig r (256);
    r.eng.setPadSample (0, makeRamp (96000));
    PadParams p; p.midiNote = 36;
    r.eng.setPadParams (0, p);
    r.run();
    renderLeft (r, 2048, { { 0, true, 36, 1.0f } });
    const float h1 = r.eng.padPlayhead (0);
    renderLeft (r, 4096);
    const float slow = r.eng.padPlayhead (0) - h1;          // playhead advance at normal speed
    p.pitchSemitones = 12.0f;
    r.eng.setPadParams (0, p);
    renderLeft (r, 2048);                                   // let the pitch glide settle
    const float h2 = r.eng.padPlayhead (0);
    renderLeft (r, 4096);
    const float fast = r.eng.padPlayhead (0) - h2;
    CHECK (std::fabs (fast / slow - 2.0f) < 0.05f);
}

static void testEnvelope()
{
    std::printf ("Attack / Decay / Sustain / Release\n");
    Rig r (256);
    r.eng.setPadSample (0, makeSample (48000, 1.0f));
    PadParams p; p.midiNote = 36;
    p.attackSeconds = 0.010f;                               // 480 frames
    p.decaySeconds = 0.010f;                                // 480 frames
    p.sustain = 0.5f;
    r.eng.setPadParams (0, p);
    r.run();
    auto out = renderLeft (r, 2048, { { 0, true, 36, 1.0f } });
    CHECK (std::fabs (out[240] - 0.5f) < 0.03f);            // halfway up the attack
    CHECK (std::fabs (out[480] - 1.0f) < 0.03f);            // top of the attack
    CHECK (std::fabs (out[720] - 0.75f) < 0.04f);           // halfway down the decay
    CHECK (std::fabs (out[1500] - 0.5f) < 0.01f);           // sustain level held

    // Gate release time
    r.eng.allSoundOff(); r.run(); r.run(); r.run();
    p.mode = PlayMode::Gate; p.attackSeconds = 0.0f; p.decaySeconds = 0.0f; p.sustain = 1.0f; p.releaseSeconds = 0.020f; // 960 frames
    r.eng.setPadParams (0, p);
    r.run();
    renderLeft (r, 1024, { { 0, true, 36, 1.0f } });
    out = renderLeft (r, 2048, { { 0, false, 36, 0.0f } });
    CHECK (std::fabs (out[480] - 0.5f) < 0.05f);            // half-way through the release
    CHECK (out[1100] < 0.001f);                             // done after 20 ms
}

static void testFadesAndReverse()
{
    std::printf ("Fade-in / Fade-out (region) and Reverse\n");
    Rig r (256);
    r.eng.setPadSample (0, makeSample (4800, 1.0f));        // 100 ms
    PadParams p; p.midiNote = 36;
    p.fadeInSeconds = 0.010f;                               // 480 frames
    p.fadeOutSeconds = 0.010f;
    r.eng.setPadParams (0, p);
    r.run();
    auto out = renderLeft (r, 5200, { { 0, true, 36, 1.0f } });
    CHECK (std::fabs (out[240] - 0.5f) < 0.03f);            // fade-in half way
    CHECK (std::fabs (out[2400] - 1.0f) < 0.001f);          // untouched in the middle
    CHECK (std::fabs (out[4800 - 240] - 0.5f) < 0.03f);     // fade-out half way
    CHECK (out[4799] < 0.01f);                              // silent at the very end

    // Reverse: a rising ramp is played falling, the file itself stays untouched
    Rig q (256);
    auto ramp = makeRamp (1000);
    q.eng.setPadSample (0, ramp);
    PadParams f; f.midiNote = 36;
    q.eng.setPadParams (0, f);
    q.run();
    auto fwd = renderLeft (q, 600, { { 0, true, 36, 1.0f } });
    q.eng.allSoundOff(); q.run(); q.run(); q.run();
    f.reverse = true;
    q.eng.setPadParams (0, f);
    q.run();
    auto rev = renderLeft (q, 600, { { 0, true, 36, 1.0f } });
    CHECK (std::fabs (fwd[300] - 0.3f) < 0.01f);
    CHECK (std::fabs (rev[300] - 0.7f) < 0.01f);
    CHECK (rev[100] > rev[400]);                            // falling
    CHECK (ramp->left[10] == 10.0f / 1000.0f);              // source data intact
}

static void testVelocityCurve()
{
    std::printf ("Velocity curve\n");
    auto level = [] (float curve, float vel)
    {
        Rig r;
        r.eng.setPadSample (0, makeSample (4000, 1.0f));
        PadParams p; p.midiNote = 36; p.velocityCurve = curve;
        r.eng.setPadParams (0, p);
        r.run();
        r.run ({ { 0, true, 36, vel } });
        return peak (r.L, 100, 400);
    };
    CHECK (std::fabs (level (0.0f, 0.5f) - 0.5f) < 0.01f);      // linear
    CHECK (std::fabs (level (1.0f, 0.5f) - 0.25f) < 0.01f);     // harder
    CHECK (std::fabs (level (-1.0f, 0.5f) - 0.7071f) < 0.01f);  // softer
}

static void testVoiceLimits()
{
    std::printf ("Voice limits and steal policies\n");
    auto setup = [] (Rig& r, StealPolicy pol, int maxVoices, int perPad)
    {
        for (int pad = 0; pad < 4; ++pad)
        {
            r.eng.setPadSample (pad, makeSample (96000, 0.1f));
            PadParams p; p.midiNote = 36 + pad;
            r.eng.setPadParams (pad, p);
        }
        EngineSettings s; s.maxVoices = maxVoices; s.maxVoicesPerPad = perPad; s.stealPolicy = pol;
        r.eng.setSettings (s);
        r.run();
    };
    {
        Rig r (256);
        setup (r, StealPolicy::Oldest, 3, 8);
        for (int pad = 0; pad < 4; ++pad) r.run ({ { 0, true, 36 + pad, 1.0f } });
        r.run(); r.run();                                       // faded-out voices disappear
        CHECK (r.eng.activeVoices() == 3);
        CHECK (! r.eng.isPadPlaying (0) && r.eng.isPadPlaying (3));   // the oldest (pad 0) was cut
    }
    {
        Rig r (256);
        setup (r, StealPolicy::IgnoreNew, 3, 8);
        for (int pad = 0; pad < 4; ++pad) r.run ({ { 0, true, 36 + pad, 1.0f } });
        r.run(); r.run();
        CHECK (r.eng.activeVoices() == 3);
        CHECK (r.eng.isPadPlaying (0) && ! r.eng.isPadPlaying (3));   // the newcomer was dropped
    }
    {
        Rig r (256);
        setup (r, StealPolicy::Oldest, 32, 2);                  // per-pad limit of 2
        for (int i = 0; i < 5; ++i) r.run ({ { 0, true, 36, 1.0f } });
        r.run(); r.run();
        CHECK (r.eng.padVoices (0) == 2);
    }
}

static void testNames()
{
    std::printf ("Auto naming\n");
    CHECK (cleanSampleName ("kick_heavy-01") == "Kick heavy 01");
    CHECK (cleanSampleName ("__snare  roll__") == "Snare roll");
    CHECK (cleanSampleName ("") == "Sample");
    CHECK (cleanSampleName ("---") == "Sample");
    CHECK (cleanSampleName ("Hat") == "Hat");
}

static void testAllSoundOff()
{
    std::printf ("All sound off\n");
    Rig r (256);
    r.eng.setPadSample (0, makeSample (48000, 1.0f));
    r.run();
    r.run ({ { 0, true, 36, 1.0f } });
    CHECK (r.eng.isPadPlaying (0));
    r.eng.allSoundOff();
    r.run(); r.run();
    CHECK (! r.eng.isPadPlaying (0));
}

// ---------------------------------------------------------------- step 2c tests (effects)
static float rms (const std::vector<float>& v, size_t a, size_t b)
{
    double acc = 0; for (size_t i = a; i < b; ++i) acc += (double) v[i] * v[i];
    return (float) std::sqrt (acc / (double) (b - a));
}

static void testFx()
{
    std::printf ("Effects: saturation, bitcrusher, chorus, delay\n");
    auto sine = [] (int frames) {
        auto s = std::make_shared<SampleData>();
        s->left.resize ((size_t) frames);
        for (int i = 0; i < frames; ++i) s->left[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        s->right = s->left; s->sampleRate = 48000.0; return SampleHandle (s);
    };
    auto render = [&] (const FxParams& fx, int frames, int sampleFrames) {
        Rig r (256);
        r.eng.setPadSample (0, sine (sampleFrames));
        PadParams p; p.midiNote = 36; p.fx = fx; p.declickSeconds = 0.0f;
        r.eng.setPadParams (0, p);
        r.run();
        return renderLeft (r, frames, { { 0, true, 36, 1.0f } });
    };
    FxParams none;
    const auto ref = render (none, 9600, 48000);

    { // default = bit-exact bypass
        FxParams f; auto o = render (f, 9600, 48000);
        bool same = true; for (size_t i = 0; i < o.size(); ++i) if (o[i] != ref[i]) same = false;
        CHECK (same);
    }
    { // saturation: hard drive raises RMS and bounds the peak
        FxParams f; f.satMode = SatMode::Soft; f.satDriveDb = 24.0f;
        auto o = render (f, 9600, 48000);
        CHECK (rms (o, 4800, 9600) > rms (ref, 4800, 9600) * 1.3f);
        CHECK (peak (o) <= 1.01f);
        f.satMode = SatMode::Tube; o = render (f, 9600, 48000);
        CHECK (peak (o) > 0.1f && peak (o) <= 1.5f);
    }
    { // bitcrusher: few bits -> staircase signal (few distinct values)
        FxParams f; f.crushMix = 1.0f; f.crushBits = 3.0f;
        auto o = render (f, 9600, 48000);
        std::vector<float> vals;
        for (size_t i = 4800; i < 9600; ++i)
        {
            bool found = false; for (float v : vals) if (std::fabs (v - o[i]) < 1e-4f) found = true;
            if (! found) vals.push_back (o[i]);
        }
        CHECK (vals.size() <= 8);
        f.crushBits = 16.0f; f.crushRate = 16.0f;
        o = render (f, 9600, 48000);
        int changes = 0; for (size_t i = 4801; i < 9600; ++i) if (o[i] != o[i - 1]) ++changes;
        CHECK (changes < 600);                                  // held 16x -> far fewer changes than 4800
    }
    { // chorus changes the signal but stays bounded
        FxParams f; f.chorusMix = 1.0f;
        auto o = render (f, 14400, 48000);
        float diff = 0; for (size_t i = 9600; i < 14400; ++i) diff = std::fmax (diff, std::fabs (o[i] - ref.size() > i ? 0.0f : 0.0f));
        CHECK (peak (o) > 0.2f && peak (o) < 1.0f);
        const auto plain = render (none, 14400, 48000);
        float d = 0; for (size_t i = 9600; i < 14400; ++i) d = std::fmax (d, std::fabs (o[i] - plain[i]));
        CHECK (d > 0.05f);
        (void) diff;
    }
    { // delay: an echo appears after the sound ended, at the right time, and dies out
        FxParams f; f.delayMix = 1.0f; f.delayMs = 100.0f; f.delayFeedback = 0.5f;
        const int len = 2400;                                    // 50 ms sound
        auto o = render (f, 48000, len);
        CHECK (rms (o, 2600, 4400) < 1e-3f);                     // gap before the echo (sound ended at 2400, echo at 4800)
        CHECK (rms (o, 4900, 7100) > 0.1f);                      // first echo
        CHECK (rms (o, 9700, 11900) > 0.03f && rms (o, 9700, 11900) < rms (o, 4900, 7100)); // second, quieter
        CHECK (rms (o, 40000, 48000) < 0.01f);                   // eventually silent
        FxParams pp = f; pp.delayPingPong = true;
        auto o2 = render (pp, 12000, len);
        CHECK (rms (o2, 4900, 7100) > 0.05f);
    }
    { // tail: the engine keeps the pad alive for the echoes; then goes idle
        FxParams f; f.delayMix = 1.0f; f.delayMs = 200.0f; f.delayFeedback = 0.5f;
        CHECK (fxTailSeconds (f) > 1.0f);
        CHECK (fxTailSeconds (none) == 0.0f);
    }
    { // sudden automation of the mix is click free
        Rig r (256);
        r.eng.setPadSample (0, sine (96000));
        PadParams p; p.midiNote = 36; r.eng.setPadParams (0, p); r.run();
        renderLeft (r, 2048, { { 0, true, 36, 1.0f } });
        p.fx.satMode = SatMode::Soft; p.fx.satDriveDb = 6.0f;
        r.eng.setPadParams (0, p);
        auto o = renderLeft (r, 4096);
        float maxStep = 0; for (size_t i = 1; i < o.size(); ++i) maxStep = std::fmax (maxStep, std::fabs (o[i] - o[i - 1]));
        CHECK (maxStep < 0.12f);                                 // no jump when the effect is switched on live
    }
}

// ---------------------------------------------------------------- step 2c tests (convolution reverb)
static void testReverb()
{
    std::printf ("Convolution reverb\n");
    // 1) streaming partitioned FFT convolution == direct convolution (random IR longer than 2 partitions, odd block sizes)
    {
        unsigned seed = 12345u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; };
        std::vector<float> ir (1700), x (6000);
        for (auto& v : ir) v = rnd() * 0.3f;
        for (auto& v : x) v = rnd();
        auto kernel = buildConvKernel (ir.data(), nullptr, (int) ir.size(), 48000.0, 48000.0);
        CHECK (kernel && kernel->numParts == 4);
        ConvInstance conv (kernel);
        double e = 0; for (float v : ir) e += (double) v * v;
        const float scale = (float) (1.0 / std::sqrt (e));
        std::vector<float> yl (x.size()), yr (x.size());
        size_t pos = 0; const int blocks[] = { 100, 333, 512, 77, 1000 };
        for (int bi = 0; pos < x.size(); bi = (bi + 1) % 5)
        {
            const size_t n = std::min ((size_t) blocks[bi], x.size() - pos);
            conv.process (x.data() + pos, x.data() + pos, yl.data() + pos, yr.data() + pos, (int) n);
            pos += n;
        }
        float maxErr = 0;
        for (size_t n = 600; n < x.size(); ++n)
        {
            double ref = 0;
            for (size_t k = 0; k < ir.size() && k + kConvBlock <= n; ++k) ref += (double) ir[k] * scale * x[n - k - kConvBlock];
            maxErr = std::fmax (maxErr, std::fabs ((float) ref - yl[n]));
            maxErr = std::fmax (maxErr, std::fabs (yl[n] - yr[n]));
        }
        CHECK (maxErr < 2e-3f);
        conv.reset();
        std::vector<float> zero (512, 0.0f), o1 (512), o2 (512);
        conv.process (zero.data(), zero.data(), o1.data(), o2.data(), 512);
        CHECK (peak (o1) == 0.0f);
    }
    // 2) inside the engine: a one-sample IR delayed by 100 frames = the pad signal copied 100 + latency frames later
    {
        std::vector<float> ir (200, 0.0f); ir[100] = 1.0f;
        auto kernel = buildConvKernel (ir.data(), nullptr, 200, 48000.0, 48000.0);
        Rig r (256);
        r.eng.setPadSample (0, makeSample (64, 1.0f));
        r.eng.setPadReverb (0, std::make_shared<ConvInstance> (kernel));
        PadParams p; p.midiNote = 36; p.declickSeconds = 0.0f; p.releaseSeconds = 0.0f; p.reverbWet = 1.0f;
        r.eng.setPadParams (0, p);
        r.run();
        const int copyAt = 100 + kConvBlock;
        auto o = renderLeft (r, 4096, { { 0, true, 36, 1.0f } });
        CHECK (peak (o, 0, 64) > 0.3f);
        // the copy appears once the wet level has settled: compare late part of the copy to the dry level
        CHECK (peak (o, (size_t) copyAt + 40, (size_t) copyAt + 64) > 0.3f);
        CHECK (peak (o, 64, 100) < 1e-4f);                         // nothing between dry and wet
        CHECK (peak (o, (size_t) copyAt + 200, 4096) < 1e-4f);     // and it ends
        // wet = 0: no copy
        Rig r2 (256);
        r2.eng.setPadSample (0, makeSample (64, 1.0f));
        r2.eng.setPadReverb (0, std::make_shared<ConvInstance> (kernel));
        p.reverbWet = 0.0f; r2.eng.setPadParams (0, p); r2.run();
        auto o2 = renderLeft (r2, 4096, { { 0, true, 36, 1.0f } });
        CHECK (peak (o2, 100, 4096) < 1e-6f);
        // removing the reverb is safe and garbage is collected off the audio thread
        r.eng.setPadReverb (0, nullptr);
        r.run();
        r.eng.collectGarbage();
    }
    // 3) a long IR keeps the pad alive for its whole length, and a 96 kHz IR is resampled to the engine rate
    {
        std::vector<float> ir (96000, 0.0f); ir[0] = 1.0f; ir[95000] = 0.5f;
        auto kernel = buildConvKernel (ir.data(), ir.data(), 96000, 96000.0, 48000.0);
        CHECK (kernel && std::abs (kernel->irFrames - 48000) <= 1);
        ConvInstance inst (kernel);
        CHECK (inst.tailFrames() > 48000);
    }
}

// ---------------------------------------------------------------- dynamic EQ
static void testDynamicEq()
{
    std::printf("Dynamic EQ\n");
    const double sr = 48000.0;
    auto runSine = [&] (EqBandParams band, float ampDb, int frames, float* secondHalfGainDb = nullptr)
    {
        EqParams p; for (auto& b : p.band) b.on = false;
        p.band[1] = band;
        PadEq eq; eq.prepare (sr); eq.setParams (p);
        std::vector<float> l ((size_t) frames), r;
        const float amp = std::pow (10.0f, ampDb / 20.0f);
        for (int i = 0; i < frames; ++i) l[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr);
        r = l;
        auto in = l;
        for (int pos = 0; pos < frames; pos += 256) { eq.setParams (p); eq.process (l.data() + pos, r.data() + pos, std::min (256, frames - pos)); }
        double a = 0, b = 0;
        for (int i = frames / 2; i < frames; ++i) { a += (double) l[(size_t) i] * l[(size_t) i]; b += (double) in[(size_t) i] * in[(size_t) i]; }
        if (secondHalfGainDb) *secondHalfGainDb = (float) (10.0 * std::log10 (a / b));
        return 0;
    };
    EqBandParams band; band.on = true; band.type = EqType::Bell; band.freq = 1000.0f; band.gainDb = -12.0f; band.q = 1.0f;

    // static curve
    {
        EqBandParams b = band; b.dynMode = 1; b.thresholdDb = -30.0f; b.ratio = 20.0f; b.kneeDb = 0.0f; b.rangeDb = 12.0f;
        CHECK (dynamicAmount (b, -60.0f) == 0.0f);
        CHECK (dynamicAmount (b, 0.0f) == 1.0f);
        CHECK (std::fabs (dynamicAmount (b, -24.0f) - 0.475f) < 0.01f);   // 6 dB over, ratio 20 -> 5.7 / 12
        b.ratio = 1.0f; CHECK (dynamicAmount (b, 0.0f) == 0.0f);
        b.ratio = 20.0f; b.dynMode = 2;
        CHECK (dynamicAmount (b, -60.0f) == 1.0f && dynamicAmount (b, 0.0f) == 0.0f);
    }
    float g = 0;
    { // static band: -12 dB whatever the level
        runSine (band, -50.0f, 24000, &g); CHECK (std::fabs (g + 12.0f) < 0.5f);
        runSine (band, -6.0f, 24000, &g);  CHECK (std::fabs (g + 12.0f) < 0.5f);
    }
    { // compress mode: untouched when quiet, full cut when loud
        EqBandParams b = band; b.dynMode = 1; b.thresholdDb = -30.0f; b.ratio = 20.0f; b.kneeDb = 0.0f; b.rangeDb = 12.0f;
        b.attackMs = 1.0f; b.releaseMs = 20.0f;
        runSine (b, -60.0f, 48000, &g); CHECK (std::fabs (g) < 0.5f);
        runSine (b, -6.0f, 48000, &g);  CHECK (g < -9.0f && g > -13.0f);
    }
    { // expand mode: the opposite
        EqBandParams b = band; b.dynMode = 2; b.thresholdDb = -30.0f; b.ratio = 20.0f; b.kneeDb = 0.0f; b.rangeDb = 12.0f;
        b.attackMs = 1.0f; b.releaseMs = 20.0f;
        runSine (b, -60.0f, 48000, &g); CHECK (g < -9.0f && g > -13.0f);
        runSine (b, -6.0f, 48000, &g);  CHECK (std::fabs (g) < 0.5f);
    }
    { // a loud signal OUTSIDE the band does not trigger it (band-pass sidechain): 100 Hz loud, band at 5 kHz
        EqBandParams b = band; b.freq = 5000.0f; b.dynMode = 1; b.thresholdDb = -30.0f; b.ratio = 20.0f; b.kneeDb = 0.0f;
        EqParams p; for (auto& x : p.band) x.on = false; p.band[1] = b;
        PadEq eq; eq.prepare (sr); eq.setParams (p);
        std::vector<float> l (48000), r;
        for (int i = 0; i < 48000; ++i) l[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 100.0 * i / sr);
        r = l; const auto in = l;
        for (int pos = 0; pos < 48000; pos += 256) { eq.setParams (p); eq.process (l.data() + pos, r.data() + pos, 256 < 48000 - pos ? 256 : 48000 - pos); }
        float maxDiff = 0; for (int i = 24000; i < 48000; ++i) maxDiff = std::fmax (maxDiff, std::fabs (l[(size_t) i] - in[(size_t) i]));
        CHECK (maxDiff < 0.02f);
    }
    { // finite and click-free while the level jumps (quiet -> loud -> quiet)
        EqBandParams b = band; b.dynMode = 1; b.thresholdDb = -30.0f; b.ratio = 8.0f; b.attackMs = 5.0f; b.releaseMs = 50.0f;
        EqParams p; for (auto& x : p.band) x.on = false; p.band[1] = b;
        PadEq eq; eq.prepare (sr); eq.setParams (p);
        const int frames = 48000;
        std::vector<float> l ((size_t) frames), r;
        for (int i = 0; i < frames; ++i)
        {
            const float amp = (i / 12000) % 2 == 0 ? 0.01f : 0.5f;
            l[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr);
        }
        r = l;
        for (int pos = 0; pos < frames; pos += 256) { eq.setParams (p); eq.process (l.data() + pos, r.data() + pos, std::min (256, frames - pos)); }
        bool finite = true; for (float v : l) if (! std::isfinite (v) || std::fabs (v) > 2.0f) finite = false;
        CHECK (finite);
    }
}

// ---------------------------------------------------------------- EQ band solo (listening aid)
static void testEqSolo()
{
    std::printf("EQ band solo\n");
    const double sr = 48000.0;
    auto levelAt = [&] (double hz, int solo, float gainDb)
    {
        EqParams p;
        p.band[1] = { true, EqType::Bell, 1000.0f, gainDb, 4.0f };
        p.soloBand = solo;
        PadEq eq; eq.prepare (sr); eq.setParams (p);
        const int frames = 24000;
        std::vector<float> l ((size_t) frames), r;
        for (int i = 0; i < frames; ++i) l[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * hz * i / sr);
        r = l; const auto in = l;
        for (int pos = 0; pos < frames; pos += 256) { eq.setParams (p); eq.process (l.data() + pos, r.data() + pos, std::min (256, frames - pos)); }
        double a = 0, b = 0;
        for (int i = frames / 2; i < frames; ++i) { a += (double) l[(size_t) i] * l[(size_t) i]; b += (double) in[(size_t) i] * in[(size_t) i]; }
        return (float) (10.0 * std::log10 (a / b));
    };
    CHECK (std::fabs (levelAt (1000.0, -1, 0.0f)) < 0.2f);              // no solo: flat band, untouched
    CHECK (std::fabs (levelAt (1000.0, 1, 0.0f)) < 1.0f);               // solo: the band's own frequency passes
    CHECK (levelAt (100.0, 1, 0.0f) < -20.0f);                          // everything else is gone
    CHECK (levelAt (10000.0, 1, 0.0f) < -20.0f);
    CHECK (std::fabs (levelAt (1000.0, 1, 12.0f)) < 1.0f);              // band-pass listening does not depend on the band gain
    { // switching solo on and off is click free and leaves the EQ intact afterwards
        EqParams p; p.band[1] = { true, EqType::Bell, 1000.0f, 0.0f, 2.0f };
        PadEq eq; eq.prepare (sr); eq.setParams (p);
        const int frames = 48000;
        std::vector<float> l ((size_t) frames), r;
        for (int i = 0; i < frames; ++i) l[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr);
        r = l; const auto in = l;
        for (int pos = 0; pos < frames; pos += 256)
        {
            p.soloBand = (pos >= 12000 && pos < 30000) ? 1 : -1;
            eq.setParams (p);
            eq.process (l.data() + pos, r.data() + pos, std::min (256, frames - pos));
        }
        float maxStep = 0; for (int i = 1; i < frames; ++i) maxStep = std::fmax (maxStep, std::fabs (l[(size_t) i] - l[(size_t) i - 1]));
        CHECK (maxStep < 0.08f);                                        // a 1 kHz sine of 0.5 steps by ~0.065 at most
        float tailDiff = 0; for (int i = 44000; i < frames; ++i) tailDiff = std::fmax (tailDiff, std::fabs (l[(size_t) i] - in[(size_t) i]));
        CHECK (tailDiff < 0.01f);
    }
}

// ---------------------------------------------------------------- slice detection
static void testSlicer()
{
    std::printf("Slice detection\n");
    const double sr = 48000.0;
    // synthetic loop: noise bursts with a fast decay at known times (seconds, amplitude), plus a quiet noise floor
    auto make = [&] (const std::vector<std::pair<double, float>>& hits, double seconds)
    {
        const int n = (int) (seconds * sr);
        std::vector<float> x ((size_t) n, 0.0f);
        unsigned seed = 7u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; };
        for (int i = 0; i < n; ++i) x[(size_t) i] = 0.0005f * rnd();
        for (auto& h : hits)
            for (int i = (int) (h.first * sr), k = 0; i < n && k < (int) (0.15 * sr); ++i, ++k)
                x[(size_t) i] += h.second * rnd() * std::exp (-(float) k / (0.03f * (float) sr));
        return x;
    };
    const std::vector<std::pair<double, float>> hits = { { 0.20, 0.9f }, { 0.70, 0.8f }, { 1.20, 0.9f }, { 1.70, 0.7f } };
    const auto x = make (hits, 2.2);
    const int n = (int) x.size();
    SliceConfig cfg;
    auto pts = detectSlicePoints (x.data(), x.data(), n, sr, cfg);
    CHECK (pts.size() == 4);
    for (size_t i = 0; i < pts.size() && i < 4; ++i)
        CHECK (std::abs ((double) pts[i] / sr - hits[i].first) < 0.008);          // within 8 ms of the real attack, never after it by much
    CHECK (std::is_sorted (pts.begin(), pts.end()));

    { // cap on the number of slices keeps the strongest
        cfg.maxSlices = 2;
        auto two = detectSlicePoints (x.data(), x.data(), n, sr, cfg);
        CHECK (two.size() == 2);
        cfg.maxSlices = 16;
    }
    { // sensitivity: a very soft hit (-30 dB) is found only with high sensitivity
        auto soft = make ({ { 0.2, 0.9f }, { 0.7, 0.9f }, { 1.2, 0.03f } }, 1.8);
        SliceConfig c; c.sensitivity = 0.1f;
        const auto lo = detectSlicePoints (soft.data(), soft.data(), (int) soft.size(), sr, c);
        c.sensitivity = 1.0f;
        const auto hi = detectSlicePoints (soft.data(), soft.data(), (int) soft.size(), sr, c);
        CHECK (lo.size() == 2);
        CHECK (hi.size() == 3);
    }
    { // minimum slice length: two hits 30 ms apart become one cut
        auto close = make ({ { 0.2, 0.9f }, { 0.23, 0.9f }, { 0.8, 0.9f } }, 1.2);
        SliceConfig c; c.minSliceMs = 100.0f;
        CHECK (detectSlicePoints (close.data(), close.data(), (int) close.size(), sr, c).size() == 2);
    }
    { // equal parts
        SliceConfig c; c.mode = SliceMode::EqualParts; c.parts = 4;
        auto eq = detectSlicePoints (x.data(), x.data(), n, sr, c);
        CHECK (eq.size() == 4 && eq[0] == 0 && eq[2] == n / 2);
        c.parts = 40;                                       // capped at 16
        CHECK (detectSlicePoints (x.data(), x.data(), n, sr, c).size() == 16);
    }
    { // silence / tiny input never crash and never return nothing
        std::vector<float> z (48000, 0.0f);
        SliceConfig c;
        CHECK (detectSlicePoints (z.data(), z.data(), (int) z.size(), sr, c).size() == 1);
        CHECK (detectSlicePoints (z.data(), z.data(), 1, sr, c).size() == 1);
        CHECK (detectSlicePoints (nullptr, nullptr, 0, sr, c).size() == 1);
    }
    { // user-edited cut list is cleaned
        auto p = normaliseSlicePoints ({ 5000, 100, 100, -4, 999999, 5100, 20000 }, 48000, 480, 16);
        CHECK (p.size() == 3 && p[0] == 100 && p[1] == 5000 && p[2] == 20000);
        CHECK (normaliseSlicePoints ({}, 100, 10, 16) == std::vector<int> { 0 });
    }
    { // zero-crossing snap: cuts land where the signal crosses zero
        std::vector<float> s (24000);
        for (int i = 0; i < 24000; ++i) s[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 300.0 * i / sr);
        for (int i = 9600; i < 24000; ++i) s[(size_t) i] *= 8.0f;   // a level jump = onset
        SliceConfig c; c.snapToZeroCrossing = true;
        auto p = detectSlicePoints (s.data(), s.data(), 24000, sr, c);
        CHECK (! p.empty());
        const int last = p.back();
        CHECK (last > 0 && std::fabs (0.5f * (s[(size_t) last] + s[(size_t) last])) < 0.05f);
    }
}

// ---------------------------------------------------------------- Mutate (variant generation)
static void testMutate()
{
    std::printf("Mutate\n");
    MutateValues base; base.pitchSemitones = 2.0f; base.pan = -0.2f; base.attackMs = 5.0f; base.reverbWet = 0.25f;
    base.eqGainDb = { 1.0f, 0.0f, -2.0f, 0.5f };
    MutateConfig cfg;
    const auto v = generateVariants (base, cfg, 8, 42);
    CHECK (v.size() == 8);
    CHECK (v[0].pitchSemitones == base.pitchSemitones && v[0].pan == base.pan && v[0].eqGainDb == base.eqGainDb);   // variant 0 = original
    bool allDifferent = true;
    for (size_t i = 1; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j)
            if (std::fabs (v[i].pitchSemitones - v[j].pitchSemitones) < 1e-4f) allDifferent = false;
    CHECK (allDifferent);                                           // spread: no two variants share a pitch
    for (const auto& x : v)                                         // inside the allowed deviation (amount 0.5: pitch +-0.5 st)
    {
        CHECK (std::fabs (x.pitchSemitones - base.pitchSemitones) <= 0.5f + 1e-4f);
        CHECK (std::fabs (x.pan - base.pan) <= 0.25f + 1e-4f);
        CHECK (x.attackMs >= base.attackMs - 1e-4f && x.attackMs <= base.attackMs + 20.0f + 1e-4f);   // only slower attacks
        CHECK (x.reverbWet >= 0.0f && x.reverbWet <= 1.0f);
        CHECK (std::fabs (x.gainDb - base.gainDb) < 1e-6f);        // gain disabled by default: untouched
    }
    CHECK (generateVariants (base, cfg, 8, 42)[3].pitchSemitones == v[3].pitchSemitones);        // deterministic
    CHECK (generateVariants (base, cfg, 8, 43)[3].pitchSemitones != v[3].pitchSemitones);        // new seed = new variants
    { // disabled parameters stay exactly as they were; amount 0 = nothing changes
        MutateConfig c; c.pitch = false; c.pan = false; c.attack = false; c.eq = false; c.reverb = false;
        for (const auto& x : generateVariants (base, c, 6, 1)) CHECK (x.pitchSemitones == base.pitchSemitones && x.eqGainDb == base.eqGainDb);
        MutateConfig z; z.amount = 0.0f;
        for (const auto& x : generateVariants (base, z, 6, 1)) CHECK (x.pan == base.pan && x.attackMs == base.attackMs);
    }
    { // legal ranges are respected even at the extremes
        MutateValues e; e.pitchSemitones = 23.9f; e.pan = 0.99f; e.gainDb = 11.9f; e.eqGainDb = { 17.9f, -17.9f, 0.0f, 0.0f };
        MutateConfig c; c.gain = true; c.amount = 1.0f;
        for (const auto& x : generateVariants (e, c, 16, 7))
            CHECK (x.pitchSemitones <= 24.0f && x.pan <= 1.0f && x.gainDb <= 12.0f && x.eqGainDb[0] <= 18.0f && x.eqGainDb[1] >= -18.0f);
    }
    CHECK (generateVariants (base, cfg, 1, 5).size() == 1);
    CHECK (generateVariants (base, cfg, 99, 5).size() == 16);
}

static void testTempo()
{
    std::printf ("Tempo detection and time-stretch\n");
    const double sr = 44100.0;
    // 8 s of a drum-like loop at 120 BPM: kick on every beat, noisy hat on the off-beats
    auto makeLoop = [&] (double bpm, double seconds)
    {
        auto s = std::make_shared<SampleData>();
        const int n = (int) (seconds * sr);
        s->left.assign ((size_t) n, 0.0f);
        const double beat = 60.0 / bpm * sr;
        unsigned seed = 12345;
        for (int b = 0; b * beat < n; ++b)
        {
            const int at = (int) (b * beat);
            for (int i = 0; i < 6000 && at + i < n; ++i)
                s->left[(size_t) (at + i)] += 0.9f * std::sin (2.0 * 3.14159265 * 60.0 * i / sr) * std::exp (-i / 1500.0f);
            const int hat = (int) ((b + 0.5) * beat);
            for (int i = 0; i < 1500 && hat + i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                s->left[(size_t) (hat + i)] += 0.3f * ((float) (seed >> 8) / 8388608.0f - 1.0f) * std::exp (-i / 400.0f);
            }
        }
        s->right = s->left; s->sampleRate = sr;
        return s;
    };
    for (double bpm : { 90.0, 120.0, 140.0 })
    {
        const auto loop = makeLoop (bpm, 8.0);
        const auto t = detectTempo (*loop);
        std::printf ("  loop %.0f BPM -> detected %.1f (confidence %.2f)\n", bpm, t.bpm, t.confidence);
        CHECK (std::fabs (t.bpm - bpm) < 2.0 || std::fabs (t.bpm - 2.0 * bpm) < 2.0 || std::fabs (t.bpm - 0.5 * bpm) < 2.0);
        CHECK (t.confidence > 0.1f);
    }
    {   // a kick (one-shot) has no tempo
        SampleData kick; kick.sampleRate = sr; kick.left.assign (20000, 0.0f);
        for (int i = 0; i < 20000; ++i) kick.left[(size_t) i] = std::sin (i * 0.02f) * std::exp (-i / 3000.0f);
        kick.right = kick.left;
        CHECK (detectTempo (kick).bpm == 0.0);
    }
    {   // stretch: length follows the ratio, pitch stays
        auto tone = std::make_shared<SampleData>();
        tone->sampleRate = sr;
        const int n = (int) (3.0 * sr);
        tone->left.resize ((size_t) n);
        for (int i = 0; i < n; ++i) tone->left[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265 * 440.0 * i / sr);
        tone->right = tone->left;
        for (double ratio : { 0.75, 1.25, 2.0 })
        {
            const auto st = timeStretch (*tone, ratio);
            CHECK (st != nullptr);
            if (! st) continue;
            CHECK (std::abs (st->numFrames () - (int) std::lround (n * ratio)) <= 1);
            // pitch: count upward zero crossings in the middle second
            const int a = st->numFrames() / 2 - (int) sr / 2, b = a + (int) sr;
            int zc = 0;
            for (int i = a + 1; i < b; ++i) if (st->left[(size_t) i - 1] < 0.0f && st->left[(size_t) i] >= 0.0f) ++zc;
            std::printf ("  stretch x%.2f: %d zero crossings in 1 s (440 expected)\n", ratio, zc);
            CHECK (zc >= 425 && zc <= 455);
            float peak = 0.0f; for (int i = 0; i < st->numFrames(); ++i) peak = std::max (peak, std::fabs (st->left[(size_t) i]));
            CHECK (peak > 0.4f && peak < 0.7f);   // no level change / clipping
        }
        CHECK (timeStretch (*tone, 3.0) == nullptr && timeStretch (*tone, 0.3) == nullptr);
    }
}

static void testLoopChokeChromatic()
{
    std::printf ("Loop, choke groups, chromatic\n");

    // ---- Loop: a 1000-frame ramp looping from 50 % keeps playing far beyond its length
    {
        Rig r (256);
        r.eng.setPadSample (0, makeRamp (1000));
        PadParams p; p.midiNote = 36; p.mode = PlayMode::Loop; p.loopStartFraction = 0.5f; p.loopCrossfadeSeconds = 0.0f;
        p.declickSeconds = 0.0f; p.releaseSeconds = 0.005f;
        r.eng.setPadParams (0, p);
        r.run();
        auto out = renderLeft (r, 6000, { { 0, true, 36, 1.0f } });
        CHECK (r.eng.isPadPlaying (0));
        float mn = 1.0f, mx = 0.0f;
        for (size_t i = 3000; i < out.size(); ++i) { mn = std::fmin (mn, out[i]); mx = std::fmax (mx, out[i]); }
        CHECK (mn >= 0.49f && mn < 0.52f);      // wraps back to the loop point (0.5), never to the start
        CHECK (mx > 0.98f && mx <= 1.0f);
        r.run ({ { 0, false, 36, 0.0f } });
        r.run(); r.run();
        CHECK (! r.eng.isPadPlaying (0));       // Note Off ends the loop

        // mouse: click starts, second click stops, mouse-up does nothing
        r.eng.triggerPad (0, 1.0f); r.run();
        r.eng.releasePad (0); r.run(); r.run();
        CHECK (r.eng.isPadPlaying (0));
        r.eng.triggerPad (0, 1.0f); r.run(); r.run(); r.run();
        CHECK (! r.eng.isPadPlaying (0));
    }
    // ---- Loop cross-fade: a steady tone stays steady across the seam (no bump, no hole)
    {
        Rig r (256);
        r.eng.setPadSample (0, makeSample (2000, 1.0f));
        PadParams p; p.midiNote = 36; p.mode = PlayMode::Loop; p.loopStartFraction = 0.25f; p.loopCrossfadeSeconds = 0.010f;
        p.declickSeconds = 0.0f;
        r.eng.setPadParams (0, p);
        r.run();
        auto out = renderLeft (r, 9000, { { 0, true, 36, 1.0f } });
        float mn = 2.0f, mx = 0.0f;
        for (size_t i = 100; i < out.size(); ++i) { mn = std::fmin (mn, out[i]); mx = std::fmax (mx, out[i]); }
        CHECK (mn > 0.999f && mx < 1.001f);
    }

    // ---- Choke groups: pad 1 (open hat) is cut by pad 2 (closed hat) of the same group, pad 3 (other group) is not
    {
        Rig r (256);
        for (int i = 0; i < 3; ++i) r.eng.setPadSample (i, makeSample (48000, 0.5f));
        PadParams a; a.midiNote = 36; a.chokeGroup = 1;
        PadParams b; b.midiNote = 37; b.chokeGroup = 1;
        PadParams c; c.midiNote = 38; c.chokeGroup = 2;
        r.eng.setPadParams (0, a); r.eng.setPadParams (1, b); r.eng.setPadParams (2, c);
        r.run();
        r.run ({ { 0, true, 36, 1.0f }, { 0, true, 38, 1.0f } });
        r.run(); r.run();
        CHECK (r.eng.isPadPlaying (0) && r.eng.isPadPlaying (2));
        r.run ({ { 0, true, 37, 1.0f } });
        r.run(); r.run();                       // 4 ms fade
        CHECK (! r.eng.isPadPlaying (0));       // cut
        CHECK (r.eng.isPadPlaying (1));         // the cutter plays
        CHECK (r.eng.isPadPlaying (2));         // other group untouched
        // group 0 = no choke
        a.chokeGroup = 0; b.chokeGroup = 0;
        r.eng.setPadParams (0, a); r.eng.setPadParams (1, b);
        r.run ({ { 0, true, 36, 1.0f } });
        r.run ({ { 0, true, 37, 1.0f } });
        r.run(); r.run();
        CHECK (r.eng.isPadPlaying (0) && r.eng.isPadPlaying (1));
    }

    // ---- Chromatic: pad 1 plays the whole keyboard, transposed by (note - 36); notes of other loaded pads stay theirs
    {
        Rig r (256);
        r.eng.setPadSample (0, makeRamp (48000));
        r.eng.setPadSample (1, makeSample (48000, 0.5f));
        PadParams a; a.midiNote = 36; a.chromatic = true; a.declickSeconds = 0.0f; a.mode = PlayMode::Gate;
        PadParams b; b.midiNote = 37;
        r.eng.setPadParams (0, a); r.eng.setPadParams (1, b);
        r.run();
        // same note: normal speed
        auto o0 = renderLeft (r, 4000, { { 0, true, 36, 1.0f } });
        CHECK (std::fabs (o0[3999] - 4000.0f / 48000.0f) < 0.005f);
        r.run ({ { 0, false, 36, 0.0f } }); r.run(); r.run();
        // 48 = one octave up: twice as fast
        r.eng.setPadParams (0, a);
        auto o1 = renderLeft (r, 4000, { { 0, true, 48, 1.0f } });
        CHECK (std::fabs (o1[3999] - 8000.0f / 48000.0f) < 0.01f);
        r.run ({ { 0, false, 48, 0.0f } }); r.run(); r.run();
        CHECK (! r.eng.isPadPlaying (0));       // Note Off releases the chromatic voice (Gate)
        // 24 = two octaves down relative to 48: free note, plays pad 1 at +... (note - 36 = -12 for 24)
        auto o2 = renderLeft (r, 4000, { { 0, true, 24, 1.0f } });
        CHECK (std::fabs (o2[3999] - 2000.0f / 48000.0f) < 0.005f);
        r.run ({ { 0, false, 24, 0.0f } }); r.run(); r.run();
        // note 37 belongs to the loaded pad 2: pad 1 must stay silent for it
        r.run ({ { 0, true, 37, 1.0f } });
        r.run();
        CHECK (r.eng.isPadPlaying (1) && ! r.eng.isPadPlaying (0));
        // an EMPTY pad does not block its note: clear pad 2, note 37 now plays pad 1 (+1 semitone)
        r.eng.setPadSample (1, nullptr);
        r.eng.stopAll(); r.run(); r.run(); r.run();
        r.run ({ { 0, true, 37, 1.0f } });
        r.run();
        CHECK (r.eng.isPadPlaying (0));
    }
}

static void testLimiter()
{
    std::printf ("Output limiter\n");
    Limiter lim;
    lim.prepare (48000.0);
    const int n = lim.latency();
    CHECK (n > 20 && n < 200);

    // quiet signal: untouched, only delayed by latency()
    {
        std::vector<float> l (2000, 0.0f), r (2000, 0.0f);
        l[100] = 0.5f; r[100] = -0.25f;
        lim.process (l.data(), r.data(), 2000, 0.95f, true);
        CHECK (std::fabs (l[100 + n] - 0.5f) < 1e-6f && std::fabs (r[100 + n] + 0.25f) < 1e-6f);
        CHECK (lim.lastReduction() == 0.0f);
    }
    // loud signal (+12 dB bursts of a sine, a lone 4x spike, DC step): never above the ceiling, still audible
    {
        lim.prepare (48000.0);
        const float ceiling = 0.9f;
        std::vector<float> l, r;
        for (int i = 0; i < 48000; ++i)
        {
            float v = 0.8f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * i / 48000.0);
            if (i > 10000 && i < 20000) v *= 5.0f;          // burst
            if (i == 30000) v = 4.0f;                       // lone spike
            if (i > 40000) v = 3.0f;                        // DC step
            l.push_back (v); r.push_back (-v * 0.5f);
        }
        float worst = 0.0f, loudest = 0.0f;
        for (size_t off = 0; off < l.size(); off += 256)
        {
            lim.process (l.data() + off, r.data() + off, (int) std::min<size_t> (256, l.size() - off), ceiling, true);
        }
        for (size_t i = 0; i < l.size(); ++i) { worst = std::fmax (worst, std::fmax (std::fabs (l[i]), std::fabs (r[i]))); }
        for (size_t i = 12000; i < 18000; ++i) loudest = std::fmax (loudest, std::fabs (l[i]));
        CHECK (worst <= ceiling + 1e-6f);
        CHECK (loudest > 0.5f);                              // limited, not muted
        // after the DC step settles the output sits at the ceiling (3.0 limited to 0.9)
        CHECK (std::fabs (l[47000] - 0.9f) < 0.02f);
    }
    // disabled: transparent even for loud input (delay only)
    {
        lim.prepare (48000.0);
        std::vector<float> l (1000, 0.0f), r (1000, 0.0f);
        l[50] = 3.0f;
        lim.process (l.data(), r.data(), 1000, 0.5f, false);
        CHECK (std::fabs (l[50 + n] - 3.0f) < 1e-6f);
    }
}

int main()
{
    testLoopChokeChromatic();
    testLimiter();
    testTempo();
    testEq();
    testFx();
    testReverb();
    testDynamicEq();
    testEqSolo();
    testSlicer();
    testMutate();
    testVelocity();
    testLiveParameters();
    testLivePitch();
    testEnvelope();
    testFadesAndReverse();
    testVelocityCurve();
    testVoiceLimits();
    testNames();
    testAllSoundOff();
    testQueue();
    testMidiOffsetAndOneShot();
    testPadIndependence();
    testGateAndRetrigger();
    testPolyphonyAndStealing();
    testMouseAndGarbage();
    testRegionAndEmpty();
    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
