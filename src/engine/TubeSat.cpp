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

void TubeSat::setMix(float mix01) noexcept {
    mix_ = std::clamp(mix01, 0.0f, 1.0f);
}

void TubeSat::reset() noexcept {
    blockL_ = DcBlock{};
    blockR_ = DcBlock{};
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

        // Nonlinearity — asymmetric tanh.
        const float satL = saturate(inL, drivePos_, driveNeg_, normPos_, normNeg_);
        const float satR = saturate(inR, drivePos_, driveNeg_, normPos_, normNeg_);

        // DC blocker — 1-pole high-pass at ~20 Hz.
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
