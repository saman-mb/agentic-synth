#pragma once

#include "ipc/IpcMessage.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

using PatchRequestCallback = std::function<void(uint32_t instanceId, const std::string& prompt)>;

// ── CompanionConnection ───────────────────────────────────────────────────────
// Represents one connected plugin instance from the companion's perspective.

class CompanionIpcServer; // forward

class CompanionConnection final : public juce::InterprocessConnection {
public:
    CompanionConnection(uint32_t connId, PatchRequestCallback onRequest, CompanionIpcServer& owner);

    // Send a PatchUpdate message to this plugin instance.
    void sendPatchUpdate(const std::string& patchJson);

    // Send a Shutdown notice to this plugin instance.
    void sendShutdown();

    [[nodiscard]] uint32_t connId() const noexcept { return connId_; }

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived(const juce::MemoryBlock&) override;

    uint32_t connId_;
    PatchRequestCallback onRequest_;
    CompanionIpcServer& owner_;
};

// ── CompanionIpcServer ────────────────────────────────────────────────────────
// Runs inside the companion app process.
// Listens on localhost:kCompanionPort and accepts any number of plugin instances.
// Shuts down when requestShutdown() is called or the last client disconnects.

class CompanionIpcServer final : public juce::InterprocessConnectionServer {
public:
    // onRequest : called on the JUCE message thread for each PatchRequest.
    // onEmpty   : called when the last plugin instance disconnects.
    explicit CompanionIpcServer(PatchRequestCallback onRequest, std::function<void()> onEmpty = {});
    ~CompanionIpcServer() override;

    // Bind and begin accepting on kCompanionPort. Returns false on failure.
    bool start();

    // Broadcast a patch JSON payload to every connected plugin instance.
    void broadcastPatchUpdate(const std::string& patchJson);

    // Target one specific plugin instance by instanceId (received via Hello).
    void sendPatchUpdate(uint32_t instanceId, const std::string& patchJson);

    // Graceful shutdown: broadcast Shutdown to all clients, then stop.
    void shutdown();

    [[nodiscard]] int connectionCount() const noexcept;

    // Called by CompanionConnection when a client disconnects.
    void onClientDisconnected(uint32_t connId);

private:
    juce::InterprocessConnection* createConnectionObject() override;

    PatchRequestCallback onRequest_;
    std::function<void()> onEmpty_;
    std::atomic<uint32_t> nextConnId_{1};
    mutable std::mutex connsMutex_;
    std::vector<std::shared_ptr<CompanionConnection>> conns_;
};

} // namespace agentic_synth::ipc
