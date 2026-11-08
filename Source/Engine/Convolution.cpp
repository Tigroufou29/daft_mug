#include "Convolution.h"

#include <algorithm>
#include <cmath>

namespace dm
{
namespace
{
constexpr int B = kConvBlock;
constexpr int N = 2 * B;
constexpr int H = B + 1;   // bins kept (real signal)
constexpr double kPi = 3.14159265358979323846;

struct Fft
{
    std::vector<float> cs, sn;
    std::vector<int> rev;
    Fft()
    {
        cs.resize (N / 2); sn.resize (N / 2); rev.resize (N);
        for (int i = 0; i < N / 2; ++i) { cs[(size_t) i] = (float) std::cos (2.0 * kPi * i / N); sn[(size_t) i] = (float) std::sin (2.0 * kPi * i / N); }
        int bits = 0; while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }
    // sign = -1 forward, +1 inverse (inverse unscaled)
    void run (float* re, float* im, int sign) const noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cs[(size_t) (k * step)], wi = sign * sn[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }
};

const Fft& fft()
{
    static const Fft f;   // built on first use (always off the audio thread: kernels / instances are created there)
    return f;
}
} // namespace

KernelHandle buildConvKernel (const float* left, const float* right, int numFrames, double irRate, double engineRate)
{
    if (left == nullptr || numFrames < 2 || irRate <= 0.0 || engineRate <= 0.0)
        return {};
    fft();

    const double ratio = irRate / engineRate;                       // source frames per output frame
    int outFrames = (int) std::floor ((double) (numFrames - 1) / ratio) + 1;
    const int maxFrames = (int) (kMaxIrSeconds * engineRate);
    const bool truncated = outFrames > maxFrames;
    outFrames = std::min (outFrames, maxFrames);
    if (outFrames < 1) return {};

    auto resample = [&] (const float* src)
    {
        std::vector<float> o ((size_t) outFrames);
        for (int i = 0; i < outFrames; ++i)
        {
            const double p = (double) i * ratio;
            const int i0 = std::min ((int) p, numFrames - 1), i1 = std::min (i0 + 1, numFrames - 1);
            const float f = (float) (p - (double) i0);
            o[(size_t) i] = src[i0] * (1.0f - f) + src[i1] * f;
        }
        if (truncated)   // 50 ms fade at the cut
        {
            const int fade = std::min (outFrames, (int) (0.05 * engineRate));
            for (int i = 0; i < fade; ++i) o[(size_t) (outFrames - 1 - i)] *= (float) i / (float) fade;
        }
        return o;
    };

    std::vector<float> l = resample (left), r;
    const bool stereo = right != nullptr;
    if (stereo) r = resample (right);

    double eL = 0.0, eR = 0.0;
    for (float v : l) eL += (double) v * v;
    for (float v : r) eR += (double) v * v;
    const double e = std::sqrt (std::max (eL, eR));
    if (e < 1.0e-12) return {};
    const float scale = (float) (1.0 / e);

    auto k = std::make_shared<ConvKernel>();
    k->irFrames = outFrames;
    k->numParts = (outFrames + B - 1) / B;
    k->stereo = stereo;
    std::vector<float> re (N), im (N);
    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        const auto& src = c == 0 ? l : r;
        auto& spec = k->spec[c];
        spec.assign ((size_t) k->numParts * 2 * H, 0.0f);
        for (int p = 0; p < k->numParts; ++p)
        {
            std::fill (re.begin(), re.end(), 0.0f);
            std::fill (im.begin(), im.end(), 0.0f);
            for (int i = 0; i < B; ++i)
            {
                const int idx = p * B + i;
                if (idx < outFrames) re[(size_t) i] = src[(size_t) idx] * scale;
            }
            fft().run (re.data(), im.data(), -1);
            float* dst = spec.data() + (size_t) p * 2 * H;
            std::copy (re.begin(), re.begin() + H, dst);
            std::copy (im.begin(), im.begin() + H, dst + H);
        }
    }
    return k;
}

ConvInstance::ConvInstance (KernelHandle k) : kernel (std::move (k))
{
    fft();
    if (! kernel) return;
    for (auto& c : ch)
    {
        c.fdl.assign ((size_t) kernel->numParts * 2 * H, 0.0f);
        c.in.assign ((size_t) B, 0.0f);
        c.prev.assign ((size_t) B, 0.0f);
        c.out.assign ((size_t) B, 0.0f);
    }
    re.assign ((size_t) N, 0.0f);
    im.assign ((size_t) N, 0.0f);
    accRe.assign ((size_t) H, 0.0f);
    accIm.assign ((size_t) H, 0.0f);
}

void ConvInstance::reset() noexcept
{
    for (auto& c : ch)
    {
        std::fill (c.fdl.begin(), c.fdl.end(), 0.0f);
        std::fill (c.in.begin(), c.in.end(), 0.0f);
        std::fill (c.prev.begin(), c.prev.end(), 0.0f);
        std::fill (c.out.begin(), c.out.end(), 0.0f);
        c.head = c.pos = 0;
    }
    dirty = false;
}

void ConvInstance::runBlock (Channel& c, const std::vector<float>& spec) noexcept
{
    const int P = kernel->numParts;
    for (int i = 0; i < B; ++i) { re[(size_t) i] = c.prev[(size_t) i]; re[(size_t) (B + i)] = c.in[(size_t) i]; }
    std::fill (im.begin(), im.end(), 0.0f);
    c.prev = c.in;
    fft().run (re.data(), im.data(), -1);

    c.head = (c.head + 1) % P;
    float* slot = c.fdl.data() + (size_t) c.head * 2 * H;
    std::copy (re.begin(), re.begin() + H, slot);
    std::copy (im.begin(), im.begin() + H, slot + H);

    float* ar = accRe.data();
    float* ai = accIm.data();
    std::fill (accRe.begin(), accRe.end(), 0.0f);
    std::fill (accIm.begin(), accIm.end(), 0.0f);
    for (int p = 0; p < P; ++p)
    {
        const int s = (c.head - p + P) % P;
        const float* x = c.fdl.data() + (size_t) s * 2 * H;
        const float* h = spec.data() + (size_t) p * 2 * H;
        const float* xr = x; const float* xi = x + H;
        const float* hr = h; const float* hi = h + H;
        for (int k = 0; k < H; ++k)
        {
            ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
            ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
        }
    }
    for (int k = 0; k < H; ++k) { re[(size_t) k] = ar[k]; im[(size_t) k] = ai[k]; }
    for (int k = 1; k < B; ++k) { re[(size_t) (N - k)] = ar[k]; im[(size_t) (N - k)] = -ai[k]; }
    fft().run (re.data(), im.data(), +1);
    const float inv = 1.0f / (float) N;
    for (int i = 0; i < B; ++i) c.out[(size_t) i] = re[(size_t) (B + i)] * inv;
}

void ConvInstance::process (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept
{
    if (! kernel || ! kernel->valid())
    {
        std::fill (wetL, wetL + n, 0.0f);
        std::fill (wetR, wetR + n, 0.0f);
        return;
    }
    dirty = true;
    const float* in[2] = { inL, inR };
    float* wet[2] = { wetL, wetR };
    for (int c = 0; c < 2; ++c)
    {
        Channel& chn = ch[c];
        const auto& spec = (c == 1 && kernel->stereo) ? kernel->spec[1] : kernel->spec[0];
        for (int i = 0; i < n; ++i)
        {
            wet[c][i] = chn.out[(size_t) chn.pos];
            chn.in[(size_t) chn.pos] = in[c][i];
            if (++chn.pos == B)
            {
                chn.pos = 0;
                runBlock (chn, spec);
            }
        }
    }
}
} // namespace dm
