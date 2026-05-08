#include "Filter.h"

#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace agentic_synth::engine {

// ---------------------------------------------------------------------------
// MoogLadder
// ---------------------------------------------------------------------------

void MoogLadder::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    updateCoefficients();
    reset();
}

void MoogLadder::setCutoff(float hz) {
    cutoff_ = hz;
    updateCoefficients();
}

void MoogLadder::setResonance(float resonance) {
    resonance_ = std::clamp(resonance, 0.0f, 1.0f);
    updateCoefficients();
}

void MoogLadder::reset() { s_.fill(0.0); }

void MoogLadder::updateCoefficients() {
    double fc = std::clamp(static_cast<double>(cutoff_), 20.0, sampleRate_ * 0.49);
    g_ = std::tan(M_PI * fc / sampleRate_);
    a_ = g_ / (1.0 + g_);
    b_ = 1.0 - a_;
    // Cap at 3.8 — self-oscillation onset is k = 4.0
    k_ = static_cast<double>(resonance_) * 3.8;
}

float MoogLadder::process(float input) {
    const double a = a_;
    const double b = b_;
    const double k = k_;
    const double a2 = a * a;
    const double a3 = a2 * a;
    const double a4 = a3 * a;

    // Solve the implicit feedback equation for y4 in closed form (linear ZDF)
    const double state_sum = b * (a3 * s_[0] + a2 * s_[1] + a * s_[2] + s_[3]);
    const double y4 = (a4 * static_cast<double>(input) + state_sum) / (1.0 + k * a4);

    // Forward-propagate stages using the resolved y4
    const double x_eff = static_cast<double>(input) - k * y4;

    const double y1 = a * x_eff + b * s_[0];
    s_[0] = 2.0 * y1 - s_[0];

    const double y2 = a * y1 + b * s_[1];
    s_[1] = 2.0 * y2 - s_[1];

    const double y3 = a * y2 + b * s_[2];
    s_[2] = 2.0 * y3 - s_[2];

    // y4 was solved analytically; update its integrator state consistently
    s_[3] = 2.0 * y4 - s_[3];

    return static_cast<float>(y4);
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
    cutoff_ = hz;
    updateCoefficients();
}

void SVFilter::setResonance(float resonance) {
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
