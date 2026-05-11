#include "Filter.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace agentic_synth::engine {

// ---------------------------------------------------------------------------
// MoogLadder
// ---------------------------------------------------------------------------
// Drive gain mapping: input is multiplied by 1 + drive * kDriveGainFactor.
// kDriveGainFactor = 4.0 → max input boost ~ 5x (~ +14 dB), giving audible
// character without instantly clipping the tanh into a square wave at drive=1.
static constexpr double kDriveGainFactor = 4.0;
// Output compensation: scale by 1 / sqrt(driveGain) so that at unity sub-cutoff
// the perceived level stays sane (the tanh also reduces gain, so this is a
// gentle counter-balance rather than full make-up).
static inline double driveCompensation(double driveGain) {
    return 1.0 / std::sqrt(driveGain);
}

void MoogLadder::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    updateCoefficients();
    reset();
}

void MoogLadder::setCutoff(float hz) {
    // Defensive boundary guard: ignore non-finite setter inputs so a bad
    // modulation source can't poison the coefficient state. VoiceManager
    // sanitises modulation via safe() in applyPatch already (Phase 1); this
    // is a belt-and-braces second layer at the filter boundary.
    if (!std::isfinite(hz)) return;
    cutoff_ = hz;
    updateCoefficients();
}

void MoogLadder::setResonance(float resonance) {
    if (!std::isfinite(resonance)) return;
    resonance_ = std::clamp(resonance, 0.0f, 1.0f);
    updateCoefficients();
}

void MoogLadder::setDrive(float drive) {
    if (!std::isfinite(drive)) return;
    drive_ = std::clamp(drive, 0.0f, 1.0f);
    driveGain_ = 1.0 + static_cast<double>(drive_) * kDriveGainFactor;
    driveComp_ = driveCompensation(driveGain_);
}

void MoogLadder::reset() { s_.fill(0.0); }

void MoogLadder::updateCoefficients() {
    double fc = std::clamp(static_cast<double>(cutoff_), 20.0, sampleRate_ * 0.49);
    g_ = std::tan(M_PI * fc / sampleRate_);
    a_ = g_ / (1.0 + g_);
    b_ = 1.0 - a_;
    // Allow full self-oscillation: at resonance=1.0 k reaches 4.1 (just past
    // the linear k=4.0 threshold so the loop is actively unstable in the
    // linear regime — but the tanh saturation in the feedback path bounds the
    // amplitude to a soft limit cycle, giving stable self-oscillation.
    k_ = static_cast<double>(resonance_) * 4.1;
}

float MoogLadder::process(float input) {
    // NaN/Inf input guard: tanh(NaN) = NaN, which would propagate forever
    // through the integrator state once a single bad sample lands. Clear
    // state and return silence so the filter recovers on the very next
    // valid sample instead of being poisoned for the rest of the session.
    if (!std::isfinite(input)) {
        reset();
        return 0.0f;
    }

    // Denormal protection: inject a tiny DC bias that cancels itself in the
    // sum but pushes integrator accumulators out of the subnormal range.
    // Portable, no SSE intrinsics required.
    constexpr double kAntiDenormal = 1.0e-20;

    const double a = a_;
    const double b = b_;
    const double k = k_;
    const double a2 = a * a;
    const double a3 = a2 * a;
    const double a4 = a3 * a;

    // Apply input drive (pre-ladder).
    const double x = static_cast<double>(input) * driveGain_;

    // Step 1 — Linear ZDF prediction of y4 (closed-form, one shot).
    //   This gives us the implicit feedback solution under the assumption that
    //   the feedback is linear (k * y4). We use this prediction *only* to
    //   compute the saturated feedback signal that goes back into the ladder.
    const double state_sum = b * (a3 * s_[0] + a2 * s_[1] + a * s_[2] + s_[3]);
    const double y4_linear = (a4 * x + state_sum) / (1.0 + k * a4);

    // Step 2 — Saturate the predicted feedback path with tanh. This is the
    // Huovilainen-style nonlinearity that gives the Moog its growl, allows
    // clean self-oscillation, and bounds the loop gain when k → 4.
    const double y4_fb = std::tanh(y4_linear);

    // Step 3 — Forward-propagate the four cascaded one-pole stages using the
    // saturated feedback. The actual output is the post-pass y4 (not the
    // saturated value), so sub-cutoff passband gain stays ~unity.
    const double x_eff = x - k * y4_fb;

    const double y1 = a * x_eff + b * s_[0];
    s_[0] = 2.0 * y1 - s_[0] + kAntiDenormal;

    const double y2 = a * y1 + b * s_[1];
    s_[1] = 2.0 * y2 - s_[1] + kAntiDenormal;

    const double y3 = a * y2 + b * s_[2];
    s_[2] = 2.0 * y3 - s_[2] + kAntiDenormal;

    const double y4 = a * y3 + b * s_[3];
    s_[3] = 2.0 * y4 - s_[3] + kAntiDenormal;

    // Output compensation keeps perceived level sane under heavy drive.
    return static_cast<float>(y4 * driveComp_);
}

// ---------------------------------------------------------------------------
// SVFilter
// ---------------------------------------------------------------------------

SVFilter::SVFilter(FilterMode mode) : mode_(mode) {}

void SVFilter::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    updateCoefficients();
    reset();
}

void SVFilter::setCutoff(float hz) {
    if (!std::isfinite(hz)) return;
    cutoff_ = hz;
    updateCoefficients();
}

void SVFilter::setResonance(float resonance) {
    if (!std::isfinite(resonance)) return;
    resonance_ = std::clamp(resonance, 0.0f, 1.0f);
    updateCoefficients();
}

void SVFilter::setMode(FilterMode mode) { mode_ = mode; }

void SVFilter::reset() {
    ic1eq_ = 0.0;
    ic2eq_ = 0.0;
}

void SVFilter::updateCoefficients() {
    double fc = std::clamp(static_cast<double>(cutoff_), 20.0, sampleRate_ * 0.49);
    const double g = std::tan(M_PI * fc / sampleRate_);
    // k = 1/Q; resonance 0→1 maps k from 2.0 (overdamped) down to 0.1 (capped above 0)
    k_ = 2.0 - 1.9 * static_cast<double>(resonance_);
    a1_ = 1.0 / (1.0 + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
}

float SVFilter::process(float x_in) {
    // NaN/Inf input guard — same rationale as MoogLadder::process.
    if (!std::isfinite(x_in)) {
        reset();
        return 0.0f;
    }
    const double x = static_cast<double>(x_in);
    const double v3 = x - ic2eq_;
    const double v1 = a1_ * ic1eq_ + a2_ * v3;
    const double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

    ic1eq_ = 2.0 * v1 - ic1eq_;
    ic2eq_ = 2.0 * v2 - ic2eq_;

    const double lp = v2;
    const double bp = v1;
    const double hp = x - k_ * v1 - v2;
    const double notch = hp + lp;

    switch (mode_) {
    case FilterMode::LP:
        return static_cast<float>(lp);
    case FilterMode::HP:
        return static_cast<float>(hp);
    case FilterMode::BP:
        return static_cast<float>(bp);
    case FilterMode::Notch:
        return static_cast<float>(notch);
    }
    return 0.0f;
}

} // namespace agentic_synth::engine
