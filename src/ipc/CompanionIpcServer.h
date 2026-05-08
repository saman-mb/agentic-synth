#pragma once

#include "ipc/IpcMessage.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

using PatchRequestCallback = std::function<void(uint32_t instanceId, const std::string& prompt)>;

class CompanionIpcServer;

// ── CompanionConnection ───────────────────────────────────────────────────────
// One accepted Unix domain socket connection from the companion's perspective.
// Owns its own reader thread; sends are protected by a mutex for thread safety.

class CompanionConnection final : private juce::Thread {
public:
    CompanionConnection(int fd, uint32_t connId, PatchRequestCallback onRequest, CompanionIpcServer& owner);
    ~CompanionConnection() override;

    void sendPatchUpdate(const std::string& patchJson);
    void sendShutdown();

    [[nodiscard]] uint32_t connId() const noexcept { return connId_; }
    [[nodiscard]] bool isAlive() const noexcept { return connected_.load(std::memory_order_acquire); }

private:
    void run() override;
    bool sendRaw(const void* buf, std::size_t len) noexcept;
    bool recvAll(void* buf, std::size_t len) noexcept;

    int fd_;
    uint32_t connId_;
    PatchRequestCallback onRequest_;
    CompanionIpcServer& owner_;
    std::atomic<bool> connected_{true};
    std::mutex writeMutex_;
};

// ── CompanionIpcServer ────────────────────────────────────────────────────────
// Companion-side Unix domain socket server.
// Binds to ipcSocketPath() and accepts any number of plugin instances.
// Shuts down when shutdown() is called or the last client disconnects.

class CompanionIpcServer final : private juce::Thread {
public:
    explicit CompanionIpcServer(PatchRequestCallback onRequest, std::function<void()> onEmpty = {});
    ~CompanionIpcServer() override;

    // Create the socket, bind, and listen. Returns false on failure.
    bool start();

    // Broadcast a patch JSON payload to every connected plugin instance.
    void broadcastPatchUpdate(const std::string& patchJson);

    // Target one specific plugin instance by instanceId.
    void sendPatchUpdate(uint32_t instanceId, const std::string& patchJson);

    // Graceful shutdown: send Shutdown to all clients, then stop.
    void shutdown();

    [[nodiscard]] int connectionCount() const noexcept;

    // Called by CompanionConnection when its socket closes.
    void onClientDisconnected(uint32_t connId);

private:
    void run() override; // accept loop

    PatchRequestCallback onRequest_;
    std::function<void()> onEmpty_;
    std::atomic<uint32_t> nextConnId_{1};
    mutable std::mutex connsMutex_;
    std::vector<std::shared_ptr<CompanionConnection>> conns_;
    int serverFd_{-1};
};

} // namespace agentic_synth::ipc
