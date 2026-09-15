#include "engine/TubeSat.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

namespace {
// Drive coefficient at drive==1.0; the patch struct clamps to 0..0.5 so the
// effective range at the saturator is 0..~4.5 — well inside tanh's nonlinear
// region without hitting the silly-edge where tanh → sign(x).
constexpr float kDriveScale = 9.0f;
// Asymmetry: positive lobe driven harder than negative for triode-style 2nd-
// harmonic emphasis. Ratios from Zölzer DAFX Fig. 4.4.
constexpr float kPosAsym = 1.10f;
constexpr float kNegAsym = 0.90f;
} // namespace

TubeSat::TubeSat() = default;

void TubeSat::prepare(double sampleRate, int channels) {
    (void)channels;
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    // 1-pole HPF coefficient for fc ≈ 20 Hz:
    //   y[n] = x[n] - x[n-1] + R * y[n-1]
    // with R ≈ 1 - 2π * fc / fs.
    constexpr float kHpfCutoffHz = 20.0f;
    const double r = 1.0 - (2.0 * 3.14159265358979 * kHpfCutoffHz) / sampleRate_;
    hpfCoeff_ = static_cast<float>(std::clamp(r, 0.0, 0.9999));
    oversamplerL_.prepare(sampleRate_);
    oversamplerR_.prepare(sampleRate_);
    reset();
    setDrive(drive_); // recompute shaping coefficients for the new SR
}

void TubeSat::setDrive(float drive01) noexcept {
    drive_ = std::clamp(drive01, 0.0f, 0.5f);
    const float k = 1.0f + drive_ * kDriveScale;
    drivePos_ = k * kPosAsym;
    driveNeg_ = k * kNegAsym;
    // tanh(0) == 0 → avoid divide-by-zero; clamp the normaliser to 1 when
    // drive collapses, which also matches the bypass identity.
    const float tanhPos = std::tanh(drivePos_);
    const float tanhNeg = std::tanh(driveNeg_);
    normPos_ = (tanhPos > 1e-6f) ? tanhPos : 1.0f;
    normNeg_ = (tanhNeg > 1e-6f) ? tanhNeg : 1.0f;
}

void TubeSat::setMix(float mix01) noexcept { mix_ = std::clamp(mix01, 0.0f, 1.0f); }

void TubeSat::reset() noexcept {
    blockL_ = DcBlock{};
    blockR_ = DcBlock{};
    oversamplerL_.reset();
    oversamplerR_.reset();
}

float TubeSat::saturate(float x, float drivePos, float driveNeg, float normPos, float normNeg) noexcept {
    if (x >= 0.0f) {
        return std::tanh(x * drivePos) / normPos;
    }
    return std::tanh(x * driveNeg) / normNeg;
}

void TubeSat::processStereo(float* left, float* right, int numSamples) noexcept {
    if (left == nullptr || right == nullptr)
        return;

    // drive_ == 0 → identity. The augmenter normally skips the call entirely
    // (engine guard) but a stray invocation must not nudge the signal.
    if (drive_ <= 0.0f || mix_ <= 0.0f) {
        return;
    }

    for (int n = 0; n < numSamples; ++n) {
        const float inL = left[n];
        const float inR = right[n];

        // Nonlinearity — asymmetric tanh, run at 2x so the harmonics it
        // generates stay below the internal Nyquist and the half-band
        // decimation filter can absorb what remains instead of letting it
        // fold back into the passband.
        float upL0 = 0.0f;
        float upL1 = 0.0f;
        float upR0 = 0.0f;
        float upR1 = 0.0f;
        oversamplerL_.upsample(inL, upL0, upL1);
        oversamplerR_.upsample(inR, upR0, upR1);
        const float satL = oversamplerL_.downsample(saturate(upL0, drivePos_, driveNeg_, normPos_, normNeg_),
                                                    saturate(upL1, drivePos_, driveNeg_, normPos_, normNeg_));
        const float satR = oversamplerR_.downsample(saturate(upR0, drivePos_, driveNeg_, normPos_, normNeg_),
                                                    saturate(upR1, drivePos_, driveNeg_, normPos_, normNeg_));

        // DC blocker — 1-pole high-pass at ~20 Hz. Linear, so it stays at the
        // base rate after decimation.
        //   y[n] = (x[n] - x[n-1]) + R * y[n-1]
        const float hpL = (satL - blockL_.xPrev) + hpfCoeff_ * blockL_.yPrev;
        const float hpR = (satR - blockR_.xPrev) + hpfCoeff_ * blockR_.yPrev;
        blockL_.xPrev = satL;
        blockL_.yPrev = hpL;
        blockR_.xPrev = satR;
        blockR_.yPrev = hpR;

        left[n] = (1.0f - mix_) * inL + mix_ * hpL;
        right[n] = (1.0f - mix_) * inR + mix_ * hpR;
    }
}

} // namespace agentic_synth::engine
