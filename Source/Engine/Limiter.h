#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

// Look-ahead brick-wall limiter for the plug-in output (plain C++, no JUCE, real-time safe after prepare()).
//
// The signal is delayed by `latency()` samples. The gain needed to keep every sample under the ceiling is
// computed on the undelayed signal, then reduced to its minimum over the look-ahead window and smoothed by a moving
// average of the same length: the gain is already down when the peak arrives, with no hard edge, so the output
// never exceeds the ceiling. The release is a slow one-pole. The delay is ALWAYS applied (limiter on or off), so the
// latency reported to the host never changes.
namespace dm
{
class Limiter
{
public:
    static constexpr double kLookaheadSeconds = 0.0015;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        n = std::max (2, (int) std::lround (kLookaheadSeconds * sr));
        const size_t len = (size_t) n + 1;
        dl.assign (len, 0.0f);
        dr.assign (len, 0.0f);
        gHist.assign (len, 1.0f);
        mHist.assign ((size_t) n, 1.0f);
        w = 0; mw = 0; sum = (double) n;
        state = 1.0f;
        relCoef = 1.0f - std::exp (-1.0f / (float) (0.08 * sr));   // ~80 ms
        reduction.store (0.0f, std::memory_order_relaxed);
    }

    int latency() const noexcept { return n; }

    // In place. ceiling: linear (e.g. 0.966 = -0.3 dBFS). enabled=false: only the delay (gain stays at unity).
    void process (float* l, float* r, int num, float ceiling, bool enabled) noexcept
    {
        float maxRed = 1.0f;
        const size_t len = dl.size();
        for (int i = 0; i < num; ++i)
        {
            const float inL = l[i], inR = r[i];
            const float peak = std::max (std::fabs (inL), std::fabs (inR));
            const float need = (enabled && peak > ceiling) ? ceiling / peak : 1.0f;

            // ring of the last n+1 needed gains -> minimum
            gHist[w] = need;
            float m = need;
            for (size_t k = 0; k < len; ++k) m = std::min (m, gHist[k]);

            // moving average (n values) of that minimum
            sum += (double) m - (double) mHist[mw];
            mHist[mw] = m;
            mw = (mw + 1) % mHist.size();
            const float avg = (float) (sum / (double) n);

            // attack: follow immediately (the average is already smooth); release: slow
            state = avg < state ? avg : state + (avg - state) * relCoef;
            const float g = state;   // state <= avg <= the gain needed by the sample being output

            // delayed samples (write the new one, read the oldest)
            const size_t rd = (w + 1) % len;
            const float outL = dl[rd], outR = dr[rd];
            dl[w] = inL; dr[w] = inR;
            w = (w + 1) % len;

            float oL = outL * g, oR = outR * g;
            if (enabled)
            {
                oL = std::max (-ceiling, std::min (ceiling, oL));   // only float rounding can get here
                oR = std::max (-ceiling, std::min (ceiling, oR));
            }
            l[i] = oL; r[i] = oR;
            maxRed = std::min (maxRed, g);
        }
        reduction.store (1.0f - maxRed, std::memory_order_relaxed);
    }

    // 0 = no gain reduction in the last block, 0.5 = -6 dB ... (any thread)
    float lastReduction() const noexcept { return reduction.load (std::memory_order_relaxed); }

private:
    double sr = 44100.0;
    int n = 66;
    std::vector<float> dl, dr, gHist, mHist;
    size_t w = 0, mw = 0;
    double sum = 0.0;
    float state = 1.0f, relCoef = 0.001f;
    std::atomic<float> reduction { 0.0f };
};
} // namespace dm
