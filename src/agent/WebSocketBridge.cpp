#include "agent/WebSocketBridge.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>

namespace agentic_synth::agent {

// ---------------------------------------------------------------------------
// SHA-1 (RFC 3174) — required for WebSocket Sec-WebSocket-Accept computation.
// ---------------------------------------------------------------------------

std::array<uint8_t, 20> WebSocketBridge::sha1(const std::string& msg) {
    auto rot32 = [](uint32_t x, int n) -> uint32_t { return (x << n) | (x >> (32 - n)); };

    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    std::vector<uint8_t> m(msg.begin(), msg.end());
    uint64_t bitlen = static_cast<uint64_t>(m.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56)
        m.push_back(0);
    for (int i = 7; i >= 0; --i)
        m.push_back(static_cast<uint8_t>(bitlen >> (i * 8)));

    for (size_t blk = 0; blk < m.size(); blk += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; ++j)
            w[j] = (static_cast<uint32_t>(m[blk + j * 4]) << 24) | (static_cast<uint32_t>(m[blk + j * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(m[blk + j * 4 + 2]) << 8) | static_cast<uint32_t>(m[blk + j * 4 + 3]);
        for (int j = 16; j < 80; ++j)
            w[j] = rot32(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int j = 0; j < 80; ++j) {
            uint32_t f, k;
            if (j < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (j < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (j < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            uint32_t tmp = rot32(a, 5) + f + e + k + w[j];
            e = d;
            d = c;
            c = rot32(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::array<uint8_t, 20> out;
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return out;
}

std::string WebSocketBridge::base64Encode(const uint8_t* data, size_t len) {
    static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len)
            b |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len)
            b |= data[i + 2];
        out += kAlpha[(b >> 18) & 63];
        out += kAlpha[(b >> 12) & 63];
        out += (i + 1 < len) ? kAlpha[(b >> 6) & 63] : '=';
        out += (i + 2 < len) ? kAlpha[b & 63] : '=';
    }
    return out;
}

std::string WebSocketBridge::computeAcceptKey(const std::string& clientKey) {
    static const std::string kMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    auto hash = sha1(clientKey + kMagic);
    return base64Encode(hash.data(), hash.size());
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WebSocketBridge::WebSocketBridge() : juce::Thread("WS-Bridge") {}

WebSocketBridge::~WebSocketBridge() { stop(); }

void WebSocketBridge::start(int port) {
    port_ = port;
    startThread();
}

void WebSocketBridge::stop() {
    signalThreadShouldExit();
    serverSock_.close(); // unblocks waitForNextConnection()
    stopThread(2000);
}

// ---------------------------------------------------------------------------
// Server accept loop (main bridge thread)
// ---------------------------------------------------------------------------

void WebSocketBridge::run() {
    if (!serverSock_.createListener(port_))
        return;

    while (!threadShouldExit()) {
        juce::StreamingSocket* client = serverSock_.waitForNextConnection();
        if (!client)
            break;

        int id = nextClientId_.fetch_add(1);
        {
            juce::ScopedLock lock(clientsMutex_);
            clients_.push_back({id, client});
        }

        juce::Thread::launch([this, client, id]() { handleClient(client, id); });
    }
}

// ---------------------------------------------------------------------------
// Per-client handler
// ---------------------------------------------------------------------------

void WebSocketBridge::handleClient(juce::StreamingSocket* sockRaw, int id) {
    std::unique_ptr<juce::StreamingSocket> sock(sockRaw);

    if (!performHandshake(sock.get())) {
        juce::ScopedLock lock(clientsMutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(), [id](const ClientEntry& e) { return e.id == id; }),
            clients_.end());
        return;
    }

    while (sock->isConnected()) {
        uint8_t opcode = 0;
        std::vector<uint8_t> payload;

        if (!readFrame(sock.get(), opcode, payload))
            break;

        if (opcode == 0x8)
            break;           // connection close
        if (opcode == 0x9) { // ping → pong
            writeFrame(sock.get(), 0xA, nullptr, 0);
            continue;
        }
        if (opcode == 0x1 && textCb_) { // text
            textCb_(std::string(payload.begin(), payload.end()), id);
        } else if (opcode == 0x2 && binaryCb_) { // binary
            binaryCb_(std::move(payload), id);
        }
    }

    {
        juce::ScopedLock lock(clientsMutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(), [id](const ClientEntry& e) { return e.id == id; }),
            clients_.end());
    }
}

// ---------------------------------------------------------------------------
// WebSocket handshake
// ---------------------------------------------------------------------------

bool WebSocketBridge::performHandshake(juce::StreamingSocket* sock) {
    // Read raw HTTP request until \r\n\r\n
    std::string request;
    request.reserve(512);
    char ch;
    while (sock->isConnected()) {
        if (sock->read(&ch, 1, true) != 1)
            return false;
        request += ch;
        if (request.size() >= 4 && request.compare(request.size() - 4, 4, "\r\n\r\n") == 0)
            break;
    }

    auto extractHeader = [&](const std::string& name) -> std::string {
        std::string needle = name + ":";
        auto pos = request.find(needle);
        if (pos == std::string::npos) {
            // case-insensitive fallback (HTTP headers are case-insensitive)
            std::string lower = request;
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string needleLower = needle;
            for (char& c : needleLower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            pos = lower.find(needleLower);
            if (pos == std::string::npos)
                return {};
        }
        pos += needle.size();
        while (pos < request.size() && (request[pos] == ' ' || request[pos] == '\t'))
            ++pos;
        auto end = request.find('\r', pos);
        if (end == std::string::npos)
            end = request.find('\n', pos);
        if (end == std::string::npos)
            end = request.size();
        return request.substr(pos, end - pos);
    };

    std::string key = extractHeader("Sec-WebSocket-Key");
    if (key.empty())
        return false;

    std::string accept = computeAcceptKey(key);
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " +
                           accept +
                           "\r\n"
                           "\r\n";

    return sock->write(response.c_str(), static_cast<int>(response.size())) == static_cast<int>(response.size());
}

// ---------------------------------------------------------------------------
// Frame I/O
// ---------------------------------------------------------------------------

bool WebSocketBridge::readFrame(juce::StreamingSocket* sock, uint8_t& opcode, std::vector<uint8_t>& payload) {
    uint8_t hdr[2];
    if (sock->read(hdr, 2, true) != 2)
        return false;

    opcode = hdr[0] & 0x0Fu;
    bool masked = (hdr[1] & 0x80u) != 0;
    uint64_t paylen = hdr[1] & 0x7Fu;

    if (paylen == 126) {
        uint8_t ext[2];
        if (sock->read(ext, 2, true) != 2)
            return false;
        paylen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (paylen == 127) {
        uint8_t ext[8];
        if (sock->read(ext, 8, true) != 8)
            return false;
        paylen = 0;
        for (int i = 0; i < 8; ++i)
            paylen = (paylen << 8) | ext[i];
    }

    uint8_t mask[4] = {};
    if (masked && sock->read(mask, 4, true) != 4)
        return false;

    payload.resize(static_cast<size_t>(paylen));
    size_t got = 0;
    while (got < static_cast<size_t>(paylen)) {
        int n = sock->read(payload.data() + got, static_cast<int>(paylen - got), true);
        if (n <= 0)
            return false;
        got += static_cast<size_t>(n);
    }

    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= mask[i & 3u];
    }

    return true;
}

bool WebSocketBridge::writeFrame(juce::StreamingSocket* sock, uint8_t opcode, const void* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80u | opcode); // FIN = 1

    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFFu) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>(len >> 8));
        frame.push_back(static_cast<uint8_t>(len));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFFu));
    }

    if (data && len > 0) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        frame.insert(frame.end(), bytes, bytes + len);
    }

    return sock->write(frame.data(), static_cast<int>(frame.size())) == static_cast<int>(frame.size());
}

bool WebSocketBridge::writeTextFrame(juce::StreamingSocket* sock, const std::string& text) {
    return writeFrame(sock, 0x01u, text.data(), text.size());
}

// ---------------------------------------------------------------------------
// Broadcasting
// ---------------------------------------------------------------------------

void WebSocketBridge::broadcast(const std::string& json) {
    juce::ScopedLock lock(clientsMutex_);
    for (auto& entry : clients_)
        writeTextFrame(entry.sock, json);
}

void WebSocketBridge::sendToClient(int clientId, const std::string& json) {
    juce::ScopedLock lock(clientsMutex_);
    for (auto& entry : clients_) {
        if (entry.id == clientId) {
            writeTextFrame(entry.sock, json);
            break;
        }
    }
}

} // namespace agentic_synth::agent
