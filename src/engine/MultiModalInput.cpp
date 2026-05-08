#include "MultiModalInput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace agentic_synth::engine {

MultiModalInput::SpectralProfile MultiModalInput::analyzeHum(const float* samples, int numSamples, int sampleRate) {

    auto mag = computeMagnitudeSpectrum(samples, numSamples);
    float centroid = computeCentroid(mag, sampleRate, static_cast<int>(mag.size()));

    SpectralProfile profile{};
    profile.centroidHz = centroid;
    profile.brightness = std::clamp(centroid / 2000.0f, 0.0f, 1.0f);

    // Roughness: spectral spread measure
    if (mag.size() > 1) {
        float weightedSum = 0, totalMag = 0;
        for (size_t i = 0; i < mag.size(); ++i) {
            float freq = kHzPerBin * i;
            weightedSum += mag[i] * std::abs(freq - centroid);
            totalMag += mag[i];
        }
        profile.roughness = totalMag > 0 ? std::clamp((weightedSum / totalMag) / 1000.0f, 0.0f, 1.0f) : 0.0f;
    }

    return profile;
}

float MultiModalInput::detectTapTempo(const float* samples, int numSamples, int sampleRate) {
    // Simple onset detection via envelope peak
    float peakEnergy = 0;
    int peakSample = 0;
    float energy = 0;

    for (int i = 1; i < numSamples; ++i) {
        energy += samples[i - 1] < 0 && samples[i] >= 0 ? samples[i] : 0;
        if (energy > peakEnergy) {
            peakEnergy = energy;
            peakSample = i;
        }
    }

    // If we found a clear onset, estimate BPM
    if (peakEnergy > 0.01f && peakSample < numSamples / 2) {
        float periodSeconds = static_cast<float>(peakSample) / sampleRate;
        return std::clamp(60.0f / periodSeconds, 40.0f, 240.0f);
    }

    return 0.0f; // No clear tempo
}

PatchStruct MultiModalInput::spectralToPatch(const SpectralProfile& profile) {
    PatchStruct patch{};

    // Bright hum → more saw, higher cutoff
    if (profile.brightness > 0.6f) {
        patch.oscillatorMix[0] = 0.7f; // saw
        patch.oscillatorMix[1] = 0.3f; // square
        patch.filterCutoffHz = 3000.0f;
    } else if (profile.brightness > 0.3f) {
        patch.oscillatorMix[0] = 0.4f;
        patch.oscillatorMix[2] = 0.6f; // triangle
        patch.filterCutoffHz = 1200.0f;
    } else {
        patch.oscillatorMix[2] = 0.8f; // triangle
        patch.oscillatorMix[3] = 0.2f; // sine
        patch.filterCutoffHz = 400.0f;
    }

    patch.filterResonance = profile.roughness * 0.5f;
    patch.ampAttackMs = 50.0f;
    patch.ampReleaseMs = 200.0f;

    return patch;
}

void MultiModalInput::applyTempoToPatch(float bpm, PatchStruct& patch) {
    if (bpm <= 0)
        return;
    previousTempo_ = bpm;

    // Map BPM to LFO rate (1/4, 1/8, 1/16 note sync)
    float beatHz = bpm / 60.0f;
    patch.lfoRateHz = beatHz; // 1/4 note
    patch.lfoDepth = 0.3f;
    patch.ampAttackMs = std::clamp(60000.0f / bpm * 0.1f, 5.0f, 1000.0f);
}

std::vector<float> MultiModalInput::computeMagnitudeSpectrum(const float* samples, int numSamples) {

    std::vector<float> window(kFftSize, 0.0f);
    int copyLen = std::min(numSamples, kFftSize);
    std::copy(samples, samples + copyLen, window.begin());

    // Hanning window
    for (int i = 0; i < kFftSize; ++i) {
        window[i] *= 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (kFftSize - 1)));
    }

    // Simple real DFT (magnitude only)
    std::vector<float> mag(kFftSize / 2, 0.0f);
    for (int k = 0; k < kFftSize / 2; ++k) {
        float real = 0, imag = 0;
        for (int n = 0; n < kFftSize; ++n) {
            float angle = 2.0f * M_PI * k * n / kFftSize;
            real += window[n] * std::cos(angle);
            imag -= window[n] * std::sin(angle);
        }
        mag[k] = std::sqrt(real * real + imag * imag) / kFftSize;
    }

    return mag;
}

float MultiModalInput::computeCentroid(const std::vector<float>& mag, int sampleRate, int fftSize) {

    float weightedSum = 0, totalMag = 0;
    for (int i = 0; i < fftSize; ++i) {
        float freq = static_cast<float>(i) * sampleRate / (2 * fftSize);
        weightedSum += mag[i] * freq;
        totalMag += mag[i];
    }
    return totalMag > 0 ? weightedSum / totalMag : 400.0f;
}

} // namespace agentic_synth::engine
