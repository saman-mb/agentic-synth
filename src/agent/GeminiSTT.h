#pragma once

#include <cstdint>
#include <string>

namespace agentic_synth::agent {

// Phase 29 — Gemini-backed speech-to-text for push-to-talk.
//
// Replaces the stub WhisperClient. PTT audio arrives as Int16 PCM at 16 kHz
// mono via WebUiComponent::push_audio_pcm. We wrap it in a WAV header,
// base64-encode, and POST to Gemini's generateContent endpoint with
// `inline_data.mime_type = audio/wav` plus a one-line system instruction
// asking for the transcript only. The single round-trip is fine for the
// 1-15s utterances PTT produces (no streaming needed).
//
// Construction is cheap. transcribe() blocks on the network call so it must
// be invoked from a worker thread, never the audio thread or message thread.

struct GeminiSTTConfig {
    std::string api_key;
    std::string model = "gemini-2.5-flash-lite";
    int timeout_ms = 15000;
};

class GeminiSTT {
public:
    GeminiSTT() = default;
    explicit GeminiSTT(GeminiSTTConfig cfg) : cfg_(std::move(cfg)) {}

    void setApiKey(std::string key) { cfg_.api_key = std::move(key); }

    bool enabled() const noexcept { return !cfg_.api_key.empty(); }

    // Synchronous Gemini round-trip. Returns the transcript text, or empty
    // string on failure (no key, network error, empty model response). Logs
    // the failure reason to stderr in the same style as PromptEnhancer.
    std::string transcribe(const std::int16_t* samples, int numSamples,
                           int sampleRate = 16000) const;

private:
    std::string http_post(const std::string& url, const std::string& json_body) const;

    GeminiSTTConfig cfg_;
};

} // namespace agentic_synth::agent
