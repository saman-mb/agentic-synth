#include "engine/Chorus.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr double kThirdTau = kTwoPi / 3.0;
}

constexpr std::array<float, 3> Chorus::kTapBaseMs;
constexpr std::array<float, 3> Chorus::kTapModMs;

Chorus::Chorus() = default;

void Chorus::prepare(double sampleRate, int channels) {
    (void)channels; // stereo-only DSP; param kept for API parity
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    // Buffer must hold the deepest tap base + modulation headroom + a couple
    // of extra samples for cubic interpolation lookback. Allocate once.
    const float maxDelaySamples = (kMaxDelayMs + kModDepthMs) * 0.001f * static_cast<float>(sampleRate_);
    bufSize_ = static_cast<int>(std::ceil(maxDelaySamples)) + 4;
    bufL_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    bufR_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    writeIdx_ = 0;
    phaseL_ = 0.0;
    phaseR_ = 0.5; // 180° offset on the right LFO → stereo width
}

void Chorus::setRate(float rateHz) noexcept {
    rateHz_ = std::clamp(rateHz, 0.05f, 8.0f);
}

void Chorus::setDepth(float depth01) noexcept {
    depth_ = std::clamp(depth01, 0.0f, 1.0f);
}

void Chorus::setMix(float mix01) noexcept {
    mix_ = std::clamp(mix01, 0.0f, 1.0f);
}

void Chorus::reset() noexcept {
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    writeIdx_ = 0;
    phaseL_ = 0.0;
    phaseR_ = 0.5;
}

float Chorus::readInterpolated(const float* buf, int bufSize, int writeIdx,
                               float delaySamples) noexcept {
    // Position is (writeIdx - delaySamples) wrapped into [0, bufSize).
    // We need four samples around the read point for Catmull-Rom / cubic
    // Hermite: y[-1], y[0], y[1], y[2] where the fractional position f lies
    // between y[0] and y[1].
    float readPos = static_cast<float>(writeIdx) - delaySamples;
    while (readPos < 0.0f)
        readPos += static_cast<float>(bufSize);
    while (readPos >= static_cast<float>(bufSize))
        readPos -= static_cast<float>(bufSize);

    const int i0 = static_cast<int>(readPos);
    const float frac = readPos - static_cast<float>(i0);

    const int im1 = (i0 - 1 + bufSize) % bufSize;
    const int i1 = (i0 + 1) % bufSize;
    const int i2 = (i0 + 2) % bufSize;

    const float ym1 = buf[im1];
    const float y0 = buf[i0];
    const float y1 = buf[i1];
    const float y2 = buf[i2];

    // Catmull-Rom cubic interpolation. Stable, low-aliasing,
    // continuous-derivative; the canonical chorus / modulated-delay readout.
    const float a = 0.5f * (-ym1 + 3.0f * y0 - 3.0f * y1 + y2);
    const float b = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c = 0.5f * (-ym1 + y1);
    const float d = y0;
    return ((a * frac + b) * frac + c) * frac + d;
}

void Chorus::processStereo(float* left, float* right, int numSamples) noexcept {
    if (bufSize_ <= 0 || left == nullptr || right == nullptr)
        return;

    // Bit-exact bypass when mix is zero — avoid any read/write of the delay
    // line so callers can flip chorus on/off without paying for it.
    if (mix_ <= 0.0f) {
        return; // dry signal stays in place
    }

    const float srMs = static_cast<float>(sampleRate_) * 0.001f;
    const double phaseInc = static_cast<double>(rateHz_) / sampleRate_;

    for (int n = 0; n < numSamples; ++n) {
        const float inL = left[n];
        const float inR = right[n];

        // Write current input into both delay lines.
        bufL_[static_cast<std::size_t>(writeIdx_)] = inL;
        bufR_[static_cast<std::size_t>(writeIdx_)] = inR;

        // Sum three taps per channel with their independent LFO offsets.
        float sumL = 0.0f;
        float sumR = 0.0f;
        for (int t = 0; t < 3; ++t) {
            const double lfoPhaseL = phaseL_ + (static_cast<double>(t) * kThirdTau) / kTwoPi;
            const double lfoPhaseR = phaseR_ + (static_cast<double>(t) * kThirdTau) / kTwoPi;
            // sin returns [-1, +1]; depth scales the excursion.
            const float lfoL = std::sin(static_cast<float>(lfoPhaseL * kTwoPi));
            const float lfoR = std::sin(static_cast<float>(lfoPhaseR * kTwoPi));
            const float modMs = kTapModMs[t] * lfoL * depth_;
            const float modMsR = kTapModMs[t] * lfoR * depth_;
            const float delaySamplesL = (kTapBaseMs[t] + modMs) * srMs;
            const float delaySamplesR = (kTapBaseMs[t] + modMsR) * srMs;
            sumL += readInterpolated(bufL_.data(), bufSize_, writeIdx_, delaySamplesL);
            sumR += readInterpolated(bufR_.data(), bufSize_, writeIdx_, delaySamplesR);
        }
        // Average of three taps.
        constexpr float kInv3 = 1.0f / 3.0f;
        const float wetL = sumL * kInv3;
        const float wetR = sumR * kInv3;

        left[n] = (1.0f - mix_) * inL + mix_ * wetL;
        right[n] = (1.0f - mix_) * inR + mix_ * wetR;

        // Advance write index + LFO phases.
        if (++writeIdx_ >= bufSize_)
            writeIdx_ = 0;
        phaseL_ += phaseInc;
        if (phaseL_ >= 1.0)
            phaseL_ -= std::floor(phaseL_);
        phaseR_ += phaseInc;
        if (phaseR_ >= 1.0)
            phaseR_ -= std::floor(phaseR_);
    }
}

} // namespace agentic_synth::engine
