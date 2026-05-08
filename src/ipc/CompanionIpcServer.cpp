#include "ipc/CompanionIpcServer.h"

#include <algorithm>
#include <cstring>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace agentic_synth::ipc {

// ── CompanionConnection ───────────────────────────────────────────────────────

CompanionConnection::CompanionConnection(int fd, uint32_t connId, PatchRequestCallback onRequest,
                                         CompanionIpcServer& owner)
    : juce::Thread("CompanionConn-" + juce::String(connId)),
      fd_(fd), connId_(connId), onRequest_(std::move(onRequest)), owner_(owner) {}

CompanionConnection::~CompanionConnection() {
    connected_ = false;
    stopThread(1000);
#if !defined(_WIN32)
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

bool CompanionConnection::recvAll(void* buf, std::size_t len) noexcept {
#if !defined(_WIN32)
    auto* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = ::read(fd_, p, len);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
#else
    return false;
#endif
}

bool CompanionConnection::sendRaw(const void* buf, std::size_t len) noexcept {
#if !defined(_WIN32)
    const auto* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd_, p, len);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
#else
    return false;
#endif
}

void CompanionConnection::sendPatchUpdate(const std::string& patchJson) {
    if (!connected_.load(std::memory_order_acquire)) return;
    IpcMsgHeader hdr{};
    hdr.protocolVersion = kIpcProtocolVersion;
    hdr.type = IpcMsgType::PatchUpdate;
    hdr.payloadLen = static_cast<uint32_t>(patchJson.size());
    std::lock_guard lock(writeMutex_);
    sendRaw(&hdr, sizeof(hdr));
    if (!patchJson.empty()) sendRaw(patchJson.data(), patchJson.size());
}

void CompanionConnection::sendShutdown() {
    if (!connected_.load(std::memory_order_acquire)) return;
    IpcMsgHeader hdr{};
    hdr.protocolVersion = kIpcProtocolVersion;
    hdr.type = IpcMsgType::Shutdown;
    std::lock_guard lock(writeMutex_);
    sendRaw(&hdr, sizeof(hdr));
}

void CompanionConnection::run() {
#if !defined(_WIN32)
    while (!threadShouldExit() && connected_.load(std::memory_order_acquire)) {
        struct pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 200) <= 0) continue;

        IpcMsgHeader hdr{};
        if (!recvAll(&hdr, sizeof(hdr))) break;
        if (hdr.protocolVersion != kIpcProtocolVersion) break;

        std::string payload;
        if (hdr.payloadLen > 0) {
            payload.resize(hdr.payloadLen);
            if (!recvAll(payload.data(), hdr.payloadLen)) break;
        }

        switch (hdr.type) {
        case IpcMsgType::PatchRequest:
            if (onRequest_) onRequest_(hdr.instanceId, payload);
            break;
        case IpcMsgType::Bye:
            connected_ = false;
            break;
        default:
            break;
        }
    }
    connected_ = false;
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    owner_.onClientDisconnected(connId_);
#endif
}

// ── CompanionIpcServer ────────────────────────────────────────────────────────

CompanionIpcServer::CompanionIpcServer(PatchRequestCallback onRequest, std::function<void()> onEmpty)
    : juce::Thread("CompanionIpcServer"), onRequest_(std::move(onRequest)), onEmpty_(std::move(onEmpty)) {}

CompanionIpcServer::~CompanionIpcServer() { shutdown(); }

bool CompanionIpcServer::start() {
#if !defined(_WIN32)
    const std::string path = ipcSocketPath();

    serverFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ < 0) return false;

    // Remove stale socket file if it exists.
    ::unlink(path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(serverFd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(serverFd_, 16) < 0) {
        ::close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    startThread();
    return true;
#else
    return false; // Windows: implement with named pipes if needed
#endif
}

void CompanionIpcServer::broadcastPatchUpdate(const std::string& patchJson) {
    std::lock_guard lock(connsMutex_);
    for (auto& c : conns_)
        c->sendPatchUpdate(patchJson);
}

void CompanionIpcServer::sendPatchUpdate(uint32_t instanceId, const std::string& patchJson) {
    std::lock_guard lock(connsMutex_);
    for (auto& c : conns_) {
        if (c->connId() == instanceId) {
            c->sendPatchUpdate(patchJson);
            return;
        }
    }
}

void CompanionIpcServer::shutdown() {
    {
        std::lock_guard lock(connsMutex_);
        for (auto& c : conns_)
            c->sendShutdown();
        conns_.clear();
    }
    stopThread(2000);
#if !defined(_WIN32)
    if (serverFd_ >= 0) {
        ::close(serverFd_);
        serverFd_ = -1;
        ::unlink(ipcSocketPath().c_str());
    }
#endif
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
                           [connId](const auto& c) { return c->connId() == connId; }),
            conns_.end());
        remaining = static_cast<int>(conns_.size());
    }
    if (remaining == 0 && onEmpty_)
        onEmpty_();
}

void CompanionIpcServer::run() {
#if !defined(_WIN32)
    while (!threadShouldExit()) {
        struct pollfd pfd{serverFd_, POLLIN, 0};
        if (::poll(&pfd, 1, 200) <= 0) continue;

        const int clientFd = ::accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0) continue;

        const uint32_t id = nextConnId_.fetch_add(1);
        auto conn = std::make_shared<CompanionConnection>(clientFd, id, onRequest_, *this);
        {
            std::lock_guard lock(connsMutex_);
            conns_.push_back(conn);
        }
        conn->startThread();
    }
#endif
}

} // namespace agentic_synth::ipc
