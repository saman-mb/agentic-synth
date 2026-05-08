#include "mapper/GrammarSampler.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

// POSIX sockets for HTTP (Mac + Linux; #ifdef guards for Windows)
#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace agentic_synth::mapper {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GrammarSampler::GrammarSampler(GrammarSamplerConfig cfg) : cfg_(std::move(cfg)) {
    if (!cfg_.grammar_path.empty()) {
        std::ifstream f(cfg_.grammar_path);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            grammar_text_ = ss.str();
        }
    }
}

// ---------------------------------------------------------------------------
// JSON reader — sequential parser for GBNF-constrained output
// ---------------------------------------------------------------------------

namespace {

struct JsonReader {
    const std::string& s;
    size_t pos{0};

    bool ok() const noexcept { return pos <= s.size(); }

    void skip_ws() noexcept {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
            ++pos;
    }

    bool expect(char c) noexcept {
        skip_ws();
        if (pos < s.size() && s[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    bool comma() noexcept { return expect(','); }

    std::optional<std::string> read_string() {
        skip_ws();
        if (pos >= s.size() || s[pos] != '"')
            return std::nullopt;
        ++pos;
        std::string out;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\') {
                ++pos;
                if (pos >= s.size())
                    return std::nullopt;
            }
            out += s[pos++];
        }
        if (pos >= s.size())
            return std::nullopt;
        ++pos; // consume closing "
        return out;
    }

    bool expect_key(std::string_view key) {
        auto k = read_string();
        return k && *k == key && expect(':');
    }

    std::optional<float> read_float() {
        skip_ws();
        const char* begin = s.data() + pos;
        const char* end = s.data() + s.size();
        float val{};
        auto [ptr, ec] = std::from_chars(begin, end, val);
        if (ec != std::errc{})
            return std::nullopt;
        pos = static_cast<size_t>(ptr - s.data());
        return val;
    }

    std::optional<uint32_t> read_uint() {
        skip_ws();
        const char* begin = s.data() + pos;
        const char* end = s.data() + s.size();
        uint32_t val{};
        auto [ptr, ec] = std::from_chars(begin, end, val);
        if (ec != std::errc{})
            return std::nullopt;
        pos = static_cast<size_t>(ptr - s.data());
        return val;
    }

    std::optional<bool> read_bool() {
        skip_ws();
        if (s.compare(pos, 4, "true") == 0) {
            pos += 4;
            return true;
        }
        if (s.compare(pos, 5, "false") == 0) {
            pos += 5;
            return false;
        }
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Enum parsers
// ---------------------------------------------------------------------------

std::optional<OscType> parse_osc_type(const std::string& v) {
    if (v == "Sine")
        return OscType::Sine;
    if (v == "Triangle")
        return OscType::Triangle;
    if (v == "Sawtooth")
        return OscType::Sawtooth;
    if (v == "Square")
        return OscType::Square;
    if (v == "Pulse")
        return OscType::Pulse;
    if (v == "Wavetable")
        return OscType::Wavetable;
    if (v == "FM")
        return OscType::FM;
    if (v == "Noise")
        return OscType::Noise;
    return std::nullopt;
}

std::optional<FilterType> parse_filter_type(const std::string& v) {
    if (v == "LowPass")
        return FilterType::LowPass;
    if (v == "HighPass")
        return FilterType::HighPass;
    if (v == "BandPass")
        return FilterType::BandPass;
    if (v == "Notch")
        return FilterType::Notch;
    if (v == "Peak")
        return FilterType::Peak;
    return std::nullopt;
}

std::optional<LfoWaveform> parse_lfo_waveform(const std::string& v) {
    if (v == "Sine")
        return LfoWaveform::Sine;
    if (v == "Triangle")
        return LfoWaveform::Triangle;
    if (v == "Sawtooth")
        return LfoWaveform::Sawtooth;
    if (v == "Square")
        return LfoWaveform::Square;
    if (v == "SampleAndHold")
        return LfoWaveform::SampleAndHold;
    return std::nullopt;
}

std::optional<LfoTarget> parse_lfo_target(const std::string& v) {
    if (v == "None")
        return LfoTarget::None;
    if (v == "Pitch")
        return LfoTarget::Pitch;
    if (v == "FilterCutoff")
        return LfoTarget::FilterCutoff;
    if (v == "Amplitude")
        return LfoTarget::Amplitude;
    if (v == "Pan")
        return LfoTarget::Pan;
    if (v == "WavetablePos")
        return LfoTarget::WavetablePos;
    if (v == "FmRatio")
        return LfoTarget::FmRatio;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Sub-structure parsers (each consumes from current pos, including braces)
// ---------------------------------------------------------------------------

#define RQ(expr)                                                                                                       \
    do {                                                                                                               \
        if (!(expr))                                                                                                   \
            return false;                                                                                              \
    } while (0)
#define RF(dest, reader)                                                                                               \
    do {                                                                                                               \
        auto _v = (reader);                                                                                            \
        if (!_v)                                                                                                       \
            return false;                                                                                              \
        (dest) = *_v;                                                                                                  \
    } while (0)

bool parse_osc(JsonReader& r, OscParams& o) {
    RQ(r.expect('{'));
    RQ(r.expect_key("type"));
    auto ts = r.read_string();
    if (!ts)
        return false;
    auto ot = parse_osc_type(*ts);
    if (!ot)
        return false;
    o.type = *ot;
    RQ(r.comma());
    RQ(r.expect_key("semitone_offset"));
    RF(o.semitone_offset, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("detune_cents"));
    RF(o.detune_cents, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("wavetable_pos"));
    RF(o.wavetable_pos, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("fm_ratio"));
    RF(o.fm_ratio, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("fm_depth"));
    RF(o.fm_depth, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("volume"));
    RF(o.volume, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("pan"));
    RF(o.pan, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("pulse_width"));
    RF(o.pulse_width, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("enabled"));
    auto b = r.read_bool();
    if (!b)
        return false;
    o.enabled = static_cast<uint8_t>(*b ? 1 : 0);
    RQ(r.expect('}'));
    return true;
}

bool parse_filter(JsonReader& r, FilterParams& f) {
    RQ(r.expect('{'));
    RQ(r.expect_key("type"));
    auto ts = r.read_string();
    if (!ts)
        return false;
    auto ft = parse_filter_type(*ts);
    if (!ft)
        return false;
    f.type = *ft;
    RQ(r.comma());
    RQ(r.expect_key("cutoff_hz"));
    RF(f.cutoff_hz, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("resonance"));
    RF(f.resonance, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("env_mod"));
    RF(f.env_mod, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("key_track"));
    RF(f.key_track, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("drive"));
    RF(f.drive, r.read_float());
    RQ(r.expect('}'));
    return true;
}

bool parse_env(JsonReader& r, EnvParams& e) {
    RQ(r.expect('{'));
    RQ(r.expect_key("attack_s"));
    RF(e.attack_s, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("decay_s"));
    RF(e.decay_s, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("sustain"));
    RF(e.sustain, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("release_s"));
    RF(e.release_s, r.read_float());
    RQ(r.expect('}'));
    return true;
}

bool parse_lfo(JsonReader& r, LfoParams& l) {
    RQ(r.expect('{'));
    RQ(r.expect_key("waveform"));
    auto ws = r.read_string();
    if (!ws)
        return false;
    auto wf = parse_lfo_waveform(*ws);
    if (!wf)
        return false;
    l.waveform = *wf;
    RQ(r.comma());
    RQ(r.expect_key("target"));
    auto ts = r.read_string();
    if (!ts)
        return false;
    auto tg = parse_lfo_target(*ts);
    if (!tg)
        return false;
    l.target = *tg;
    RQ(r.comma());
    RQ(r.expect_key("rate_hz"));
    RF(l.rate_hz, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("depth"));
    RF(l.depth, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("phase_offset"));
    RF(l.phase_offset, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("bpm_sync"));
    auto b = r.read_bool();
    if (!b)
        return false;
    l.bpm_sync = static_cast<uint8_t>(*b ? 1 : 0);
    RQ(r.expect('}'));
    return true;
}

bool parse_reverb(JsonReader& r, ReverbParams& rv) {
    RQ(r.expect('{'));
    RQ(r.expect_key("size"));
    RF(rv.size, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("damping"));
    RF(rv.damping, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("width"));
    RF(rv.width, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("mix"));
    RF(rv.mix, r.read_float());
    RQ(r.expect('}'));
    return true;
}

bool parse_delay(JsonReader& r, DelayParams& d) {
    RQ(r.expect('{'));
    RQ(r.expect_key("time_s"));
    RF(d.time_s, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("feedback"));
    RF(d.feedback, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("mix"));
    RF(d.mix, r.read_float());
    RQ(r.comma());
    RQ(r.expect_key("bpm_sync"));
    auto b = r.read_bool();
    if (!b)
        return false;
    d.bpm_sync = static_cast<uint8_t>(*b ? 1 : 0);
    RQ(r.expect('}'));
    return true;
}

#undef RQ
#undef RF

// Simple float-range clamp validation
bool validate_patch(const PatchStruct& p) {
    if (p.version != kPatchStructVersion)
        return false;
    for (const auto& o : p.osc) {
        if (o.volume < 0.0f || o.volume > 1.0f)
            return false;
        if (o.pan < -1.0f || o.pan > 1.0f)
            return false;
    }
    if (p.filter.cutoff_hz < 20.0f || p.filter.cutoff_hz > 20000.0f)
        return false;
    if (p.filter.resonance < 0.0f || p.filter.resonance > 1.0f)
        return false;
    if (p.master_gain < 0.0f || p.master_gain > 1.0f)
        return false;
    if (p.voice_count == 0 || p.voice_count > 16)
        return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<PatchStruct> GrammarSampler::parse_patch_json(const std::string& json_text) {
    JsonReader r{json_text};
    PatchStruct p = make_default_patch();

    if (!r.expect('{'))
        return std::nullopt;

    auto rk = [&](std::string_view key) { return r.expect_key(key); };

    if (!rk("version"))
        return std::nullopt;
    {
        auto v = r.read_uint();
        if (!v)
            return std::nullopt;
        p.version = *v;
    }
    if (!r.comma() || !rk("patch_id"))
        return std::nullopt;
    {
        auto v = r.read_uint();
        if (!v)
            return std::nullopt;
        p.patch_id = *v;
    }

    if (!r.comma() || !rk("osc"))
        return std::nullopt;
    if (!r.expect('['))
        return std::nullopt;
    for (int i = 0; i < kMaxOscillators; ++i) {
        if (i > 0 && !r.comma())
            return std::nullopt;
        if (!parse_osc(r, p.osc[i]))
            return std::nullopt;
    }
    if (!r.expect(']'))
        return std::nullopt;

    if (!r.comma() || !rk("filter"))
        return std::nullopt;
    if (!parse_filter(r, p.filter))
        return std::nullopt;
    if (!r.comma() || !rk("filter_env"))
        return std::nullopt;
    if (!parse_env(r, p.filter_env))
        return std::nullopt;
    if (!r.comma() || !rk("amp_env"))
        return std::nullopt;
    if (!parse_env(r, p.amp_env))
        return std::nullopt;

    if (!r.comma() || !rk("lfo"))
        return std::nullopt;
    if (!r.expect('['))
        return std::nullopt;
    for (int i = 0; i < kMaxLfos; ++i) {
        if (i > 0 && !r.comma())
            return std::nullopt;
        if (!parse_lfo(r, p.lfo[i]))
            return std::nullopt;
    }
    if (!r.expect(']'))
        return std::nullopt;

    if (!r.comma() || !rk("reverb"))
        return std::nullopt;
    if (!parse_reverb(r, p.reverb))
        return std::nullopt;
    if (!r.comma() || !rk("delay"))
        return std::nullopt;
    if (!parse_delay(r, p.delay))
        return std::nullopt;

    if (!r.comma() || !rk("master_gain"))
        return std::nullopt;
    {
        auto v = r.read_float();
        if (!v)
            return std::nullopt;
        p.master_gain = *v;
    }
    if (!r.comma() || !rk("portamento_s"))
        return std::nullopt;
    {
        auto v = r.read_float();
        if (!v)
            return std::nullopt;
        p.portamento_s = *v;
    }
    if (!r.comma() || !rk("voice_count"))
        return std::nullopt;
    {
        auto v = r.read_uint();
        if (!v)
            return std::nullopt;
        p.voice_count = static_cast<uint8_t>(*v);
    }

    if (!r.expect('}'))
        return std::nullopt;
    if (!validate_patch(p))
        return std::nullopt;
    return p;
}

// ---------------------------------------------------------------------------
// HTTP POST (POSIX, blocking)
// ---------------------------------------------------------------------------

namespace {

// Parse "http://host:port/path" → host, port, path
bool parse_url(const std::string& url, std::string& host, int& port, std::string& path) {
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0)
        return false;
    std::string rest = url.substr(prefix.size());
    auto slash = rest.find('/');
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) {
        host = hostport;
        port = 80;
    } else {
        host = hostport.substr(0, colon);
        port = std::stoi(hostport.substr(colon + 1));
    }
    return true;
}

} // namespace

std::string GrammarSampler::http_post(const std::string& json_body) const {
    std::string host, path;
    int port{};
    if (!parse_url(cfg_.server_url + "/completion", host, port, path))
        return {};

#ifdef _WIN32
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        return {};

    int fd = static_cast<int>(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (fd < 0) {
        freeaddrinfo(res);
        return {};
    }

    if (connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return {};
    }
    freeaddrinfo(res);

    // Set socket timeout
    struct timeval tv {};
    tv.tv_sec = cfg_.timeout_ms / 1000;
    tv.tv_usec = (cfg_.timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.0\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << json_body;
    std::string req_str = req.str();

    if (send(fd, req_str.data(), static_cast<int>(req_str.size()), 0) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return {};
    }

    std::string response;
    char buf[4096];
    int n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
        response.append(buf, static_cast<size_t>(n));

#ifdef _WIN32
    closesocket(fd);
    WSACleanup();
#else
    close(fd);
#endif

    // Strip HTTP headers
    auto sep = response.find("\r\n\r\n");
    return (sep == std::string::npos) ? response : response.substr(sep + 4);
}

// ---------------------------------------------------------------------------
// Extract "content" from llama.cpp /completion JSON response
// ---------------------------------------------------------------------------

std::string GrammarSampler::extract_content(const std::string& resp) {
    // Find "content": "..."
    const std::string key = "\"content\"";
    auto pos = resp.find(key);
    if (pos == std::string::npos)
        return {};
    pos += key.size();
    // Skip whitespace and colon
    while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == ':'))
        ++pos;
    if (pos >= resp.size() || resp[pos] != '"')
        return {};
    ++pos;
    std::string out;
    while (pos < resp.size() && resp[pos] != '"') {
        if (resp[pos] == '\\') {
            ++pos;
            if (pos >= resp.size())
                break;
            switch (resp[pos]) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            default:
                out += resp[pos];
                break;
            }
        } else {
            out += resp[pos];
        }
        ++pos;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Build llama.cpp /completion request body
// ---------------------------------------------------------------------------

std::string GrammarSampler::build_request(const std::string& user_prompt, uint32_t patch_id) const {
    // Escape strings for JSON embedding
    auto json_escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else
                out += c;
        }
        return out;
    };

    std::string prompt_text = cfg_.system_prompt + "\n\nGenerate a JSON patch (patch_id=" + std::to_string(patch_id) +
                              ") for: " + user_prompt;

    std::ostringstream body;
    body << "{" << "\"prompt\":\"" << json_escape(prompt_text) << "\"," << "\"grammar\":\""
         << json_escape(grammar_text_) << "\"," << "\"max_tokens\":" << cfg_.max_tokens << ","
         << "\"temperature\":" << cfg_.temperature << "," << "\"stop\":[\"}\"]" << "}";
    // Note: the stop token is not used because grammar already terminates the object
    // Re-build without incomplete stop token
    std::ostringstream body2;
    body2 << "{" << "\"prompt\":\"" << json_escape(prompt_text) << "\"," << "\"grammar\":\""
          << json_escape(grammar_text_) << "\"," << "\"max_tokens\":" << cfg_.max_tokens << ","
          << "\"temperature\":" << cfg_.temperature << "}";
    return body2.str();
}

// ---------------------------------------------------------------------------
// Generate
// ---------------------------------------------------------------------------

std::optional<PatchStruct> GrammarSampler::generate(const std::string& user_prompt, uint32_t patch_id) const {
    const std::string req_body = build_request(user_prompt, patch_id);
    const std::string response = http_post(req_body);
    if (response.empty())
        return std::nullopt;
    const std::string content = extract_content(response);
    if (content.empty())
        return std::nullopt;
    return parse_patch_json(content);
}

} // namespace agentic_synth::mapper
