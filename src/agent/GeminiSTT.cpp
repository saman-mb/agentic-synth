#include "GeminiSTT.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

namespace agentic_synth::agent {

namespace {

// Pack a 44-byte canonical WAV header for 16 kHz mono PCM Int16 in front of
// `samples`. Returns the complete byte stream.
std::vector<std::uint8_t> wrapWav(const std::int16_t* samples, int numSamples, int sampleRate) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(numSamples) * 2;
    const std::uint32_t chunkSize = 36 + dataBytes;
    const std::uint16_t channels = 1;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * channels * bitsPerSample / 8;
    const std::uint16_t blockAlign = channels * bitsPerSample / 8;

    std::vector<std::uint8_t> out;
    out.reserve(44 + dataBytes);
    auto u16 = [&](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto u32 = [&](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    };
    auto str = [&](const char* s) {
        for (; *s; ++s) out.push_back(static_cast<std::uint8_t>(*s));
    };

    str("RIFF");
    u32(chunkSize);
    str("WAVE");
    str("fmt ");
    u32(16);            // PCM fmt subchunk size
    u16(1);             // PCM format code
    u16(channels);
    u32(static_cast<std::uint32_t>(sampleRate));
    u32(byteRate);
    u16(blockAlign);
    u16(bitsPerSample);
    str("data");
    u32(dataBytes);

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(samples);
    out.insert(out.end(), bytes, bytes + dataBytes);
    return out;
}

// RFC 4648 base64 encode. Mirrors what JUCE's Base64 would do but lives
// in-translation-unit so this file has no JUCE dependency.
std::string base64Encode(const std::uint8_t* data, std::size_t len) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16)
                              | (static_cast<std::uint32_t>(data[i + 1]) << 8)
                              | static_cast<std::uint32_t>(data[i + 2]);
        out += kAlphabet[(v >> 18) & 0x3f];
        out += kAlphabet[(v >> 12) & 0x3f];
        out += kAlphabet[(v >> 6) & 0x3f];
        out += kAlphabet[v & 0x3f];
        i += 3;
    }
    if (i < len) {
        std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < len)
            v |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 0x3f];
        out += kAlphabet[(v >> 12) & 0x3f];
        out += (i + 1 < len) ? kAlphabet[(v >> 6) & 0x3f] : '=';
        out += '=';
    }
    return out;
}

// Tempfile RAII — mirrors PromptEnhancer pattern so curl can read the body
// via --data-binary @ without shell-escaping the (potentially MB-sized)
// JSON payload.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& contents) {
        const char* tmp = std::getenv("TMPDIR");
        if (tmp == nullptr || *tmp == 0)
            tmp = "/tmp";
        std::mt19937_64 rng{std::random_device{}()};
        const auto rnd = rng();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/stt_req_%llx.json", static_cast<unsigned long long>(rnd));
        path = std::string(tmp) + buf;
        std::ofstream f(path, std::ios::binary);
        f << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
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

std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        if (i + 1 >= s.size()) break;
        const char n = s[++i];
        switch (n) {
        case '"':  out += '"'; break;
        case '\\': out += '\\'; break;
        case 'n':  out += '\n'; break;
        case 't':  out += '\t'; break;
        case 'r':  out += '\r'; break;
        case 'u': {
            if (i + 4 >= s.size()) return out;
            const std::string hex = s.substr(i + 1, 4);
            i += 4;
            try {
                const auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                if (cp < 0x80) out += static_cast<char>(cp);
            } catch (...) {}
            break;
        }
        default: out += n; break;
        }
    }
    return out;
}

std::string extract_text(const std::string& resp) {
    const std::string key = "\"text\"";
    auto pos = resp.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':')) ++pos;
    if (pos >= resp.size() || resp[pos] != '"') return {};
    ++pos;
    std::string raw;
    while (pos < resp.size()) {
        if (resp[pos] == '\\') {
            if (pos + 1 >= resp.size()) break;
            raw += resp[pos];
            raw += resp[pos + 1];
            pos += 2;
            continue;
        }
        if (resp[pos] == '"') break;
        raw += resp[pos++];
    }
    return json_unescape(raw);
}

} // namespace

std::string GeminiSTT::http_post(const std::string& url, const std::string& json_body) const {
    TempFile req(json_body);
    std::ostringstream cmd;
    const int timeout_s = std::max(1, cfg_.timeout_ms / 1000);
    cmd << "curl --silent --show-error --fail-with-body"
        << " --max-time " << timeout_s
        << " -H 'Content-Type: application/json'"
        << " --data-binary @" << req.path << " '" << url << "' 2>/dev/null";
    std::string out;
    std::array<char, 4096> buf{};
#ifdef _WIN32
    FILE* p = _popen(cmd.str().c_str(), "r");
#else
    FILE* p = popen(cmd.str().c_str(), "r");
#endif
    if (!p) return {};
    std::size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), p)) > 0)
        out.append(buf.data(), n);
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return out;
}

std::string GeminiSTT::transcribe(const std::int16_t* samples, int numSamples, int sampleRate) const {
    if (cfg_.api_key.empty()) {
        std::cerr << "[GeminiSTT] no API key set — cannot transcribe\n";
        return {};
    }
    if (samples == nullptr || numSamples <= 0) {
        std::cerr << "[GeminiSTT] empty audio buffer\n";
        return {};
    }

    const auto wav = wrapWav(samples, numSamples, sampleRate);
    const std::string b64 = base64Encode(wav.data(), wav.size());

    static constexpr const char* kPrompt =
        "Transcribe the audio to plain text. Output the transcript only — no "
        "preamble, no quotes, no punctuation around the transcript, no model "
        "commentary. If the audio is silent or unintelligible, output an empty "
        "string.";

    std::ostringstream body;
    body << "{"
         << "\"contents\":[{\"parts\":["
         << "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"" << b64 << "\"}},"
         << "{\"text\":\"" << json_escape(kPrompt) << "\"}"
         << "]}],"
         << "\"generationConfig\":{\"temperature\":0.0}"
         << "}";

    const std::string url = "https://generativelanguage.googleapis.com/v1beta/models/"
                          + cfg_.model + ":generateContent?key=" + cfg_.api_key;

    const std::string resp = http_post(url, body.str());
    if (resp.empty()) {
        std::cerr << "[GeminiSTT] empty response from " << cfg_.model << "\n";
        return {};
    }

    std::string text = extract_text(resp);
    if (text.empty()) {
        std::cerr << "[GeminiSTT] could not extract transcript from response\n";
        return {};
    }
    // Strip surrounding quotes / trailing newlines / leading whitespace the
    // model sometimes emits despite the instruction.
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'
                              || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    if (start > 0) text.erase(0, start);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);

    std::cerr << "[GeminiSTT] transcript (" << text.size() << " chars): " << text << "\n";
    return text;
}

} // namespace agentic_synth::agent
