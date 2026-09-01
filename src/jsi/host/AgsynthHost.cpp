#include "jsi/host/AgsynthHost.h"

#include "jsi/audio/AudioStream.h"
#include "jsi/host/SpscRing.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

namespace agentic_synth::jsi {
namespace {

constexpr uint32_t kMaxPatchBytes = 2048;
constexpr uint32_t kParamPathCap = 64;
constexpr uint32_t kDefaultMaxBlock = 8192;
constexpr uint32_t kStreamFrames = 256;

struct PatchCommand {
    uint32_t len;
    uint8_t bytes[kMaxPatchBytes];
};

struct ParamCommand {
    char path[kParamPathCap];
    float value;
};

} // namespace

struct AgsynthHost::Impl {
    ags_engine* engine{nullptr};
    double sample_rate{0.0};
    int max_block{static_cast<int>(kDefaultMaxBlock)};
    AudioStream stream;
    LatestWins2<PatchCommand> patches;
    SpscRing<ParamCommand, 64> params;
    SpscRing<ags_event, 64> events;
};

AgsynthHost::AgsynthHost() : impl_(nullptr) {}

AgsynthHost::~AgsynthHost() {
    if (impl_ == nullptr)
        return;
    impl_->stream.stop();
    if (impl_->engine != nullptr) {
        ags_engine_destroy(impl_->engine);
        impl_->engine = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

AgsynthHost* AgsynthHost::create(double sample_rate, int max_block) {
    auto* host = new (std::nothrow) AgsynthHost();
    if (host == nullptr)
        return nullptr;
    host->impl_ = new (std::nothrow) Impl();
    if (host->impl_ == nullptr) {
        delete host;
        return nullptr;
    }
    host->impl_->engine = ags_engine_create(sample_rate, max_block);
    if (host->impl_->engine == nullptr) {
        delete host;
        return nullptr;
    }
    host->impl_->sample_rate = sample_rate;
    host->impl_->max_block = max_block;
    return host;
}

void AgsynthHost::destroy(AgsynthHost* host) { delete host; }

bool AgsynthHost::alive() const { return impl_ != nullptr && impl_->engine != nullptr; }

bool AgsynthHost::streamRunning() const { return impl_ != nullptr && impl_->stream.running(); }

double AgsynthHost::sampleRate() const { return impl_ != nullptr ? impl_->sample_rate : 0.0; }

int AgsynthHost::maxBlock() const { return impl_ != nullptr ? impl_->max_block : 0; }

int AgsynthHost::setPatch(const void* bytes, uint32_t len) {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (bytes == nullptr)
        return AGS_ERR_NULL;
    if (len != ags_patch_struct_size() || len > kMaxPatchBytes)
        return AGS_ERR_SIZE;
    PatchCommand cmd{};
    cmd.len = len;
    std::memcpy(cmd.bytes, bytes, len);
    impl_->patches.push(cmd); // latest-wins; never blocks
    return AGS_OK;
}

int AgsynthHost::setParam(const char* path, float value) {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (path == nullptr)
        return AGS_ERR_NULL;
    if (path[0] == '\0')
        return AGS_ERR_PARAM;
    if (!std::isfinite(value))
        return AGS_ERR_PARAM;
    const std::size_t n = std::strlen(path);
    if (n >= kParamPathCap)
        return AGS_ERR_SIZE;
    ParamCommand cmd{};
    std::memcpy(cmd.path, path, n);
    cmd.path[n] = '\0';
    cmd.value = value;
    if (!impl_->params.push(cmd))
        return AGS_JSI_ERR_QUEUE;
    return AGS_OK;
}

int AgsynthHost::pushEvents(const ags_event* events, uint32_t count) {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (count == 0)
        return AGS_OK;
    if (events == nullptr)
        return AGS_ERR_NULL;
    for (uint32_t i = 0; i < count; ++i) {
        if (!impl_->events.push(events[i]))
            return AGS_JSI_ERR_QUEUE;
    }
    return AGS_OK;
}

int AgsynthHost::processBlock(float* out_interleaved, uint32_t frames, uint32_t channels) {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (out_interleaved == nullptr)
        return AGS_ERR_NULL;

    int rc = AGS_OK;

    PatchCommand patch{};
    if (impl_->patches.drain_latest(patch)) {
        const int prc = ags_engine_set_patch(impl_->engine, patch.bytes, patch.len);
        if (prc != AGS_OK && rc == AGS_OK)
            rc = prc;
    }

    ParamCommand param{};
    while (impl_->params.pop(param)) {
        const int prc = ags_engine_set_param(impl_->engine, param.path, param.value);
        if (prc != AGS_OK && rc == AGS_OK)
            rc = prc;
    }

    ags_event ev{};
    ags_event batch[64];
    uint32_t n = 0;
    while (n < 64 && impl_->events.pop(ev))
        batch[n++] = ev;
    if (n > 0) {
        const int erc = ags_engine_push_events(impl_->engine, batch, n);
        if (erc != AGS_OK && rc == AGS_OK)
            rc = erc;
    }

    const int rrc = ags_engine_render(impl_->engine, out_interleaved, frames, channels);
    if (rrc != AGS_OK && rc == AGS_OK)
        rc = rrc;
    return rc;
}

int AgsynthHost::renderOffline(const void* patch_bytes, uint32_t patch_len, const ags_event* events,
                               uint32_t event_count, double sample_rate, uint32_t frames, float* out_interleaved) {
    return ags_render_offline(patch_bytes, patch_len, events, event_count, sample_rate, frames, out_interleaved);
}

int AgsynthHost::start() {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (impl_->stream.running())
        return AGS_OK;
    return impl_->stream.start(this, impl_->sample_rate, kStreamFrames);
}

int AgsynthHost::stop() {
    if (impl_ == nullptr)
        return AGS_ERR_NULL;
    return impl_->stream.stop();
}

int AgsynthHost::stateSize(uint32_t* len) const {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (impl_->stream.running())
        return AGS_ERR_STATE;
    return ags_state_size(impl_->engine, len);
}

int AgsynthHost::saveState(void* buf, uint32_t len) const {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (impl_->stream.running())
        return AGS_ERR_STATE;
    return ags_state_save(impl_->engine, buf, len);
}

int AgsynthHost::loadState(const void* buf, uint32_t len) {
    if (impl_ == nullptr || impl_->engine == nullptr)
        return AGS_ERR_NULL;
    if (impl_->stream.running())
        return AGS_ERR_STATE;
    return ags_state_load(impl_->engine, buf, len);
}

int AgsynthHost::recreate(double sample_rate, int max_block) {
    if (impl_ == nullptr)
        return AGS_ERR_NULL;
    impl_->stream.stop();

    uint8_t blob[kMaxPatchBytes];
    uint32_t n = 0;
    if (impl_->engine != nullptr) {
        const int src = ags_state_size(impl_->engine, &n);
        if (src != AGS_OK)
            return src;
        if (n > kMaxPatchBytes)
            return AGS_ERR_SIZE;
        const int sv = ags_state_save(impl_->engine, blob, n);
        if (sv != AGS_OK)
            return sv;
        ags_engine_destroy(impl_->engine);
        impl_->engine = nullptr;
    }

    impl_->engine = ags_engine_create(sample_rate, max_block);
    if (impl_->engine == nullptr)
        return AGS_ERR_PARAM;
    impl_->sample_rate = sample_rate;
    impl_->max_block = max_block;
    if (n > 0) {
        const int lc = ags_state_load(impl_->engine, blob, n);
        if (lc != AGS_OK)
            return lc;
    }
    return AGS_OK;
}

} // namespace agentic_synth::jsi
