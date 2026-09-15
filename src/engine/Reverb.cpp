#include "engine/Reverb.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

// Classic Freeverb comb/allpass tunings are declared inside prepare() so we
// can scale them by sampleRate without polluting global namespace.

// ── Comb ────────────────────────────────────────────────────────────────────

float Reverb::Comb::process(float input) noexcept {
    const int size = static_cast<int>(buf.size());
    float out = buf[static_cast<std::size_t>(idx)];
    // One-pole lowpass on the feedback path (Freeverb-original).
    // y[n] = x[n] * (1-d) + y[n-1] * d
    lowpassState = out * (1.0f - damping) + lowpassState * damping;
    buf[static_cast<std::size_t>(idx)] = input + lowpassState * feedback;
    if (++idx >= size)
        idx = 0;
    return out;
}

void Reverb::Comb::resize(int sizeSamples) {
    buf.assign(static_cast<std::size_t>(std::max(1, sizeSamples)), 0.0f);
    idx = 0;
    lowpassState = 0.0f;
}

void Reverb::Comb::reset() noexcept {
    std::fill(buf.begin(), buf.end(), 0.0f);
    idx = 0;
    lowpassState = 0.0f;
}

// ── Allpass ─────────────────────────────────────────────────────────────────

float Reverb::Allpass::process(float input) noexcept {
    const int size = static_cast<int>(buf.size());
    const float bufout = buf[static_cast<std::size_t>(idx)];
    // Schroeder allpass:
    //   out = -input + bufout
    //   buf = input + bufout * feedback
    const float out = -input + bufout;
    buf[static_cast<std::size_t>(idx)] = input + bufout * feedback;
    if (++idx >= size)
        idx = 0;
    return out;
}

void Reverb::Allpass::resize(int sizeSamples) {
    buf.assign(static_cast<std::size_t>(std::max(1, sizeSamples)), 0.0f);
    idx = 0;
}

void Reverb::Allpass::reset() noexcept {
    std::fill(buf.begin(), buf.end(), 0.0f);
    idx = 0;
}

// ── Reverb ──────────────────────────────────────────────────────────────────

Reverb::Reverb() = default;

void Reverb::prepare(double sampleRate) {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;

    // Freeverb-original tunings (samples @ 44.1 kHz). The full 8-comb set
    // restores the modal density Freeverb relies on for a diffused tail; the
    // first four alone ring with sparse, audible modes at large sizes (#432).
    static constexpr int kCombL[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kAllpassL[kNumAllpasses] = {556, 441};
    static constexpr int kStereoSpread = 23;

    const double scale = sampleRate_ / 44100.0;

    for (int i = 0; i < kNumCombs; ++i) {
        const int lenL = static_cast<int>(std::lround(kCombL[i] * scale));
        const int lenR = static_cast<int>(std::lround((kCombL[i] + kStereoSpread) * scale));
        combsL_[static_cast<std::size_t>(i)].resize(lenL);
        combsR_[static_cast<std::size_t>(i)].resize(lenR);
    }
    for (int i = 0; i < kNumAllpasses; ++i) {
        const int lenL = static_cast<int>(std::lround(kAllpassL[i] * scale));
        const int lenR = static_cast<int>(std::lround((kAllpassL[i] + kStereoSpread) * scale));
        allpassesL_[static_cast<std::size_t>(i)].resize(lenL);
        allpassesR_[static_cast<std::size_t>(i)].resize(lenR);

        // Freeverb-original: fixed 0.5 allpass feedback.
        allpassesL_[static_cast<std::size_t>(i)].feedback = 0.5f;
        allpassesR_[static_cast<std::size_t>(i)].feedback = 0.5f;
    }

    // Re-apply current parameters so derived state matches the new SR.
    setSize(size_);
    setDamp(damp_);
}

void Reverb::setSize(float size01) noexcept {
    size_ = std::clamp(size01, 0.0f, 1.0f);
    // size=0 → 0.70  (short room), size=1 → 0.98 (long hall)
    const float fb = 0.70f + size_ * 0.28f;
    for (auto& c : combsL_)
        c.feedback = fb;
    for (auto& c : combsR_)
        c.feedback = fb;
}

void Reverb::setDamp(float damp01) noexcept {
    damp_ = std::clamp(damp01, 0.0f, 1.0f);
    // Freeverb-original caps internal damping at damp * 0.5, which leaves a
    // permanently bright tail at damp = 1.0. Rescale the user range onto
    // [0, kMaxInternalDamping] with a squared curve: low/mid settings track
    // the original character closely (0.5 -> 0.24 vs 0.25), while the top of
    // the knob reaches an audibly dark ~350 Hz feedback corner (#432).
    const float d = kMaxInternalDamping * damp_ * damp_;
    for (auto& c : combsL_)
        c.damping = d;
    for (auto& c : combsR_)
        c.damping = d;
}

void Reverb::setMix(float mix01) noexcept { mix_ = std::clamp(mix01, 0.0f, 1.0f); }

void Reverb::process(float inL, float inR, float& outL, float& outR) noexcept {
    // Mono sum into the reverb network with a small input gain to keep
    // headroom under control (Freeverb uses 0.015; we use 0.5 for a brighter
    // tail since downstream gain stages are unity here).
    const float in = (inL + inR) * 0.5f;

    // Sum of all parallel combs per channel.
    float wetL = 0.0f;
    float wetR = 0.0f;
    for (auto& c : combsL_)
        wetL += c.process(in);
    for (auto& c : combsR_)
        wetR += c.process(in);

    // Series allpass diffusers.
    for (auto& ap : allpassesL_)
        wetL = ap.process(wetL);
    for (auto& ap : allpassesR_)
        wetR = ap.process(wetR);

    // Equal-power-ish linear crossfade. Scale by the comb-bank size so the
    // restored 8-comb bank does not double the wet level versus the old
    // 4-comb bank (see PR #432 for the measured CPU/level comparison).
    constexpr float kWetGain = 1.0f / static_cast<float>(kNumCombs);
    const float wetGainL = wetL * kWetGain;
    const float wetGainR = wetR * kWetGain;

    outL = mix_ * wetGainL + (1.0f - mix_) * inL;
    outR = mix_ * wetGainR + (1.0f - mix_) * inR;
}

void Reverb::reset() noexcept {
    for (auto& c : combsL_)
        c.reset();
    for (auto& c : combsR_)
        c.reset();
    for (auto& ap : allpassesL_)
        ap.reset();
    for (auto& ap : allpassesR_)
        ap.reset();
}

} // namespace agentic_synth::engine
