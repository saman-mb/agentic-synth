#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

namespace agentic_synth::agent {

// ---------------------------------------------------------------------------
// WebSocketBridge — minimal RFC 6455 WebSocket server using JUCE sockets.
//
// Runs a background accept thread; spawns per-client std::threads.
// Text frames → textCallback_.  Binary frames (raw f32 PCM at 16 kHz from
// the browser PTT component) → binaryCallback_.
//
// Ownership: each client socket is held in a shared_ptr shared between the
// clients_ registry and the per-client thread.  stop() closes every socket
// (unblocking reads) then joins every client thread before returning, so
// no member is accessed after destruction.
// ---------------------------------------------------------------------------

class WebSocketBridge : private juce::Thread {
public:
    using TextCallback = std::function<void(std::string json, int clientId)>;
    using BinaryCallback = std::function<void(std::vector<uint8_t> data, int clientId)>;

    // Incoming frames larger than this are rejected to prevent remote OOM.
    static constexpr uint64_t kMaxPayloadBytes = 1u << 20; // 1 MiB

    WebSocketBridge();
    ~WebSocketBridge() override;

    // Non-copyable, non-movable.
    WebSocketBridge(const WebSocketBridge&) = delete;
    WebSocketBridge& operator=(const WebSocketBridge&) = delete;

    void start(int port = 9002);
    void stop();

    // Broadcast a JSON text frame to all connected clients.
    void broadcast(const std::string& json);

    // Send a text frame to one specific client.
    void sendToClient(int clientId, const std::string& json);

    void setTextCallback(TextCallback cb) { textCb_ = std::move(cb); }
    void setBinaryCallback(BinaryCallback cb) { binaryCb_ = std::move(cb); }

private:
    // juce::Thread entry point.
    void run() override;

    // Per-client handler — runs in a tracked std::thread.
    void handleClient(std::shared_ptr<juce::StreamingSocket> sock, int id);

    // WebSocket handshake (HTTP upgrade).
    static bool performHandshake(juce::StreamingSocket* sock);

    // Read one WebSocket frame.  Returns false on disconnect / error / oversize.
    static bool readFrame(juce::StreamingSocket* sock, uint8_t& opcode, std::vector<uint8_t>& payload);

    // Write a server-side (unmasked) text frame.
    static bool writeTextFrame(juce::StreamingSocket* sock, const std::string& text);

    // Write a raw frame with given opcode.
    static bool writeFrame(juce::StreamingSocket* sock, uint8_t opcode, const void* data, size_t len);

    // WebSocket handshake helpers.
    static std::string computeAcceptKey(const std::string& clientKey);
    static std::string base64Encode(const uint8_t* data, size_t len);
    static std::array<uint8_t, 20> sha1(const std::string& msg);

    int port_{9002};
    std::atomic<int> nextClientId_{0};

    TextCallback textCb_;
    BinaryCallback binaryCb_;

    struct ClientEntry {
        int id;
        std::shared_ptr<juce::StreamingSocket> sock;
    };

    juce::CriticalSection clientsMutex_;
    std::vector<ClientEntry> clients_;

    // Per-client threads — joined in stop() to prevent dangling-this.
    std::mutex clientThreadsMutex_;
    std::vector<std::thread> clientThreads_;

    juce::StreamingSocket serverSock_;
};

} // namespace agentic_synth::agent
