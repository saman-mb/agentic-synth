#include "VAOscillator.h"

#include <cmath>
#include <cstdint>

namespace agentic_synth::engine {

namespace {

uint32_t xorshift32(uint32_t& state) noexcept {
    uint32_t x = state ? state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

// 24-bit mantissa in (0, 1] — portable, no <random>.
double unit01(uint32_t& state) noexcept {
    const uint32_t r = xorshift32(state);
    return static_cast<double>((r >> 8) + 1u) * (1.0 / 16777216.0);
}

} // namespace

VAOscillator::VAOscillator() { seedDrift(1u); }

void VAOscillator::seedDrift(uint32_t seed) noexcept {
    rngState_ = seed ? seed : 1u;
    driftCents_ = 0.0;
    drawNewDriftTarget();
    driftTimer_ = driftPeriod_;
}

void VAOscillator::drawNewDriftTarget() noexcept {
    driftTarget_ = -5.0 + 10.0 * unit01(rngState_);
    driftPeriod_ = driftPeriodMin_ + (driftPeriodMax_ - driftPeriodMin_) * unit01(rngState_);
    if (driftPeriod_ < 1.0)
        driftPeriod_ = 1.0;
}

void VAOscillator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    // drift target updates every 1–5 s
    driftPeriodMin_ = sampleRate * 1.0;
    driftPeriodMax_ = sampleRate * 5.0;
    // exponential smoothing: tau ≈ 2 s so drift moves imperceptibly fast
    constexpr double tau = 2.0;
    driftAlpha_ = 1.0 - std::exp(-1.0 / (tau * sampleRate_));

    // Leaky-integrator coefficient for the triangle path. One-pole DC blocker
    // with corner ≈ 5 Hz: well below the lowest musical fundamental so the
    // audible spectrum is untouched, but DC settles in ~30 ms.
    constexpr double kTriLeakFc = 5.0;
    triLeak_ = 1.0 - 2.0 * 3.141592653589793 * kTriLeakFc / sampleRate_;

    reset();
}

void VAOscillator::setWaveform(Waveform w) noexcept { waveform_ = w; }

void VAOscillator::setFrequency(double hz) noexcept {
    frequency_ = hz;
    updatePhaseInc();
}

void VAOscillator::setDetuneCents(double cents) noexcept {
    detuneCents_ = cents;
    updatePhaseInc();
}

void VAOscillator::reset() noexcept {
    phase_ = 0.0;
    triAccum_ = -1.0;
    driftCents_ = 0.0;
    drawNewDriftTarget();
    driftTimer_ = driftPeriod_;
    updatePhaseInc();
}

double VAOscillator::getDriftCents() const noexcept { return driftCents_; }

void VAOscillator::updatePhaseInc() noexcept {
    double effectiveCents = detuneCents_ + driftCents_;
    double effectiveFreq = frequency_ * std::pow(2.0, effectiveCents / 1200.0);
    phaseInc_ = effectiveFreq / sampleRate_;
}

void VAOscillator::tickDrift() noexcept {
    driftTimer_ -= 1.0;
    if (driftTimer_ <= 0.0) {
        drawNewDriftTarget();
        driftTimer_ = driftPeriod_;
    }
    // Invariant: |driftCents_| ≤ 5 because |(1-a)*x + a*y| ≤ max(|x|,|y|) for a∈[0,1]
    driftCents_ += driftAlpha_ * (driftTarget_ - driftCents_);
}

// Residual for a unit-height discontinuity at t=0.  Returns a value in [-1,1]
// that is subtracted from the naive saw (or added/subtracted for square).
double VAOscillator::polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0; // rises from -1 to 0 over one dt window
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0; // rises from 0 to +1 approaching the wrap
    }
    return 0.0;
}

double VAOscillator::saw() noexcept {
    double out = 2.0 * phase_ - 1.0;
    out -= polyBlep(phase_, phaseInc_);
    return out;
}

double VAOscillator::square() noexcept {
    double out = phase_ < 0.5 ? 1.0 : -1.0;
    // upward step at t=0, downward step at t=0.5
    out += polyBlep(phase_, phaseInc_);
    out -= polyBlep(std::fmod(phase_ + 0.5, 1.0), phaseInc_);
    return out;
}

double VAOscillator::triangle() noexcept {
    // Triangle is the running integral of 4·phaseInc·square.
    // triAccum_ initialises to -1 at phase=0 so the first half-cycle rises to +1.
    // A one-pole leak (fc≈5 Hz) replaces the previous 1024-sample clamp: it
    // bleeds off DC continuously rather than letting it accrue between resets,
    // killing the gritty edge artefacts the clamp produced at high notes.
    double sq = square();
    triAccum_ = triAccum_ * triLeak_ + 4.0 * phaseInc_ * sq;
    return triAccum_;
}

float VAOscillator::processSample() noexcept {
    tickDrift();
    updatePhaseInc();

    double out = 0.0;
    switch (waveform_) {
    case Waveform::Saw:
        out = saw();
        break;
    case Waveform::Square:
        out = square();
        break;
    case Waveform::Triangle:
        out = triangle();
        break;
    }

    phase_ += phaseInc_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    return static_cast<float>(out);
}

} // namespace agentic_synth::engine
