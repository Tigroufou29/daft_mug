#include "Tempo.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dm
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> toMono (const SampleData& s)
{
    std::vector<float> m (s.left.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (s.left[i] + s.right[i]);
    return m;
}

double foldBpm (double bpm)
{
    if (bpm <= 0.0) return 0.0;
    while (bpm < 75.0) bpm *= 2.0;
    while (bpm >= 165.0) bpm *= 0.5;
    return bpm;
}
} // namespace

TempoResult detectTempo (const SampleData& s)
{
    TempoResult out;
    if (! s.isValid() || s.sampleRate <= 0.0) return out;
    const double sr = s.sampleRate;
    if ((double) s.numFrames() < 1.5 * sr) return out;   // not a loop

    // --- onset strength: log-energy rise of the high-passed signal, window 1024, hop 256
    const auto mono = toMono (s);
    const int win = 1024, hop = 256;
    const int numFrames = ((int) mono.size() - win) / hop;
    if (numFrames < 64) return out;
    std::vector<double> onset ((size_t) numFrames, 0.0);
    double prevLog = 0.0;
    for (int f = 0; f < numFrames; ++f)
    {
        double e = 0.0;
        const float* x = mono.data() + (size_t) f * hop;
        for (int i = 1; i < win; ++i)
        {
            const double d = (double) x[i] - (double) x[i - 1];
            e += d * d;
        }
        e /= win;
        const double l = std::log (1.0 + 1.0e4 * e);
        onset[(size_t) f] = f == 0 ? 0.0 : std::max (0.0, l - prevLog);
        prevLog = l;
    }
    double mean = 0.0;
    for (double v : onset) mean += v;
    mean /= (double) numFrames;
    for (auto& v : onset) v -= mean;

    // --- autocorrelation over the lags of 55..200 BPM (and multiples for refinement)
    const double fps = sr / hop;
    const int minLag = std::max (2, (int) std::floor (fps * 60.0 / 200.0));
    const int maxLag = std::min (numFrames / 2, (int) std::ceil (fps * 60.0 / 55.0) + 2);
    if (maxLag <= minLag + 2) return out;
    const int maxNeeded = std::min (numFrames - 1, maxLag * 4 + 8);

    std::vector<double> r ((size_t) maxNeeded + 1, 0.0);
    double r0 = 0.0;
    for (int i = 0; i < numFrames; ++i) r0 += onset[(size_t) i] * onset[(size_t) i];
    if (r0 <= 1.0e-9) return out;
    for (int lag = 1; lag <= maxNeeded; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < numFrames; ++i)
            acc += onset[(size_t) i] * onset[(size_t) (i + lag)];
        r[(size_t) lag] = acc / r0 * ((double) numFrames / (double) (numFrames - lag));   // unbiased
    }

    // --- comb score: a real tempo also correlates at 2x and 4x its period
    auto at = [&] (double lag)
    {
        const int a = (int) std::floor (lag);
        if (a < 1 || a + 1 > maxNeeded) return 0.0;
        const double t = lag - a;
        return r[(size_t) a] * (1.0 - t) + r[(size_t) (a + 1)] * t;
    };
    double bestScore = -1e9, bestLag = 0.0, sumScore = 0.0;
    int counted = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double sc = at (lag) + 0.5 * at (2.0 * lag) + 0.25 * at (4.0 * lag);
        sumScore += sc; ++counted;
        if (sc > bestScore) { bestScore = sc; bestLag = lag; }
    }
    if (bestLag <= 0.0 || counted == 0) return out;

    // --- refine with the largest multiple that exists (parabolic interpolation, error divided by the multiple)
    double refined = bestLag;
    for (int m : { 4, 2, 1 })
    {
        const int centre = (int) std::lround (bestLag * m);
        const int lo = std::max (2, centre - m - 1), hi = std::min (maxNeeded - 1, centre + m + 1);
        if (hi <= lo + 1) continue;
        int pk = lo;
        for (int k = lo; k <= hi; ++k) if (r[(size_t) k] > r[(size_t) pk]) pk = k;
        if (pk <= 1 || pk >= maxNeeded) continue;
        const double a = r[(size_t) pk - 1], b = r[(size_t) pk], c = r[(size_t) pk + 1];
        const double den = a - 2.0 * b + c;
        const double off = std::fabs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
        refined = ((double) pk + std::max (-1.0, std::min (1.0, off))) / m;
        break;
    }

    const double bpm = foldBpm (60.0 * fps / refined);
    const double meanScore = sumScore / counted;
    out.bpm = std::floor (bpm * 10.0 + 0.5) / 10.0;
    // confidence: how much the best comb score stands out from the average one (relative to r[0] = 1)
    out.confidence = (float) std::max (0.0, std::min (1.0, (bestScore - meanScore) * 1.5));
    return out;
}

std::shared_ptr<SampleData> timeStretch (const SampleData& in, double ratio)
{
    if (! in.isValid() || ratio < 0.5 || ratio > 2.0 || ! (ratio == ratio)) return nullptr;
    const int frames = in.numFrames();
    if (frames < 64) return nullptr;

    auto out = std::make_shared<SampleData>();
    out->sampleRate = in.sampleRate;
    out->name = in.name;
    const int outFrames = std::max (1, (int) std::lround ((double) frames * ratio));

    // window ~46 ms (power-of-two-ish, even), synthesis hop = half a window (Hann at 50 % overlap sums to 1)
    int N = 1;
    while ((double) N < 0.046 * in.sampleRate) N <<= 1;
    N = std::max (512, N);
    const int Hs = N / 2;
    const double Ha = (double) Hs / ratio;
    const int tol = N / 4;
    const int D = 4;   // decimation for the coarse search

    const auto mono = toMono (in);
    auto sampleAt = [&] (const std::vector<float>& v, long i) { return (i >= 0 && i < (long) v.size()) ? v[(size_t) i] : 0.0f; };

    // decimated copy (simple 4-point average) for the coarse similarity search
    std::vector<float> dec ((size_t) (frames / D + 2), 0.0f);
    for (size_t i = 0; i < dec.size(); ++i)
    {
        float a = 0.0f;
        for (int k = 0; k < D; ++k) a += sampleAt (mono, (long) (i * D + k));
        dec[i] = a / D;
    }

    std::vector<float> hann ((size_t) N);
    for (int n = 0; n < N; ++n)
        hann[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * n / N));

    // --- choose, for each output frame, the input position whose waveform best continues the previous one
    const int numOut = (outFrames + Hs - 1) / Hs;
    std::vector<long> chosen ((size_t) numOut, 0);
    const int Nd = N / D;
    for (int k = 1; k < numOut; ++k)
    {
        const long prev = chosen[(size_t) k - 1];
        const long nominal = (long) std::lround ((double) k * Ha);
        const long natural = prev + Hs;   // where the previous frame would continue
        long lo = std::max (0L, nominal - tol), hi = std::min ((long) frames - 1, nominal + tol);
        if (hi < lo) { chosen[(size_t) k] = std::min ((long) frames - 1, std::max (0L, nominal)); continue; }

        auto score = [&] (long cand, int step, const std::vector<float>& v, long scale)
        {
            // normalised cross-correlation between the candidate segment and the natural continuation
            double xy = 0.0, yy = 1e-9;
            for (int i = 0; i < (scale == 1 ? N : Nd); i += step)
            {
                const double a = sampleAt (v, natural / scale + i);
                const double b = sampleAt (v, cand / scale + i);
                xy += a * b; yy += b * b;
            }
            return xy / std::sqrt (yy);
        };

        long best = lo; double bestSc = -1e30;
        for (long c = lo; c <= hi; c += D)
        {
            const double sc = score (c, 1, dec, D);
            if (sc > bestSc) { bestSc = sc; best = c; }
        }
        // fine search at full rate around the coarse winner
        long fine = best; double fineSc = -1e30;
        for (long c = std::max (lo, best - D); c <= std::min (hi, best + D); ++c)
        {
            const double sc = score (c, 4, mono, 1);
            if (sc > fineSc) { fineSc = sc; fine = c; }
        }
        chosen[(size_t) k] = fine;
    }

    // --- overlap-add (same positions for both channels)
    out->left.assign ((size_t) outFrames + (size_t) N, 0.0f);
    out->right.assign ((size_t) outFrames + (size_t) N, 0.0f);
    for (int k = 0; k < numOut; ++k)
    {
        const long src = chosen[(size_t) k];
        const long dst = (long) k * Hs;
        for (int n = 0; n < N; ++n)
        {
            float w = hann[(size_t) n];
            if (k == 0 && n < Hs) w = 1.0f;   // no fade-in at the very start
            out->left[(size_t) (dst + n)] += w * sampleAt (in.left, src + n);
            out->right[(size_t) (dst + n)] += w * sampleAt (in.right, src + n);
        }
    }
    out->left.resize ((size_t) outFrames);
    out->right.resize ((size_t) outFrames);
    return out;
}
} // namespace dm
