#include "jsi/audio/AudioStream.h"

#include "agsynth.h"
#include "jsi/host/AgsynthHost.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

#include <new>

namespace agentic_synth::jsi {

namespace {

OSStatus onRender(void* in_ref, AudioUnitRenderActionFlags* /*flags*/, const AudioTimeStamp* /*stamp*/, UInt32 /*bus*/,
                  UInt32 in_frames, AudioBufferList* io_data) {
    auto* host = static_cast<AgsynthHost*>(in_ref);
    if (host == nullptr || io_data == nullptr || io_data->mNumberBuffers == 0)
        return noErr;
    AudioBuffer& buf = io_data->mBuffers[0];
    if (buf.mData == nullptr || in_frames == 0)
        return noErr;
    const uint32_t channels = buf.mNumberChannels != 0 ? buf.mNumberChannels : 2;
    host->processBlock(static_cast<float*>(buf.mData), in_frames, channels);
    return noErr;
}

} // namespace

struct AudioStream::Impl {
    AudioUnit unit{nullptr};
};

AudioStream::~AudioStream() { stop(); }

bool AudioStream::running() const { return impl_ != nullptr && impl_->unit != nullptr; }

int AudioStream::start(AgsynthHost* host, double sample_rate, uint32_t frames) {
    if (host == nullptr)
        return AGS_ERR_NULL;
    if (impl_ != nullptr)
        return AGS_OK;
    if (frames == 0 || !(sample_rate > 0.0))
        return AGS_ERR_PARAM;

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr)
        return AGS_ERR_STATE;

    AudioUnit unit = nullptr;
    if (AudioComponentInstanceNew(comp, &unit) != noErr || unit == nullptr)
        return AGS_ERR_STATE;

    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = sample_rate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 32;
    asbd.mBytesPerFrame = 8;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerPacket = 8;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd)) !=
        noErr) {
        AudioComponentInstanceDispose(unit);
        return AGS_ERR_STATE;
    }

    AURenderCallbackStruct cb{};
    cb.inputProc = onRender;
    cb.inputProcRefCon = host;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb)) !=
        noErr) {
        AudioComponentInstanceDispose(unit);
        return AGS_ERR_STATE;
    }

    (void)frames;
    if (AudioUnitInitialize(unit) != noErr || AudioOutputUnitStart(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return AGS_ERR_STATE;
    }

    auto* impl = new (std::nothrow) Impl();
    if (impl == nullptr) {
        AudioOutputUnitStop(unit);
        AudioComponentInstanceDispose(unit);
        return AGS_ERR_STATE;
    }
    impl->unit = unit;
    impl_ = impl;
    return AGS_OK;
}

int AudioStream::stop() {
    if (impl_ == nullptr)
        return AGS_OK;
    if (impl_->unit != nullptr) {
        AudioOutputUnitStop(impl_->unit);
        AudioUnitUninitialize(impl_->unit);
        AudioComponentInstanceDispose(impl_->unit);
        impl_->unit = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
    return AGS_OK;
}

} // namespace agentic_synth::jsi

#endif // TARGET_OS_IPHONE
#endif // __APPLE__
