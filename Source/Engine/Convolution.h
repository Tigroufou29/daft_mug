#pragma once

#include <memory>
#include <vector>

namespace dm
{
// Uniform-partitioned FFT convolution (overlap-save) for the per-pad convolution reverb.
//  - ConvKernel   : immutable, built OFF the audio thread (resample, normalise, FFT of every partition). Shareable.
//  - ConvInstance : kernel + the running state of ONE pad (input history, accumulators). Created off the
//                   audio thread (it allocates), then only touched by the audio thread. No allocation in process().
// Latency of the wet signal: kConvBlock samples (about 10 ms at 48 kHz). The dry signal is not delayed.
constexpr int kConvBlock = 512;
constexpr double kMaxIrSeconds = 4.0;   // longer impulse responses are truncated (with a short fade)

struct ConvKernel
{
    int numParts = 0;
    int irFrames = 0;                       // length after resampling / truncation
    bool stereo = false;                    // false: both channels use spec[0]
    std::vector<float> spec[2];             // [part][re(B+1), im(B+1)]
    bool valid() const noexcept { return numParts > 0; }
};
using KernelHandle = std::shared_ptr<const ConvKernel>;

// `right` may be null (mono IR). Normalises the energy, resamples to engineRate, truncates to kMaxIrSeconds.
KernelHandle buildConvKernel (const float* left, const float* right, int numFrames, double irRate, double engineRate);

class ConvInstance
{
public:
    explicit ConvInstance (KernelHandle kernel);

    int tailFrames() const noexcept { return kernel ? kernel->irFrames + kConvBlock : 0; }
    bool isDirty() const noexcept { return dirty; }
    void reset() noexcept;
    // Writes the wet signal (convolved, delayed by kConvBlock) for the given input. In-place not allowed.
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept;

private:
    struct Channel
    {
        std::vector<float> fdl;       // numParts * 2 * (B+1), ring of past input spectra
        std::vector<float> in, prev, out;
        int head = 0, pos = 0;
    };
    void runBlock (Channel& c, const std::vector<float>& spec) noexcept;

    KernelHandle kernel;
    Channel ch[2];
    std::vector<float> re, im, accRe, accIm;
    bool dirty = false;
};
} // namespace dm
