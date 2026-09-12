#pragma once

#include <cstdint>

namespace agentic_synth::jsi {

class AgsynthHost;

// OS audio callback that calls AgsynthHost::processBlock on the RT thread.
// Linux: std::thread stub (CI / tests). Android: AAudio. iOS: RemoteIO.
// JSI stays control-rate only — this type never calls into JS.
class AudioStream {
public:
    AudioStream() = default;
    ~AudioStream();

    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    int start(AgsynthHost* host, double sample_rate, uint32_t frames);
    int stop();
    [[nodiscard]] bool running() const;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace agentic_synth::jsi
