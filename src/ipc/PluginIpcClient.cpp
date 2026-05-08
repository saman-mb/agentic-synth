#include "ipc/PluginIpcClient.h"

#include <juce_core/juce_core.h>

namespace agentic_synth::ipc {

// ── Static process-wide instance counter ─────────────────────────────────────

std::atomic<int>& PluginIpcClient::instanceCount() noexcept {
    static std::atomic<int> count{0};
    return count;
}

// ── Construction / destruction ────────────────────────────────────────────────

PluginIpcClient::PluginIpcClient(uint32_t instanceId, PatchJsonCallback onPatch)
    : instanceId_(instanceId), onPatch_(std::move(onPatch)) {
    ++instanceCount();
}

PluginIpcClient::~PluginIpcClient() {
    if (isConnected()) {
        sendMsg(IpcMsgType::Bye);
        disconnect();
    }
    --instanceCount();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool PluginIpcClient::connectToCompanion(int timeoutMs) {
    if (isConnected()) return true;

    if (!connectToSocket("127.0.0.1", kCompanionPort, timeoutMs)) {
        tryLaunchCompanion();
        juce::Thread::sleep(500);
        return connectToSocket("127.0.0.1", kCompanionPort, timeoutMs);
    }
    return true;
}

void PluginIpcClient::requestPatch(const std::string& prompt) {
    sendMsg(IpcMsgType::PatchRequest, prompt);
}

bool PluginIpcClient::isActive() const noexcept {
    return isConnected();
}

// ── InterprocessConnection callbacks ─────────────────────────────────────────

void PluginIpcClient::connectionMade() {
    sendMsg(IpcMsgType::Hello);
}

void PluginIpcClient::connectionLost() {
    // Retry once after a short pause; the companion may be restarting.
    juce::Timer::callAfterDelay(1000, [this] {
        connectToCompanion();
    });
}

void PluginIpcClient::messageReceived(const juce::MemoryBlock& data) {
    IpcMsgHeader hdr;
    std::string payload;
    if (!parseMessage(data, hdr, payload)) return;

    switch (hdr.type) {
        case IpcMsgType::PatchUpdate:
            if (onPatch_ && !payload.empty())
                onPatch_(payload);
            break;
        case IpcMsgType::Shutdown:
            disconnect();
            break;
        default:
            break;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void PluginIpcClient::sendMsg(IpcMsgType type, const std::string& payload) {
    if (!isConnected()) return;
    sendMessage(makeMessage(type, instanceId_, payload));
}

void PluginIpcClient::tryLaunchCompanion() {
    // Look for the companion binary next to the plugin bundle.
    const juce::File execDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    const juce::File companion = execDir.getChildFile("AgenticSynthCompanion");
    if (companion.existsAsFile())
        companion.startAsProcess();
}

} // namespace agentic_synth::ipc
