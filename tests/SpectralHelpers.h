#pragma once

// Test-only spectrum helpers for the #431 oversampling checks.
//
// The metric counts FFT energy that does not land on an integer multiple of a
// chosen fundamental. Harmonic distortion (integer multiples) is expected from
// any waveshaper; *inharmonic* energy is the fingerprint of aliasing — the
// folded products sit between the harmonics when the fundamental is chosen not
// to divide the sample rate.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "engine/WavetableOscillator.h"

namespace agentic_synth::test {

// Blackman-Harris 4-term window: ≈ −92 dB sidelobes, so leakage from the
// strong harmonics does not masquerade as alias energy.
inline std::vector<float> blackmanHarrisWindow(std::size_t n) {
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        w[i] = static_cast<float>(0.35875 - 0.48829 * std::cos(2.0 * pi * t) + 0.14128 * std::cos(4.0 * pi * t) -
                                  0.01168 * std::cos(6.0 * pi * t));
    }
    return w;
}

inline std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// `signal` must be a power-of-two length. Returns sqrt(inharmonic energy /
// fundamental energy). A pure (or purely harmonically distorted) tone returns
// ~0; aliasing raises it.
inline double inharmonicRatio(const std::vector<float>& signal, double f0, double sampleRate, double toleranceHz) {
    const std::size_t n = signal.size();
    if (n < 2)
        return 0.0;
    const auto window = blackmanHarrisWindow(n);

    std::vector<std::complex<float>> spec(n);
    for (std::size_t i = 0; i < n; ++i)
        spec[i] = std::complex<float>(signal[i] * window[i], 0.0f);
    agentic_synth::engine::fftRadix2ForTesting(spec, /*inverse=*/false);

    const double binHz = sampleRate / static_cast<double>(n);
    double harmonic = 0.0;
    double inharmonic = 0.0;
    double fundamental = 0.0;
    for (std::size_t k = 1; k <= n / 2; ++k) {
        const double hz = static_cast<double>(k) * binHz;
        const double multiple = std::round(hz / f0);
        const bool isHarmonic = multiple >= 1.0 && std::fabs(hz - multiple * f0) <= toleranceHz;
        const double power = std::norm(spec[k]);
        if (isHarmonic) {
            harmonic += power;
            if (multiple == 1.0)
                fundamental += power;
        } else {
            inharmonic += power;
        }
    }
    (void)harmonic;
    return std::sqrt(inharmonic / std::max(fundamental, 1e-30));
}

} // namespace agentic_synth::test
