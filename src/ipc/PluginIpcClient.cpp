#include "ipc/PluginIpcClient.h"

#include <cstring>

#if !defined(_WIN32)
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

// ── Static process-wide helpers ───────────────────────────────────────────────

std::atomic<int>& PluginIpcClient::instanceCount() noexcept {
    static std::atomic<int> count{0};
    return count;
}

std::atomic<bool>& PluginIpcClient::launchScheduled() noexcept {
    static std::atomic<bool> s{false};
    return s;
}

// ── Construction / destruction ────────────────────────────────────────────────

PluginIpcClient::PluginIpcClient(uint32_t instanceId, PatchJsonCallback onPatch)
    : juce::Thread("PluginIpcClientReader"), instanceId_(instanceId), onPatch_(std::move(onPatch)) {
    ++instanceCount();
}

PluginIpcClient::~PluginIpcClient() {
    if (connected_.load(std::memory_order_acquire))
        sendMsg(IpcMsgType::Bye);
    connected_ = false;
    stopThread(1000);
#if !defined(_WIN32)
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    --instanceCount();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool PluginIpcClient::connectToCompanion(int /*timeoutMs*/) {
    if (connected_.load(std::memory_order_acquire))
        return true;

#if !defined(_WIN32)
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string path = ipcSocketPath();
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        // Defer fork/exec to the JUCE message thread so it never happens inside
        // a plugin callback or audio-adjacent thread (issue #227).
        if (!launchScheduled().exchange(true)) {
            juce::MessageManager::callAsync([this] {
                tryLaunchCompanion();
                launchScheduled() = false;
                juce::Timer::callAfterDelay(500, [this] { connectToCompanion(); });
            });
        }
        return false;
    }

    fd_ = fd;
    connected_ = true;
    startThread();
    sendMsg(IpcMsgType::Hello);
    return true;
#else
    return false; // Windows: implement with named pipes or TCP if needed
#endif
}

void PluginIpcClient::requestPatch(const std::string& prompt) {
    sendMsg(IpcMsgType::PatchRequest, prompt);
}

bool PluginIpcClient::isActive() const noexcept {
    return connected_.load(std::memory_order_acquire);
}

// ── Reader thread ─────────────────────────────────────────────────────────────

void PluginIpcClient::run() {
#if !defined(_WIN32)
    while (!threadShouldExit() && connected_.load(std::memory_order_acquire)) {
        struct pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 200) <= 0) continue;

        // Read full header.
        IpcMsgHeader hdr{};
        {
            char* p = reinterpret_cast<char*>(&hdr);
            std::size_t rem = sizeof(hdr);
            while (rem > 0) {
                const ssize_t n = ::read(fd_, p, rem);
                if (n <= 0) goto disconnect;
                p += n;
                rem -= static_cast<std::size_t>(n);
            }
        }
        if (hdr.protocolVersion != kIpcProtocolVersion) break;

        {
            // Read payload.
            std::string payload;
            if (hdr.payloadLen > 0) {
                payload.resize(hdr.payloadLen);
                char* p = payload.data();
                std::size_t rem = hdr.payloadLen;
                while (rem > 0) {
                    const ssize_t n = ::read(fd_, p, rem);
                    if (n <= 0) goto disconnect;
                    p += n;
                    rem -= static_cast<std::size_t>(n);
                }
            }

            switch (hdr.type) {
            case IpcMsgType::PatchUpdate:
                if (onPatch_ && !payload.empty()) onPatch_(payload);
                break;
            case IpcMsgType::Shutdown:
                connected_ = false;
                break;
            default:
                break;
            }
        }
        continue;

disconnect:
        break;
    }

    connected_ = false;
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }

    // Reconnect after a delay; the companion may be restarting.
    juce::Timer::callAfterDelay(1000, [this] { connectToCompanion(); });
#endif
}

// ── Private helpers ───────────────────────────────────────────────────────────

void PluginIpcClient::sendMsg(IpcMsgType type, const std::string& payload) {
#if !defined(_WIN32)
    if (!connected_.load(std::memory_order_acquire) || fd_ < 0) return;

    IpcMsgHeader hdr{};
    hdr.protocolVersion = kIpcProtocolVersion;
    hdr.type = type;
    hdr.instanceId = instanceId_;
    hdr.payloadLen = static_cast<uint32_t>(payload.size());

    std::lock_guard lock(writeMutex_);
    {
        const char* p = reinterpret_cast<const char*>(&hdr);
        std::size_t rem = sizeof(hdr);
        while (rem > 0) {
            const ssize_t n = ::write(fd_, p, rem);
            if (n <= 0) return;
            p += n;
            rem -= static_cast<std::size_t>(n);
        }
    }
    if (!payload.empty()) {
        const char* p = payload.data();
        std::size_t rem = payload.size();
        while (rem > 0) {
            const ssize_t n = ::write(fd_, p, rem);
            if (n <= 0) return;
            p += n;
            rem -= static_cast<std::size_t>(n);
        }
    }
#endif
}

void PluginIpcClient::tryLaunchCompanion() {
    // Runs on the JUCE message thread — safe for process creation.
    const juce::File execDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    const juce::File companion = execDir.getChildFile("AgenticSynthCompanion");
    if (companion.existsAsFile())
        companion.startAsProcess();
}

} // namespace agentic_synth::ipc
