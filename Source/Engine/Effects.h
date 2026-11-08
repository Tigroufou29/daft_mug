#pragma once

#include <vector>

namespace dm
{
enum class SatMode : int { Off = 0, Soft = 1, Hard = 2, Tube = 3 };

// Per-pad creative effects. Chain: saturation -> bitcrusher -> [EQ] -> chorus -> delay.
struct FxParams
{
    SatMode satMode = SatMode::Off;
    float satDriveDb = 6.0f;      // 0 .. 24
    float satOutDb = 0.0f;        // -12 .. +12
    float satMix = 1.0f;          // 0 .. 1

    float crushBits = 16.0f;      // 1 .. 16
    float crushRate = 1.0f;       // 1 .. 32 (sample-rate divider)
    float crushMix = 0.0f;        // 0 .. 1 (0 = bypass)

    float chorusMix = 0.0f;       // 0 .. 1 (0 = bypass)
    float chorusRateHz = 0.8f;    // 0.1 .. 5
    float chorusDepth = 0.5f;     // 0 .. 1

    float delayMix = 0.0f;        // 0 .. 1 (0 = bypass)
    float delayMs = 250.0f;       // 10 .. 1000
    float delayFeedback = 0.35f;  // 0 .. 0.95
    bool delayPingPong = false;
};

// Seconds an effect keeps sounding after the input stops (the engine keeps the pad alive for this long).
float fxTailSeconds (const FxParams& p) noexcept;

class PadFx
{
public:
    static constexpr float kMaxDelaySeconds = 1.0f;
    static constexpr float kChorusBufferSeconds = 0.05f;

    void prepare (double sampleRate);      // allocates (not real-time)
    void reset();
    void setParams (const FxParams& p) noexcept;
    bool isAlive() const noexcept { return alive; }
    bool needsReset() const noexcept { return initialised || dlDirty || chDirty; }
    void processPre (float* l, float* r, int n) noexcept;    // saturation + bitcrusher
    void processPost (float* l, float* r, int n) noexcept;   // chorus + delay

private:
    static constexpr int kSub = 32;
    double sr = 48000.0;
    double coef = 0.01;
    FxParams target;
    bool alive = false;

    double satMix = 0, crushMix = 0, chorusMix = 0, delayMix = 0, delaySamples = 0;
    double drive = 1, outGain = 1;
    bool initialised = false;

    double heldL = 0, heldR = 0, crushPhase = 0;

    std::vector<float> chL, chR, dlL, dlR;
    int chPos = 0, dlPos = 0;
    double lfoPhase = 0;
    bool chDirty = false, dlDirty = false;

    float shape (float x) const noexcept;
};
} // namespace dm
