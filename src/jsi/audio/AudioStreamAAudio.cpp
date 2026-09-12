#include "jsi/audio/AudioStream.h"

#include "agsynth.h"
#include "jsi/host/AgsynthHost.h"

#if defined(__ANDROID__)

#include <aaudio/AAudio.h>

#include <new>

namespace agentic_synth::jsi {

namespace {

aaudio_data_callback_result_t onAudio(AAudioStream* /*stream*/, void* user, void* audio_data, int32_t num_frames) {
    auto* host = static_cast<AgsynthHost*>(user);
    if (host != nullptr && audio_data != nullptr && num_frames > 0)
        host->processBlock(static_cast<float*>(audio_data), static_cast<uint32_t>(num_frames), 2);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace

struct AudioStream::Impl {
    AAudioStream* stream{nullptr};
};

AudioStream::~AudioStream() { stop(); }

bool AudioStream::running() const { return impl_ != nullptr && impl_->stream != nullptr; }

int AudioStream::start(AgsynthHost* host, double sample_rate, uint32_t frames) {
    if (host == nullptr)
        return AGS_ERR_NULL;
    if (impl_ != nullptr)
        return AGS_OK;
    if (frames == 0 || !(sample_rate > 0.0))
        return AGS_ERR_PARAM;

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr)
        return AGS_ERR_STATE;

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sample_rate));
    AAudioStreamBuilder_setFramesPerDataCallback(builder, static_cast<int32_t>(frames));
    AAudioStreamBuilder_setDataCallback(builder, onAudio, host);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    AAudioStream* stream = nullptr;
    const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK || stream == nullptr)
        return AGS_ERR_STATE;

    auto* impl = new (std::nothrow) Impl();
    if (impl == nullptr) {
        AAudioStream_close(stream);
        return AGS_ERR_STATE;
    }
    impl->stream = stream;
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        delete impl;
        return AGS_ERR_STATE;
    }
    impl_ = impl;
    return AGS_OK;
}

int AudioStream::stop() {
    if (impl_ == nullptr)
        return AGS_OK;
    if (impl_->stream != nullptr) {
        AAudioStream_requestStop(impl_->stream);
        AAudioStream_close(impl_->stream);
        impl_->stream = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
    return AGS_OK;
}

} // namespace agentic_synth::jsi

#endif // __ANDROID__
