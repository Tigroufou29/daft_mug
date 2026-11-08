#include "Mutate.h"

#include <algorithm>
#include <numeric>

namespace dm
{
namespace
{
struct Rng
{
    uint64_t s;
    explicit Rng (uint32_t seed) : s (0x9E3779B97F4A7C15ull ^ ((uint64_t) seed * 0xBF58476D1CE4E5B9ull + 1ull)) { next(); next(); }
    uint32_t next() noexcept
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;   // xorshift64
        return (uint32_t) (s >> 16);
    }
    float uniform() noexcept { return (float) (next() & 0xFFFFFF) / 16777216.0f; }   // [0, 1)
    template <class It> void shuffle (It a, It b) noexcept
    {
        for (auto n = b - a; n > 1; --n)
            std::swap (a[n - 1], a[(int) (next() % (uint32_t) n)]);
    }
};

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// m stratified samples in [-1, 1], shuffled
std::vector<float> strata (Rng& rng, int m)
{
    std::vector<int> idx ((size_t) m);
    std::iota (idx.begin(), idx.end(), 0);
    rng.shuffle (idx.begin(), idx.end());
    std::vector<float> u ((size_t) m);
    for (int i = 0; i < m; ++i)
        u[(size_t) i] = ((float) idx[(size_t) i] + rng.uniform()) / (float) m * 2.0f - 1.0f;
    return u;
}
} // namespace

std::vector<MutateValues> generateVariants (const MutateValues& base, const MutateConfig& cfg, int count, uint32_t seed)
{
    count = std::max (1, std::min (16, count));
    std::vector<MutateValues> out ((size_t) count, base);
    const int m = count - 1;
    if (m <= 0) return out;

    Rng rng (seed);
    const float a = clampf (cfg.amount, 0.0f, 1.0f);

    auto each = [&] (bool enabled, float range, auto apply)
    {
        if (! enabled || range <= 0.0f || a <= 0.0f) return;
        const auto u = strata (rng, m);
        for (int i = 0; i < m; ++i)
            apply (out[(size_t) i + 1], u[(size_t) i] * range * a);
    };

    each (cfg.pitch, cfg.pitchRange, [] (MutateValues& v, float d) { v.pitchSemitones = clampf (v.pitchSemitones + d, -24.0f, 24.0f); });
    each (cfg.pan, cfg.panRange, [] (MutateValues& v, float d) { v.pan = clampf (v.pan + d, -1.0f, 1.0f); });
    each (cfg.gain, cfg.gainRange, [] (MutateValues& v, float d) { v.gainDb = clampf (v.gainDb + d, -60.0f, 12.0f); });
    each (cfg.reverb, cfg.reverbRange, [] (MutateValues& v, float d) { v.reverbWet = clampf (v.reverbWet + d, 0.0f, 1.0f); });
    if (cfg.attack && cfg.attackRange > 0.0f && a > 0.0f)   // only slower attacks: (u + 1) / 2 in [0, 1]
    {
        const auto u = strata (rng, m);
        for (int i = 0; i < m; ++i)
            out[(size_t) i + 1].attackMs = clampf (base.attackMs + (u[(size_t) i] + 1.0f) * 0.5f * cfg.attackRange * a, 0.0f, 2000.0f);
    }
    for (size_t band = 0; band < base.eqGainDb.size(); ++band)
        each (cfg.eq, cfg.eqRange, [band] (MutateValues& v, float d) { v.eqGainDb[band] = clampf (v.eqGainDb[band] + d, -18.0f, 18.0f); });
    return out;
}
} // namespace dm
