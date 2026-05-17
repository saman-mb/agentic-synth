#include "agent/MorphTelemetry.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace agentic_synth::agent {

namespace {

// ── Minimal SHA-256 ──────────────────────────────────────────────────────────
// Self-contained so we don't pull juce_cryptography into the agent lib just to
// hash a prompt to 8 hex chars. Reference: FIPS 180-4. Output is the canonical
// 32-byte digest; hashPrompt() emits the first 8 hex chars (= 4 bytes).
constexpr std::array<uint32_t, 64> kSHA256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotr(uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

std::array<uint8_t, 32> sha256(const std::string& s) noexcept {
    std::array<uint32_t, 8> H = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    // Pre-process: append 0x80, pad with 0x00 to 56 mod 64, then 8-byte big-endian length.
    std::vector<uint8_t> msg(s.begin(), s.end());
    const uint64_t bitlen = static_cast<uint64_t>(s.size()) * 8ull;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0x00);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xffu));

    for (size_t off = 0; off < msg.size(); off += 64) {
        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[off + 4 * i]) << 24) |
                   (static_cast<uint32_t>(msg[off + 4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + 4 * i + 2]) << 8) |
                   (static_cast<uint32_t>(msg[off + 4 * i + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        uint32_t e = H[4], f = H[5], g = H[6], h = H[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = h + S1 + ch + kSHA256K[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }

    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[4 * i + 0] = static_cast<uint8_t>((H[i] >> 24) & 0xff);
        out[4 * i + 1] = static_cast<uint8_t>((H[i] >> 16) & 0xff);
        out[4 * i + 2] = static_cast<uint8_t>((H[i] >> 8) & 0xff);
        out[4 * i + 3] = static_cast<uint8_t>(H[i] & 0xff);
    }
    return out;
}

// ── JSON helpers ─────────────────────────────────────────────────────────────

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

std::string iso8601_now() {
    using namespace std::chrono;
    const auto tp = system_clock::now();
    const std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

const char* kindString(MorphEventKind k) noexcept {
    switch (k) {
    case MorphEventKind::MorphRequested:  return "morph_requested";
    case MorphEventKind::VariationPicked: return "variation_picked";
    case MorphEventKind::MacroTweaked:    return "macro_tweaked";
    case MorphEventKind::ABToggled:       return "ab_toggled";
    case MorphEventKind::PresetCommitted: return "preset_committed";
    case MorphEventKind::BounceToWav:     return "bounce_to_wav";
    }
    return "unknown";
}

} // namespace

MorphTelemetry& MorphTelemetry::instance() {
    static MorphTelemetry inst;
    return inst;
}

std::string MorphTelemetry::defaultPath() {
    std::string dir;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    dir = appdata ? (std::string(appdata) + "\\AgenticSynth") : "AgenticSynth";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    dir = home ? (std::string(home) + "/Library/Application Support/AgenticSynth")
               : "/tmp/AgenticSynth";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        dir = std::string(xdg) + "/AgenticSynth";
    } else {
        const char* home = std::getenv("HOME");
        dir = home ? (std::string(home) + "/.local/share/AgenticSynth")
                   : "/tmp/AgenticSynth";
    }
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/morph_telemetry.jsonl";
}

void MorphTelemetry::setPathForTest(std::string p) noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = std::move(p);
    resolved_ = !path_.empty();
}

std::string MorphTelemetry::path() const {
    std::lock_guard<std::mutex> lock(mu_);
    return path_;
}

std::string MorphTelemetry::hashPrompt(const std::string& prompt) {
    const auto digest = sha256(prompt);
    static const char* hex = "0123456789abcdef";
    char buf[9];
    for (int i = 0; i < 4; ++i) {
        buf[2 * i]     = hex[(digest[i] >> 4) & 0xf];
        buf[2 * i + 1] = hex[digest[i] & 0xf];
    }
    buf[8] = '\0';
    return std::string{buf};
}

void MorphTelemetry::log(const MorphEvent& ev) noexcept {
    try {
        if (!enabled_)
            return;
        std::lock_guard<std::mutex> lock(mu_);
        if (!resolved_) {
            path_ = defaultPath();
            resolved_ = true;
        }
        if (path_.empty())
            return;

        std::ofstream f(path_, std::ios::out | std::ios::app);
        if (!f)
            return;

        const std::string ts = ev.ts.empty() ? iso8601_now() : ev.ts;

        std::ostringstream ss;
        ss << "{"
           << "\"ts\":\""   << jsonEscape(ts)             << "\","
           << "\"kind\":\"" << kindString(ev.kind)        << "\"";

        switch (ev.kind) {
        case MorphEventKind::MorphRequested:
            ss << ",\"prompt_hash\":\"" << jsonEscape(ev.prompt_hash) << "\""
               << ",\"history_size\":"  << ev.history_size
               << ",\"liked_size\":"    << ev.liked_size;
            break;
        case MorphEventKind::VariationPicked:
            ss << ",\"strategy_id\":"            << ev.strategy_id
               << ",\"label\":\""                << jsonEscape(ev.label) << "\""
               << ",\"time_since_arrival_ms\":"  << ev.time_since_arrival_ms;
            break;
        case MorphEventKind::MacroTweaked:
            ss << ",\"macro_index\":" << ev.macro_index
               << ",\"value\":"       << ev.value
               << ",\"dwell_ms\":"    << ev.dwell_ms;
            break;
        case MorphEventKind::ABToggled:
            ss << ",\"from_slot\":" << ev.from_slot
               << ",\"to_slot\":"   << ev.to_slot;
            break;
        case MorphEventKind::PresetCommitted:
            ss << ",\"name_length\":"  << ev.name_length
               << ",\"prompt_hash\":\""<< jsonEscape(ev.prompt_hash) << "\"";
            break;
        case MorphEventKind::BounceToWav:
            ss << ",\"duration_s\":" << ev.duration_s;
            break;
        }

        ss << "}\n";

        f << ss.str();
        f.flush();
    } catch (...) {
        // Telemetry never throws.
    }
}

} // namespace agentic_synth::agent
