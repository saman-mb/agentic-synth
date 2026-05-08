#include "ipc/CompanionIpcServer.h"

#include <algorithm>

namespace agentic_synth::ipc {

// ── CompanionConnection ───────────────────────────────────────────────────────

CompanionConnection::CompanionConnection(uint32_t connId, PatchRequestCallback onRequest, CompanionIpcServer& owner)
    : connId_(connId), onRequest_(std::move(onRequest)), owner_(owner) {}

void CompanionConnection::sendPatchUpdate(const std::string& patchJson) {
    if (isConnected())
        sendMessage(makeMessage(IpcMsgType::PatchUpdate, 0, patchJson));
}

void CompanionConnection::sendShutdown() {
    if (isConnected())
        sendMessage(makeMessage(IpcMsgType::Shutdown, 0));
}

void CompanionConnection::connectionMade() {}

void CompanionConnection::connectionLost() { owner_.onClientDisconnected(connId_); }

void CompanionConnection::messageReceived(const juce::MemoryBlock& data) {
    IpcMsgHeader hdr;
    std::string payload;
    if (!parseMessage(data, hdr, payload))
        return;

    switch (hdr.type) {
    case IpcMsgType::PatchRequest:
        if (onRequest_)
            onRequest_(hdr.instanceId, payload);
        break;
    case IpcMsgType::Bye:
        disconnect();
        break;
    default:
        break;
    }
}

// ── CompanionIpcServer ────────────────────────────────────────────────────────

CompanionIpcServer::CompanionIpcServer(PatchRequestCallback onRequest, std::function<void()> onEmpty)
    : onRequest_(std::move(onRequest)), onEmpty_(std::move(onEmpty)) {}

CompanionIpcServer::~CompanionIpcServer() { shutdown(); }

bool CompanionIpcServer::start() { return beginWaitingForSocket(kCompanionPort, "127.0.0.1"); }

void CompanionIpcServer::broadcastPatchUpdate(const std::string& patchJson) {
    std::lock_guard lock(connsMutex_);
    for (auto* c : conns_)
        c->sendPatchUpdate(patchJson);
}

void CompanionIpcServer::sendPatchUpdate(uint32_t instanceId, const std::string& patchJson) {
    std::lock_guard lock(connsMutex_);
    for (auto* c : conns_) {
        if (c->connId() == instanceId) {
            c->sendPatchUpdate(patchJson);
            return;
        }
    }
}

void CompanionIpcServer::shutdown() {
    {
        std::lock_guard lock(connsMutex_);
        for (auto* c : conns_)
            c->sendShutdown();
        conns_.clear(); // drop raw pointers; JUCE deletes the objects
    }
    stop();
}

int CompanionIpcServer::connectionCount() const noexcept {
    std::lock_guard lock(connsMutex_);
    return static_cast<int>(conns_.size());
}

void CompanionIpcServer::onClientDisconnected(uint32_t connId) {
    int remaining = 0;
    {
        std::lock_guard lock(connsMutex_);
        conns_.erase(
            std::remove_if(conns_.begin(), conns_.end(),
                           [connId](CompanionConnection* c) { return c->connId() == connId; }),
            conns_.end());
        remaining = static_cast<int>(conns_.size());
    }
    // Notify companion logic when the last plugin instance leaves.
    if (remaining == 0 && onEmpty_)
        onEmpty_();
}

juce::InterprocessConnection* CompanionIpcServer::createConnectionObject() {
    const uint32_t id = nextConnId_.fetch_add(1);
    // Allocate with new — JUCE's InterprocessConnectionServer stores this in its
    // internal OwnedArray and will delete it.  We keep only a raw pointer.
    auto* conn = new CompanionConnection(id, onRequest_, *this);
    std::lock_guard lock(connsMutex_);
    conns_.push_back(conn);
    return conn;
}

} // namespace agentic_synth::ipc
