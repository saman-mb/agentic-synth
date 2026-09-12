#include "jsi/audio/AudioStream.h"

#include "agsynth.h"
#include "jsi/host/AgsynthHost.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <thread>

namespace agentic_synth::jsi {

struct AudioStream::Impl {
    AgsynthHost* host{nullptr};
    std::atomic<bool> stop{false};
    std::thread thread;
    uint32_t frames{256};
    uint32_t channels{2};
    std::unique_ptr<float[]> buf;
};

AudioStream::~AudioStream() { stop(); }

bool AudioStream::running() const { return impl_ != nullptr; }

int AudioStream::start(AgsynthHost* host, double sample_rate, uint32_t frames) {
    if (host == nullptr)
        return AGS_ERR_NULL;
    if (impl_ != nullptr)
        return AGS_OK;
    if (frames == 0 || !(sample_rate > 0.0))
        return AGS_ERR_PARAM;

    auto* impl = new (std::nothrow) Impl();
    if (impl == nullptr)
        return AGS_ERR_STATE;
    impl->host = host;
    impl->frames = frames;
    impl->buf.reset(new (std::nothrow) float[static_cast<std::size_t>(frames) * impl->channels]);
    if (!impl->buf) {
        delete impl;
        return AGS_ERR_STATE;
    }

    const auto period = std::chrono::microseconds(static_cast<int>(1.0e6 * static_cast<double>(frames) / sample_rate));

    try {
        impl->thread = std::thread([impl, period]() {
            while (!impl->stop.load(std::memory_order_acquire)) {
                impl->host->processBlock(impl->buf.get(), impl->frames, impl->channels);
                std::this_thread::sleep_for(period);
            }
        });
    } catch (...) {
        delete impl;
        return AGS_ERR_STATE;
    }

    impl_ = impl;
    return AGS_OK;
}

int AudioStream::stop() {
    if (impl_ == nullptr)
        return AGS_OK;
    impl_->stop.store(true, std::memory_order_release);
    if (impl_->thread.joinable())
        impl_->thread.join();
    delete impl_;
    impl_ = nullptr;
    return AGS_OK;
}

} // namespace agentic_synth::jsi
