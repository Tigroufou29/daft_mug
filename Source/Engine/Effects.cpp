#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dm
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
} // namespace

float fxTailSeconds (const FxParams& p) noexcept
{
    float t = 0.0f;
    if (p.delayMix > 0.0f)
    {
        const float fb = std::min (0.95f, std::max (0.0f, p.delayFeedback));
        // time for the repeats to fall below -60 dB
        const float repeats = fb < 0.01f ? 1.0f : std::min (60.0f, 1.0f + std::log (0.001f) / std::log (fb));
        t = std::max (t, std::min (60.0f, repeats * p.delayMs * 0.001f));
    }
    if (p.chorusMix > 0.0f)
        t = std::max (t, 0.1f);
    return t;
}

void PadFx::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    coef = 1.0 - std::exp (-1.0 / (0.010 * sr));
    const auto dl = (size_t) (kMaxDelaySeconds * sr) + 8;
    const auto ch = (size_t) (kChorusBufferSeconds * sr) + 8;
    dlL.assign (dl, 0.0f); dlR.assign (dl, 0.0f);
    chL.assign (ch, 0.0f); chR.assign (ch, 0.0f);
    reset();
}

void PadFx::reset()
{
    std::fill (dlL.begin(), dlL.end(), 0.0f);
    std::fill (dlR.begin(), dlR.end(), 0.0f);
    std::fill (chL.begin(), chL.end(), 0.0f);
    std::fill (chR.begin(), chR.end(), 0.0f);
    dlPos = chPos = 0;
    lfoPhase = 0.0;
    heldL = heldR = crushPhase = 0.0;
    satMix = crushMix = chorusMix = delayMix = 0.0;
    initialised = false;
    chDirty = dlDirty = false;
    alive = false;
}

void PadFx::setParams (const FxParams& p) noexcept
{
    target = p;
    target.satDriveDb = (float) clampd (p.satDriveDb, 0.0, 24.0);
    target.crushBits = (float) clampd (p.crushBits, 1.0, 16.0);
    target.crushRate = (float) clampd (p.crushRate, 1.0, 32.0);
    target.delayMs = (float) clampd (p.delayMs, 10.0, kMaxDelaySeconds * 1000.0);
    target.delayFeedback = (float) clampd (p.delayFeedback, 0.0, 0.95);
    if (! initialised)
    {
        drive = dbToGain (target.satDriveDb);
        outGain = dbToGain (target.satOutDb);
        delaySamples = target.delayMs * 0.001 * sr;
        initialised = true;
    }
    const bool satOn = target.satMode != SatMode::Off && target.satMix > 0.0f;
    alive = satOn || target.crushMix > 0.0f || target.chorusMix > 0.0f || target.delayMix > 0.0f
            || satMix > 1e-4 || crushMix > 1e-4 || chorusMix > 1e-4 || delayMix > 1e-4 || dlDirty || chDirty;
}

float PadFx::shape (float x) const noexcept
{
    switch (target.satMode)
    {
        case SatMode::Soft: return std::tanh (x);
        case SatMode::Hard: return std::max (-1.0f, std::min (1.0f, x * 1.5f)) * (1.0f / 1.0f);
        case SatMode::Tube:
        {
            const float bias = 0.25f;   // asymmetric -> even harmonics
            return std::tanh (x + bias) - std::tanh (bias);
        }
        case SatMode::Off: break;
    }
    return x;
}

void PadFx::processPre (float* l, float* r, int n) noexcept
{
    const bool satWanted = target.satMode != SatMode::Off && target.satMix > 0.0f;
    const double satT = satWanted ? (double) target.satMix : 0.0;
    const double crT = (double) target.crushMix;
    if (! satWanted && satMix < 1e-4 && crT <= 0.0 && crushMix < 1e-4)
    {
        satMix = crushMix = 0.0;
        return;
    }
    const double driveT = dbToGain (target.satDriveDb), outT = dbToGain (target.satOutDb);
    const double levels = std::pow (2.0, (double) target.crushBits - 1.0);
    const double step = 1.0 / (double) target.crushRate;

    for (int i = 0; i < n; ++i)
    {
        satMix += (satT - satMix) * coef;
        crushMix += (crT - crushMix) * coef;
        if (std::fabs (satT - satMix) < 1e-4) satMix = satT;       // settle exactly: a fully wet effect is fully wet
        if (std::fabs (crT - crushMix) < 1e-4) crushMix = crT;
        drive += (driveT - drive) * coef;
        outGain += (outT - outGain) * coef;

        float xl = l[i], xr = r[i];
        if (satMix > 1e-4)
        {
            const float d = (float) drive, o = (float) outGain;
            const float wl = shape (xl * d) * o, wr = shape (xr * d) * o;
            xl += (wl - xl) * (float) satMix;
            xr += (wr - xr) * (float) satMix;
        }
        if (crushMix > 1e-4)
        {
            crushPhase += step;
            if (crushPhase >= 1.0)
            {
                crushPhase -= 1.0;
                heldL = std::round ((double) xl * levels) / levels;
                heldR = std::round ((double) xr * levels) / levels;
            }
            xl += ((float) heldL - xl) * (float) crushMix;
            xr += ((float) heldR - xr) * (float) crushMix;
        }
        l[i] = xl;
        r[i] = xr;
    }
}

void PadFx::processPost (float* l, float* r, int n) noexcept
{
    const double chT = (double) target.chorusMix, dlT = (double) target.delayMix;
    const bool chOn = chT > 0.0 || chorusMix > 1e-4;
    const bool dlOn = dlT > 0.0 || delayMix > 1e-4 || dlDirty;
    if (! chOn && chDirty)
    {
        std::fill (chL.begin(), chL.end(), 0.0f);
        std::fill (chR.begin(), chR.end(), 0.0f);
        chDirty = false;
        chorusMix = 0.0;
    }
    if (! chOn && ! dlOn)
        return;

    const int chSize = (int) chL.size(), dlSize = (int) dlL.size();
    const double lfoInc = target.chorusRateHz / sr;
    const double baseDelay = 0.012 * sr;                    // 12 ms centre
    const double depthSamples = (double) target.chorusDepth * 0.008 * sr;   // up to +-8 ms
    const double delayT = (double) target.delayMs * 0.001 * sr;
    const float fb = target.delayFeedback;
    const bool pp = target.delayPingPong;

    for (int i = 0; i < n; ++i)
    {
        float xl = l[i], xr = r[i];

        if (chOn)
        {
            chorusMix += (chT - chorusMix) * coef;
            if (std::fabs (chT - chorusMix) < 1e-4) chorusMix = chT;
            chL[(size_t) chPos] = xl;
            chR[(size_t) chPos] = xr;
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const double sL = std::sin (2.0 * kPi * lfoPhase), sR = std::sin (2.0 * kPi * lfoPhase + 0.5 * kPi);
            auto tap = [&] (const std::vector<float>& buf, double d)
            {
                double pos = (double) chPos - d;
                while (pos < 0.0) pos += (double) chSize;
                const int i0 = (int) pos;
                const double fr = pos - (double) i0;
                const int i1 = (i0 + 1) % chSize;
                return (float) ((double) buf[(size_t) i0] * (1.0 - fr) + (double) buf[(size_t) i1] * fr);
            };
            const float wl = tap (chL, baseDelay + sL * depthSamples);
            const float wr = tap (chR, baseDelay + sR * depthSamples);
            const float m = (float) chorusMix;
            xl = xl * (1.0f - 0.5f * m) + wl * m * 0.7f;
            xr = xr * (1.0f - 0.5f * m) + wr * m * 0.7f;
            chPos = (chPos + 1) % chSize;
            chDirty = true;
        }

        if (dlOn)
        {
            delayMix += (dlT - delayMix) * coef;
            if (std::fabs (dlT - delayMix) < 1e-4) delayMix = dlT;
            delaySamples += (delayT - delaySamples) * (coef * 0.1);   // slow glide: no zipper when time is automated
            double pos = (double) dlPos - delaySamples;
            while (pos < 0.0) pos += (double) dlSize;
            const int i0 = (int) pos;
            const double fr = pos - (double) i0;
            const int i1 = (i0 + 1) % dlSize;
            const float tl = (float) ((double) dlL[(size_t) i0] * (1.0 - fr) + (double) dlL[(size_t) i1] * fr);
            const float tr = (float) ((double) dlR[(size_t) i0] * (1.0 - fr) + (double) dlR[(size_t) i1] * fr);

            float inL = xl, inR = xr;
            if (pp)   // ping-pong: mono input feeds the left tap, feedback crosses channels
            {
                const float mono = 0.5f * (xl + xr);
                dlL[(size_t) dlPos] = mono + tr * fb;
                dlR[(size_t) dlPos] = tl * fb;
            }
            else
            {
                dlL[(size_t) dlPos] = inL + tl * fb;
                dlR[(size_t) dlPos] = inR + tr * fb;
            }
            const float m = (float) delayMix;
            xl += tl * m;
            xr += tr * m;
            dlPos = (dlPos + 1) % dlSize;
            // denormal guard + idle detection
            if (std::fabs (dlL[(size_t) dlPos]) < 1e-20f) dlL[(size_t) dlPos] = 0.0f;
            if (std::fabs (dlR[(size_t) dlPos]) < 1e-20f) dlR[(size_t) dlPos] = 0.0f;
            dlDirty = true;
        }
        l[i] = xl;
        r[i] = xr;
    }

    if (dlT <= 0.0 && delayMix < 1e-4 && dlDirty)
    {
        std::fill (dlL.begin(), dlL.end(), 0.0f);
        std::fill (dlR.begin(), dlR.end(), 0.0f);
        dlDirty = false;
        delayMix = 0.0;
    }
}
} // namespace dm
