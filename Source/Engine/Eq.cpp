#include "Eq.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace dm
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

inline double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

bool isBandNeutral (const EqBandParams& b) noexcept
{
    if (! b.on) return true;
    if (b.type == EqType::HighPass || b.type == EqType::LowPass) return false;
    return std::fabs (b.gainDb) < 0.005f;
}

float dynamicAmount (const EqBandParams& b, float levelDb) noexcept
{
    if (b.dynMode == 0) return 1.0f;
    const float knee = std::max (0.0f, b.kneeDb);
    // excess above the threshold (compress) or below it (expand), with a soft knee
    const float over = b.dynMode == 1 ? levelDb - b.thresholdDb : b.thresholdDb - levelDb;
    float soft;
    if (knee < 1.0e-3f) soft = std::max (0.0f, over);
    else if (over <= -0.5f * knee) soft = 0.0f;
    else if (over >= 0.5f * knee) soft = over;
    else soft = (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
    const float ratio = std::max (1.0f, b.ratio);
    const float x = soft * (1.0f - 1.0f / ratio);
    const float s = x / std::max (0.1f, b.rangeDb);
    return s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
}

BiquadCoeffs designBand (const EqBandParams& band, double sr)
{
    const double f = clampd (band.freq, 10.0, sr * 0.45);
    const double q = clampd (band.q, 0.05, 20.0);
    const double w0 = 2.0 * kPi * f / sr;
    const double cs = std::cos (w0), sn = std::sin (w0);
    const double alpha = sn / (2.0 * q);
    const double A = std::pow (10.0, clampd (band.gainDb, -36.0, 36.0) / 40.0);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (band.type)
    {
        case EqType::Bell:
            b0 = 1.0 + alpha * A;  b1 = -2.0 * cs;  b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;  a1 = -2.0 * cs;  a2 = 1.0 - alpha / A;
            break;
        case EqType::LowShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cs + s);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
            b2 = A * ((A + 1.0) - (A - 1.0) * cs - s);
            a0 = (A + 1.0) + (A - 1.0) * cs + s;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            a2 = (A + 1.0) + (A - 1.0) * cs - s;
            break;
        }
        case EqType::HighShelf:
        {
            const double s = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cs + s);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
            b2 = A * ((A + 1.0) + (A - 1.0) * cs - s);
            a0 = (A + 1.0) - (A - 1.0) * cs + s;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            a2 = (A + 1.0) - (A - 1.0) * cs - s;
            break;
        }
        case EqType::HighPass:
            b0 = (1.0 + cs) * 0.5;  b1 = -(1.0 + cs);  b2 = (1.0 + cs) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cs;    a2 = 1.0 - alpha;
            break;
        case EqType::LowPass:
            b0 = (1.0 - cs) * 0.5;  b1 = 1.0 - cs;     b2 = (1.0 - cs) * 0.5;
            a0 = 1.0 + alpha;       a1 = -2.0 * cs;    a2 = 1.0 - alpha;
            break;
    }

    BiquadCoeffs c;
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
    return c;
}

double magnitudeDb (const BiquadCoeffs& c, double freq, double sr)
{
    const double w = 2.0 * kPi * clampd (freq, 1.0, sr * 0.5) / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
    const double mag = std::abs (num) / std::max (1.0e-12, std::abs (den));
    return 20.0 * std::log10 (std::max (mag, 1.0e-9));
}

// ----------------------------------------------------------------------------------------- PadEq
void PadEq::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    coef = 1.0 - std::exp (-(double) kSubBlock / (0.010 * sr));
    reset();
}

void PadEq::reset()
{
    for (auto& b : bands)
    {
        b.initialised = false;
        b.mix = 0.0;
        b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0;
        b.ringing = false;
        b.detZ1 = b.detZ2 = 0.0;
        b.envDb = -120.0;
    }
    soloMix = soloMixTarget = 0.0;
    soloZ1[0] = soloZ1[1] = soloZ2[0] = soloZ2[1] = 0.0;
    soloBand = -1;
    alive = false;
}

void PadEq::setParams (const EqParams& p) noexcept
{
    bool any = false;
    for (int i = 0; i < kEqBands; ++i)
    {
        auto& b = bands[(size_t) i];
        b.target = p.band[(size_t) i];
        const bool wanted = p.on && ! isBandNeutral (b.target);
        b.mixTarget = wanted ? 1.0 : 0.0;

        if (! b.initialised)
        {
            // first use (or after reset): start exactly on the target, no glide from a default
            b.type = b.target.type;
            b.freqLog2 = std::log2 ((double) clampd (b.target.freq, 10.0, 24000.0));
            b.gainDb = b.target.dynMode == 0 ? (double) b.target.gainDb : (double) (b.target.gainDb * dynamicAmount (b.target, -120.0f));
            b.detZ1 = b.detZ2 = 0.0;
            b.envDb = -120.0;
            b.qLog2 = std::log2 (clampd (b.target.q, 0.05, 20.0));
            b.c = designBand (b.target, sr);
            b.initialised = true;
        }
        // a type change cannot glide: it takes effect while the band is faded out (mix ~ 0) or immediately if inactive
        if (b.type != b.target.type)
        {
            if (b.mix < 0.001 || b.mixTarget == 0.0)
            {
                b.type = b.target.type;
                b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0;
            }
            else
                b.mixTarget = 0.0;   // fade out, switch type, fade back in
        }
        if (wanted || b.mix > 0.0005 || b.ringing) any = true;
    }
    if (p.soloBand >= 0 && p.soloBand < kEqBands)
    {
        if (soloMixTarget == 0.0 && soloMix <= 0.0)
        {
            soloFreqLog2 = std::log2 ((double) clampd (p.band[(size_t) p.soloBand].freq, 10.0, 24000.0));
            soloQLog2 = std::log2 (clampd (p.band[(size_t) p.soloBand].q, 0.05, 20.0));
            soloZ1[0] = soloZ1[1] = soloZ2[0] = soloZ2[1] = 0.0;
        }
        soloBand = p.soloBand;
        soloMixTarget = 1.0;
    }
    else
        soloMixTarget = 0.0;
    if (soloMixTarget > 0.0 || soloMix > 0.0005) any = true;
    alive = any;
}

void PadEq::processSub (float* l, float* r, int n) noexcept
{
    // solo: keep a copy of the EQ input; the band-pass of it is crossfaded in at the end
    const bool soloRun = soloMixTarget > 0.0 || soloMix > 0.0;
    float inL[kSubBlock], inR[kSubBlock];
    if (soloRun)
        for (int i = 0; i < n; ++i) { inL[i] = l[i]; inR[i] = r[i]; }

    for (auto& b : bands)
    {
        const double tFreq = std::log2 (clampd (b.target.freq, 10.0, 24000.0));
        const double tQ = std::log2 (clampd (b.target.q, 0.05, 20.0));

        // dynamic EQ: measure the level inside the band, derive the gain scale (static bands: scale = 1, bit-exact)
        double gainTarget = (double) b.target.gainDb;
        if (b.target.dynMode != 0)
        {
            if (b.mixTarget > 0.0 || b.mix > 0.0005)
            {
                const double w0 = 2.0 * kPi * clampd ((double) b.target.freq, 20.0, sr * 0.45) / sr;
                const double alpha = std::sin (w0) / (2.0 * clampd ((double) b.target.q, 0.7, 10.0));
                const double a0 = 1.0 + alpha;
                const double d0 = alpha / a0, d2 = -alpha / a0, da1 = -2.0 * std::cos (w0) / a0, da2 = (1.0 - alpha) / a0;
                double peak = 0.0, z1 = b.detZ1, z2 = b.detZ2;
                for (int i = 0; i < n; ++i)
                {
                    const double in = 0.5 * ((double) l[i] + (double) r[i]);
                    const double y = d0 * in + z1;                // transposed direct form II band-pass
                    z1 = -da1 * y + z2;
                    z2 = d2 * in - da2 * y;
                    peak = std::max (peak, std::fabs (y));
                }
                b.detZ1 = z1; b.detZ2 = z2;
                const double lvl = 20.0 * std::log10 (std::max (peak, 1.0e-6));
                const double ms = lvl > b.envDb ? (double) b.target.attackMs : (double) b.target.releaseMs;
                const double k = 1.0 - std::exp (-(double) n / (std::max (0.05, ms) * 0.001 * sr));
                b.envDb += (lvl - b.envDb) * k;
            }
            gainTarget = (double) b.target.gainDb * (double) dynamicAmount (b.target, (float) b.envDb);
        }

        // glide the parameters
        b.freqLog2 += (tFreq - b.freqLog2) * coef;
        b.gainDb += (gainTarget - b.gainDb) * coef;
        b.qLog2 += (tQ - b.qLog2) * coef;
        if (std::fabs (tFreq - b.freqLog2) < 1.0e-5) b.freqLog2 = tFreq;
        if (std::fabs (gainTarget - b.gainDb) < 1.0e-4) b.gainDb = gainTarget;
        if (std::fabs (tQ - b.qLog2) < 1.0e-5) b.qLog2 = tQ;

        // crossfade amount for this sub-block (linear ramp from the previous value)
        const double mixStart = b.mix;
        b.mix += (b.mixTarget - b.mix) * coef;
        if (std::fabs (b.mixTarget - b.mix) < 1.0e-4) b.mix = b.mixTarget;
        const double mixEnd = b.mix;

        if (mixStart <= 0.0 && mixEnd <= 0.0)
        {
            if (b.ringing)
            {
                b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0;
                b.ringing = false;
            }
            continue;   // inactive band: zero cost
        }

        EqBandParams cur = b.target;
        cur.type = b.type;
        cur.freq = (float) std::exp2 (b.freqLog2);
        cur.gainDb = (float) b.gainDb;
        cur.q = (float) std::exp2 (b.qLog2);
        b.c = designBand (cur, sr);
        b.ringing = true;

        const double c0 = b.c.b0, c1 = b.c.b1, c2 = b.c.b2, a1 = b.c.a1, a2 = b.c.a2;
        double* const z1 = b.z1;
        double* const z2 = b.z2;
        float* const ch[2] = { l, r };
        const double dMix = (mixEnd - mixStart) / (double) n;

        for (int c = 0; c < 2; ++c)
        {
            double s1 = z1[c], s2 = z2[c];
            double m = mixStart;
            float* x = ch[c];
            for (int i = 0; i < n; ++i)
            {
                const double in = (double) x[i];
                const double y = c0 * in + s1;                // transposed direct form II
                s1 = c1 * in - a1 * y + s2;
                s2 = c2 * in - a2 * y;
                m += dMix;
                x[i] = (float) (in + (y - in) * m);           // crossfade dry -> filtered
            }
            // flush denormals
            z1[c] = std::fabs (s1) < 1.0e-30 ? 0.0 : s1;
            z2[c] = std::fabs (s2) < 1.0e-30 ? 0.0 : s2;
        }

        // type switch pending and the band is now silent: apply it and fade back in
        if (b.type != b.target.type && b.mix < 0.001)
        {
            b.type = b.target.type;
            b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0;
            b.mixTarget = (b.target.on && ! isBandNeutral (b.target)) ? 1.0 : 0.0;
        }
    }

    if (soloRun)
    {
        const auto& tb = bands[(size_t) std::max (0, std::min (kEqBands - 1, soloBand))].target;
        const double tFreq = std::log2 (clampd (tb.freq, 10.0, 24000.0));
        const double tQ = std::log2 (clampd (tb.q, 0.05, 20.0));
        soloFreqLog2 += (tFreq - soloFreqLog2) * coef;
        soloQLog2 += (tQ - soloQLog2) * coef;
        const double mixStart = soloMix;
        soloMix += (soloMixTarget - soloMix) * coef;
        if (std::fabs (soloMixTarget - soloMix) < 1.0e-4) soloMix = soloMixTarget;
        const double dMix = (soloMix - mixStart) / (double) n;

        // RBJ band-pass (constant 0 dB peak), 12 dB/oct skirts; Q at least 0.7 so a low-Q band still isolates something
        const double f = clampd (std::exp2 (soloFreqLog2), 10.0, sr * 0.45);
        const double q = clampd (std::exp2 (soloQLog2), 0.7, 20.0);
        const double w0 = 2.0 * kPi * f / sr, alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
        const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;
        float* const ch[2] = { l, r };
        const float* const src[2] = { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            double s1 = soloZ1[c], s2 = soloZ2[c], m = mixStart;
            for (int i = 0; i < n; ++i)
            {
                const double in = (double) src[c][i];
                const double y = b0 * in + s1;
                s1 = -a1 * y + s2;
                s2 = b2 * in - a2 * y;
                m += dMix;
                ch[c][i] = (float) ((double) ch[c][i] * (1.0 - m) + y * m);
            }
            soloZ1[c] = std::fabs (s1) < 1.0e-30 ? 0.0 : s1;
            soloZ2[c] = std::fabs (s2) < 1.0e-30 ? 0.0 : s2;
        }
    }

    bool any = soloMixTarget > 0.0 || soloMix > 0.0005;
    for (auto& b : bands)
        if (b.mixTarget > 0.0 || b.mix > 0.0005 || b.ringing) any = true;
    alive = any;
}

void PadEq::process (float* left, float* right, int numFrames) noexcept
{
    int pos = 0;
    while (pos < numFrames)
    {
        const int n = std::min (kSubBlock, numFrames - pos);
        processSub (left + pos, right + pos, n);
        pos += n;
    }
}
} // namespace dm
