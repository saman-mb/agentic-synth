#pragma once

// Phase G / #247 — "Hum or tap a pitch". A small monophonic pitch detector
// that runs over a recorded hum buffer (Int16 PCM @ 16 kHz mono — the same
// shape the push-to-talk path hands to GeminiSTT). Output is a MIDI note
// number + confidence + raw frequency so the UI can show "TIMBRE heard a
// note around B♭3" without leaking "fundamental frequency 233.08 Hz" into
// the musician-register copy.
//
// Algorithm: autocorrelation with parabolic peak interpolation. YIN was the
// alternative — autocorrelation is a hair noisier on near-silence but
// stays well under 5 ms on a 1-second buffer and has zero external deps,
// which matters for the agent library footprint. Quality is sufficient for
// a "did you mean" hint, not for tuner-grade f0 reporting.
//
// Confidence is 0..1 and is computed from the height of the peak relative
// to autocorrelation at lag 0 (the energy). Below ~0.5 the result is
// effectively noise; the WebUiComponent emits the event only when >0.7.

#include <cstdint>

namespace agentic_synth::agent {

struct PitchResult {
    int midi_note{-1};       // -1 = no pitch detected
    float confidence{0.0f};  // 0..1
    float frequency_hz{0.0f};
};

class PitchDetector {
public:
    // Detect the fundamental in a monophonic Int16 PCM buffer. `samples`
    // contains `numSamples` little-endian Int16 values at `sampleRate` Hz.
    // Returns an empty result (midi_note == -1) on too-short input or on
    // a confidence < 0.3 result.
    //
    // Frequency search range: 60 Hz (B1 ≈ midi 35) to 1000 Hz (B5 ≈ midi 83).
    // Wider ranges produce more octave errors on humming, which is the
    // expected input. Keyboard-piano taps fall well within this band.
    [[nodiscard]] static PitchResult detect(const std::int16_t* samples,
                                            int numSamples,
                                            int sampleRate = 16000) noexcept;

    // MIDI note rounded from a frequency in Hz. Useful for tests and for
    // callers that already have a frequency in hand (e.g. tap-tempo).
    [[nodiscard]] static int frequencyToMidi(float freqHz) noexcept;
};

} // namespace agentic_synth::agent
