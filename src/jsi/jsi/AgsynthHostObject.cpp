#include "jsi/jsi/AgsynthHostObject.h"

#ifdef AGENTIC_SYNTH_HAS_JSI

#include "jsi/host/AgsynthHost.h"

#include <cstdint>
#include <cstring>
#include <memory>

using facebook::jsi::ArrayBuffer;
using facebook::jsi::Function;
using facebook::jsi::HostObject;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

namespace agentic_synth::jsi {
namespace {

int as_int(const Value& v, int fallback) { return v.isNumber() ? static_cast<int>(v.asNumber()) : fallback; }

const uint8_t* ab_data(Runtime& rt, const Value& v, size_t& len) {
    if (!v.isObject())
        return nullptr;
    Object obj = v.asObject(rt);
    if (!obj.isArrayBuffer(rt))
        return nullptr;
    ArrayBuffer ab = obj.getArrayBuffer(rt);
    len = ab.size(rt);
    return ab.data(rt);
}

uint8_t* ab_data_mut(Runtime& rt, const Value& v, size_t& len) {
    if (!v.isObject())
        return nullptr;
    Object obj = v.asObject(rt);
    if (!obj.isArrayBuffer(rt))
        return nullptr;
    ArrayBuffer ab = obj.getArrayBuffer(rt);
    len = ab.size(rt);
    return ab.data(rt);
}

bool pcm_buffer_short(size_t byte_len, uint32_t frames, uint32_t channels) {
    const size_t need = static_cast<size_t>(frames) * static_cast<size_t>(channels) * sizeof(float);
    return byte_len < need;
}

class AgsynthHostObject final : public HostObject {
public:
    ~AgsynthHostObject() override { AgsynthHost::destroy(host_); }

    Value get(Runtime& rt, const PropNameID& name) override {
        const std::string key = name.utf8(rt);

        if (key == "create") {
            return Function::createFromHostFunction(
                rt, name, 2, [this](Runtime& /*rt*/, const Value&, const Value* args, size_t count) {
                    if (count < 1 || !args[0].isNumber())
                        return Value(AGS_ERR_PARAM);
                    const double sr = args[0].asNumber();
                    const int mb = count > 1 ? as_int(args[1], 8192) : 8192;
                    AgsynthHost::destroy(host_);
                    host_ = AgsynthHost::create(sr, mb);
                    return Value(host_ != nullptr ? AGS_OK : AGS_ERR_PARAM);
                });
        }
        if (key == "destroy") {
            return Function::createFromHostFunction(rt, name, 0, [this](Runtime&, const Value&, const Value*, size_t) {
                AgsynthHost::destroy(host_);
                host_ = nullptr;
                return Value(AGS_OK);
            });
        }
        if (key == "setPatch") {
            return Function::createFromHostFunction(rt, name, 1,
                                                    [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                                                        if (host_ == nullptr)
                                                            return Value(AGS_ERR_NULL);
                                                        if (count < 1)
                                                            return Value(AGS_ERR_PARAM);
                                                        size_t len = 0;
                                                        const uint8_t* data = ab_data(rt, args[0], len);
                                                        if (data == nullptr)
                                                            return Value(AGS_ERR_NULL);
                                                        return Value(host_->setPatch(data, static_cast<uint32_t>(len)));
                                                    });
        }
        if (key == "setParam") {
            return Function::createFromHostFunction(
                rt, name, 2, [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                    if (host_ == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (count < 2 || !args[0].isString() || !args[1].isNumber())
                        return Value(AGS_ERR_PARAM);
                    const std::string path = args[0].asString(rt).utf8(rt);
                    return Value(host_->setParam(path.c_str(), static_cast<float>(args[1].asNumber())));
                });
        }
        if (key == "pushEvents") {
            return Function::createFromHostFunction(rt, name, 1,
                                                    [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                                                        if (host_ == nullptr)
                                                            return Value(AGS_ERR_NULL);
                                                        if (count < 1)
                                                            return Value(AGS_ERR_PARAM);
                                                        size_t len = 0;
                                                        const uint8_t* data = ab_data(rt, args[0], len);
                                                        if (data == nullptr)
                                                            return Value(AGS_ERR_NULL);
                                                        if (len % sizeof(ags_event) != 0)
                                                            return Value(AGS_ERR_SIZE);
                                                        const auto n = static_cast<uint32_t>(len / sizeof(ags_event));
                                                        const auto* ev = reinterpret_cast<const ags_event*>(data);
                                                        return Value(host_->pushEvents(ev, n));
                                                    });
        }
        if (key == "processBlock") {
            return Function::createFromHostFunction(
                rt, name, 3, [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                    if (host_ == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (host_->streamRunning())
                        return Value(AGS_ERR_STATE);
                    if (count < 3 || !args[1].isNumber() || !args[2].isNumber())
                        return Value(AGS_ERR_PARAM);
                    size_t len = 0;
                    uint8_t* data = ab_data_mut(rt, args[0], len);
                    if (data == nullptr)
                        return Value(AGS_ERR_NULL);
                    const auto frames = static_cast<uint32_t>(args[1].asNumber());
                    const auto channels = static_cast<uint32_t>(args[2].asNumber());
                    if (pcm_buffer_short(len, frames, channels))
                        return Value(AGS_ERR_SIZE);
                    return Value(host_->processBlock(reinterpret_cast<float*>(data), frames, channels));
                });
        }
        if (key == "renderOffline") {
            return Function::createFromHostFunction(
                rt, name, 5, [](Runtime& rt, const Value&, const Value* args, size_t count) {
                    if (count < 5 || !args[2].isNumber() || !args[3].isNumber())
                        return Value(AGS_ERR_PARAM);
                    size_t plen = 0;
                    size_t elen = 0;
                    size_t olen = 0;
                    const uint8_t* patch = ab_data(rt, args[0], plen);
                    const uint8_t* events = ab_data(rt, args[1], elen);
                    uint8_t* out = ab_data_mut(rt, args[4], olen);
                    if (patch == nullptr || out == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (elen % sizeof(ags_event) != 0)
                        return Value(AGS_ERR_SIZE);
                    const auto n = static_cast<uint32_t>(elen / sizeof(ags_event));
                    const auto frames = static_cast<uint32_t>(args[3].asNumber());
                    if (pcm_buffer_short(olen, frames, 2))
                        return Value(AGS_ERR_SIZE);
                    const auto* ev = (n == 0) ? nullptr : reinterpret_cast<const ags_event*>(events);
                    return Value(AgsynthHost::renderOffline(patch, static_cast<uint32_t>(plen), ev, n,
                                                            args[2].asNumber(), frames, reinterpret_cast<float*>(out)));
                });
        }
        if (key == "start") {
            return Function::createFromHostFunction(rt, name, 0, [this](Runtime&, const Value&, const Value*, size_t) {
                return Value(host_ != nullptr ? host_->start() : AGS_ERR_NULL);
            });
        }
        if (key == "stop") {
            return Function::createFromHostFunction(rt, name, 0, [this](Runtime&, const Value&, const Value*, size_t) {
                return Value(host_ != nullptr ? host_->stop() : AGS_ERR_NULL);
            });
        }
        if (key == "stateSize") {
            return Function::createFromHostFunction(rt, name, 0, [this](Runtime&, const Value&, const Value*, size_t) {
                if (host_ == nullptr)
                    return Value(0);
                uint32_t n = 0;
                if (host_->stateSize(&n) != AGS_OK)
                    return Value(0);
                return Value(static_cast<int>(n));
            });
        }
        if (key == "saveState") {
            return Function::createFromHostFunction(
                rt, name, 1, [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                    if (host_ == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (count < 1)
                        return Value(AGS_ERR_PARAM);
                    size_t len = 0;
                    uint8_t* data = ab_data_mut(rt, args[0], len);
                    if (data == nullptr)
                        return Value(AGS_ERR_NULL);
                    return Value(host_->saveState(data, static_cast<uint32_t>(len)));
                });
        }
        if (key == "loadState") {
            return Function::createFromHostFunction(
                rt, name, 1, [this](Runtime& rt, const Value&, const Value* args, size_t count) {
                    if (host_ == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (count < 1)
                        return Value(AGS_ERR_PARAM);
                    size_t len = 0;
                    const uint8_t* data = ab_data(rt, args[0], len);
                    if (data == nullptr)
                        return Value(AGS_ERR_NULL);
                    return Value(host_->loadState(data, static_cast<uint32_t>(len)));
                });
        }
        if (key == "recreate") {
            return Function::createFromHostFunction(
                rt, name, 2, [this](Runtime& /*rt*/, const Value&, const Value* args, size_t count) {
                    if (host_ == nullptr)
                        return Value(AGS_ERR_NULL);
                    if (count < 1 || !args[0].isNumber())
                        return Value(AGS_ERR_PARAM);
                    const int mb = count > 1 ? as_int(args[1], 8192) : 8192;
                    return Value(host_->recreate(args[0].asNumber(), mb));
                });
        }
        return Value::undefined();
    }

private:
    AgsynthHost* host_{nullptr};
};

} // namespace

void installAgsynthHost(Runtime& runtime) {
    auto obj = std::make_shared<AgsynthHostObject>();
    runtime.global().setProperty(runtime, "__AgsynthHost", Object::createFromHostObject(runtime, std::move(obj)));
}

} // namespace agentic_synth::jsi

#endif // AGENTIC_SYNTH_HAS_JSI
