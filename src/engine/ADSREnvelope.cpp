#include "engine/ADSREnvelope.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

// Exponential segments use a leaky-integrator recurrence: y[n] = c*y[n-1] + b
// The fixed point (y* = b/(1-c)) overshoots the nominal target by ±tco so the
// threshold crossing happens at exactly numSamples, regardless of start level.
//
// Derivation for each segment (tco = curvature parameter):
//   Attack  (→ 1.0): fp = 1+tco,        c^N = tco/(1+tco)
//   Decay   (→ S):   fp = S-tco,         c^N = tco/(1-S+tco)
//   Release (→ 0):   fp = -tco,          c^N = tco/(1+tco)  [calibrated from 1.0]

ADSREnvelope::ADSREnvelope(const double sampleRate) : sampleRate_{sampleRate} { recalcCoefficients(); }

void ADSREnvelope::setSampleRate(const double sampleRate) {
    sampleRate_ = sampleRate;
    recalcCoefficients();
}

void ADSREnvelope::setParams(const Params& params) {
    params_ = params;
    recalcCoefficients();
}

void ADSREnvelope::noteOn() { stage_ = Stage::Attack; }

void ADSREnvelope::noteOff() {
    if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

float ADSREnvelope::process() {
    switch (stage_) {
    case Stage::Idle:
        break;

    case Stage::Attack:
        output_ = attackCoeff_ * output_ + attackBase_;
        if (output_ >= 1.0F) {
            output_ = 1.0F;
            stage_ = Stage::Decay;
        }
        break;

    case Stage::Decay:
        output_ = decayCoeff_ * output_ + decayBase_;
        if (output_ <= params_.sustainLevel) {
            output_ = params_.sustainLevel;
            stage_ = Stage::Sustain;
        }
        break;

    case Stage::Sustain:
        output_ = params_.sustainLevel;
        break;

    case Stage::Release:
        output_ = releaseCoeff_ * output_ + releaseBase_;
        if (output_ <= 0.0F) {
            output_ = 0.0F;
            stage_ = Stage::Idle;
        }
        break;
    }
    return output_;
}

bool ADSREnvelope::isActive() const noexcept { return stage_ != Stage::Idle; }

void ADSREnvelope::reset() {
    stage_ = Stage::Idle;
    output_ = 0.0F;
}

void ADSREnvelope::recalcCoefficients() {
    const double tco = std::max(1.0e-6, static_cast<double>(params_.curvature));
    const double sustain = std::clamp(static_cast<double>(params_.sustainLevel), 0.0, 1.0);
    const double sr = sampleRate_;

    // Attack: 0 → 1, fixed point = 1+tco
    {
        const double N = static_cast<double>(params_.attackSeconds) * sr;
        if (N <= 0.0) {
            attackCoeff_ = 0.0F;
            attackBase_ = static_cast<float>(1.0 + tco);
        } else {
            const double ratio = std::max(1e-10, (1.0 + tco) / tco);
            const double c = std::exp(-std::log(ratio) / N);
            attackCoeff_ = static_cast<float>(c);
            attackBase_ = static_cast<float>((1.0 + tco) * (1.0 - c));
        }
    }

    // Decay: 1 → sustain, fixed point = sustain-tco
    {
        const double N = static_cast<double>(params_.decaySeconds) * sr;
        if (N <= 0.0) {
            decayCoeff_ = 0.0F;
            decayBase_ = static_cast<float>(sustain);
        } else {
            const double ratio = std::max(1e-10, (1.0 - sustain + tco) / tco);
            const double c = std::exp(-std::log(ratio) / N);
            decayCoeff_ = static_cast<float>(c);
            decayBase_ = static_cast<float>((sustain - tco) * (1.0 - c));
        }
    }

    // Release: currentLevel → 0, fixed point = -tco, calibrated from 1.0→0
    {
        const double N = static_cast<double>(params_.releaseSeconds) * sr;
        if (N <= 0.0) {
            releaseCoeff_ = 0.0F;
            releaseBase_ = 0.0F;
        } else {
            const double ratio = std::max(1e-10, (1.0 + tco) / tco);
            const double c = std::exp(-std::log(ratio) / N);
            releaseCoeff_ = static_cast<float>(c);
            releaseBase_ = static_cast<float>(-tco * (1.0 - c));
        }
    }
}

} // namespace agentic_synth::engine
