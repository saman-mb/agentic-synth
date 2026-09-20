#pragma once

#include <array>
#include <cmath>

namespace agentic_synth::engine {

// 2x oversampling for memoryless / in-loop nonlinearities.
//
// A single linear-phase half-band FIR lowpass is used in both directions. The
// kernel is cut at one quarter of the *oversampled* rate — exactly the Nyquist
// frequency of the base rate — so the same filter is both an anti-imaging
// filter on the way up and an anti-aliasing filter on the way down:
//
//   upsample:    zero-stuff by 2  → half-band lowpass      (anti-imaging)
//   nonlinearity: run at 2x so harmonics land below the internal Nyquist
//   downsample:  half-band lowpass → discard every 2nd      (anti-aliasing)
//
// A half-band filter has an odd tap count, h[centre] = 0.5 and every
// even-offset tap exactly zero; the odd taps carry the correction. The kernel
// is windowed (Blackman-Harris, ≈ −92 dB sidelobes) and its DC gain normalised
// to unity, which keeps the passband level-matched to the base-rate signal.
// Content that still sits above the internal Nyquist when the nonlinearity
// runs is absorbed by the decimation filter instead of folding back into the
// passband.
//
// Realtime safe: all state lives in fixed-size std::arrays, so there is no
// allocation or locking anywhere. The coefficients are rate-relative, so a
// sample-rate change only needs to reset the history (prepare()/reset()).
class Oversampler2x {
public:
    // Odd tap count is required for the half-band zero pattern. 63 taps leave
    // a transition band wide enough to reach the −90 dB stopband before the
    // first image that could fold into the baseband.
    static constexpr int kTaps = 63;
    // Power-of-two ring length ≥ kTaps so the history index can mask instead
    // of modulo on the per-sample path.
    static constexpr int kBuffer = 128;
    static constexpr int kMask = kBuffer - 1;
    // Compensates the half-amplitude that zero-stuffing introduces on the way
    // up. Decimation needs no such factor.
    static constexpr float kUpsampleGain = 2.0f;
    // Non-zero taps: h[centre] plus the odd-offset taps. The even-offset taps
    // are exactly zero and are skipped on the hot path. For odd kTaps this is
    // (kTaps + 3) / 2.
    static constexpr int kNonZeroTaps = (kTaps + 3) / 2;
    static constexpr int kCentre = kTaps / 2;

    Oversampler2x() noexcept { design(); }

    // The half-band cutoff is expressed relative to the sample rate, so no
    // coefficient depends on the absolute rate; this only resets the history.
    void prepare(double /*sampleRate*/) noexcept {
        design();
        reset();
    }

    void reset() noexcept {
        up_.fill(0.0f);
        down_.fill(0.0f);
        upPos_ = 0;
        downPos_ = 0;
    }

    // Consume one base-rate sample, produce the two 2x-rate samples.
    //
    // Polyphase form: the odd internal phase is fed only by the centre tap
    // (every even-offset tap is zero and the zero-stuffed buffer is empty at
    // the matching positions), so it is a pure gain/one-sample memory read;
    // only the even phase needs the non-zero sub-filter.
    void upsample(float x, float& out0, float& out1) noexcept {
        up_[upPos_ = (upPos_ + 1) & kMask] = x;
        out0 = kUpsampleGain * convolveNonZero(up_, upPos_);
        up_[upPos_ = (upPos_ + 1) & kMask] = 0.0f;
        out1 = (kUpsampleGain * hCentre_) * up_[(upPos_ - kCentre) & kMask];
    }

    // Consume the two 2x-rate samples, produce one base-rate sample.
    float downsample(float in0, float in1) noexcept {
        down_[downPos_ = (downPos_ + 1) & kMask] = in0;
        down_[downPos_ = (downPos_ + 1) & kMask] = in1;
        return convolveNonZero(down_, downPos_);
    }

private:
    float convolveNonZero(const std::array<float, kBuffer>& buf, int pos) const noexcept {
        float acc = 0.0f;
        for (int j = 0; j < kNonZeroTaps; ++j)
            acc += nzCoef_[j] * buf[(pos - nzIndex_[j]) & kMask];
        return acc;
    }

    void design() noexcept {
        constexpr float pi = 3.14159265358979323846f;
        float full[kTaps];
        float sum = 0.0f;
        for (int i = 0; i < kTaps; ++i) {
            const int d = i - kCentre;
            float ideal = 0.0f;
            if (d == 0)
                ideal = 0.5f;
            else if ((d & 1) != 0)
                ideal = std::sin(0.5f * pi * static_cast<float>(d)) / (pi * static_cast<float>(d));

            // Blackman-Harris 4-term window, centred on the middle tap.
            const float t = static_cast<float>(i) / static_cast<float>(kTaps - 1);
            const float w = 0.35875f - 0.48829f * std::cos(2.0f * pi * t) + 0.14128f * std::cos(4.0f * pi * t) -
                            0.01168f * std::cos(6.0f * pi * t);

            full[i] = ideal * w;
            sum += full[i];
        }
        // Unity DC gain → the passband is level-matched in both directions.
        const float norm = (sum != 0.0f) ? 1.0f / sum : 1.0f;

        int nz = 0;
        for (int i = 0; i < kTaps; ++i) {
            const float coeff = full[i] * norm;
            if (coeff != 0.0f) {
                nzIndex_[nz] = i;
                nzCoef_[nz] = coeff;
                ++nz;
            }
            if (i == kCentre)
                hCentre_ = coeff;
        }
    }

    std::array<float, kNonZeroTaps> nzCoef_{};
    std::array<int, kNonZeroTaps> nzIndex_{};
    float hCentre_{0.5f};
    // Interpolation and decimation each hold their own history so the two
    // directions never share (or corrupt) state.
    std::array<float, kBuffer> up_{};
    std::array<float, kBuffer> down_{};
    int upPos_{0};
    int downPos_{0};
};

} // namespace agentic_synth::engine
