#pragma once

#include <array>

namespace dm
{
constexpr int kEqBands = 4;

enum class EqType : int
{
    Bell = 0,
    LowShelf = 1,
    HighShelf = 2,
    HighPass = 3,    // 12 dB/oct, Q = resonance
    LowPass = 4      // 12 dB/oct, Q = resonance
};

struct EqBandParams
{
    bool on = true;
    EqType type = EqType::Bell;
    float freq = 1000.0f;     // 20 .. 20000 Hz
    float gainDb = 0.0f;      // -18 .. +18 dB (ignored by high-pass / low-pass)
    float q = 1.0f;           // 0.1 .. 10

    // Dynamic EQ. dynMode 0 = static band. Otherwise the band gain is scaled by an amount s in [0, 1] driven by the
    // level measured IN the band (band-pass sidechain at the band frequency and Q):
    //   Compress : s = 0 below the threshold, rising to 1 as the level goes above it (the band engages on loud signal)
    //   Expand   : s = 0 above the threshold, rising to 1 as the level falls below it (the band engages on quiet signal)
    // The excess in dB is shaped by the ratio and divided by `rangeDb` (full gain reached when excess*(1-1/ratio) = range).
    // A ratio of 1 means "no dynamics" (s stays 0).
    int dynMode = 0;          // 0 off, 1 compress, 2 expand
    float thresholdDb = -24.0f;   // -60 .. 0
    float ratio = 4.0f;           // 1 .. 20
    float attackMs = 10.0f;       // 0.1 .. 200
    float releaseMs = 100.0f;     // 5 .. 2000
    float kneeDb = 6.0f;          // 0 .. 24
    float rangeDb = 12.0f;        // 1 .. 24
};

// Dynamics amount s (0..1) for a detected level in dB (static curve, no time smoothing). Exposed for the tests / UI.
float dynamicAmount (const EqBandParams& band, float levelDb) noexcept;

// Four independent bands per pad, plus a pad-level on/off.
struct EqParams
{
    bool on = true;
    // Listening aid (not a sound parameter): -1 = off, 0..3 = hear ONLY what passes through a band-pass centred on that
    // band (its frequency and Q), taken at the EQ input. Meant for hunting an unpleasant frequency.
    int soloBand = -1;
    std::array<EqBandParams, kEqBands> band {};

    EqParams()
    {
        band[0] = { true, EqType::LowShelf, 80.0f, 0.0f, 0.71f };
        band[1] = { true, EqType::Bell, 400.0f, 0.0f, 1.0f };
        band[2] = { true, EqType::Bell, 2500.0f, 0.0f, 1.0f };
        band[3] = { true, EqType::HighShelf, 8000.0f, 0.0f, 0.71f };
    }
};

// Transfer function of one band (RBJ "Audio EQ Cookbook" biquad), normalised so a0 = 1.
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

BiquadCoeffs designBand (const EqBandParams& band, double sampleRate);

// Magnitude of the response in dB at `freq` (used by the EQ curve in the UI and by the tests).
double magnitudeDb (const BiquadCoeffs& c, double freq, double sampleRate);

// True when a band does nothing audible (a bell/shelf at 0 dB, or a disabled band).
bool isBandNeutral (const EqBandParams& band) noexcept;

// Stereo 4-band equaliser of one pad. Real-time safe: no allocation after construction.
//  - parameters are smoothed (frequency, gain and Q glide) -> no clicks when automated
//  - enabling / disabling a band or the whole EQ is a short crossfade -> no clicks
//  - neutral bands cost nothing
class PadEq
{
public:
    void prepare (double sampleRate);
    void reset();

    void setParams (const EqParams& params) noexcept;   // audio thread, once per block
    // true while the EQ still changes the signal or is gliding / ringing -> the caller keeps feeding it
    bool isAlive() const noexcept { return alive; }
    void process (float* left, float* right, int numFrames) noexcept;

private:
    struct Band
    {
        EqBandParams target;
        EqType type = EqType::Bell;
        // dynamics (sidechain band-pass + envelope in dB)
        double detZ1 = 0.0, detZ2 = 0.0, envDb = -120.0;
        double freqLog2 = 10.0, gainDb = 0.0, qLog2 = 0.0;   // smoothed values
        double mix = 0.0;                                      // 0 = bypass, 1 = fully applied (crossfade)
        double mixTarget = 0.0;
        BiquadCoeffs c;
        double z1[2] = { 0.0, 0.0 }, z2[2] = { 0.0, 0.0 };
        bool initialised = false;
        bool ringing = false;
    };

    static constexpr int kSubBlock = 32;
    void processSub (float* l, float* r, int n) noexcept;

    double sr = 48000.0;
    double coef = 0.2;   // smoothing per sub-block (about 10 ms)
    std::array<Band, kEqBands> bands {};

    // solo (band-pass listening)
    int soloBand = -1;
    double soloMix = 0.0, soloMixTarget = 0.0, soloFreqLog2 = 10.0, soloQLog2 = 0.0;
    double soloZ1[2] = { 0.0, 0.0 }, soloZ2[2] = { 0.0, 0.0 };
    bool alive = false;
};
} // namespace dm
