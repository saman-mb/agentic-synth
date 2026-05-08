#pragma once

#include <string>
#include <functional>

namespace agentic_synth::agent {

// Whisper.cpp speech-to-text client.
// Encapsulates local Whisper inference.
// Falls back cleanly if whisper.cpp is not available.

class WhisperClient {
public:
    WhisperClient();
    ~WhisperClient();

    // Initialize the whisper model
    // @param modelPath path to Whisper GGML model file
    // @return true if initialized successfully
    bool init(const std::string& modelPath);

    // Transcribe audio buffer to text
    // @param samples 16-bit PCM mono audio samples
    // @param numSamples number of samples
    // @param sampleRate sample rate in Hz
    // @return transcribed text
    std::string transcribe(const int16_t* samples, int numSamples, int sampleRate);

    // Transcribe from WAV file
    std::string transcribeFile(const std::string& wavPath);

    // Real-time streaming callback
    using TranscriptionCallback = std::function<void(const std::string& partial)>;
    void startStreaming(TranscriptionCallback onPartial);
    void stopStreaming();
    void feedAudio(const int16_t* samples, int numSamples);

    // Status
    bool isInitialized() const { return initialized_; }
    bool isStreaming() const { return streaming_; }

private:
    bool initialized_ = false;
    bool streaming_ = false;
    void* whisper_ = nullptr;  // Opaque whisper context

    // Buffer for streaming
    static constexpr int kStreamBufferSize = 16000;  // 1 second at 16kHz
    int16_t streamBuffer_[16000]{};
    int streamPos_ = 0;
};

} // namespace agentic_synth::agent
