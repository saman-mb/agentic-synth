#pragma once

#include "agsynth.h"

#include <cstdint>

namespace agentic_synth::jsi {

// Bridge-local overflow. TS maps AGS_ERR_* plus this QUEUE code.
// Do not add this enumerator to agsynth.h (C ABI is frozen).
enum { AGS_JSI_ERR_QUEUE = 5 };

// JUCE-free host around ags_engine_* only (never VoiceManager).
//
// Threading: the JS/control thread enqueues; the RT thread drains then
// calls the C API. Never call the C API from both threads. processBlock
// is the only RT entry (no malloc / lock / syscall).
//
// create(sr, max_block=8192) allocates the engine and three SPSC rings.
// Patch mailbox cap 2, latest-wins. Param cap 64 {char path[64]; float}.
// Event cap 64. Param/event overflow returns AGS_JSI_ERR_QUEUE — no block,
// no malloc.
//
// renderOffline is TEST ONLY and matches ags_render_offline (fresh engine,
// absolute event offsets, not the AudioStream).
class AgsynthHost {
public:
    static AgsynthHost* create(double sample_rate, int max_block = 8192);
    static void destroy(AgsynthHost* host);

    AgsynthHost(const AgsynthHost&) = delete;
    AgsynthHost& operator=(const AgsynthHost&) = delete;

    int setPatch(const void* bytes, uint32_t len);
    int setParam(const char* path, float value);
    int pushEvents(const ags_event* events, uint32_t count);

    // RT: drain rings, then ags_engine_render into the caller buffer.
    int processBlock(float* out_interleaved, uint32_t frames, uint32_t channels);

    static int renderOffline(const void* patch_bytes, uint32_t patch_len, const ags_event* events, uint32_t event_count,
                             double sample_rate, uint32_t frames, float* out_interleaved);

    // Lifecycle (control thread). stop joins the optional AudioStream stub
    // before any C API call. recreate is stop → save → destroy → create → load.
    int start();
    int stop();
    int stateSize(uint32_t* len) const;
    int saveState(void* buf, uint32_t len) const;
    int loadState(const void* buf, uint32_t len);
    int recreate(double sample_rate, int max_block = 8192);

    [[nodiscard]] bool alive() const;
    [[nodiscard]] double sampleRate() const;
    [[nodiscard]] int maxBlock() const;

private:
    AgsynthHost();
    ~AgsynthHost();

    struct Impl;
    Impl* impl_;
};

} // namespace agentic_synth::jsi
