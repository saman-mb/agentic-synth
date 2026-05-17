#pragma once

#include "engine/PatchStruct.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace agentic_synth::engine {

// Phase D / #268 (partial) — offline audio bounce.
//
// Drives a fresh VoiceManager instance through a synchronous off-thread render
// of a single MIDI note with a sustained body + release tail, returning the
// resulting interleaved stereo buffer. Used by the "Bounce to wav" affordance
// on a committed preset (#260) so producers can take TIMBRE sounds into a
// track without involving the realtime audio engine.
//
// The renderer never touches the live VoiceManager — it constructs its own
// graph each call, so the live audio thread is undisturbed.
struct BounceConfig {
    int sample_rate_hz{48000};
    int bit_depth{24};
    int channels{2};
    float duration_s{4.0f};
    int midi_note{60};          // C3 = MIDI 60 in note-numbering used by TIMBRE
    int velocity{100};          // 1..127 (mapped to 0..1 inside)
    float note_hold_s{3.0f};    // note-on for N seconds, then release for the remainder
};

// Render the given patch through a fresh VoiceManager + FX chain into an
// interleaved stereo float buffer.  Size = round(duration_s * sample_rate_hz)
// frames × channels samples.  Thread-safe: each call constructs all DSP state.
[[nodiscard]] std::vector<float> renderPatchToBuffer(const PatchStruct& patch, const BounceConfig& cfg);

// Render via renderPatchToBuffer then write a 24-bit PCM stereo WAV to disk.
// Returns true on success; false on render/write failure (caller surfaces).
bool renderPatchToWav(const PatchStruct& patch, const juce::File& path, const BounceConfig& cfg);

} // namespace agentic_synth::engine
