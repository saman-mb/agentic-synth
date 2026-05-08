#include "WhisperClient.h"

namespace agentic_synth::agent {

WhisperClient::WhisperClient() = default;
WhisperClient::~WhisperClient() = default;

bool WhisperClient::init(const std::string& modelPath) {
    // TODO: implement with whisper.cpp API
    // whisper_context* ctx = whisper_init_from_file(modelPath.c_str());
    // whisper_ = ctx;
    // initialized_ = (ctx != nullptr);
    (void)modelPath;
    return false;  // whisper not bundled yet
}

std::string WhisperClient::transcribe(const int16_t* samples, int numSamples, int sampleRate) {
    // TODO: whisper_full() call
    (void)samples;
    (void)numSamples;
    (void)sampleRate;
    return "";
}

std::string WhisperClient::transcribeFile(const std::string& wavPath) {
    (void)wavPath;
    return "";
}

void WhisperClient::startStreaming(TranscriptionCallback onPartial) {
    (void)onPartial;
    streaming_ = true;
    streamPos_ = 0;
}

void WhisperClient::stopStreaming() {
    streaming_ = false;
    streamPos_ = 0;
}

void WhisperClient::feedAudio(const int16_t* samples, int numSamples) {
    if (!streaming_) return;

    for (int i = 0; i < numSamples && streamPos_ < kStreamBufferSize; ++i) {
        streamBuffer_[streamPos_++] = samples[i];
    }
}

} // namespace agentic_synth::agent
