#include "SliceDetector.h"

#include <algorithm>
#include <cmath>

namespace dm
{
namespace
{
int snapToZero (const float* l, const float* r, int n, int frame, int radius)
{
    int best = std::max (0, std::min (frame, n - 1));
    float bestAbs = 1.0e9f;
    for (int i = std::max (1, frame - radius); i <= std::min (n - 1, frame + radius); ++i)
    {
        const float a = 0.5f * (l[i - 1] + r[i - 1]), b = 0.5f * (l[i] + r[i]);
        if ((a <= 0.0f) != (b <= 0.0f))                    // sign change between i-1 and i
        {
            const float m = std::min (std::fabs (a), std::fabs (b));
            const float score = m + 1.0e-6f * (float) std::abs (i - frame);   // closest wins on ties
            if (score < bestAbs) { bestAbs = score; best = std::fabs (a) < std::fabs (b) ? i - 1 : i; }
        }
    }
    return best;
}
} // namespace

std::vector<int> normaliseSlicePoints (std::vector<int> points, int numFrames, int minFrames, int maxSlices)
{
    std::sort (points.begin(), points.end());
    std::vector<int> out;
    for (int p : points)
    {
        if (p < 0 || p >= numFrames) continue;
        if (! out.empty() && p - out.back() < std::max (1, minFrames)) continue;
        out.push_back (p);
        if ((int) out.size() >= std::max (1, maxSlices)) break;
    }
    if (out.empty()) out.push_back (0);
    return out;
}

std::vector<int> detectSlicePoints (const float* left, const float* right, int numFrames, double sr, const SliceConfig& cfg)
{
    const int maxSlices = std::max (1, std::min (16, cfg.maxSlices));
    if (left == nullptr || right == nullptr || numFrames < 2 || sr <= 0.0)
        return { 0 };

    if (cfg.mode == SliceMode::EqualParts)
    {
        const int parts = std::max (1, std::min (maxSlices, cfg.parts));
        std::vector<int> pts;
        for (int i = 0; i < parts; ++i)
            pts.push_back ((int) ((long long) numFrames * i / parts));
        return pts;
    }

    // ---- transient detection: energy of the pre-emphasised signal per 5 ms hop, onset = rise in dB
    const int hop = std::max (16, (int) (0.005 * sr));
    const int frames = numFrames / hop;
    if (frames < 3) return { 0 };

    std::vector<float> db ((size_t) frames);
    float peakDb = -200.0f;
    float prev = 0.0f;
    for (int k = 0; k < frames; ++k)
    {
        double e = 0.0;
        for (int i = k * hop; i < (k + 1) * hop; ++i)
        {
            const float x = 0.5f * (left[i] + right[i]);
            const float d = x - 0.95f * prev;
            prev = x;
            e += (double) d * d;
        }
        db[(size_t) k] = 10.0f * (float) std::log10 (e / hop + 1.0e-12);
        peakDb = std::max (peakDb, db[(size_t) k]);
    }

    const float sens = std::max (0.0f, std::min (1.0f, cfg.sensitivity));
    const float riseThreshold = 18.0f - 15.0f * sens;          // 18 dB .. 3 dB jump between two 5 ms frames
    const float floorDb = peakDb - (12.0f + 30.0f * sens);     // ignore attacks far below the loudest part (12 dB .. 42 dB)
    const int minGap = std::max (1, (int) (cfg.minSliceMs * 0.001 * sr / hop));

    struct Cand { int frame; float strength; };
    std::vector<Cand> cands;
    for (int k = 1; k < frames; ++k)
    {
        // rise relative to the quietest of the previous 3 frames: catches onsets that ramp up over 10-15 ms
        const float ref = std::min ({ db[(size_t) k - 1], db[(size_t) std::max (0, k - 2)], db[(size_t) std::max (0, k - 3)] });
        const float rise = db[(size_t) k] - ref;
        const bool localMax = db[(size_t) k] >= db[(size_t) k - 1] && (k + 1 >= frames || db[(size_t) k] >= db[(size_t) k + 1] - 6.0f);
        if (rise >= riseThreshold && db[(size_t) k] >= floorDb && localMax)
            cands.push_back ({ k, rise });
    }
    // strongest first, keep those at least minGap apart
    std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.strength > b.strength; });
    std::vector<int> chosen;
    for (const auto& c : cands)
    {
        bool ok = true;
        for (int o : chosen) if (std::abs (o - c.frame) < minGap) { ok = false; break; }
        if (ok) chosen.push_back (c.frame);
    }
    std::sort (chosen.begin(), chosen.end());
    if ((int) chosen.size() > maxSlices)
    {
        // too many: keep the strongest ones
        std::vector<Cand> kept;
        for (int f : chosen)
            for (const auto& c : cands) if (c.frame == f) { kept.push_back (c); break; }
        std::sort (kept.begin(), kept.end(), [] (const Cand& a, const Cand& b) { return a.strength > b.strength; });
        kept.resize ((size_t) maxSlices);
        chosen.clear();
        for (const auto& c : kept) chosen.push_back (c.frame);
        std::sort (chosen.begin(), chosen.end());
    }

    std::vector<int> pts;
    const int radius = std::max (8, (int) (0.002 * sr));
    for (int f : chosen)
    {
        // the energy frame f contains the attack: start slightly before it (2 ms), then look for a zero crossing
        int p = std::max (0, f * hop - (int) (0.002 * sr));
        if (cfg.snapToZeroCrossing) p = snapToZero (left, right, numFrames, p, radius);
        pts.push_back (p);
    }
    if (pts.empty()) return { 0 };
    return normaliseSlicePoints (pts, numFrames, (int) (cfg.minSliceMs * 0.001 * sr * 0.5), maxSlices);
}
} // namespace dm
