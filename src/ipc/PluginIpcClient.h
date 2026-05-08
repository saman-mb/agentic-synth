#pragma once

#include "ipc/IpcMessage.h"

#include <atomic>
#include <functional>
#include <string>

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

// Called on the JUCE message thread when the companion sends a patch JSON.
using PatchJsonCallback = std::function<void(const std::string& patchJson)>;

// One per plugin instance. Connects to the companion app via a named pipe.
// The first instance in the DAW process also tries to launch the companion
// if the pipe server is not yet available.
//
// Thread safety: connectToCompanion() and requestPatch() may be called from
// any non-audio thread. Callbacks are delivered on the JUCE message thread.
class PluginIpcClient final : private juce::InterprocessConnection {
public:
    PluginIpcClient(uint32_t instanceId, PatchJsonCallback onPatch);
    ~PluginIpcClient() override;

    // Try to connect; launches companion when unavailable (async fire-and-forget).
    // Returns true immediately if the pipe is open after timeoutMs.
    bool connectToCompanion(int timeoutMs = kConnectTimeoutMs);

    // Send an NL prompt to the companion's AgentBridge.
    void requestPatch(const std::string& prompt);

    [[nodiscard]] bool isActive() const noexcept;

private:
    // juce::InterprocessConnection overrides
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived(const juce::MemoryBlock& data) override;

    void sendMsg(IpcMsgType type, const std::string& payload = {});

    // Returns a process-wide reference count (shared across all instances).
    static std::atomic<int>& instanceCount() noexcept;

    // Try to find and launch the companion app binary.
    static void tryLaunchCompanion();

    uint32_t instanceId_;
    PatchJsonCallback onPatch_;
};

} // namespace agentic_synth::ipc
