#pragma once

#include "ipc/IpcMessage.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

// Callback fired on the reader thread when the companion sends a patch JSON.
using PatchJsonCallback = std::function<void(const std::string& patchJson)>;

// One per plugin instance. Connects to the companion via a Unix domain socket.
// On first failed connect, schedules tryLaunchCompanion() on the JUCE message
// thread so fork/exec never happens inside a plugin or audio callback.
//
// Thread safety: connectToCompanion() and requestPatch() may be called from
// any non-audio thread. Callbacks are delivered on the reader thread.
class PluginIpcClient final : private juce::Thread {
public:
    PluginIpcClient(uint32_t instanceId, PatchJsonCallback onPatch);
    ~PluginIpcClient() override;

    // Connect to the companion socket; schedules a launch+retry if unavailable.
    // Returns true immediately if the socket is open after the attempt.
    bool connectToCompanion(int timeoutMs = kConnectTimeoutMs);

    // Send an NL prompt to the companion's AgentBridge.
    void requestPatch(const std::string& prompt);

    [[nodiscard]] bool isActive() const noexcept;

private:
    // Reader thread entry point.
    void run() override;

    void sendMsg(IpcMsgType type, const std::string& payload = {});

    // Process-wide instance counter.
    static std::atomic<int>& instanceCount() noexcept;

    // Guards against spawning multiple companion processes in a race.
    static std::atomic<bool>& launchScheduled() noexcept;

    // Locate and start the companion binary (call only on the message thread).
    static void tryLaunchCompanion();

    uint32_t instanceId_;
    PatchJsonCallback onPatch_;
    int fd_{-1};
    std::atomic<bool> connected_{false};
    std::mutex writeMutex_;
};

} // namespace agentic_synth::ipc
