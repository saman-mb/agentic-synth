#pragma once

#include "engine/PatchStruct.h"

#include <vector>
#include <complex>

namespace agentic_synth::engine {

// Multi-modal input: hum or tap → timbre/rhythm intent
// Uses basic FFT analysis to extract spectral shape and tap tempo.

class MultiModalInput {
public:
    // Analyze hummed/buzzed audio buffer to extract spectral profile
    // Returns spectral centroid and formant-like shape hints
    struct SpectralProfile {
        float centroidHz = 400.0f;     // Spectral centroid
        float brightness = 0.5f;       // 0 = dark, 1 = bright
        float roughness = 0.0f;        // 0 = smooth, 1 = rough
    };

    // Analyze a buffer of microphone audio samples
    SpectralProfile analyzeHum(const float* samples, int numSamples, int sampleRate);

    // Detect tap tempo from impulse peaks in audio buffer
    // Returns detected BPM (0 if no clear tempo detected)
    float detectTapTempo(const float* samples, int numSamples, int sampleRate);

    // Convert spectral profile to patch parameter hints
    PatchStruct spectralToPatch(const SpectralProfile& profile);

    // Map tap tempo to patch rhythm parameters
    void applyTempoToPatch(float bpm, PatchStruct& patch);

private:
    // Simple DFT for spectral analysis
    std::vector<float> computeMagnitudeSpectrum(const float* samples, int numSamples);
    float computeCentroid(const std::vector<float>& magSpectrum, int sampleRate, int fftSize);
    float previousTempo_ = 120.0f;
    static constexpr int kFftSize = 1024;
    static constexpr float kHzPerBin = 44100.0f / kFftSize;
};

} // namespace agentic_synth::engine
