#pragma once

#include <cstdint>
#include <string>

namespace agentic_synth::ipc {

// Bump when the binary layout changes.
constexpr uint32_t kIpcProtocolVersion = 1;

// Named pipe identifier — same on every platform JUCE supports.
constexpr const char* kPipeName = "AgenticSynthIPC";

// Connection timeout used by both sides.
constexpr int kConnectTimeoutMs = 2000;

// Localhost TCP port shared by all plugin instances and the companion.
// Chosen in the IANA dynamic/private range.
constexpr int kCompanionPort = 49123;

// ── Message types ─────────────────────────────────────────────────────────────

enum class IpcMsgType : uint8_t {
    Hello = 1,        // Plugin → Companion: announce instance
    Bye = 2,          // Plugin → Companion: instance is shutting down
    PatchRequest = 3, // Plugin → Companion: submit NL prompt (payload = UTF-8 prompt)
    PatchUpdate = 4,  // Companion → Plugin: send patch JSON (payload = JSON string)
    Shutdown = 5,     // Companion → Plugin: server is exiting
};

// Fixed-size header placed before every variable-length payload.
// Serialised little-endian and sent as part of a JUCE MemoryBlock.
#pragma pack(push, 1)
struct IpcMsgHeader {
    uint32_t protocolVersion{kIpcProtocolVersion};
    IpcMsgType type{IpcMsgType::Hello};
    uint8_t pad[3]{};
    uint32_t instanceId{0}; // originating plugin instance
    uint32_t payloadLen{0}; // bytes that follow this header
};
#pragma pack(pop)

static_assert(sizeof(IpcMsgHeader) == 16);

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a MemoryBlock ready to pass to InterprocessConnection::sendMessage().
inline juce::MemoryBlock makeMessage(IpcMsgType type, uint32_t instanceId, const std::string& payload = {}) {
    IpcMsgHeader hdr;
    hdr.type = type;
    hdr.instanceId = instanceId;
    hdr.payloadLen = static_cast<uint32_t>(payload.size());

    juce::MemoryBlock block(sizeof(IpcMsgHeader) + payload.size());
    block.copyFrom(&hdr, 0, sizeof(IpcMsgHeader));
    if (!payload.empty())
        block.copyFrom(payload.data(), static_cast<int>(sizeof(IpcMsgHeader)), static_cast<int>(payload.size()));
    return block;
}

// Parse a received MemoryBlock; returns false when the block is malformed.
inline bool parseMessage(const juce::MemoryBlock& block, IpcMsgHeader& hdrOut, std::string& payloadOut) {
    if (block.getSize() < sizeof(IpcMsgHeader))
        return false;

    std::memcpy(&hdrOut, block.getData(), sizeof(IpcMsgHeader));

    if (hdrOut.protocolVersion != kIpcProtocolVersion)
        return false;

    const std::size_t expectedSize = sizeof(IpcMsgHeader) + hdrOut.payloadLen;
    if (block.getSize() < expectedSize)
        return false;

    const auto* payloadPtr = static_cast<const char*>(block.getData()) + sizeof(IpcMsgHeader);
    payloadOut.assign(payloadPtr, hdrOut.payloadLen);
    return true;
}

} // namespace agentic_synth::ipc
