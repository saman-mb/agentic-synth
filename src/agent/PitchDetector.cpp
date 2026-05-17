#include "agent/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace agentic_synth::agent {

namespace {

constexpr float kMinFreqHz = 60.0f;   // ~B1 — below this is humming-into-the-mic rumble
constexpr float kMaxFreqHz = 1000.0f; // ~B5 — top of where humming + piano taps live
constexpr float kMinConfidence = 0.3f;

// Convert Int16 → normalized float and DC-block via running mean. The hum
// buffer often has a DC offset from cheap laptop mics; removing it before
// autocorrelation cleans up the lag-0 energy estimate.
std::vector<float> normalizeAndDCBlock(const std::int16_t* samples, int numSamples) {
    std::vector<float> out;
    out.resize(static_cast<size_t>(numSamples));
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(samples[i]);
    const float mean = static_cast<float>(sum / std::max(1, numSamples));
    for (int i = 0; i < numSamples; ++i) {
        const float s = static_cast<float>(samples[i]) - mean;
        out[static_cast<size_t>(i)] = s / 32768.0f;
    }
    return out;
}

} // namespace

PitchResult PitchDetector::detect(const std::int16_t* samples,
                                  int numSamples,
                                  int sampleRate) noexcept {
    PitchResult result{};
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0)
        return result;

    const int minLag = static_cast<int>(std::floor(static_cast<float>(sampleRate) / kMaxFreqHz));
    const int maxLag = static_cast<int>(std::ceil(static_cast<float>(sampleRate) / kMinFreqHz));
    if (numSamples <= maxLag * 2)
        return result;

    std::vector<float> buf = normalizeAndDCBlock(samples, numSamples);

    // RMS gate — silence-in / silence-out (a quiet mic should report
    // "no pitch" rather than a noisy spurious lock).
    double rmsSum = 0.0;
    for (float v : buf)
        rmsSum += static_cast<double>(v) * v;
    const float rms = static_cast<float>(std::sqrt(rmsSum / static_cast<double>(buf.size())));
    if (rms < 0.005f) // below ~ -46 dBFS → treat as silence
        return result;

    // Autocorrelation at lag 0 (energy) for confidence normalization.
    double energy = 0.0;
    const int N = numSamples;
    for (int i = 0; i < N; ++i)
        energy += static_cast<double>(buf[static_cast<size_t>(i)]) * buf[static_cast<size_t>(i)];
    if (energy <= 0.0)
        return result;

    // Scan autocorrelation for first valley→peak transition above minLag.
    // We pick the highest peak in [minLag, maxLag] and require it to be a
    // local max (prior + next sample lower). Avoids picking lag-0 side lobes.
    int bestLag = -1;
    double bestPeak = 0.0;
    double prev = 0.0;
    double cur = 0.0;
    for (int lag = minLag; lag <= maxLag && lag + 1 < N; ++lag) {
        double acc = 0.0;
        for (int i = 0; i + lag < N; ++i)
            acc += static_cast<double>(buf[static_cast<size_t>(i)]) *
                   buf[static_cast<size_t>(i + lag)];

        // Test cur as local max against prev (one step back) and acc (next).
        if (lag > minLag + 1 && cur > prev && cur > acc && cur > bestPeak) {
            bestPeak = cur;
            bestLag = lag - 1;
        }
        prev = cur;
        cur = acc;
    }
    // Tail edge: cur may still be a peak.
    if (cur > bestPeak && cur > prev) {
        bestPeak = cur;
        bestLag = maxLag;
    }
    if (bestLag < 0)
        return result;

    // Parabolic interpolation around bestLag for sub-sample accuracy.
    // y_{-1}, y_0, y_{+1} are autocorrelations at bestLag-1, bestLag, bestLag+1.
    auto acAt = [&](int lag) -> double {
        if (lag < 1 || lag + 1 >= N)
            return 0.0;
        double acc = 0.0;
        for (int i = 0; i + lag < N; ++i)
            acc += static_cast<double>(buf[static_cast<size_t>(i)]) *
                   buf[static_cast<size_t>(i + lag)];
        return acc;
    };
    const double yL = acAt(bestLag - 1);
    const double yM = acAt(bestLag);
    const double yR = acAt(bestLag + 1);
    const double denom = (yL - 2.0 * yM + yR);
    const double offset = denom != 0.0 ? 0.5 * (yL - yR) / denom : 0.0;
    const float refinedLag = static_cast<float>(bestLag) + static_cast<float>(offset);
    if (refinedLag <= 0.0f)
        return result;

    const float freq = static_cast<float>(sampleRate) / refinedLag;
    if (freq < kMinFreqHz || freq > kMaxFreqHz)
        return result;

    const float confidence = std::clamp(static_cast<float>(bestPeak / energy), 0.0f, 1.0f);
    if (confidence < kMinConfidence)
        return result;

    result.frequency_hz = freq;
    result.confidence = confidence;
    result.midi_note = frequencyToMidi(freq);
    return result;
}

int PitchDetector::frequencyToMidi(float freqHz) noexcept {
    if (freqHz <= 0.0f)
        return -1;
    // 69 + 12 * log2(f / 440)
    const float midi = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
    return static_cast<int>(std::lround(midi));
}

} // namespace agentic_synth::agent
